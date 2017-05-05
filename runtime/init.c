#include "cilk.h"
#include "tls.h"
#include <pthread.h>
#include <stdlib.h>

global_state * global_state_init(int argc, char* argv[]) {
  __cilkrts_alert("Initializing global state.\n");
  
  global_state * g = (global_state *) malloc(sizeof(global_state));

  g->active_size = 2;

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

  return l;
}

void deques_init(global_state * g) {
  __cilkrts_alert("Initializing deques.\n");
  for (int i = 0; i < g->active_size; i++) {
    g->deques[i].top = NULL;
    g->deques[i].bottom = NULL;
    g->deques[i].mutex_owner = NOBODY;
    Cilk_mutex_init(&(g->deques[i].mutex));
  }
}

void workers_init(global_state * g) {
  __cilkrts_alert("Initializing workers.\n");
  for (int i = 0; i < g->active_size; i++) {
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

global_state * __cilkrts_init(int argc, char* argv[]) {
  global_state * g = global_state_init(argc, argv);
  __cilkrts_init_tls_variables();
  workers_init(g);

  return g;
}
