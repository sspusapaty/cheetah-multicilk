#include "sched.h"
#include "cilk2c.h"
#include "jmpbuf.h"
#include "tls.h"
#include "fiber-procs.h"
#include "return.h"
#include "readydeque.h"
#include "exception.h"
#include "steal.h"
#include "rts_rand.h"

#include <stdio.h>

__attribute__((noreturn))
void longjmp_to_user_code(__cilkrts_worker * ws, Closure *t) {
  __cilkrts_stack_frame *sf = t->frame;
  cilk_fiber *fiber = t->fiber;

  CILK_ASSERT(sf && fiber);

  if(ws->l->provably_good_steal) {
    // in this case, we simply longjmp back into the original fiber
    // the SP(sf) has been updated with the right orig_rsp already
    CILK_ASSERT(t->orig_rsp == NULL);
    if( !((char *)FP(sf) > fiber->m_stack) || !((char *)FP(sf) < fiber->m_stack_base)) {
        fprintf(stderr, "%d: bp: %p, sp: %p, m_stack: %p, m_stack_base %p\n", 
                ws->self, FP(sf), SP(sf), fiber->m_stack, fiber->m_stack_base); 
    }
    CILK_ASSERT(((char *)FP(sf) > fiber->m_stack) 
             && ((char *)FP(sf) < fiber->m_stack_base));
    CILK_ASSERT(((char *)SP(sf) > fiber->m_stack) 
             && ((char *)SP(sf) < fiber->m_stack_base));
    ws->l->provably_good_steal = 0; // unset
  } else { // this is stolen work; the fiber is a new fiber
    if(t == ws->g->invoke_main && ws->g->invoke_main_initialized == 0) {
        ws->g->invoke_main_initialized = 1; 
        init_fiber_run(fiber, sf);
    } else {
      void *new_rsp = sysdep_reset_jump_buffers_for_resume(fiber, sf); 
      CILK_ASSERT(SP(sf) == new_rsp);
    }
  }
  sysdep_longjmp_to_sf(sf);
}

