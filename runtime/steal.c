#include "steal.h"
#include "cilk2c.h"
#include "common.h"
#include "exception.h"
#include "membar.h"
#include "readydeque.h"
#include "fiber-procs.h"

//---------- 3 ----------//

/* 
 * ANGE: Cannot call this function until we have done the stack remapping
 * for the stolen closure.
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

/* 
 * ANGE: Cannot call this function until we have done the stack remapping
 * for the stolen closure 
 */
Closure * setup_call_parent_closure_helper(__cilkrts_worker *const ws, 
					   __cilkrts_worker *const victim_ws, 
					   __cilkrts_stack_frame *frame,
					   Closure *oldest) {

  Closure *call_parent, *curr_cl;

  if(oldest->frame == frame) {
    CILK_ASSERT(__cilkrts_stolen(oldest->frame));
    return oldest;
  }
 
  call_parent = setup_call_parent_closure_helper(ws, victim_ws, 
						 frame->call_parent, oldest);
  __cilkrts_set_stolen(frame);
  curr_cl = Closure_create(ws);    
  curr_cl->frame = frame;

  CILK_ASSERT(frame->worker == victim_ws); 
  curr_cl->status = CLOSURE_SUSPENDED;
  curr_cl->frame->worker = (__cilkrts_worker *) NOBODY;
    
  Closure_add_callee(ws, call_parent, curr_cl);

  return curr_cl;
}

//---------- 2 ----------//

/* 
 * ANGE: Cannot call this function until we have done the stack remapping
 * for the stolen closure 
 */
void setup_closures_in_stacklet(__cilkrts_worker *const ws, 
				__cilkrts_worker *const victim_ws, 
				Closure *youngest_cl) {

  Closure *call_parent;
  Closure *oldest_cl = youngest_cl->call_parent;
  __cilkrts_stack_frame *youngest, *oldest;  

  youngest = youngest_cl->frame; 
  oldest = oldest_non_stolen_frame_in_stacklet(youngest);

  CILK_ASSERT(youngest == youngest_cl->frame);
  CILK_ASSERT(youngest->worker == victim_ws); 
  CILK_ASSERT(__cilkrts_not_stolen(youngest));
    
  CILK_ASSERT((oldest_cl->frame == NULL &&
	       oldest != youngest) || 
	      (oldest_cl->frame == oldest->call_parent &&
	       __cilkrts_stolen(oldest_cl->frame)) );

  if( oldest_cl->frame ==  NULL ) {
    CILK_ASSERT(__cilkrts_not_stolen(oldest));
    CILK_ASSERT(oldest->flags & CILK_FRAME_DETACHED);
    __cilkrts_set_stolen(oldest); 
    oldest_cl->frame = oldest;
  }
  CILK_ASSERT(oldest->worker == victim_ws); 
  oldest_cl->frame->worker = (__cilkrts_worker *) NOBODY;

  call_parent = setup_call_parent_closure_helper(ws, 
						 victim_ws, youngest->call_parent, oldest_cl);
  __cilkrts_set_stolen(youngest);

  Closure_add_callee(ws, call_parent, youngest_cl);
}

//---------- 1 ----------//

/*
 * Do the thief part of Dekker's protocol.  Return 1 upon success,
 * 0 otherwise.  The protocol fails when the victim already popped
 * T so that E=T.
 */
