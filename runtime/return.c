#include "return.h"
#include "readydeque.h"
#include "tls.h"
#include "common.h"
#include "cilk2c.h"
#include "exception.h"
#include "membar.h"
#include "steal.h"

//---------- SET ----------//

/* ANGE: doing an "unconditional steal" to steal back the call parent
 * closure */
Closure * setup_call_parent_resumption(__cilkrts_worker *const ws, Closure *t) {

  deque_assert_ownership(ws, ws->self);
  Closure_assert_ownership(ws, t);

  CILK_ASSERT(ws == __cilkrts_get_tls_worker());
  CILK_ASSERT(__cilkrts_stolen(t->frame) != 0);
  CILK_ASSERT(t->frame != NULL);
  CILK_ASSERT(t->frame->worker == (__cilkrts_worker *) NOBODY);
  CILK_ASSERT(t->status == CLOSURE_SUSPENDED);
  // CILK_ASSERT(t->frame->magic == CILK_STACKFRAME_MAGIC);

  t->status = CLOSURE_RUNNING;
  t->frame->worker = ws;

  CILK_ASSERT(t->frame->worker != (__cilkrts_worker *) NOBODY); 
  CILK_ASSERT(t->frame->worker == ws); 
  CILK_ASSERT(ws->head == ws->tail);
 
  ws->current_stack_frame = t->frame;
  reset_exception_pointer(ws, t);

  return t;
}

void Cilk_set_return(__cilkrts_worker *const ws) {

  Closure *t;

  __cilkrts_alert(3, "[%d]: (Cilk_set_return)\n", ws->self);

  deque_lock_self(ws);
  t = deque_peek_bottom(ws, ws->self);
  Closure_lock(ws, t);

  CILK_ASSERT(t->status == CLOSURE_RUNNING);

  CILK_ASSERT(Closure_has_children(t) == 0);

  if(t->call_parent != NULL) {
    CILK_ASSERT(t->spawn_parent == NULL);
    CILK_ASSERT((t->frame->flags & CILK_FRAME_DETACHED) == 0);

    Closure *call_parent = t->call_parent;
    Closure *t1 = deque_xtract_bottom(ws, ws->self);

    CILK_ASSERT(t == t1);
    CILK_ASSERT(__cilkrts_stolen(t->frame));
    // CILK_ASSERT(t->frame->debug_call_parent == call_parent->frame);

    t->frame = NULL;

    Closure_unlock(ws, t);

    Closure_lock(ws, call_parent);

    // MAK: FIBER-RETURN
    call_parent->fiber = t->fiber;
    t->fiber = NULL;
    
    Closure_remove_callee(ws, call_parent); 
    setup_call_parent_resumption(ws, call_parent);
    Closure_unlock(ws, call_parent);

    Closure_destroy(ws, t);
    deque_add_bottom(ws, call_parent, ws->self);

  } else {
    CILK_ASSERT(t == ws->g->invoke_main);
        
    Closure_unlock(ws, t);
  }

  deque_unlock_self(ws);
}

//---------- RET ----------//

/***
 * Return protocol for a spawned child.
 *
 * If any reducer is accessed by the child closure, we need to reduce the
 * reducer views with the child's right_sib_rmap, and its left sibling's
 * right_sib_rmap (or parent's child_rmap if it's the left most child)
 * before we unlink the child from its sibling closure list.
 *
 * When we modify the sibling links (left_sib / right_sib), we always lock
 * the parent and the child.  When we retrieve the reducer maps from left
 * sibling or parent from their place holders (right_sib_rmap / child_rmap), 
 * we always lock the closure from whom we are getting the rmap from.  
 * The locking order is always parent first then child, right child first, 
 * then left.
 * 
 * Once we have done the reduce operation, we try to deposit the rmap from
 * the child to either it's left sibling's right_sib_rmap or parent's
 * child_rmap.  Note that even though we have performed the reduce, by the
 * time we deposit the rmap, the child's left sibling may have changed, 
 * or child may become the new left most child.  Similarly, the child's
 * right_sib_rmap may have something new again.  If that's the case, we
 * need to do the reduce again (in deposit_reducer_map).
 * 
 * This function returns a closure to be executed next, or NULL if none.
 * The child must not be locked by ourselves, and be in no deque.
 ***/
Closure *Closure_return(__cilkrts_worker *const ws, Closure *child) {

  Closure *parent;
  Closure *res = (Closure *) NULL;

  CILK_ASSERT(child);
  CILK_ASSERT(child->join_counter == 0);
  CILK_ASSERT(child->status == CLOSURE_RETURNING);
  CILK_ASSERT(child->owner_ready_deque == NOBODY);
  Closure_assert_alienation(ws, child);

  CILK_ASSERT(child->has_cilk_callee == 0);
  CILK_ASSERT(child->call_parent == NULL);
  CILK_ASSERT(child->spawn_parent != NULL);

  __cilkrts_alert(3, "[%d]: (Closure_return) child %o\n", ws->self, child);

  parent = child->spawn_parent;

  // At this point the status is as follows: the child is in no deque 
  // and unlocked.  However, the child is still linked with its siblings, 
  Closure_lock(ws, parent);

  CILK_ASSERT(parent->status != CLOSURE_RETURNING);
  CILK_ASSERT(parent->frame != NULL);
  // CILK_ASSERT(parent->frame->magic == CILK_STACKFRAME_MAGIC);

  Closure_lock(ws, child);
  Closure_remove_child(ws, parent, child);

  /* now the child is no longer needed */
  Closure_unlock(ws, child);
  Closure_destroy(ws, child);

  /* 
   * the two fences ensure dag consistency (Backer)
   */
  CILK_ASSERT(parent->join_counter > 0);
  Cilk_fence();
  --parent->join_counter;
  Cilk_fence();

  res = provably_good_steal_maybe(ws, parent);
  Closure_unlock(ws, parent);

  return res;
}

/* 
 * ANGE: t is returning; call the return protocol; see comments above
 * Closure_return.  res is either the next closure to execute (provably-good-
 * steal the parent closure), or NULL is nothing should be executed next.
 *
 * Only called from do_what_it_says when the closure->status =
 * CLOSURE_RETURNING
 */
Closure *return_value(__cilkrts_worker *const ws, Closure *t) {
  __cilkrts_alert(3, "[%d]: (return_value) closure %p\n", ws->self, t);

  Closure *res = NULL;
  CILK_ASSERT(t->status == CLOSURE_RETURNING);
  
  __cilkrts_alert(3, "[%d]: returning closure %p\n", ws->self, t);
  if(t->call_parent == NULL) {
    res = Closure_return(ws, t);

  } else {
    // ANGE: the ONLY way a closure with call parent can reach here
    // is when the user program calls Cilk_exit, leading to global abort
    // MAK: We don't support this!!!
  }

  return res;
}
