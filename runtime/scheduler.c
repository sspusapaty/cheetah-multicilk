#include <stdio.h>
#include <pthread.h>

#include "scheduler.h"
#include "closure.h"
#include "cilk-internal.h"
#include "jmpbuf.h"
#include "readydeque.h"
#include "membar.h"
#include "fiber.h"

// ==============================================
// Misc. helper functions 
// ==============================================

/***********************************************************
 * Internal random number generator.
 ***********************************************************/
static unsigned int rts_rand(__cilkrts_worker *const w) {
  w->l->rand_next = w->l->rand_next * 1103515245 + 12345;
  return (w->l->rand_next >> 16);
}

static void rts_srand(__cilkrts_worker *const w, unsigned int seed) {
  w->l->rand_next = seed;
}

/***********************************************************
 * Managing the 'E' in the THE protocol 
 ***********************************************************/
static void increment_exception_pointer(__cilkrts_worker *const w, 
                                        __cilkrts_worker *const victim_w, 
                                        Closure *cl) {
  Closure_assert_ownership(w, cl);
  CILK_ASSERT(w, cl->status == CLOSURE_RUNNING);

  if(victim_w->exc != EXCEPTION_INFINITY) {
    ++(victim_w->exc);
    /* make sure the exception is visible, before we continue */
    Cilk_fence();
  }
}

static void decrement_exception_pointer(__cilkrts_worker *const w, 
                                        __cilkrts_worker *const victim_w,
				        Closure *cl) {
  Closure_assert_ownership(w, cl);
  CILK_ASSERT(w, cl->status == CLOSURE_RUNNING);
  if(victim_w->exc != EXCEPTION_INFINITY)
    --(victim_w->exc);
}

static void reset_exception_pointer(__cilkrts_worker *const w, Closure *cl) {

  Closure_assert_ownership(w, cl);
  CILK_ASSERT(w, (cl->frame == NULL) || 
	      (cl->frame->worker == w) || 
	      (cl == w->g->invoke_main && 
	       cl->frame->worker == (__cilkrts_worker *) NOBODY) );

  w->exc = w->head;
}

/* Unused for now but may be helpful later
static void signal_immediate_exception_to_all(__cilkrts_worker *const w) {

    int i, active_size = w->g->options.nproc;
    __cilkrts_worker *curr_w;

    for(i=0; i<active_size; i++) {
        curr_w = w->g->workers[i];
        curr_w->exc = EXCEPTION_INFINITY;
    }
    // make sure the exception is visible, before we continue
    Cilk_fence();
}
*/

static void setup_for_execution(__cilkrts_worker * w, Closure *t) {

  __cilkrts_alert(ALERT_SCHED, "[%d]: (setup_for_execution) closure %p\n", w->self, t);
  t->frame->worker = w;
  t->status = CLOSURE_RUNNING;

  w->head = (__cilkrts_stack_frame **) w->l->shadow_stack+1;
  w->tail = (__cilkrts_stack_frame **) w->l->shadow_stack+1;

  /* push the first frame on the current_stack_frame */
  w->current_stack_frame = t->frame;	
  reset_exception_pointer(w, t);
}

// ANGE: When this is called, either a) a worker is about to pass a sync (though not on
// the right fiber), or b) a worker just performed a provably good steal
// successfully
static void setup_for_sync(__cilkrts_worker *w, Closure *t) {

  Closure_assert_ownership(w, t);
  // ANGE: this must be true since in case a) we would have freed it in
  // Cilk_sync, or in case b) we would have freed it when we first returned to
  // the runtime before doing the provably good steal.
  CILK_ASSERT(w, w->l->fiber_to_free == NULL);
  CILK_ASSERT(w, t->fiber != t->fiber_child);
  
  // ANGE: note that in case a) this fiber won't get freed for awhile, since
  // we will longjmp back to the original function's fiber and never go back to the 
  // runtime; we will only free it either once when we get back to the runtime
  // or when we encounter a case where we need to.
  w->l->fiber_to_free = t->fiber; 
  t->fiber = t->fiber_child;
  t->fiber_child = NULL;
  __cilkrts_alert(ALERT_STEAL | ALERT_FIBER, 
      "[%d]: (setup_for_sync) set t %p and t->fiber %p\n", w->self, t, t->fiber);
  __cilkrts_set_synced(t->frame);

  CILK_ASSERT(w, w->current_stack_frame == t->frame);

  SP(t->frame) = (void *) t->orig_rsp;
  t->orig_rsp = NULL; // unset once we have sync-ed
  t->frame->worker = w;
}


// ==============================================
// TLS related functions 
// ==============================================
static pthread_key_t worker_key;

void __cilkrts_init_tls_variables() {
  int status;
  status = pthread_key_create(&worker_key, NULL);
  CILK_ASSERT_G(status == 0);   
}

void * __cilkrts_get_current_thread_id() {
  return (void *)pthread_self();
}

__cilkrts_worker * __cilkrts_get_tls_worker() {
  return (__cilkrts_worker *)pthread_getspecific(worker_key);
}

