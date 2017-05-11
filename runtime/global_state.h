#ifndef _GLOBAL_STATE_H
#define _GLOBAL_STATE_H

// Forward declaration
typedef struct global_state global_state;

// Includes
#include "cilk_options.h"
#include "readydeque.h"
#include "worker.h"
#include "closure.h"

// Actual declaration
struct global_state {
  /* globally-visible options (read-only in child processes) */
  Cilk_options *options;

  /*
   * this string is printed when an assertion fails.  If we just inline
   * it, apparently gcc generates many copies of the string.
   */
  const char *assertion_failed_msg;
  const char *stack_overflow_msg;

  /* Number of processors Cilk is running on */
  int active_size;
  int pthread_stacksize;

  /* dynamically-allocated array of deques, one per processor */
  ReadyDeque *deques;
  __cilkrts_worker **workers;
  pthread_t * threads;

  Closure *invoke_main;

  volatile int start;
  volatile int done;

  int cilk_main_argc;
  char **cilk_main_args;
  
  int cilk_main_return;
  int cilk_main_exit;
};
#endif