int do_dekker_on(__cilkrts_worker *const ws, 
		 __cilkrts_worker *const victim_ws, 
		 Closure *cl) {

  Closure_assert_ownership(ws, cl);

  increment_exception_pointer(ws, victim_ws, cl);
  Cilk_membar_StoreLoad(); 

  /* 
   * ANGE: the thief won't steal from this victim if there is only one
   * frame on cl's stack
   */
  if(victim_ws->head >= victim_ws->tail) {
    decrement_exception_pointer(ws, victim_ws, cl);
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
 * 
 * ANGE: with shadow frames alloced on the TLMM stack, the thief cannot 
 * read the content of the frames (it's trying to steal) until it remaps 
 * its own TLMM stack accordingly.  But we don't want to remap while holding
 * the deque lock, so the thief must guess the low watermark for the frame,
 * and gather information necessary to remap its TLMM stack before 
 * releasing the lock on victim's deque.  Furthermore, the thief must 
 * delay promoting the frames in the stolen stacklet until it remaps its
 * stack.   
 ***/
Closure *promote_child(__cilkrts_worker *const ws,
		       __cilkrts_worker *const victim_ws, 
		       Closure *cl, Closure **res) {
                            
  int pn = victim_ws->self;

  Closure_assert_ownership(ws, cl);

  deque_assert_ownership(ws, pn);
  CILK_ASSERT(cl->status == CLOSURE_RUNNING);
  CILK_ASSERT(cl->owner_ready_deque == pn);
  CILK_ASSERT(cl->next_ready == NULL);
  /* cl may have a call parent: it might be promoted as its containing
   * stacklet is stolen, and it's call parent is promoted into full and
   * suspended
   */
  CILK_ASSERT(cl == ws->g->invoke_main || cl->spawn_parent || cl->call_parent);

  Closure *spawn_parent = NULL;
  __cilkrts_stack_frame **head =  victim_ws->head;  
  __cilkrts_stack_frame *frame_to_steal = *head;  

  /* can't promote until we are sure nobody works on the frame */
  /* ANGE: Why does checking H <= E  ensures that nobody works on the
   * frame ??? */
  CILK_ASSERT(head <= victim_ws->exc);
  /* ANGE: this must be true if we get this far */
  // Note that it can be that H == T here; victim could have done
  // T-- after the thief has done Dekker; in which case, thief gets the last
  // frame, and H == T. Victim won't be able to proceed further until 
  // the thief finishes stealing, releasing the deque lock; at which point, 
  // the victim will realize that it should return back to runtime.
  CILK_ASSERT(head <= victim_ws->tail);
  CILK_ASSERT(frame_to_steal != NULL);

  // ANGE: if cl's frame is not set, the top stacklet must contain more 
  // than one frame, because the right-most (oldest) frame must be a spawn
  // helper which can only call a Cilk function.  On the other hand, if
  // cl's frame is set AND equal to the frame at *HEAD, cl must be either
  // the root frame (invoke_main) or have been stolen before.
  // MAK: With the logic change, not sure this is valid
  if(cl->frame == frame_to_steal) {
    spawn_parent = cl;
  } else { // cl->frame could either be NULL or some older frame 
    // ANGE: if this is true, we must create a new Closure representing
    // the left-most frame (the one to be stolen and resume). 
    // We will then setup the rest after we perform the mapping in
    // thief's stack.
    spawn_parent = Closure_create(ws);
    spawn_parent->frame = frame_to_steal;
    spawn_parent->status = CLOSURE_RUNNING;

    // ANGE: this is only temporary; will reset this after the stack has
    // been remapped; so lets not set the callee in cl yet ... although
    // we do need to set the has_callee in cl, so that cl does not get
    // resumed by some other child performing provably good steal.
    Closure_add_temp_callee(ws, cl, spawn_parent);
    spawn_parent->call_parent = cl; 
    

    // suspend cl & remove it from deque
    Closure_suspend_victim(ws, pn, cl);  
    Closure_unlock(ws, cl);

    Closure_lock(ws, spawn_parent);
    *res = spawn_parent;
  }

  CILK_ASSERT(spawn_parent->has_cilk_callee == 0);
  Closure *spawn_child = Closure_create(ws);

  spawn_child->spawn_parent = spawn_parent;
  spawn_child->status = CLOSURE_RUNNING;

  /***
   * Register this child, which sets up its siblinb links.
   * We do this here intead of in finish_promote, because we must setup 
   * the sib links for the new child before its pointer escapses.
   ***/
  Closure_add_child(ws, spawn_parent, spawn_child);

  (victim_ws->head)++;
  // ANGE: we set this frame lazily 
  spawn_child->frame = (__cilkrts_stack_frame *) NULL; 

  /* insert the closure on the victim processor's deque */
  deque_add_bottom(ws, spawn_child, pn);

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
void finish_promote(__cilkrts_worker *const ws, 
		    __cilkrts_worker *const victim_ws,
		    Closure *parent, Closure *child) {

  Closure_assert_ownership(ws, parent);
  Closure_assert_alienation(ws, child);
  CILK_ASSERT(parent->has_cilk_callee == 0);

  /* the parent is still locked; we still need to update it */
  /* join counter update */
  parent->join_counter++;

  // ANGE: the "else" case applies to a closure which has its frame 
  // set, but not its frame_rsp.  These closures may have been stolen 
  // before as part of a stacklet, so its frame is set (and stolen 
  // flag is set), but its frame_rsp is not set, because it didn't 
  // spawn until now.
  if(__cilkrts_not_stolen(parent->frame)) {
    setup_closures_in_stacklet(ws, victim_ws, parent);
  }
  //fixup_stack_mapping(ws, parent);
	
  CILK_ASSERT(parent->frame->worker == victim_ws); 

  __cilkrts_set_unsynced(parent->frame);
  /* Make the parent ready */
  Closure_make_ready(parent);

  return;
}

//---------- 0 ----------//

/*
 * stealing protocol.  Tries to steal from the victim; returns a
 * stolen closure, or NULL if none.
 */
Closure *Closure_steal(__cilkrts_worker *const ws, int victim) {

  Closure *res = (Closure *) NULL;
  Closure *cl, *child;
  __cilkrts_worker *victim_ws;
  cilk_fiber *fiber = NULL;
  cilk_fiber *parent_fiber = NULL;
  
  int success = 0;

  //----- EVENT_STEAL_ATTEMPT

  if( deque_trylock(ws, victim) == 0 ) {
    return NULL; 
  }

  cl = deque_peek_top(ws, victim);
    
  if (cl) {
    if( Closure_trylock(ws, cl) == 0 ) {
      deque_unlock(ws, victim);
      return NULL;
    }

    __cilkrts_alert("Worker %d: trying steal from worker %d\n", ws->self, victim);
    victim_ws = ws->g->workers[victim];
    fiber = cilk_fiber_allocate_from_heap();

    switch (cl->status) {
    case CLOSURE_READY:
      __cilkrts_bug("Bug: ready closure in ready deque\n");
      break;

    case CLOSURE_RUNNING:
      /* send the exception to the worker */
      if (do_dekker_on(ws, victim_ws, cl)) {
	parent_fiber = cl->fiber;
	
	/* 
	 * if we could send the exception, promote
	 * the child to a full closure, and steal
	 * the parent
	 */
	child = promote_child(ws, victim_ws, cl, &res);

	/* detach the parent */
	// ANGE: the top of the deque could have changed in the
	// else case.
	if(res == (Closure *) NULL) {
	  res = deque_xtract_top(ws, victim);
	  CILK_ASSERT(cl == res);
	}
	Closure_assert_ownership(ws, res);
	    
	finish_promote(ws, victim_ws, res, child);

	// MAK: FIBER-RAND STEAL
	// I think this is an okay place
	cl->fiber = 0;
	res->fiber = fiber;
	child->fiber = parent_fiber;
	
	deque_unlock(ws, victim); // at this point, more steals can happen from the victim.

	CILK_ASSERT(res->right_most_child == child);
	CILK_ASSERT(res->frame->worker == victim_ws); 
	Closure_unlock(ws, res);

	//----- EVENT_STEAL
	success = 0;

      } else {
	//----- EVENT_STEAL_NO_DEKKER
	goto give_up;
      }

      break;

    case CLOSURE_SUSPENDED:
      __cilkrts_bug("Bug: suspended closure in ready deque\n");
      break;

    case CLOSURE_RETURNING:
      /* ok, let it leave alone */
      //----- EVENT_STEAL_RETURNING

    give_up:
      // MUST unlock the closure before the queue;
      // see rule D in the file PROTOCOLS
      Closure_unlock(ws, cl);
      deque_unlock(ws, victim);
      break;

    default:
      __cilkrts_bug("Bug: unknown closure status\n");
    }

  } else {
    deque_unlock(ws, victim);
    //----- EVENT_STEAL_EMPTY_DEQUE
  }
 
  if (0 == success) {
    __cilkrts_alert("Worker %d: steal failed...\n", ws->self);
  } else {
    __cilkrts_alert("Worker %d: steal succeeded!\n", ws->self);
    // Since our steal was successful, finish initialization of
    // the fiber.
    cilk_fiber_reset_state(fiber, fiber_proc_to_resume_user_code_for_random_steal);
  }
    
  return res;
}

//---------- - ----------//

Closure *provably_good_steal_maybe(__cilkrts_worker *const ws, Closure *parent) {

  Closure *res = (Closure *) NULL;

  Closure_assert_ownership(ws, parent);

  if (!Closure_has_children(parent) &&
      parent->status == CLOSURE_SUSPENDED) {

    CILK_ASSERT(parent->frame != NULL);
    CILK_ASSERT(parent->frame->worker == (__cilkrts_worker *) NOBODY);

    /* do a provably-good steal; this is *really* simple */
    res = parent;
    ws->l->provablyGoodSteal = 1;
    res->frame->worker = ws;
    // MAK: FIBER-GOOD STEAL
    res->fiber = res->fiber_child;
    res->fiber_child = NULL;
    
    //----- EVENT_PROVABLY_GOOD_STEAL

    /* debugging stuff */
    CILK_ASSERT(parent->owner_ready_deque == NOBODY);

    __cilkrts_set_synced(parent->frame);
    Closure_make_ready(parent);

  } else {
    ws->l->provablyGoodSteal = 0;
  }

  return res;
}
