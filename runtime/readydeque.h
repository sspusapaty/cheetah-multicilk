#ifndef _READYDEQUE_H
#define _READYDEQUE_H

// Forward declaration
typedef struct ReadyDeque ReadyDeque;

// Includes
#include "cilk_mutex.h"
#include "closure.h"
#include "common.h"
#include "worker.h"

// Actual declaration
struct ReadyDeque {
  Cilk_mutex mutex;
  Cilk_mutex steal_mutex;
  int mutex_owner; // WHEN_CILK_DEBUG
  int steal_mutex_owner; // WHEN_CILK_DEBUG
  Closure *top, *bottom;
  CILK_CACHE_LINE_PAD;
};

// assert that pn's deque be locked by ourselves 
static inline void deque_assert_ownership(__cilkrts_worker *const ws,       
                       int pn) {

    CILK_ASSERT(ws->g->deques[pn].mutex_owner == ws->self);
}

static inline void deque_lock_self(__cilkrts_worker *const ws) {

  int pn = ws->self;
  Cilk_mutex_wait(&ws->g->deques[pn].mutex);
  ws->g->deques[pn].mutex_owner = ws->self; // WHEN_CILK_DEBUG
}

static inline void deque_unlock_self(__cilkrts_worker *const ws) {

  int pn = ws->self;
  ws->g->deques[pn].mutex_owner = NOBODY; // WHEN_CILK_DEBUG
  Cilk_mutex_signal(&ws->g->deques[pn].mutex);
}

static inline int deque_trylock(__cilkrts_worker *const ws, int pn) {

  int ret = Cilk_mutex_try(&ws->g->deques[pn].mutex);
  
  if(ret) {
    ws->g->deques[pn].mutex_owner = ws->self;
  } // WHEN_CILK_DEBUG

    return ret;
} 

static inline void deque_lock(__cilkrts_worker *const ws, int pn) {

  Cilk_mutex_wait(&ws->g->deques[pn].mutex);
  ws->g->deques[pn].mutex_owner = ws->self; // WHEN_CILK_DEBUG
}

static inline void deque_unlock(__cilkrts_worker *const ws, int pn) {

  ws->g->deques[pn].mutex_owner = NOBODY; // WHEN_CILK_DEBUG
  Cilk_mutex_signal(&ws->g->deques[pn].mutex);
}

/* 
 * functions that add/remove elements from the top/bottom
 * of deques
 *
 * ANGE: the precondition of these functions is that the worker ws -> self 
 * must have locked worker pn's deque before entering the function
 */
Closure *deque_xtract_top(__cilkrts_worker *const ws, int pn);

Closure *deque_peek_top(__cilkrts_worker *const ws, int pn);

Closure *deque_xtract_bottom(__cilkrts_worker *const ws, int pn);

Closure *deque_peek_bottom(__cilkrts_worker *const ws, int pn);

/*
 * ANGE: this allow ws -> self to append Closure cl onto worker pn's ready
 *       deque (i.e. make cl the new bottom).
 */
void deque_add_bottom(__cilkrts_worker *const ws, Closure *cl, int pn);

static inline void deque_assert_is_bottom(__cilkrts_worker *const ws, Closure *t) {

  /* ANGE: still need to make sure the worker self has the lock */
  deque_assert_ownership(ws, ws->self);
  CILK_ASSERT(t == deque_peek_bottom(ws, ws->self));
}


/* ANGE: remove closure for frame f from bottom of pn's deque and _really_ 
 *       free them (i.e. not internal-free).  As far as I can tell. 
 *       This is called only in invoke_main_slow in invoke-main.c. 
 */
void Cilk_remove_and_free_closure_and_frame(__cilkrts_worker *const ws,
					    __cilkrts_stack_frame *f, int pn);
#endif