void __cilkrts_set_tls_worker(__cilkrts_worker *w) {
    int status;
    status = pthread_setspecific(worker_key, w);
    CILK_ASSERT_G(status == 0);
    return;
}


// ==============================================
// Closure return protocol related functions
// ==============================================

/* Doing an "unconditional steal" to steal back the call parent closure */
static Closure * 
setup_call_parent_resumption(__cilkrts_worker *const w, Closure *t) {

  deque_assert_ownership(w, w->self);
  Closure_assert_ownership(w, t);

  CILK_ASSERT(w, w == __cilkrts_get_tls_worker());
  CILK_ASSERT(w, __cilkrts_stolen(t->frame) != 0);
  CILK_ASSERT(w, t->frame != NULL);
  CILK_ASSERT(w, t->frame->worker == (__cilkrts_worker *) NOBODY);
  CILK_ASSERT(w, t->status == CLOSURE_SUSPENDED);
  CILK_ASSERT(w, w->head == w->tail);
  CILK_ASSERT(w, w->current_stack_frame == t->frame);

  t->status = CLOSURE_RUNNING;
  t->frame->worker = w;
  reset_exception_pointer(w, t);

  return t;
}

void Cilk_set_return(__cilkrts_worker *const w) {

  Closure *t;

  __cilkrts_alert(ALERT_RETURN, "[%d]: (Cilk_set_return)\n", w->self);

  deque_lock_self(w);
  t = deque_peek_bottom(w, w->self);
  Closure_lock(w, t);

  CILK_ASSERT(w, t->status == CLOSURE_RUNNING);
  CILK_ASSERT(w, Closure_has_children(t) == 0);

  if(t->call_parent != NULL) {
    CILK_ASSERT(w, t->spawn_parent == NULL);
    CILK_ASSERT(w, (t->frame->flags & CILK_FRAME_DETACHED) == 0);

    Closure *call_parent = t->call_parent;
    Closure *t1 = deque_xtract_bottom(w, w->self);

    USE_UNUSED(t1);
    CILK_ASSERT(w, t == t1);
    CILK_ASSERT(w, __cilkrts_stolen(t->frame));

    t->frame = NULL;
    Closure_unlock(w, t);

    Closure_lock(w, call_parent);
    CILK_ASSERT(w, call_parent->fiber == t->fiber);
    t->fiber = NULL;
    
    Closure_remove_callee(w, call_parent); 
    setup_call_parent_resumption(w, call_parent);
    Closure_unlock(w, call_parent);

    Closure_destroy(w, t);
    deque_add_bottom(w, call_parent, w->self);

  } else {
    CILK_ASSERT(w, t == w->g->invoke_main);
    Closure_unlock(w, t);
  }
  deque_unlock_self(w);
}

Closure *provably_good_steal_maybe(__cilkrts_worker *const w, Closure *parent) {

  Closure_assert_ownership(w, parent);
  __cilkrts_alert(ALERT_STEAL, "[%d]: (provably_good_steal_maybe) cl %p\n", w->self, parent);
  CILK_ASSERT(w, w->l->provably_good_steal == 0);
  
  if (!Closure_has_children(parent) &&
      parent->status == CLOSURE_SUSPENDED) {
    __cilkrts_alert(ALERT_STEAL | ALERT_SYNC, 
        "[%d]: (provably_good_steal_maybe) completing a sync\n", w->self);

    CILK_ASSERT(w, parent->frame != NULL);
    CILK_ASSERT(w, parent->frame->worker == (__cilkrts_worker *) NOBODY);

    /* do a provably-good steal; this is *really* simple */
    w->l->provably_good_steal = 1;

    setup_for_sync(w, parent);
    CILK_ASSERT(w, parent->owner_ready_deque == NOBODY);
    Closure_make_ready(parent);

    __cilkrts_alert(ALERT_STEAL, 
        "[%d]: (provably_good_steal_maybe) returned %p\n", w->self, parent);

    return parent;
  }

  return NULL;
}

/***
 * Return protocol for a spawned child.
 *
 * Some notes on reducer implementation (which was taken out):
 *
 * If any reducer is accessed by the child closure, we need to reduce the
 * reducer view with the child's right_sib_rmap, and its left sibling's
 * right_sib_rmap (or parent's child_rmap if it's the left most child)
 * before we unlink the child from its sibling closure list.
 *
 * When we modify the sibling links (left_sib / right_sib), we always lock
 * the parent and the child.  When we retrieve the reducer maps from left
 * sibling or parent from their place holders (right_sib_rmap / child_rmap), 
 * we always lock the closure from whom we are getting the rmap from.  
 * The locking order is always parent first then child, right child first, 
 * then left.
 * 
 * Once we have done the reduce operation, we try to deposit the rmap from
 * the child to either it's left sibling's right_sib_rmap or parent's
 * child_rmap.  Note that even though we have performed the reduce, by the
 * time we deposit the rmap, the child's left sibling may have changed, 
 * or child may become the new left most child.  Similarly, the child's
 * right_sib_rmap may have something new again.  If that's the case, we
 * need to do the reduce again (in deposit_reducer_map).
 * 
 * This function returns a closure to be executed next, or NULL if none.
 * The child must not be locked by ourselves, and be in no deque.
 ***/