__attribute__((noreturn))
void longjmp_to_runtime(__cilkrts_worker * w) {

  __cilkrts_alert(ALERT_SCHED | ALERT_FIBER, "[%d]: (longjmp_to_runtime)\n", w->self);

  // Current fiber is either the (1) one we are about to free,
  // or (2) it has been passed up to the parent.
  // cilk_fiber *current_fiber = __cilkrts_get_tls_cilk_fiber();

  // Clear the sf in the current fiber for cleanliness, to prevent
  // us from accidentally resuming a bad sf.
  // Technically, resume_sf gets overwritten for a fiber when
  // we are about to resume it anyway.
  // current_fiber->resume_sf = NULL;
  // CILK_ASSERT(current_fiber->owner == w);

  if (w->l->fiber_to_free) {
    __cilkrts_alert(ALERT_FIBER, "[%d]: (longjmp_to_runtime) freeing fiber %p\n", 
                    w->self, w->l->fiber_to_free);
    // Case 1: we are freeing this fiber.  We never
    // resume this fiber again after jumping into the runtime.
    // CILK_ASSERT(current_fiber == w->l->fiber_to_free);
    // w->l->fiber_to_free = NULL;

    // Extra check. Normally, the fiber we are about to switch to
    // should have a NULL owner.
    // CILK_ASSERT(NULL == w->l->runtime_fiber->owner);

    /*
    cilk_fiber_remove_reference_from_self_and_resume_other(current_fiber,
							   w->l->runtime_fiber);
    // We should never come back here!
    CILK_ASSERT(0);
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

Closure *setup_for_execution(__cilkrts_worker * ws, Closure *t) {
  __cilkrts_alert(ALERT_SCHED, "[%d]: (setup_for_execution) closure %p\n", ws->self, t);
  t->frame->worker = ws;
  t->status = CLOSURE_RUNNING;

  ws->head = (__cilkrts_stack_frame **) ws->l->shadow_stack+1;
  ws->tail = (__cilkrts_stack_frame **) ws->l->shadow_stack+1;

  /* push the first frame on the current_stack_frame */
  ws->current_stack_frame = t->frame;	

  reset_exception_pointer(ws, t);

  return t;
}

// ANGE: When this is called, either a) a worker is about to pass a sync (though not on
// the right fiber), or b) a worker just performed a provably good steal
// successfully
Closure *setup_for_sync(__cilkrts_worker *ws, Closure *t) {

  Closure_assert_ownership(ws, t);
  // ANGE: this must be true since in case a) we would have freed it in
  // Cilk_sync, or in case b) we would have freed it when we first returned to
  // the runtime before doing the provably good steal.
  CILK_ASSERT(ws->l->fiber_to_free == NULL);
  CILK_ASSERT(t->fiber != t->fiber_child);
  
  // ANGE: note that in case a) this fiber won't get freed for awhile, since
  // we will longjmp back to the original function's fiber and never go back to the 
  // runtime; we will only free it either once when we get back to the runtime
  // or when we encounter a case where we need to.
  ws->l->fiber_to_free = t->fiber; 
  t->fiber = t->fiber_child;
  t->fiber_child = NULL;
  __cilkrts_alert(ALERT_STEAL | ALERT_FIBER, 
      "[%d]: (setup_for_sync) set t %p and t->fiber %p\n", ws->self, t, t->fiber);
  __cilkrts_set_synced(t->frame);

  SP(t->frame) = (void *) t->orig_rsp;
  t->orig_rsp = NULL; // unset once we have sync-ed

  return t;
}

Closure *do_what_it_says(__cilkrts_worker * ws, Closure *t) {

  Closure *res = NULL;
  __cilkrts_stack_frame *f;

  __cilkrts_alert(ALERT_SCHED, "[%d]: (do_what_it_says) closure %p\n", ws->self, t);
  Closure_lock(ws, t);

  switch (t->status) {
  case CLOSURE_READY:
    // ANGE: anything we need to free must have been freed at this point
    CILK_ASSERT(ws->l->fiber_to_free == NULL);

    __cilkrts_alert(ALERT_SCHED, "[%d]: (do_what_it_says) CLOSURE_READY\n", ws->self);
    /* just execute it */
    setup_for_execution(ws, t);
    f = t->frame;
    // t->fiber->resume_sf = f; // I THINK this works
    __cilkrts_alert(ALERT_SCHED, "[%d]: (do_what_it_says) resume_sf = %p\n", ws->self, f);
    CILK_ASSERT(f);
    Closure_unlock(ws, t);
    
    // MUST unlock the closure before locking the queue 
    // (rule A in file PROTOCOLS)
    deque_lock_self(ws);
    deque_add_bottom(ws, t, ws->self);
    deque_unlock_self(ws);
    
    /* now execute it */
    __cilkrts_alert(ALERT_SCHED, "[%d]: (do_what_it_says) Jump into user code.\n", ws->self);

    // CILK_ASSERT(ws->l->runtime_fiber != t->fiber);
    // cilk_fiber_suspend_self_and_resume_other(ws->l->runtime_fiber, t->fiber);
    // __cilkrts_alert(ALERT_SCHED, "[%d]: (do_what_it_says) Back from user code.\n", ws->self);
    if( __builtin_setjmp(ws->l->rts_ctx) == 0 ) {
      longjmp_to_user_code(ws, t);
    } else {
      CILK_ASSERT(ws == __cilkrts_get_tls_worker());
      if(ws->l->fiber_to_free) { cilk_fiber_deallocate_to_heap(ws->l->fiber_to_free); }
      ws->l->fiber_to_free = NULL;
    }

    break; // ?

  case CLOSURE_RETURNING:
    __cilkrts_alert(ALERT_SCHED, "[%d]: (do_what_it_says) CLOSURE_RETURNING\n", ws->self);
    // the return protocol assumes t is not locked, and everybody 
    // will respect the fact that t is returning
    Closure_unlock(ws, t);
    res = return_value(ws, t);

    break; // ?

  default:
    __cilkrts_alert(ALERT_SCHED, "[%d]: (do_what_it_says) got status %d.\n", 
            ws->self, t->status);
    __cilkrts_bug("BUG in do_what_it_says()\n");
    break;
  }

  return res;
}

void worker_scheduler(__cilkrts_worker * ws, Closure * t) {
  CILK_ASSERT(ws == __cilkrts_get_tls_worker());
  __cilkrts_alert(ALERT_SCHED, "[%d]: (worker_scheduler) closure %p\n", ws->self, t);

  rts_srand(ws, ws->self * 162347);

  while (!ws->g->done) {
    __cilkrts_alert(ALERT_SCHED, 
            "[%d]: (worker_scheduler) Looking for work\n", ws->self);
    if (!t) {
      // try to get work from our local queue
      __cilkrts_alert(ALERT_SCHED, 
            "[%d]: (worker_scheduler) No pre-existing work!  Checking local deque\n", ws->self);
      deque_lock_self(ws);
      t = deque_xtract_bottom(ws, ws->self);
      deque_unlock_self(ws);
    }

    if (!t) __cilkrts_alert(ALERT_SCHED, "[%d]: (worker_scheduler) Nothing in local deque!  Trying to steal\n", ws->self);

    while (!t && !ws->g->done) {
      int victim = rts_rand(ws) % ws->g->active_size;
      if( victim != ws->self ) {
	t = Closure_steal(ws, victim);
      }
    }

    if (!ws->g->done) {
      // if provably-good steals happened, it will contain
      // the next closure to execute
      t = do_what_it_says(ws, t);
      __cilkrts_alert(ALERT_SCHED, "[%d]: (worker_scheduler) do_what_it_says returned %p\n", ws->self, t);
    }
  }
}

/*
void Cilk_scheduler(__cilkrts_worker *const ws, Closure *t) {
{


   // t contains 'the next thing to do'.  Initially, the
   // scheduler on proc 0 executes the main closure.

  int victim;
  unsigned const int active_size = ws->g->active_size;

  CILK_ASSERT(ws->l->magic == CILK_WS_MAGIC + ((unsigned int)ws->self));

  rts_srand(ws, ws->self * 162347);

  while (!ws->g->done) {
    if (!t) {
      // try to get work from our local queue 
      deque_lock_self(ws);
      t = deque_xtract_bottom(ws, ws->self);
      deque_unlock_self(ws);
    }

    while (!t && !ws->g->done) {

      victim = rts_rand(ws) % active_size;
      if( victim != ws->self ) {
	t = Closure_steal(ws, victim);
      }
    }

    if (!ws->g->done) {
      // if provably-good steals happened, it will contain
      // the next closure to execute
      t = do_what_it_says(ws, t);
    }
  }

  return;
}
*/
