#include "cilk.h"
#include "tls.h"
#include "sched.h"

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

global_state * global_state_init(int argc, char* argv[]) {
  __cilkrts_alert(1, "Initializing global state.\n");
  
  global_state * g = (global_state *) malloc(sizeof(global_state));

  g->active_size = 2;

  g->start = 0;
  g->done = 0;
  
  g->workers = (__cilkrts_worker **) malloc(g->active_size * sizeof(__cilkrts_worker *));
  g->deques = (ReadyDeque *) malloc(g->active_size * sizeof(ReadyDeque));
  
  g->threads = (pthread_t *) malloc(g->active_size * sizeof(pthread_t));
    
  g->cilk_main_argc = argc;
  g->cilk_main_args = argv;

  return g;
}

local_state * worker_local_init() {
  local_state * l = (local_state *) malloc(sizeof(local_state));
  l->deque_depth = 10000;
  l->shadow_stack = (CilkShadowStack) malloc(l->deque_depth * sizeof(struct __cilkrts_stack_frame *));
  l->runtime_fiber = NULL;
  l->fiber_to_free = NULL;

  return l;
}

void deques_init(global_state * g) {
  __cilkrts_alert(1, "Initializing deques.\n");
  for (int i = 0; i < g->active_size; i++) {
    g->deques[i].top = NULL;
    g->deques[i].bottom = NULL;
    g->deques[i].mutex_owner = NOBODY;
    Cilk_mutex_init(&(g->deques[i].mutex));
  }
}

void workers_init(global_state * g) {
  __cilkrts_alert(1, "Initializing workers.\n");
  for (int i = 0; i < g->active_size; i++) {
    __cilkrts_alert(2, "Initializing worker %d.\n", i);
    __cilkrts_worker * w = (__cilkrts_worker *) malloc(sizeof(__cilkrts_worker));
    w->self = i; // i + 1?
    w->g = g;
    w->l = worker_local_init();
    
    w->ltq_limit = w->l->shadow_stack + w->l->deque_depth;
    g->workers[i] = w;
    w->tail = w->l->shadow_stack + 1;
    w->head = w->l->shadow_stack + 1;
    w->exc = w->head;
  }
}

void* scheduler_thread_proc(void * arg) {
  __cilkrts_worker * w = (__cilkrts_worker *)arg;
  long long idle = 0;
  __cilkrts_alert(2, "Thread of worker %d: scheduler_thread_proc\n", w->self);
  __cilkrts_set_tls_worker(w);
  // Create a cilk fiber for this worker on this thread.
  w->l->runtime_fiber = cilk_fiber_allocate_from_thread();
  cilk_fiber_set_owner(w->l->runtime_fiber, w);

  while(!w->g->start) {
    usleep(1);
    idle++;
  }
  __cilkrts_alert(2, "[%d]: idled for %d loops\n", w->self, idle);


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

void threads_init(global_state * g) {
  __cilkrts_alert(1, "Setting up threads.\n");
  for (int i = 0; i < g->active_size; i++) {
    int status = pthread_create(&g->threads[i],
                                NULL,
                                scheduler_thread_proc,
                                g->workers[i]);
    if (status != 0) __cilkrts_bug("Cilk runtime error: thread creation (%d) failed: %d\n", i, status);
  }
  usleep(10);
}

global_state * __cilkrts_init(int argc, char* argv[]) {
  __cilkrts_alert(1, "__cilkrts_init\n");
  global_state * g = global_state_init(argc, argv);
  __cilkrts_init_tls_variables();
  workers_init(g);
  threads_init(g);

  return g;
}