Closure *Closure_return(__cilkrts_worker *const w, Closure *child) {

  Closure *parent;
  Closure *res = (Closure *) NULL;

  CILK_ASSERT(w, child);
  CILK_ASSERT(w, child->join_counter == 0);
  CILK_ASSERT(w, child->status == CLOSURE_RETURNING);
  CILK_ASSERT(w, child->owner_ready_deque == NOBODY);
  Closure_assert_alienation(w, child);

  CILK_ASSERT(w, child->has_cilk_callee == 0);
  CILK_ASSERT(w, child->call_parent == NULL);
  CILK_ASSERT(w, child->spawn_parent != NULL);

  parent = child->spawn_parent;

  __cilkrts_alert(ALERT_RETURN, "[%d]: (Closure_return) child %p, parent %p\n", 
                  w->self, child, parent);

  // At this point the status is as follow: the child is in no deque 
  // and unlocked.  However, the child is still linked with its siblings, 
  Closure_lock(w, parent);

  CILK_ASSERT(w, parent->status != CLOSURE_RETURNING);
  CILK_ASSERT(w, parent->frame != NULL);
  CILK_ASSERT(w, parent->frame->magic == CILK_STACKFRAME_MAGIC);

  Closure_lock(w, child);

  // Execute left-holder logic for stacks.
  if(child->left_sib || parent->fiber_child) {
    // Case where we are not the leftmost stack.
    CILK_ASSERT(w, parent->fiber_child != child->fiber);
    cilk_fiber_deallocate(child->fiber);
  } else {
    // We are leftmost, pass stack/fiber up to parent.  
    // Thus, no stack/fiber to free.
    parent->fiber_child = child->fiber;
  }

  /* now the child is no longer needed */
  Closure_remove_child(w, parent, child);
  child->fiber = NULL;
  Closure_unlock(w, child);
  Closure_destroy(w, child);

  /* the two fences ensure dag consistency (Backer) */
  CILK_ASSERT(w, parent->join_counter > 0);
  Cilk_fence();
  --parent->join_counter;
  Cilk_fence();

  res = provably_good_steal_maybe(w, parent);
  Closure_unlock(w, parent);

  return res;
}

/* 
 * ANGE: t is returning; call the return protocol; see comments above
 * Closure_return.  res is either the next closure to execute (provably-good-
 * steal the parent closure), or NULL is nothing should be executed next.
 *
 * Only called from do_what_it_says when the closure->status =
 * CLOSURE_RETURNING
 */
Closure *return_value(__cilkrts_worker *const w, Closure *t) {
  __cilkrts_alert(ALERT_RETURN, "[%d]: (return_value) closure %p\n", w->self, t);

  Closure *res = NULL;
  CILK_ASSERT(w, t->status == CLOSURE_RETURNING);
  CILK_ASSERT(w, t->call_parent == NULL);
  
  if(t->call_parent == NULL) {
    res = Closure_return(w, t);

  }/* else {
    // ANGE: the ONLY way a closure with call parent can reach here
    // is when the user program calls Cilk_exit, leading to global abort
    // Not supported at the moment 
  }*/

  __cilkrts_alert(ALERT_RETURN, "[%d]: (return_value) returning closure %p\n", w->self, t);

  return res;
}

/* 
 * ANGE: this is called from the user code (generated by compiler in cilkc)
 *       if Cilk_cilk2c_pop_check returns TRUE (i.e. E >= T).  Two 
 *       possibilities: 1. someone stole the last frame from this worker, 
 *       hence pop_check fails (E >= T) when child returns.  2. Someone 
 *       invokes signal_immediate_exception with the closure currently 
 *       running on the worker's deque.  This is only possible with abort.
 *
 *       If this function returns 1, the user code then calls 
 *       Cilk_cilk2c_before_return, which destroys the shadow frame and
 *       return back to caller. 
 */
void Cilk_exception_handler() {

    Closure *t;
    __cilkrts_worker *w = __cilkrts_get_tls_worker();

    deque_lock_self(w);
    t = deque_peek_bottom(w, w->self);

    CILK_ASSERT(w, t);
    Closure_lock(w, t);

    __cilkrts_alert(ALERT_EXCEPT, "[%d]: (Cilk_exception_handler) closure %p!\n", w->self, t);

    /* ANGE: resetting the E pointer since we are handling the exception */
    reset_exception_pointer(w, t);

    CILK_ASSERT(w, t->status == CLOSURE_RUNNING ||
                   // for during abort process
                   t->status == CLOSURE_RETURNING);

    if( w->head > w->tail ) {
      __cilkrts_alert(ALERT_EXCEPT, "[%d]: (Cilk_exception_handler) this is a steal!\n", w->self);

      if(t->status == CLOSURE_RUNNING) {
        CILK_ASSERT(w, Closure_has_children(t) == 0);
        t->status = CLOSURE_RETURNING;
      }

      Closure_unlock(w, t);
      deque_unlock_self(w);
      longjmp_to_runtime(w); // NOT returning back to user code

    } else { // not steal, not abort; false alarm
      Closure_unlock(w, t);
      deque_unlock_self(w);

      return;
    }

}

