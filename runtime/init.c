#include "cilk.h"
#include "tls.h"
#include <pthread.h>
#include <stdlib.h>

global_state * global_state_init(int argc, char* argv[]) {
  global_state * g = (global_state *) malloc(sizeof(global_state));

  g->active_size = 2;
  
  g->workers = (__cilkrts_worker **) malloc(g->active_size * sizeof(__cilkrts_worker *));
  g->threads = (pthread_t *) malloc((g->active_size - 1)* sizeof(pthread_t));
    
  g->cilk_main_argc = argc;
  g->cilk_main_args = argv;

  return g;
}

void workers_init(global_state * g) {
  for (int i = 0; i < g->active_size; i++) {
    g->workers[i] = (__cilkrts_worker *) malloc(sizeof(__cilkrts_worker));
    g->workers[i]->self = i; // i + 1?
    g->workers[i]->g = g;
    g->workers[i]->l = (local_state *) malloc(sizeof(local_state));
  }
}

global_state * __cilkrts_init(int argc, char* argv[]) {
  global_state * g = global_state_init(argc, argv);
  __cilkrts_init_tls_variables();
  workers_init(g);

  return g;
}
