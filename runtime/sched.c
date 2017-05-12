#include "sched.h"
#include "jmpbuf.h"
#include "tls.h"
#include "return.h"
#include "readydeque.h"
#include "exception.h"
#include "steal.h"
#include "rts_rand.h"

#include <stdio.h>

void longjmp_to_runtime(__cilkrts_worker * w) {
  Closure * cl;
  __cilkrts_alert(3, "[%d]: longjmp_to_runtime\n", w->self);

  // Current fiber is either the (1) one we are about to free,
  // or (2) it has been passed up to the parent.
  // deque_lock(w, w->self);
  // cl = deque_peek_bottom(w, w->self);
  // deque_unlock(w, w->self);
  
  cilk_fiber *current_fiber = __cilkrts_get_tls_cilk_fiber();

  //CILK_ASSERT(NULL == cl->fiber);


  // Clear the sf in the current fiber for cleanliness, to prevent
  // us from accidentally resuming a bad sf.
  // Technically, resume_sf gets overwritten for a fiber when
  // we are about to resume it anyway.
  current_fiber->resume_sf = NULL;
  CILK_ASSERT(current_fiber->owner == w);

    
  if (w->l->fiber_to_free) {
    // Case 1: we are freeing this fiber.  We never
    // resume this fiber again after jumping into the runtime.
    w->l->fiber_to_free = NULL;

    // Extra check. Normally, the fiber we are about to switch to
    // should have a NULL owner.
    CILK_ASSERT(NULL == w->l->runtime_fiber->owner);
  }
  
  cilk_fiber_suspend_self_and_resume_other(current_fiber,
					   w->l->runtime_fiber);

  // __builtin_longjmp(w->l->runtime_fiber->ctx, 1);

    /* 
    char * rsp = NULL;
    ASM_GET_SP(rsp);
    ASM_SET_SP(w->l->runtime_fiber->m_stack_base);
    worker_scheduler(w);
    ASM_SET_SP(rsp);
    */
  __cilkrts_alert(3, "[%d]: exit longjmp_to_runtime\n", w->self);
}

Closure *setup_for_execution(__cilkrts_worker * ws, Closure *t) {
  __cilkrts_alert(3, "[%d]: Preparing closure %p\n", ws->self, t);
  t->frame->worker = ws;
  t->status = CLOSURE_RUNNING;

  ws->head = (__cilkrts_stack_frame **) ws->l->shadow_stack+1;
  ws->tail = (__cilkrts_stack_frame **) ws->l->shadow_stack+1;

  /* push the first frame on the current_stack_frame */
  ws->current_stack_frame = t->frame;	

  reset_exception_pointer(ws, t);

  return t;
}

Closure *do_what_it_says(__cilkrts_worker * ws, Closure *t) {

  Closure *res = NULL;
  __cilkrts_stack_frame *f;

  
  __cilkrts_alert(3, "[%d]: do_what_it_says closure %p\n", ws->self, t);
  Closure_lock(ws, t);

  switch (t->status) {
  case CLOSURE_READY:
    /* just execute it */
    setup_for_execution(ws, t);
    f = t->frame;
    
    CILK_ASSERT(f);
	  
    Closure_unlock(ws, t);
    
    // MUST unlock the closure before locking the queue 
    // (rule A in file PROTOCOLS)
    deque_lock_self(ws);
    deque_add_bottom(ws, t, ws->self);
    deque_unlock_self(ws);
    
    /* now execute it */
    __cilkrts_alert(3, "[%d]: Jump into user code.\n", ws->self);

    CILK_ASSERT(ws->l->runtime_fiber != t->fiber);

    cilk_fiber_suspend_self_and_resume_other(ws->l->runtime_fiber, t->fiber);

    __cilkrts_alert(3, "[%d]: Back from user code.\n", ws->self);
	    
    break; // ?

  case CLOSURE_RETURNING:
    // the return protocol assumes t is not locked, and everybody 
    // will respect the fact that t is returning
    Closure_unlock(ws, t);

    res = return_value(ws, t);
	    
    break; // ?
  default:
    __cilkrts_bug("BUG in do_what_it_says()\n");
    break;
  }
  __cilkrts_alert(3, "[%d]: do_what_it_says returned %p\n", ws->self, res);
  return res;
}

void worker_scheduler(__cilkrts_worker * ws, Closure * t) {
  CILK_ASSERT(ws == __cilkrts_get_tls_worker());
  __cilkrts_alert(2, "[%d]: worker_scheduler\n", ws->self);

  rts_srand(ws, ws->self * 162347);

  while (!ws->g->done) {
    __cilkrts_alert(2, "[%d]: worker_scheduler loop\n", ws->self);
    if (!t) {
      // try to get work from our local queue
      __cilkrts_alert(3, "[%d]: no work!  Checking local deque\n", ws->self);
      deque_lock_self(ws);
      t = deque_xtract_bottom(ws, ws->self);
      deque_unlock_self(ws);
    }

    if (!t) __cilkrts_alert(3, "[%d]:  Checking for steal\n", ws->self);

    while (!t && !ws->g->done) {

      int victim = rts_rand(ws) % ws->g->active_size;
      if( victim != ws->self ) {
	__cilkrts_alert(5, "[%d]:  Checking worker %d for work\n", ws->self, victim);
	t = Closure_steal(ws, victim);
      }
    }

    if (!ws->g->done) {
      // if provably-good steals happened, it will contain
      // the next closure to execute
      t = do_what_it_says(ws, t);
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