// ==============================================
// Steal related functions 
// ==============================================

/* 
 * This return the oldest frame in stacklet that has not been promoted to
 * full frame (i.e., never been stolen), or the closest detached frame 
 * if nothing in this stacklet has been promoted. 
 */
static inline __cilkrts_stack_frame * oldest_non_stolen_frame_in_stacklet(__cilkrts_stack_frame *head) {

  __cilkrts_stack_frame *cur = head;
  while(cur && (cur->flags & CILK_FRAME_DETACHED) == 0 && 
	cur->call_parent && __cilkrts_stolen(cur->call_parent) == 0) {
    cur = cur->call_parent;
  }

  return cur;
}

Closure * setup_call_parent_closure_helper(__cilkrts_worker *const w, 
					   __cilkrts_worker *const victim_w, 
					   __cilkrts_stack_frame *frame,
					   Closure *oldest) {

  Closure *call_parent, *curr_cl;

  if(oldest->frame == frame) {
    CILK_ASSERT(w, __cilkrts_stolen(oldest->frame));
    CILK_ASSERT(w, oldest->fiber);
    return oldest;
  }
 
  call_parent = setup_call_parent_closure_helper(w, victim_w, 
						 frame->call_parent, oldest);
  __cilkrts_set_stolen(frame);
  curr_cl = Closure_create(w);
  curr_cl->frame = frame;

  CILK_ASSERT(w, frame->worker == victim_w); 
  CILK_ASSERT(w, call_parent->fiber);
  curr_cl->status = CLOSURE_SUSPENDED;
  curr_cl->frame->worker = (__cilkrts_worker *) NOBODY;
  curr_cl->fiber = call_parent->fiber;
    
  Closure_add_callee(w, call_parent, curr_cl);

  return curr_cl;
}

void setup_closures_in_stacklet(__cilkrts_worker *const w, 
				__cilkrts_worker *const victim_w, 
				Closure *youngest_cl) {

  Closure *call_parent;
  Closure *oldest_cl = youngest_cl->call_parent;
  __cilkrts_stack_frame *youngest, *oldest;

  youngest = youngest_cl->frame;
  oldest = oldest_non_stolen_frame_in_stacklet(youngest);

  CILK_ASSERT(w, youngest == youngest_cl->frame);
  CILK_ASSERT(w, youngest->worker == victim_w); 
  CILK_ASSERT(w, __cilkrts_not_stolen(youngest));
    
  CILK_ASSERT(w, (oldest_cl->frame == NULL &&
	       oldest != youngest) || 
	      (oldest_cl->frame == oldest->call_parent &&
	       __cilkrts_stolen(oldest_cl->frame)) );

  if( oldest_cl->frame ==  NULL ) {
    CILK_ASSERT(w, __cilkrts_not_stolen(oldest));
    CILK_ASSERT(w, oldest->flags & CILK_FRAME_DETACHED);
    __cilkrts_set_stolen(oldest);
    oldest_cl->frame = oldest;
  }
  CILK_ASSERT(w, oldest->worker == victim_w);
  oldest_cl->frame->worker = (__cilkrts_worker *) NOBODY;

  call_parent = setup_call_parent_closure_helper(w,
						 victim_w, youngest->call_parent, oldest_cl);
  __cilkrts_set_stolen(youngest);
  // ANGE: right now they are not the same, but when the youngest returns they should be.
  CILK_ASSERT(w, youngest_cl->fiber != oldest_cl->fiber);

  CILK_ASSERT(w, youngest->worker == victim_w);
  Closure_add_callee(w, call_parent, youngest_cl);
}

/*
 * Do the thief part of Dekker's protocol.  Return 1 upon success,
 * 0 otherwise.  The protocol fails when the victim already popped
 * T so that E=T.
 */
int do_dekker_on(__cilkrts_worker *const w, 
		 __cilkrts_worker *const victim_w, 
		 Closure *cl) {

  Closure_assert_ownership(w, cl);

  increment_exception_pointer(w, victim_w, cl);
  Cilk_membar_StoreLoad(); 

  /* 
   * ANGE: the thief won't steal from this victim if there is only one
   * frame on cl's stack
   */
  if(victim_w->head >= victim_w->tail) {
    decrement_exception_pointer(w, victim_w, cl);
    return 0;
  }

  return 1;
}

/***
 * promote the child frame of parent to a full closure.
 * Detach the parent and return it.
 *
 * Assumptions: the parent is running on victim, and we own
 * the locks of both parent and deque[victim].
 * The child keeps running on the same cache of the parent.
 * The parent's join counter is incremented.
 *
 * In order to promote a child frame to a closure,
 * the parent's frame must be the last in its ready queue.
 *
 * Returns the child. 
 * 
 * ANGE: I don't think this function actually detach the parent.  Someone
 *       calling this function has to do deque_xtract_top on the victim's 
 *       deque to get the parent closure.  This is the only time I can 
 *       think of, where the ready deque contains more than one frame.
 ***/
