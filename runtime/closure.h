#ifndef _CLOSURE_H
#define _CLOSURE_H

// Includes
#include "debug.h"

#include "cilk-internal.h"
#include "fiber.h"
#include "mutex.h"

// Forward declaration
typedef struct Closure Closure;

enum ClosureStatus { CLOSURE_RUNNING = 42, CLOSURE_SUSPENDED,
                     CLOSURE_RETURNING, CLOSURE_READY };

#if CILK_DEBUG
#define Closure_assert_ownership(w, t) Closure_assert_ownership(w, t)
#define Closure_assert_alienation(w, t) Closure_assert_alienation(w, t)
#define CILK_CLOSURE_MAGIC 0xDEADFACE
#define Closure_checkmagic(w, t) Closure_checkmagic(w, t)
#else 
#define Closure_assert_ownership(w, t)
#define Closure_assert_alienation(w, t)
#define Closure_checkmagic(w, t)
#endif

/* 
 * the list of children is not distributed among
 * the children themselves, in order to avoid extra protocols
 * and locking.
 */
struct Closure {
  cilk_mutex mutex;              /* mutual exclusion lock */
  WHEN_CILK_DEBUG(int mutex_owner);

  __cilkrts_stack_frame *frame;  /* rest of the closure */

  struct cilk_fiber *fiber;
  struct cilk_fiber *fiber_child;
  
  WHEN_CILK_DEBUG(int owner_ready_deque);
  
  int join_counter; /* number of outstanding spawned children */
  char *orig_rsp;  /* the rsp one should use when sync successfully */

  enum ClosureStatus status;

  int has_cilk_callee;
  Closure * callee;
  
  Closure *call_parent; /* the "parent" closure that called */
  Closure *spawn_parent; /* the "parent" closure that spawned */

  Closure *left_sib;  // left *spawned* sibling in the closure tree
  Closure *right_sib; // right *spawned* sibling in the closur tree
  // right most *spawned* child in the closure tree
  Closure *right_most_child; 

  /*
   * stuff related to ready deque. 
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
  WHEN_CILK_DEBUG(unsigned int magic);
  CILK_CACHE_LINE_PAD;
};

#if CILK_DEBUG
void Closure_assert_ownership(__cilkrts_worker *const w, Closure *t);
void Closure_assert_alienation(__cilkrts_worker *const w, Closure *t);
void Closure_checkmagic(__cilkrts_worker *const w, Closure *t);
#define Closure_assert_ownership(w, t) Closure_assert_ownership(w, t)
#define Closure_assert_alienation(w, t) Closure_assert_alienation(w, t)
#define Closure_checkmagic(w, t) Closure_checkmagic(w, t)
#else 
#define Closure_assert_ownership(w, t)
#define Closure_assert_alienation(w, t)
#define Closure_checkmagic(w, t)
#endif // CILK_DEBUG

int Closure_trylock(__cilkrts_worker *const w, Closure *t);
void Closure_lock(__cilkrts_worker *const w, Closure *t);
void Closure_unlock(__cilkrts_worker *const w, Closure *t);

int Closure_at_top_of_stack(__cilkrts_worker *const w);
int Closure_has_children(Closure *cl);

Closure *Closure_create(__cilkrts_worker *const w);
Closure *Closure_create_main();

void Closure_add_child(__cilkrts_worker *const w,
		       Closure *parent, Closure *child);
void Closure_remove_child(__cilkrts_worker *const w,
			  Closure *parent, Closure *child);
void Closure_add_temp_callee(__cilkrts_worker *const w, 
			     Closure *caller, Closure *callee);
void Closure_add_callee(__cilkrts_worker *const w, 
			Closure *caller, Closure *callee);
void Closure_remove_callee(__cilkrts_worker *const w, Closure *caller);

void Closure_suspend_victim(__cilkrts_worker *const w, 
			    int victim, Closure *cl);
void Closure_suspend(__cilkrts_worker *const w, Closure *cl);

void Closure_make_ready(Closure *cl);
void Closure_destroy(__cilkrts_worker *const w, Closure *t);
void Closure_destroy_main(Closure *t);
#endif
