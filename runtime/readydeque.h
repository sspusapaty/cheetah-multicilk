#ifndef _READYDEQUE_H
#define _READYDEQUE_H

// Forward declaration
typedef struct ReadyDeque ReadyDeque;

// Includes
#include "cilk-internal.h"
#include "debug.h"
#include "closure.h"
#include "mutex.h"

// Actual declaration
struct ReadyDeque {
  cilk_mutex mutex;
  Closure *top, *bottom;
  WHEN_CILK_DEBUG(int mutex_owner);
  CILK_CACHE_LINE_PAD;
};

// assert that pn's deque be locked by ourselves 
static inline void deque_assert_ownership(__cilkrts_worker *const w, int pn) {
    CILK_ASSERT(w, w->g->deques[pn].mutex_owner == w->self);
}

static inline void deque_lock_self(__cilkrts_worker *const w) {
  int pn = w->self;
  cilk_mutex_lock(&w->g->deques[pn].mutex);
  WHEN_CILK_DEBUG(w->g->deques[pn].mutex_owner = w->self);
}

static inline void deque_unlock_self(__cilkrts_worker *const w) {
  int pn = w->self;
  WHEN_CILK_DEBUG(w->g->deques[pn].mutex_owner = NOBODY);
  cilk_mutex_unlock(&w->g->deques[pn].mutex);
}

static inline int deque_trylock(__cilkrts_worker *const w, int pn) {
  int ret = cilk_mutex_try(&w->g->deques[pn].mutex);
  if(ret) {
    WHEN_CILK_DEBUG(w->g->deques[pn].mutex_owner = w->self);
  }
  return ret;
}

static inline void deque_lock(__cilkrts_worker *const w, int pn) {
  cilk_mutex_lock(&w->g->deques[pn].mutex);
  WHEN_CILK_DEBUG(w->g->deques[pn].mutex_owner = w->self);
}

static inline void deque_unlock(__cilkrts_worker *const w, int pn) {
  WHEN_CILK_DEBUG(w->g->deques[pn].mutex_owner = NOBODY); 
  cilk_mutex_unlock(&w->g->deques[pn].mutex);
}

/* 
 * functions that add/remove elements from the top/bottom of deques
 *
 * ANGE: the precondition of these functions is that the worker w -> self 
 * must have locked worker pn's deque before entering the function
 */
Closure *deque_xtract_top(__cilkrts_worker *const w, int pn);

Closure *deque_peek_top(__cilkrts_worker *const w, int pn);

Closure *deque_xtract_bottom(__cilkrts_worker *const w, int pn);

Closure *deque_peek_bottom(__cilkrts_worker *const w, int pn);

/*
 * ANGE: this allow w -> self to append Closure cl onto worker pn's ready
 *       deque (i.e. make cl the new bottom).
 */
void deque_add_bottom(__cilkrts_worker *const w, Closure *cl, int pn);

void deque_assert_is_bottom(__cilkrts_worker *const w, Closure *t);


/* ANGE: remove closure for frame f from bottom of pn's deque and _really_ 
 *       free them (i.e. not internal-free).  As far as I can tell. 
 *       This is called only in invoke_main_slow in invoke-main.c. 
 */
void Cilk_remove_and_free_closure_and_frame(__cilkrts_worker *const w,
					    __cilkrts_stack_frame *f, int pn);
#endif