Closure *promote_child(__cilkrts_worker *const w,
		       __cilkrts_worker *const victim_w, 
		       Closure *cl, Closure **res) {
                            
  int pn = victim_w->self;

  deque_assert_ownership(w, pn);
  Closure_assert_ownership(w, cl);

  CILK_ASSERT(w, cl->status == CLOSURE_RUNNING);
  CILK_ASSERT(w, cl->owner_ready_deque == pn);
  CILK_ASSERT(w, cl->next_ready == NULL);

  /* cl may have a call parent: it might be promoted as its containing
   * stacklet is stolen, and it's call parent is promoted into full and
   * suspended
   */
  CILK_ASSERT(w, cl == w->g->invoke_main || cl->spawn_parent || cl->call_parent);

  Closure *spawn_parent = NULL;
  __cilkrts_stack_frame *volatile *volatile head =  victim_w->head;  
  __cilkrts_stack_frame *volatile frame_to_steal = *head;  

  // ANGE: this must be true if we get this far
  // Note that it can be that H == T here; victim could have done T-- 
  // after the thief passes Dekker; in which case, thief gets the last
  // frame, and H == T.  Victim won't be able to proceed further until 
  // the thief finishes stealing, releasing the deque lock; at which point, 
  // the victim will realize that it should return back to runtime.
  CILK_ASSERT(w, head <= victim_w->exc);
  CILK_ASSERT(w, head <= victim_w->tail);
  CILK_ASSERT(w, frame_to_steal != NULL);

  // ANGE: if cl's frame is not set, the top stacklet must contain more 
  // than one frame, because the right-most (oldest) frame must be a spawn
  // helper which can only call a Cilk function.  On the other hand, if
  // cl's frame is set AND equal to the frame at *HEAD, cl must be either
  // the root frame (invoke_main) or have been stolen before.
  if(cl->frame == frame_to_steal) {
    spawn_parent = cl;

  } else { 
    // cl->frame could either be NULL or some older frame (e.g., 
    // cl->frame was stolen and resumed, it calls another frame which spawned,
    // and the spawned frame is the frame_to_steal now). 
    // ANGE: if this is the case, we must create a new Closure representing
    // the left-most frame (the one to be stolen and resume). 
    spawn_parent = Closure_create(w);
    spawn_parent->frame = frame_to_steal;
    spawn_parent->status = CLOSURE_RUNNING;
    
    // ANGE: this is only temporary; will reset this after the stack has
    // been remapped; so lets not set the callee in cl yet ... although
    // we do need to set the has_callee in cl, so that cl does not get
    // resumed by some other child performing provably good steal.
    Closure_add_temp_callee(w, cl, spawn_parent);
    spawn_parent->call_parent = cl;
    
    // suspend cl & remove it from deque
    Closure_suspend_victim(w, pn, cl);
    Closure_unlock(w, cl);

    Closure_lock(w, spawn_parent);
    *res = spawn_parent;
  }

  if(spawn_parent->orig_rsp == NULL) {
    spawn_parent->orig_rsp = SP(frame_to_steal);
  }

  CILK_ASSERT(w, spawn_parent->has_cilk_callee == 0);
  Closure *spawn_child = Closure_create(w);

  spawn_child->spawn_parent = spawn_parent;
  spawn_child->status = CLOSURE_RUNNING;

  /***
   * Register this child, which sets up its siblinb links.
   * We do this here intead of in finish_promote, because we must setup 
   * the sib links for the new child before its pointer escapses.
   ***/
  Closure_add_child(w, spawn_parent, spawn_child);

  (victim_w->head)++;
  // ANGE: we set this frame lazily 
  spawn_child->frame = (__cilkrts_stack_frame *) NULL; 

  /* insert the closure on the victim processor's deque */
  deque_add_bottom(w, spawn_child, pn);
	
  /* at this point the child can be freely executed */
  return spawn_child;
}

/***
 * Finishes the promotion process.  The child is already fully promoted
 * and requires no more work (we only use the given pointer to identify
 * the child).  This function does some more work on the parent to make
 * the promotion complete.
 *
 * ANGE: This includes remapping the thief's stack, promote everything along
 * the stolen stacklet into full closures, and finally fix up the stack
 * mapping if we mapped too much or too little the first time around.
 ***/
void finish_promote(__cilkrts_worker *const w, 
		    __cilkrts_worker *const victim_w,
		    Closure *parent, Closure *child) {

  CILK_ASSERT(w, parent->frame->worker == victim_w); 

  Closure_assert_ownership(w, parent);
  Closure_assert_alienation(w, child);
  CILK_ASSERT(w, parent->has_cilk_callee == 0);

  /* the parent is still locked; we still need to update it */
  /* join counter update */
  parent->join_counter++;

  // ANGE: the "else" case applies to a closure which has its frame 
  // set, but not its frame_rsp.  These closures may have been stolen 
  // before as part of a stacklet, so its frame is set (and stolen 
  // flag is set), but its frame_rsp is not set, because it didn't 
  // spawn until now.
  if(__cilkrts_not_stolen(parent->frame)) {
    setup_closures_in_stacklet(w, victim_w, parent);
  }
  //fixup_stack_mapping(w, parent);
	
  CILK_ASSERT(w, parent->frame->worker == victim_w); 

  __cilkrts_set_unsynced(parent->frame);
  /* Make the parent ready */
  Closure_make_ready(parent);

  return;
}

