#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#include "debug.h"
#include "readydeque.h"
#include "scheduler.h"

extern void cleanup_invoke_main(Closure *invoke_main);
extern int parse_command_line(struct rts_options *options, int *argc, char *argv[]);

static global_state * global_state_init(int argc, char* argv[]) {
  __cilkrts_alert(ALERT_BOOT, "[M]: (global_state_init) Initializing global state.\n");
  global_state * g = (global_state *) malloc(sizeof(global_state));
  
  if(parse_command_line(&g->options, &argc, argv)) {
    // user invoked --help; quit
    free(g);
    exit(0);
  }

  int active_size = g->options.nproc;
  g->invoke_main_initialized = 0;
  g->start = 0;
  g->done = 0;
  
  g->workers = (__cilkrts_worker **) malloc(active_size * sizeof(__cilkrts_worker *));
  g->deques = (ReadyDeque *) malloc(active_size * sizeof(ReadyDeque));
  
  g->threads = (pthread_t *) malloc(active_size * sizeof(pthread_t));
    
  g->cilk_main_argc = argc;
  g->cilk_main_args = argv;

  return g;
}

static local_state * worker_local_init(global_state *g) {
  local_state * l = (local_state *) malloc(sizeof(local_state));
  l->shadow_stack = (__cilkrts_stack_frame **) 
      malloc(g->options.deqdepth * sizeof(struct __cilkrts_stack_frame *));
  for(int i=0; i < JMPBUF_SIZE; i++) { l->rts_ctx[i] = NULL; }
  l->fiber_to_free = NULL;
  l->provably_good_steal = 0;

  return l;
}

static void deques_init(global_state * g) {
  __cilkrts_alert(ALERT_BOOT, "[M]: (deques_init) Initializing deques.\n");
  for (int i = 0; i < g->options.nproc; i++) {
    g->deques[i].top = NULL;
    g->deques[i].bottom = NULL;
    WHEN_CILK_DEBUG(g->deques[i].mutex_owner = NOBODY);
    Cilk_mutex_init(&(g->deques[i].mutex));
  }
}

static void workers_init(global_state * g) {
  __cilkrts_alert(ALERT_BOOT, "[M]: (workers_init) Initializing workers.\n");
  for (int i = 0; i < g->options.nproc; i++) {
    __cilkrts_alert(ALERT_BOOT, "[M]: (workers_init) Initializing worker %d.\n", i);
    __cilkrts_worker * w = (__cilkrts_worker *) malloc(sizeof(__cilkrts_worker));
    w->self = i;
    w->g = g;
    w->l = worker_local_init(g);
    
    w->ltq_limit = w->l->shadow_stack + g->options.deqdepth;
    g->workers[i] = w;
    w->tail = w->l->shadow_stack + 1;
    w->head = w->l->shadow_stack + 1;
    w->exc = w->head;
    w->current_stack_frame = NULL;
  }
}

static void* scheduler_thread_proc(void * arg) {
  __cilkrts_worker * w = (__cilkrts_worker *)arg;
  long long idle = 0;
  __cilkrts_alert(ALERT_BOOT, "[%d]: (scheduler_thread_proc)\n", w->self);
  __cilkrts_set_tls_worker(w);

  while(!w->g->start) {
    usleep(1);
    idle++;
  }

  if (w->self == 0) { worker_scheduler(w, w->g->invoke_main); } 
  else { worker_scheduler(w, NULL); }
    
  return 0;
}

static void threads_init(global_state * g) {
  __cilkrts_alert(ALERT_BOOT, "[M]: (threads_init) Setting up threads.\n");
  for (int i = 0; i < g->options.nproc; i++) {
    int status = pthread_create(&g->threads[i],
                                NULL,
                                scheduler_thread_proc,
                                g->workers[i]);
    if (status != 0) 
      __cilkrts_bug("Cilk: thread creation (%d) failed: %d\n", i, status);
  }
  usleep(10);
}

global_state * __cilkrts_init(int argc, char* argv[]) {
  __cilkrts_alert(ALERT_BOOT, "[M]: (__cilkrts_init)\n");
  global_state * g = global_state_init(argc, argv);
  __cilkrts_init_tls_variables();
  workers_init(g);
  deques_init(g);
  threads_init(g);

  return g;
}

static void global_state_deinit(global_state *g) {
  __cilkrts_alert(ALERT_BOOT, "[M]: (global_state_deinit) Clean up global state.\n");

  cleanup_invoke_main(g->invoke_main);
  free(g->workers);
  free(g->deques);
  free(g->threads);
  free(g);
}

static void deques_deinit(global_state * g) {
  __cilkrts_alert(ALERT_BOOT, "[M]: (deques_deinit) Clean up deques.\n");
  for (int i = 0; i < g->options.nproc; i++) {
    CILK_ASSERT_G(g->deques[i].mutex_owner == NOBODY);
    Cilk_mutex_destroy(&(g->deques[i].mutex));
  }
}

static void workers_deinit(global_state * g) {
  __cilkrts_alert(ALERT_BOOT, "[M]: (workers_deinit) Clean up workers.\n");
  for(int i = 0; i < g->options.nproc; i++) {
    __cilkrts_worker *w = g->workers[i];
    CILK_ASSERT(w, w->l->fiber_to_free == NULL);

    free(w->l->shadow_stack);
    free(w->l);
    free(w);
    g->workers[i] = NULL;
  }
}

void __cilkrts_cleanup(global_state *g) {
  workers_deinit(g);
  deques_deinit(g);
  global_state_deinit(g);
}
