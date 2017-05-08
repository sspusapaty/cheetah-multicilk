#ifndef _CLOSURE_H
#define _CLOSURE_H

// Forward declaration
typedef struct Closure Closure;
enum ClosureStatus { CLOSURE_RUNNING = 42, CLOSURE_SUSPENDED,
                     CLOSURE_RETURNING, CLOSURE_READY };
/* The order is important here. */
enum AbortStatus { ABORT_ALL = 30 , ALMOST_NO_ABORT, NO_ABORT};

// Includes
#include "cilk_mutex.h"
#include "stack_frame.h"
#include "fiber.h"
#include "types.h"
#include "common.h"
/* 
 * the list of children is not distributed among
 * the children themselves, in order to avoid extra protocols
 * and locking.
 */
struct Closure {
  Cilk_mutex mutex;          /* mutual exclusion lock */

  __cilkrts_stack_frame *frame;       /* rest of the closure */
  cilk_fiber * fiber;
  
  //WHEN_DEBUG_VERBOSE(int mutex_action;)
  int mutex_owner;
  int owner_ready_deque;
  
  int join_counter; /* number of spawned outstanding children */

  enum ClosureStatus status;

  int has_cilk_callee; // MAK: Can delete
  Closure * callee; // MAK: Can delete
  
  Closure *call_parent; /* the "parent" closure that called */
  Closure *spawn_parent; /* the "parent" closure that spawned */

  Closure *left_sib;  // left *spawned* sibling in the closure tree
  Closure *right_sib; // right *spawned* sibling in the closur tree
  // right most *spawned* child in the closure tree
  Closure *right_most_child; 

  /*
   * stuff related to ready deque.  These fields
   * must be managed only by the queue manager in sched.c
   *
   * ANGE: for top of the ReadyDeque, prev_ready = NULL
   *       for bottom of the ReadyDeque, next_ready = NULL
   *       next_ready pointing downward, prev_ready pointing upward 
   *
   *       top
   *  next | ^
   *       | | prev
   *       v |
   *       ...
   *  next | ^
   *       | | prev
   *       v |
   *      bottom
   */
  Closure *next_ready;
  Closure *prev_ready;

  /* miscellanea */
  unsigned int magic;
  CILK_CACHE_LINE_PAD;
};

void Closure_assert_ownership(__cilkrts_worker *const ws, Closure *t);

void Closure_assert_alienation(__cilkrts_worker *const ws, Closure *t);

int Closure_trylock(__cilkrts_worker *const ws, Closure *t);

void Closure_lock(__cilkrts_worker *const ws, Closure *t);

void Closure_unlock(__cilkrts_worker *const ws, Closure *t);

int Closure_at_top_of_stack(__cilkrts_worker *const ws);

int Closure_has_children(Closure *cl);

Closure *Closure_create(__cilkrts_worker *const ws);

Closure *Cilk_Closure_create_malloc(global_state *const g, 
                                    __cilkrts_worker *const ws);

void Closure_add_child(__cilkrts_worker *const ws,
		       Closure *parent, Closure *child);

void Closure_remove_child(__cilkrts_worker *const ws,
			  Closure *parent, Closure *child);

void Closure_add_temp_callee(__cilkrts_worker *const ws, 
			     Closure *caller, Closure *callee);

void Closure_add_callee(__cilkrts_worker *const ws, 
			Closure *caller, Closure *callee);

void Closure_remove_callee(__cilkrts_worker *const ws, Closure *caller);

void Closure_suspend_victim(__cilkrts_worker *const ws, 
			    int victim, Closure *cl);

void Closure_suspend(__cilkrts_worker *const ws, Closure *cl);

void Closure_make_ready(Closure *cl);

#endif