/*
 * stealing protocol.  Tries to steal from the victim; returns a
 * stolen closure, or NULL if none.
 */
Closure *Closure_steal(__cilkrts_worker *const w, int victim) {

  Closure *res = (Closure *) NULL;
  Closure *cl, *child;
  __cilkrts_worker *victim_w;
  cilk_fiber *fiber = NULL;
  cilk_fiber *parent_fiber = NULL;
  
  int success = 0;

  //----- EVENT_STEAL_ATTEMPT

  if( deque_trylock(w, victim) == 0 ) {
    return NULL; 
  }

  cl = deque_peek_top(w, victim);
    
  if (cl) {
    if( Closure_trylock(w, cl) == 0 ) {
      deque_unlock(w, victim);
      return NULL;
    }

    __cilkrts_alert(ALERT_STEAL, "[%d]: trying steal from W%d; cl=%p\n", w->self, victim, cl);
    victim_w = w->g->workers[victim];

    switch (cl->status) {
    case CLOSURE_READY:
      __cilkrts_bug("Bug: ready closure in ready deque\n");
      break;

    case CLOSURE_RUNNING:
      /* send the exception to the worker */
      if (do_dekker_on(w, victim_w, cl)) {
        __cilkrts_alert(ALERT_STEAL, 
            "[%d]: (Closure_steal) can steal from W%d; cl=%p\n", w->self, victim, cl);
	fiber = cilk_fiber_allocate(w);
	parent_fiber = cl->fiber;
	
	/* 
	 * if we could send the exception, promote
	 * the child to a full closure, and steal
	 * the parent
	 */
	child = promote_child(w, victim_w, cl, &res);
        __cilkrts_alert(ALERT_STEAL, 
            "[%d]: (Closure_steal) promote gave cl/res/child = %p/%p/%p\n", w->self, cl, res, child);

	/* detach the parent */
	// ANGE: the top of the deque could have changed in the else case.
	if(res == (Closure *) NULL) {
	  res = deque_xtract_top(w, victim);
	  CILK_ASSERT(w, cl == res);
	}
        CILK_ASSERT(w, res->frame->worker == victim_w); 
	Closure_assert_ownership(w, res);
	finish_promote(w, victim_w, res, child);
	
	res->fiber = fiber;
	child->fiber = parent_fiber;
        __cilkrts_alert(ALERT_STEAL, 
            "[%d]: (Closure_steal) cl/res/child = %p/%p/%p\n", w->self, cl, res, child);
        __cilkrts_alert(ALERT_FIBER, 
            "[%d]: (Closure_steal) cl/res/child now have fiber %p/%p/%p\n", 
            w->self, 0, fiber, parent_fiber);
	deque_unlock(w, victim); // at this point, more steals can happen from the victim.

	CILK_ASSERT(w, res->right_most_child == child);
	CILK_ASSERT(w, res->frame->worker == victim_w); 
	Closure_unlock(w, res);

	//----- EVENT_STEAL
	success = 1;

      } else {
	//----- EVENT_STEAL_NO_DEKKER
        // __cilkrts_alert(ALERT_STEAL, "Worker %d: steal failed: dekker fail\n", w->self);

	goto give_up;
      }
      break;

    case CLOSURE_SUSPENDED:
      __cilkrts_bug("Bug: suspended closure in ready deque\n");
      break;

    case CLOSURE_RETURNING:
      /* ok, let it leave alone */
      //----- EVENT_STEAL_RETURNING
      // __cilkrts_alert(ALERT_STEAL, "Worker %d: steal failed: returning closure\n", w->self);

    give_up:
      // MUST unlock the closure before the queue;
      // see rule D in the file PROTOCOLS
      Closure_unlock(w, cl);
      deque_unlock(w, victim);
      break;

    default:
      __cilkrts_bug("Bug: unknown closure status\n");
    }
  } else {
    deque_unlock(w, victim);
    //----- EVENT_STEAL_EMPTY_DEQUE
  }
 
  if (success) {
    __cilkrts_alert(ALERT_STEAL, "[%d]: (Closure_steal) stole from [%d]; res=%p\n", w->self, victim, res);
    // Since our steal was successful, finish initialization of
    // the fiber.
    // cilk_fiber_reset_state(fiber, fiber_proc_to_resume_user_code_for_random_steal);
  }

  return res;
}


// ==============================================
// Scheduling functions 
// ==============================================

#include <stdio.h>

