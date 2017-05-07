#include "exception.h"
#include "membar.h"
#include "common.h"

void increment_exception_pointer(__cilkrts_worker *const ws, 
				 __cilkrts_worker *const victim_ws, 
				 Closure *cl) {

  Closure_assert_ownership(ws, cl);
  CILK_ASSERT(cl->status == CLOSURE_RUNNING);

  if(victim_ws->exc != EXCEPTION_INFINITY) {
    ++(victim_ws->exc);
    /* make sure the exception is visible, before we continue */
    Cilk_fence();
  }
}

void decrement_exception_pointer(__cilkrts_worker *const ws, 
				 __cilkrts_worker *const victim_ws, 
				 Closure *cl) {

  Closure_assert_ownership(ws, cl);
  CILK_ASSERT(cl->status == CLOSURE_RUNNING);

  if(victim_ws->exc != EXCEPTION_INFINITY)
    --(victim_ws->exc);
}

void reset_exception_pointer(__cilkrts_worker *const ws, Closure *cl) {

  Closure_assert_ownership(ws, cl);
  CILK_ASSERT((cl->frame == NULL) || 
	      (cl->frame->worker == ws) || 
	      (cl == ws->g->invoke_main && 
	       cl->frame->worker == (__cilkrts_worker *) NOBODY) );

  ws->exc = ws->head;
}


void signal_immediate_exception_to_all(__cilkrts_worker *const ws) {

    int i, active_size = ws->g->active_size;
    __cilkrts_worker *curr_ws;

    for(i=0; i<active_size; i++) {
        curr_ws = ws->g->workers[i];
        curr_ws->exc = EXCEPTION_INFINITY;
    }
    // make sure the exception is visible, before we continue
    Cilk_fence();
}
