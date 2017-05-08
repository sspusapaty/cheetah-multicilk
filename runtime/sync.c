#include "sync.h"
#include "closure.h"
#include "readydeque.h"
#include "tls.h"
#include "common.h"

/* at a slow sync; return 0 if the sync succeeds, and 1 if suspended */
/* ANGE: The return value is opposite of what I thought */
int Cilk_sync(__cilkrts_worker *const ws, 
              __cilkrts_stack_frame *frame) {

  Closure *t;
  int res = SYNC_READY;

  //----- EVENT_CILK_SYNC

  deque_lock_self(ws);
  t = deque_peek_bottom(ws, ws->self); 
  Closure_lock(ws, t);
  /* assert we are really at the top of the stack */
  CILK_ASSERT(ws, Closure_at_top_of_stack(ws));

  // reset_closure_frame(ws, t);
  CILK_ASSERT(ws == __cilkrts_get_tls_worker());
  CILK_ASSERT(t->status == CLOSURE_RUNNING);
  CILK_ASSERT(t->frame != NULL);
  CILK_ASSERT(t->frame == frame);
  CILK_ASSERT(frame->worker == ws);
  CILK_ASSERT(__cilkrts_stolen(t->frame));
  CILK_ASSERT(t->has_cilk_callee == 0);
  // CILK_ASSERT(ws, t->frame->magic == CILK_STACKFRAME_MAGIC);

  if(Closure_has_children(t)) {

    // place holder for reducer map; the views in tlmm (if any) are updated 
    // by the last strand in Closure t before sync; need to reduce 
    // these when successful provably good steal occurs
    Closure_suspend(ws, t);
    res = SYNC_NOT_READY;

  } else {
    __cilkrts_set_synced(t->frame);
    // restore the original rsp 
    //t->frame->ctx[RSP_INDEX] = (void *) t->frame_rsp;

  }

  Closure_unlock(ws, t);
  deque_unlock_self(ws);


  return res;
}