__attribute__((noreturn))
void longjmp_to_user_code(__cilkrts_worker * w, Closure *t) {
  __cilkrts_stack_frame *sf = t->frame;
  cilk_fiber *fiber = t->fiber;

  CILK_ASSERT(w, sf && fiber);

  if(w->l->provably_good_steal) {
    // in this case, we simply longjmp back into the original fiber
    // the SP(sf) has been updated with the right orig_rsp already
    CILK_ASSERT(w, t->orig_rsp == NULL);
    CILK_ASSERT(w, ((char *)FP(sf) > fiber->m_stack) 
             && ((char *)FP(sf) < fiber->m_stack_base));
    CILK_ASSERT(w, ((char *)SP(sf) > fiber->m_stack) 
             && ((char *)SP(sf) < fiber->m_stack_base));
    w->l->provably_good_steal = 0; // unset
  } else { // this is stolen work; the fiber is a new fiber
    if(t == w->g->invoke_main && w->g->invoke_main_initialized == 0) {
        w->g->invoke_main_initialized = 1; 
        init_fiber_run(fiber, sf);
    } else {
      void *new_rsp = sysdep_reset_jump_buffers_for_resume(fiber, sf); 
      USE_UNUSED(new_rsp);
      CILK_ASSERT(w, SP(sf) == new_rsp);
    }
  }
  sysdep_longjmp_to_sf(sf);
}

__attribute__((noreturn))
void longjmp_to_runtime(__cilkrts_worker * w) {

  __cilkrts_alert(ALERT_SCHED | ALERT_FIBER, 
                  "[%d]: (longjmp_to_runtime)\n", w->self);

  // Current fiber is either the (1) one we are about to free,
  // or (2) it has been passed up to the parent.
  // cilk_fiber *current_fiber = __cilkrts_get_tls_cilk_fiber();

  // Clear the sf in the current fiber for cleanliness, to prevent
  // us from accidentally resuming a bad sf.
  // Technically, resume_sf gets overwritten for a fiber when
  // we are about to resume it anyway.
  // current_fiber->resume_sf = NULL;
  // CILK_ASSERT(w, current_fiber->owner == w);

  if (w->l->fiber_to_free) {
    __cilkrts_alert(ALERT_FIBER, "[%d]: (longjmp_to_runtime) freeing fiber %p\n", 
                    w->self, w->l->fiber_to_free);
    // Case 1: we are freeing this fiber.  We never
    // resume this fiber again after jumping into the runtime.
    // CILK_ASSERT(w, current_fiber == w->l->fiber_to_free);
    // w->l->fiber_to_free = NULL;

    // Extra check. Normally, the fiber we are about to switch to
    // should have a NULL owner.
    // CILK_ASSERT(w, NULL == w->l->runtime_fiber->owner);

    /*
    cilk_fiber_remove_reference_from_self_and_resume_other(current_fiber,
							   w->l->runtime_fiber);
    // We should never come back here!
    CILK_ASSERT(w, 0);
    */

  } /* else {
    __cilkrts_alert(ALERT_FIBER, "[%d]: (longjmp_to_runtime) passing fiber %p\n", 
                    w->self, current_fiber);
    // Case 2: We are passing the fiber to our parent because we
    // are leftmost.  We should come back later to
    // resume execution of user code.
    //
    // If we are not freeing a fiber, there we must be
    // returning from a spawn or processing an exception.  The
    // "sync" path always frees a fiber.
    // 
    // We must be the leftmost child, and by left holder logic, we
    // have already moved the current fiber into our parent full
    // frame.

    cilk_fiber_suspend_self_and_resume_other(current_fiber,
					     w->l->runtime_fiber);
    // Resuming this fiber returns control back to
    // this function because our implementation uses OS fibers.
    //
    // On Unix, we could have the choice of passing the
    // user_code_resume_after_switch_into_runtime as an extra "resume_proc"
    // that resumes execution of user code instead of the
    // jumping back here, and then jumping back to user code.

    user_code_resume_after_switch_into_runtime(current_fiber);
  } */

  // __builtin_longjmp(w->l->runtime_fiber->ctx, 1);

    /* 
    char * rsp = NULL;
    ASM_GET_SP(rsp);
    ASM_SET_SP(w->l->runtime_fiber->m_stack_base);
    worker_scheduler(w);
    ASM_SET_SP(rsp);
    */
  // __cilkrts_alert(3, "[%d]: (longjmp_to_runtime) exit\n", w->self);

  __builtin_longjmp(w->l->rts_ctx, 1);
}

