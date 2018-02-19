#ifndef _GLOBAL_STATE_H
#define _GLOBAL_STATE_H

// Forward declaration
typedef struct global_state global_state;

// Includes
#include <pthread.h>
#include "rts-config.h"
#include "common.h"

// Actual declaration
struct rts_options {
  int nproc;
  int deqdepth;
  int stacksize;
  int alloc_batch_size;
};

#define DEFAULT_OPTIONS \
{                                                        \
  DEFAULT_NPROC,       /* num of workers to create */    \
  DEFAULT_DEQ_DEPTH,   /* num of entries in deque */     \
  DEFAULT_STACK_SIZE,  /* stack size to use for fiber */ \
  DEFAULT_ALLOC_BATCH, /* alloc_batch_size */            \
}

// Actual declaration
struct global_state {
  /* globally-visible options (read-only after init) */
  struct rts_options options;

  /*
   * this string is printed when an assertion fails.  If we just inline
   * it, apparently gcc generates many copies of the string.
   */
  const char *assertion_failed_msg;
  const char *stack_overflow_msg;

  /* dynamically-allocated array of deques, one per processor */
  struct ReadyDeque *deques;
  __cilkrts_worker **workers;
  pthread_t * threads;

  struct Closure *invoke_main;

  volatile int invoke_main_initialized;
  volatile int start;
  volatile int done;

  int cilk_main_argc;
  char **cilk_main_args;
  
  int cilk_main_return;
  int cilk_main_exit;
};

int parse_command_line(struct rts_options *options, int *argc, char *argv[]);

#endif // _GLOBAL_STATE_H
