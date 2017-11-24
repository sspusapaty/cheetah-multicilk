#include "sync.h"
#include "sched.h"
#include "closure.h"
#include "readydeque.h"
#include "tls.h"
#include "common.h"
#include "cilk2c.h"

/* at a slow sync; return 0 if the sync succeeds, and 1 if suspended */
/* ANGE: The return value is opposite of what I thought */
int Cilk_sync(__cilkrts_worker *const ws, 
              __cilkrts_stack_frame *frame) {
  
  __cilkrts_alert(ALERT_SYNC, "[%d]: (Cilk_sync) frame %p\n", ws->self, frame);

  Closure *t;
  int res = SYNC_READY;

  //----- EVENT_CILK_SYNC

  deque_lock_self(ws);
  t = deque_peek_bottom(ws, ws->self); 
  Closure_lock(ws, t);
  /* assert we are really at the top of the stack */
  CILK_ASSERT(Closure_at_top_of_stack(ws));

  // reset_closure_frame(ws, t);
  CILK_ASSERT(ws == __cilkrts_get_tls_worker());
  CILK_ASSERT(t->status == CLOSURE_RUNNING);
  CILK_ASSERT(t->frame != NULL);
  CILK_ASSERT(t->frame == frame);
  CILK_ASSERT(frame->worker == ws);
  CILK_ASSERT(__cilkrts_stolen(t->frame));
  CILK_ASSERT(t->has_cilk_callee == 0);
  // CILK_ASSERT(ws, t->frame->magic == CILK_STACKFRAME_MAGIC);
 
  // ANGE: we might have passed a sync successfully before and never gotten back to
  // runtime but returning to another ancestor that needs to sync ... in which
  // case we might have a fiber to free, but it's never the same fiber that we
  // are on right now.
  if(ws->l->fiber_to_free) {
      CILK_ASSERT(ws->l->fiber_to_free != t->fiber);
      // we should free this fiber now and we can as long as we are not on it
      cilk_fiber_deallocate_to_heap(ws->l->fiber_to_free);
      ws->l->fiber_to_free = NULL;
  }

  if(Closure_has_children(t)) {
    // MAK: FIBER-SYNC GUESS
    __cilkrts_alert(ALERT_SYNC, "[%d]: (Cilk_sync) outstanding children\n", ws->self, frame);

    ws->l->fiber_to_free = t->fiber;
    t->fiber = NULL;
    // place holder for reducer map; the views in tlmm (if any) are updated 
    // by the last strand in Closure t before sync; need to reduce 
    // these when successful provably good steal occurs
    Closure_suspend(ws, t);
    res = SYNC_NOT_READY;
  } else {
    setup_for_sync(ws, t);
  }

  Closure_unlock(ws, t);
  deque_unlock_self(ws);

  return res;
}