/* at a slow sync; return 0 if the sync succeeds, and 1 if suspended */
/* ANGE: The return value is opposite of what I thought */
int Cilk_sync(__cilkrts_worker *const w, __cilkrts_stack_frame *frame) {
  
  __cilkrts_alert(ALERT_SYNC, "[%d]: (Cilk_sync) frame %p\n", w->self, frame);

  Closure *t;
  int res = SYNC_READY;

  //----- EVENT_CILK_SYNC

  deque_lock_self(w);
  t = deque_peek_bottom(w, w->self); 
  Closure_lock(w, t);
  /* assert we are really at the top of the stack */
  CILK_ASSERT(w, Closure_at_top_of_stack(w));

  // reset_closure_frame(w, t);
  CILK_ASSERT(w, w == __cilkrts_get_tls_worker());
  CILK_ASSERT(w, t->status == CLOSURE_RUNNING);
  CILK_ASSERT(w, t->frame != NULL);
  CILK_ASSERT(w, t->frame == frame);
  CILK_ASSERT(w, frame->worker == w);
  CILK_ASSERT(w, __cilkrts_stolen(t->frame));
  CILK_ASSERT(w, t->has_cilk_callee == 0);
  // CILK_ASSERT(w, w, t->frame->magic == CILK_STACKFRAME_MAGIC);
 
  // ANGE: we might have passed a sync successfully before and never gotten back to
  // runtime but returning to another ancestor that needs to sync ... in which
  // case we might have a fiber to free, but it's never the same fiber that we
  // are on right now.
  if(w->l->fiber_to_free) {
      CILK_ASSERT(w, w->l->fiber_to_free != t->fiber);
      // we should free this fiber now and we can as long as we are not on it
      cilk_fiber_deallocate(w->l->fiber_to_free);
      w->l->fiber_to_free = NULL;
  }

  if(Closure_has_children(t)) {
    // MAK: FIBER-SYNC GUESS
    __cilkrts_alert(ALERT_SYNC, "[%d]: (Cilk_sync) outstanding children\n", w->self, frame);

    w->l->fiber_to_free = t->fiber;
    t->fiber = NULL;
    // place holder for reducer map; the view in tlmm (if any) are updated 
    // by the last strand in Closure t before sync; need to reduce 
    // these when successful provably good steal occurs
    Closure_suspend(w, t);
    res = SYNC_NOT_READY;
  } else {
    setup_for_sync(w, t);
  }

  Closure_unlock(w, t);
  deque_unlock_self(w);

  return res;
}
static Closure * do_what_it_says(__cilkrts_worker * w, Closure *t) {

  Closure *res = NULL;
  __cilkrts_stack_frame *f;

  __cilkrts_alert(ALERT_SCHED, "[%d]: (do_what_it_says) closure %p\n", w->self, t);
  Closure_lock(w, t);

  switch (t->status) {
  case CLOSURE_READY:
    // ANGE: anything we need to free must have been freed at this point
    CILK_ASSERT(w, w->l->fiber_to_free == NULL);

    __cilkrts_alert(ALERT_SCHED, "[%d]: (do_what_it_says) CLOSURE_READY\n", w->self);
    /* just execute it */
    setup_for_execution(w, t);
    f = t->frame;
    // t->fiber->resume_sf = f; // I THINK this works
    __cilkrts_alert(ALERT_SCHED, "[%d]: (do_what_it_says) resume_sf = %p\n", w->self, f);
    CILK_ASSERT(w, f);
    Closure_unlock(w, t);
    
    // MUST unlock the closure before locking the queue 
    // (rule A in file PROTOCOLS)
    deque_lock_self(w);
    deque_add_bottom(w, t, w->self);
    deque_unlock_self(w);
    
    /* now execute it */
    __cilkrts_alert(ALERT_SCHED, "[%d]: (do_what_it_says) Jump into user code.\n", w->self);

    // CILK_ASSERT(w, w->l->runtime_fiber != t->fiber);
    // cilk_fiber_suspend_self_and_resume_other(w->l->runtime_fiber, t->fiber);
    // __cilkrts_alert(ALERT_SCHED, "[%d]: (do_what_it_says) Back from user code.\n", w->self);
    if( __builtin_setjmp(w->l->rts_ctx) == 0 ) {
      longjmp_to_user_code(w, t);
    } else {
      CILK_ASSERT(w, w == __cilkrts_get_tls_worker());
      // CILK_ASSERT(w, t->fiber == w->l->fiber_to_free);
      if(w->l->fiber_to_free) { cilk_fiber_deallocate(w->l->fiber_to_free); }
      w->l->fiber_to_free = NULL;
    }

    break; // ?

  case CLOSURE_RETURNING:
    __cilkrts_alert(ALERT_SCHED, "[%d]: (do_what_it_says) CLOSURE_RETURNING\n", w->self);
    // the return protocol assumes t is not locked, and everybody 
    // will respect the fact that t is returning
    Closure_unlock(w, t);
    res = return_value(w, t);

    break; // ?

  default:
    __cilkrts_alert(ALERT_SCHED, "[%d]: (do_what_it_says) got status %d.\n", 
            w->self, t->status);
    __cilkrts_bug("BUG in do_what_it_says()\n");
    break;
  }

  return res;
}

void worker_scheduler(__cilkrts_worker * w, Closure * t) {

  CILK_ASSERT(w, w == __cilkrts_get_tls_worker());
  rts_srand(w, w->self * 162347);

  while (!w->g->done) {
    if (!t) {
      // try to get work from our local queue
      deque_lock_self(w);
      t = deque_xtract_bottom(w, w->self);
      deque_unlock_self(w);
    }

    while (!t && !w->g->done) {
      int victim = rts_rand(w) % w->g->options.nproc;
      if( victim != w->self ) {
	t = Closure_steal(w, victim);
      }
    }

    if (!w->g->done) {
      // if provably-good steal happens, do_what_it_says will return
      // the next closure to execute
      t = do_what_it_says(w, t);
    }
  }
}

