#include "sched.h"
#include "jmpbuf.h"
#include "tls.h"

#include <stdio.h>

void longjmp_to_runtime(__cilkrts_worker * w) {
  __cilkrts_alert("Thread of worker %d: longjmp_to_runtime\n", w->self);

  if (__builtin_expect(w->l->runtime_fiber != NULL, 1)) {
    longjmp(w->l->runtime_fiber->ctx, 0);
  } else {
    w->l->runtime_fiber = cilk_fiber_allocate_from_heap();
    cilk_fiber_set_owner(w->l->runtime_fiber, w);

    
    char * rsp = NULL;
    ASM_GET_SP(rsp);
    ASM_SET_SP(w->l->runtime_fiber->m_stack_base);
    worker_scheduler(w);
    ASM_SET_SP(rsp);
    __cilkrts_alert("Thread of worker %d: exit longjmp_to_runtime\n", w->self);
   }
}
  

void worker_scheduler(__cilkrts_worker * w) {
  __cilkrts_alert("Thread of worker %d: worker_scheduler\n", w->self);
  
  CILK_ASSERT(w == __cilkrts_get_tls_worker());
}

void Cilk_scheduler(struct __cilkrts_worker *const ws, Closure *t) {

  /* 
   * t contains 'the next thing to do'.  Initially, the
   * scheduler on proc 0 executes the main closure.
   */
  int victim;
  unsigned const int active_size = USE_PARAMETER_WS(active_size);


  CILK_ASSERT(ws, ws->self >= 0);
  CILK_ASSERT(ws, ws->l->magic == CILK_WS_MAGIC + ((unsigned int)ws->self));

  rts_srand(ws, ws->self * 162347);

  Cilk_enter_state(ws, STATE_TOTAL);

  while (!USE_SHARED_WS(done)) {
    if (!t) {
      /* try to get work from our local queue */
      deque_lock_self(ws);
      t = deque_xtract_bottom(ws, ws->self);
      deque_unlock_self(ws);
    }

    while (!t && !USE_SHARED_WS(done)) {
      /* otherwise, steal */
      Cilk_enter_state_unspecified(ws);
            
            

      victim = rts_rand(ws) % active_size;
      if( victim != ws->self ) {
	t = Closure_steal(ws, victim);

	if(!t && USE_PARAMETER_WS(options->yieldslice) &&
	   !USE_SHARED_WS(done)) {
	  Cilk_lower_priority(ws);
	} 
      }
    }

    if (!USE_SHARED_WS(done)) {
      // if provably-good steals happened, it will contain
      // the next closure to execute
      t = do_what_it_says(ws, t);
    }
  }

  Cilk_exit_state(ws, STATE_TOTAL);

  return;
}
