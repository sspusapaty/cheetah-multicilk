#include "cilk.h"
#include "tls.h"
#include "sched.h"

#include <stdio.h>


extern int cilk_main(int argc, char * argv[]);

void* scheduler_thread_proc(void * arg)
{
  __cilkrts_worker * w = (__cilkrts_worker *)arg;

    
  __cilkrts_set_tls_worker(w);

  // Create a cilk fiber for this worker on this thread.
  w->l->runtime_fiber = cilk_fiber_allocate_from_thread();
  cilk_fiber_set_owner(w->l->runtime_fiber, w);
    
  worker_scheduler(w);
  
  // Deallocate the scheduling fiber.  This operation reverses the
  // effect cilk_fiber_allocate_from_thread() and must be done in this
  // thread before it exits.
  int ref_count = cilk_fiber_deallocate_from_thread(w->l->runtime_fiber);
  // Scheduling fibers should never have extra references to them.
  // We only get extra references into fibers because of Windows
  // exceptions.
  CILK_ASSERT(0 == ref_count);
  w->l->runtime_fiber = NULL;
    
  return 0;
}

void main_thread_init(global_state * g) {
  __cilkrts_alert("Setting up main thread.\n");
  __cilkrts_worker * w = g->workers[0];
  __cilkrts_set_tls_worker(w);

  w->l->runtime_fiber = NULL;

  g->cilk_main_return = cilk_main(g->cilk_main_argc,
				  g->cilk_main_args);

  longjmp_to_runtime(w);
}

void threads_init(global_state * g) {
  __cilkrts_alert("Setting up system threads.\n");
  for (int i = 0; i < g->active_size - 1; i++) {
    int status = pthread_create(&g->threads[i],
                                NULL,
                                scheduler_thread_proc,
                                g->workers[i + 1]);
    if (status != 0)
      __cilkrts_bug("Cilk runtime error: thread creation (%d) failed: %d\n", i, status);
    }
}

void __cilkrts_run(global_state * g) {
  threads_init(g);

  main_thread_init(g);
}
