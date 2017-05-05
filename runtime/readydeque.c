#include "readydeque.h"

/*********************************************************
 * Management of ReadyDeques
 *********************************************************/

// assert that pn's deque be locked by ourselves 
void deque_assert_ownership(__cilkrts_worker *const ws, int pn) {
    CILK_ASSERT(ws->g->deques[pn].mutex_owner == ws->self);
}

void deque_lock_self(__cilkrts_worker *const ws) {
  int pn = ws->self;
  Cilk_mutex_wait(&ws->g->deques[pn].mutex);
  ws->g->deques[pn].mutex_owner = ws->self; // WHEN_CILK_DEBUG
}

void deque_unlock_self(__cilkrts_worker *const ws) {
  int pn = ws->self;
  ws->g->deques[pn].mutex_owner = NOBODY; // WHEN_CILK_DEBUG
  Cilk_mutex_signal(&ws->g->deques[pn].mutex);
}

int deque_trylock(__cilkrts_worker *const ws, int pn) {
  int ret = Cilk_mutex_try(&ws->g->deques[pn].mutex);
  
  if(ret) {
    ws->g->deques[pn].mutex_owner = ws->self;
  } // WHEN_CILK_DEBUG

    return ret;
} 

void deque_lock(__cilkrts_worker *const ws, int pn) {
  Cilk_mutex_wait(&ws->g->deques[pn].mutex);
  ws->g->deques[pn].mutex_owner = ws->self; // WHEN_CILK_DEBUG
}

void deque_unlock(__cilkrts_worker *const ws, int pn) {
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
Closure *deque_xtract_top(__cilkrts_worker *const ws, int pn) {

  Closure *cl;

  /* ANGE: make sure ws has the lock on worker pn's deque */
  deque_assert_ownership(ws, pn);

  cl = ws->g->deques[pn].top;
  if(cl) {
    // CILK_ASSERT(cl->owner_ready_deque == pn);
    ws->g->deques[pn].top = cl->next_ready;
    /* ANGE: if there is only one entry in the deque ... */
    if (cl == ws->g->deques[pn].bottom) {
      CILK_ASSERT(cl->next_ready == (Closure *) NULL);
      ws->g->deques[pn].bottom = (Closure *) NULL;
    } else {
      CILK_ASSERT(cl->next_ready);
      (cl->next_ready)->prev_ready = (Closure *) NULL;
    }
    cl->owner_ready_deque = NOBODY; // WHEN_CILK_DEBUG
  } else {
    CILK_ASSERT(ws->g->deques[pn].bottom == (Closure *)NULL);
  }

  return cl;
}

Closure *deque_peek_top(__cilkrts_worker *const ws, int pn) {

  Closure *cl;

  /* ANGE: make sure ws has the lock on worker pn's deque */
  deque_assert_ownership(ws, pn);

  /* ANGE: return the top but does not unlink it from the rest */
  cl = ws->g->deques[pn].top;
  if(cl) {
    CILK_ASSERT(cl->owner_ready_deque == pn);
  } else {
    CILK_ASSERT(ws->g->deques[pn].bottom == (Closure *)NULL);
  }

  return cl;
}

Closure *deque_xtract_bottom(__cilkrts_worker *const ws, int pn) {

  Closure *cl;

  /* ANGE: make sure ws has the lock on worker pn's deque */
  deque_assert_ownership(ws, pn);

  cl = ws->g->deques[pn].bottom;
  if(cl) {
    CILK_ASSERT(cl->owner_ready_deque == pn);
    ws->g->deques[pn].bottom = cl->prev_ready;
    if(cl == ws->g->deques[pn].top) {
      CILK_ASSERT(cl->prev_ready == (Closure *) NULL);
      ws->g->deques[pn].top = (Closure *) NULL;
    } else {
      CILK_ASSERT(cl->prev_ready);
      (cl->prev_ready)->next_ready = (Closure *) NULL;
    }

    cl->owner_ready_deque = NOBODY; // WHEN_CILK_DEBUG
  } else {
    CILK_ASSERT(ws->g->deques[pn].top == (Closure *)NULL);
  }

  return cl;
}

Closure *deque_peek_bottom(__cilkrts_worker *const ws, int pn) {

  Closure *cl;

  /* ANGE: make sure ws has the lock on worker pn's deque */
  deque_assert_ownership(ws, pn);

  cl = ws->g->deques[pn].bottom;
  if(cl) {
    CILK_ASSERT(cl->owner_ready_deque == pn);
  } else {
    CILK_ASSERT(ws->g->deques[pn].top == (Closure *)NULL);
  }

  return cl;
}

void deque_assert_is_bottom(__cilkrts_worker *const ws, Closure *t) {

  /* ANGE: still need to make sure the worker self has the lock */
  deque_assert_ownership(ws, ws->self);
  CILK_ASSERT(t == deque_peek_bottom(ws, ws->self));
}

/*
 * ANGE: this allow ws -> self to append Closure cl onto worker pn's ready
 *       deque (i.e. make cl the new bottom).
 */
void deque_add_bottom(__cilkrts_worker *const ws, Closure *cl, int pn) {

  deque_assert_ownership(ws, pn);
  CILK_ASSERT(cl->owner_ready_deque == NOBODY);

  cl->prev_ready = ws->g->deques[pn].bottom;
  cl->next_ready = (Closure *)NULL;
  ws->g->deques[pn].bottom = cl;
  cl->owner_ready_deque = pn; // WHEN_CILK_DEBUG

  if (ws->g->deques[pn].top) {
    CILK_ASSERT(cl->prev_ready);
    (cl->prev_ready)->next_ready = cl;
  } else {
    ws->g->deques[pn].top = cl;
  }
}


/* ANGE: remove closure for frame f from bottom of pn's deque and _really_ 
 *       free them (i.e. not internal-free).  As far as I can tell. 
 *       This is called only in invoke_main_slow in invoke-main.c. 
 */
void Cilk_remove_and_free_closure_and_frame(__cilkrts_worker *const ws,
					    __cilkrts_stack_frame *f, int pn) {
  Closure *t;

  deque_lock(ws, pn);
  t = deque_xtract_bottom(ws, pn);

  CILK_ASSERT(t->frame == f);
  deque_unlock(ws, pn);

  /* ANGE: there is no splitter logging in the invoke_main frame */
  //Cilk_free(f);
  //Closure_destroy_malloc(ws, t);
}
