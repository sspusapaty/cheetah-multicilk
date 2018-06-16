#include <stdio.h>

#include "cilk-internal.h"
#include "cilk2c.h"
#include "fiber.h"
#include "membar.h"
#include "scheduler.h"

extern unsigned long ZERO;

extern int cilk_main(int argc, char * argv[]);
extern global_state * __cilkrts_init(int argc, char* argv[]);
extern void __cilkrts_cleanup(global_state *g);

__attribute__((noreturn)) void invoke_main(); // forward decl

Closure * create_invoke_main(global_state *const g) {

  Closure *t;
  __cilkrts_stack_frame * sf;
  struct cilk_fiber *fiber;

  t = Closure_create_main();
  t->status = CLOSURE_READY;

  __cilkrts_alert(ALERT_BOOT, "[M]: (create_invoke_main) invoke_main = %p.\n", t);

  sf = malloc(sizeof(*sf));
  fiber = cilk_main_fiber_allocate();
  
  // it's important to set the following fields for the root closure, 
  // because we use the info to jump to the right stack position and start
  // executing user code.  For any other frames, these fields get setup 
  // in user code before a spawn and when it gets stolen the first time.
  void *new_rsp = (void *)sysdep_reset_jump_buffers_for_resume(fiber, sf);
  CILK_ASSERT_G(SP(sf) == new_rsp);
  FP(sf) = new_rsp;
  PC(sf) = (void *) invoke_main;

  sf->flags = CILK_FRAME_VERSION;
  __cilkrts_set_stolen(sf);
  // FIXME
  sf->flags |= CILK_FRAME_DETACHED;
    
  t->frame = sf;
  sf->worker = (__cilkrts_worker *) NOBODY;
  t->fiber = fiber;
  // WHEN_CILK_DEBUG(sf->magic = CILK_STACKFRAME_MAGIC);
  
  __cilkrts_alert(ALERT_BOOT, "[M]: (create_invoke_main) invoke_main->fiber = %p.\n", fiber);
    
  return t;
}

void cleanup_invoke_main(Closure *invoke_main) {
  cilk_main_fiber_deallocate(invoke_main->fiber); 
  free(invoke_main->frame);
  Closure_destroy_main(invoke_main);
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
 * - it allow us to do tlmm_set_closure_stack_mapping in a natural way 
 for the invoke_main closure (otherwise would need to setup it specially).
 * - the sync point after spawn of cilk_main provides a natural point to
 *   resume if user ever calls Cilk_exit and abort the entire computation.
 */
__attribute__((noreturn))
void invoke_main() {
   
  __cilkrts_worker *w = __cilkrts_get_tls_worker();
  __cilkrts_stack_frame *sf = w->current_stack_frame;

  char * rsp;
  char * nsp;
  int _tmp;
  int argc = w->g->cilk_main_argc;
  char **args = w->g->cilk_main_args;

  ASM_GET_SP(rsp);
  __cilkrts_alert(ALERT_BOOT, "[%d]: (invoke_main).\n", w->self);
  __cilkrts_alert(ALERT_BOOT, "[%d]: (invoke_main) rsp = %p.\n", w->self, rsp);

  alloca(ZERO);

  __cilkrts_save_fp_ctrl_state(sf);
  if(__builtin_setjmp(sf->ctx) == 0) {
    spawn_cilk_main(&_tmp, argc, args);
  } else {
    // ANGE: Important to reset using sf->worker;
    // otherwise w gets cached in a register 
    w = sf->worker;
    __cilkrts_alert(ALERT_BOOT, "[%d]: (invoke_main) corrected worker after spawn.\n", w->self);
  }

  ASM_GET_SP(nsp);
  __cilkrts_alert(ALERT_BOOT, "[%d]: (invoke_main) new rsp = %p.\n", w->self, nsp);

  CILK_ASSERT_G(w == __cilkrts_get_tls_worker());

  if(__cilkrts_unsynced(sf)) {
    __cilkrts_save_fp_ctrl_state(sf);
    if(__builtin_setjmp(sf->ctx) == 0) {
      __cilkrts_sync(sf);
    } else {
      // ANGE: Important to reset using sf->worker; 
      // otherwise w gets cached in a register
      w = sf->worker;
      __cilkrts_alert(ALERT_BOOT, "[%d]: (invoke_main) corrected worker after sync.\n", w->self);
    }
  }
  
  CILK_ASSERT_G(w == __cilkrts_get_tls_worker());
  w->g->cilk_main_return = _tmp;
  // WHEN_CILK_DEBUG(sf->magic = ~CILK_STACKFRAME_MAGIC);

  CILK_WMB();
		   
  w->g->done = 1;

  // Cilk_remove_and_free_closure_and_frame(w, sf, w->self);

  // done; go back to runtime
  longjmp_to_runtime(w);
}

static void main_thread_init(global_state * g) {
  __cilkrts_alert(ALERT_BOOT, "[M]: (main_thread_init) Setting up main thread's closure.\n");

  g->invoke_main = create_invoke_main(g);
  // ANGE: the order here is important!
  Cilk_membar_StoreStore();
  g->start = 1;
}

static void threads_join(global_state * g) {
  for (int i = 0; i < g->options.nproc; i++) {
    int status = pthread_join(g->threads[i], NULL);
    if(status != 0)
      __cilkrts_bug("Cilk runtime error: thread join (%d) failed: %d\n", i, status);
  }
  __cilkrts_alert(ALERT_BOOT, "[M]: (threads_join) All workers joined!\n");
}

static void __cilkrts_run(global_state * g) {
  main_thread_init(g);
  // Sleeping
  threads_join(g);
}

static void __cilkrts_exit(global_state * g) {
  __cilkrts_cleanup(g);
}

#undef main
int main(int argc, char* argv[]) {
  int ret;

  global_state * g = __cilkrts_init(argc, argv);
  fprintf(stderr, "Cheetah: invoking user main with %d workers.\n", g->options.nproc);

  __cilkrts_run(g);
  ret = g->cilk_main_return;

  __cilkrts_exit(g);

  return ret;
}
