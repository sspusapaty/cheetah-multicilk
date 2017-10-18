#include "invoke_main.h"
#include "cilk2c.h"
#include "tls.h"
#include "sched.h"
#include "membar.h"

extern unsigned long ZERO;

extern int cilk_main(int argc, char * argv[]);

Closure * create_invoke_main(global_state *const g) {

  Closure *t;
  __cilkrts_stack_frame * sf;
  cilk_fiber * f;

  t = Closure_create();
  t->status = CLOSURE_READY;

  __cilkrts_alert(ALERT_BOOT, "[M]: (create_invoke_main) invoke_main = %p.\n", t);

  sf = (__cilkrts_stack_frame *)malloc(sizeof(__cilkrts_stack_frame));

  f = cilk_fiber_allocate_from_heap();
  cilk_fiber_reset_state(f, invoke_main);
  
  // it's important to set the following fields for the root closure, 
  // because we use the info to jump to the right stack position and start
  // executing user code.  For any other frames, these fields get setup 
  // in user code before a spawn and when it gets stolen the first time.
  // sf->ctx[RSP_INDEX] = (void *) stack_top;
  // sf->ctx[RBP_INDEX] = (void *) stack_top;
  // sf->ctx[RIP_INDEX] = (void *) invoke_main;

  // t->frame_rsp = stack_top;
  // t->assumed_frame_rsp = UNSET_ADDR;

  sf->flags = 0;
  __cilkrts_set_stolen(sf);
  // FIXME
  sf->flags |= CILK_FRAME_DETACHED;
    
  t->frame = sf;
  sf->worker = (__cilkrts_worker *) NOBODY;
  t->fiber = f;
  
  __cilkrts_alert(ALERT_BOOT, "[M]: (create_invoke_main) invoke_main->fiber = %p.\n", f);
    
  return t;
}

void spawn_cilk_main(int *res, int argc, char * args[]) {

  HELPER_PREAMBLE
    __cilkrts_enter_frame_fast(sf);
  __cilkrts_detach(sf);
  *res = cilk_main(argc, args);
  __cilkrts_pop_frame(sf);
  __cilkrts_leave_frame(sf);
}


/*
 * ANGE: strictly speaking, we could just call cilk_main instead of spawn,
 * but spawning has a couple advantages: 
 * - it allows us to do tlmm_set_closure_stack_mapping in a natural way 
 for the invoke_main closure (otherwise would need to setup it specially).
 * - the sync point after spawn of cilk_main provides a natural point to
 *   resume if user ever calls Cilk_exit and abort the entire computation.
 */
void invoke_main(cilk_fiber * f) {
   
  __cilkrts_worker *ws = __cilkrts_get_tls_worker();
  // Closure * t = ws->g->invoke_main;
  __cilkrts_stack_frame *sf = ws->current_stack_frame;

  char * rsp;
  char * nsp;
  int _tmp, i;
  int argc = ws->g->cilk_main_argc;
  char **args = ws->g->cilk_main_args;

  ASM_GET_SP(rsp);
  __cilkrts_alert(ALERT_BOOT, "[%d]: (invoke_main).\n", ws->self);
  __cilkrts_alert(ALERT_BOOT, "[%d]: (invoke_main) rsp = %p.\n", ws->self, rsp);

  alloca(ZERO);

  cilk_fiber_set_owner(f, ws);
    
  if(__builtin_setjmp(sf->ctx) == 0) {
    //ws->g->invoke_main->frame_rsp = (AddrType) sf->ctx[RSP_INDEX];
    spawn_cilk_main(&_tmp, argc, args);
  } else {
    // ANGE: Important to reset using sf->worker;
    // otherwise ws gets cached in a register 
    ws = sf->worker;
    __cilkrts_alert(ALERT_BOOT, "[%d]: (invoke_main) corrected worker after spawn.\n", ws->self);
  }
  ASM_GET_SP(nsp);
  __cilkrts_alert(ALERT_BOOT, "[%d]: (invoke_main) new rsp = %p.\n", ws->self, nsp);

  CILK_ASSERT(ws == __cilkrts_get_tls_worker());

  if(__cilkrts_unsynced(sf)) {
    if(__builtin_setjmp(sf->ctx) == 0) {
      __cilkrts_sync(sf);
    } else {
      // ANGE: Important to reset using sf->worker; 
      // otherwise ws gets cached in a register
      ws = sf->worker;
      __cilkrts_alert(ALERT_BOOT, "[%d]: (invoke_main) corrected worker after sync.\n", ws->self);
    }
  }
  
  CILK_ASSERT(ws == __cilkrts_get_tls_worker());

  ASM_SET_SP(rsp);
  ws->g->cilk_main_return = _tmp;

  CILK_WMB();
		   
  ws->g->done = 1;

  // Cilk_remove_and_free_closure_and_frame(ws, sf, ws->self);

  // done; go back to runtime
  longjmp_to_runtime(ws);
}
