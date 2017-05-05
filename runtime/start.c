#include "cilk.h"
#include "tls.h"
#include "sched.h"
#include "invoke_main.h"

#include <stdio.h>


void* scheduler_thread_proc(void * arg) {
  __cilkrts_worker * w = (__cilkrts_worker *)arg;
    
  __cilkrts_set_tls_worker(w);

  // Create a cilk fiber for this worker on this thread.
  w->l->runtime_fiber = cilk_fiber_allocate_from_thread();
  cilk_fiber_set_owner(w->l->runtime_fiber, w);

  if (w->self == 0) {
    worker_scheduler(w, w->g->invoke_main);
  } else {
    worker_scheduler(w, NULL);
  }
  
  
  int ref_count = cilk_fiber_deallocate_from_thread(w->l->runtime_fiber);

  CILK_ASSERT(0 == ref_count);
  w->l->runtime_fiber = NULL;
    
  return 0;
}

void main_thread_init(global_state * g) {
  __cilkrts_alert("Setting up main thread's closure.\n");

  g->invoke_main = create_invoke_main(g);
}

void threads_init(global_state * g) {
  __cilkrts_alert("Setting up threads.\n");
  for (int i = 0; i < g->active_size; i++) {
    int status = pthread_create(&g->threads[i],
                                NULL,
                                scheduler_thread_proc,
                                g->workers[i]);
    if (status != 0)
      __cilkrts_bug("Cilk runtime error: thread creation (%d) failed: %d\n", i, status);
    }
}

void threads_join(global_state * g) {
  for (int i = 0; i < g->active_size; i++) {
    void * ret;
    
    int status = pthread_join(g->threads[i], &ret);

    if (status != 0)
      __cilkrts_bug("Cilk runtime error: thread join (%d) failed: %d\n", i, status);
    }
}

void __cilkrts_run(global_state * g) {
  main_thread_init(g);
  
  threads_init(g);
  // Sleeping
  threads_join(g);
}
