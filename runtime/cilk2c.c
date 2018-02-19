#include <stdio.h>

#include "debug.h"

#include "cilk2c.h"
#include "cilk-internal.h"
#include "fiber.h"
#include "membar.h"
#include "scheduler.h"

// This function is actually inlined by the compiler, so user code actually
// use the version that the compiler knows of
void __cilkrts_enter_frame(__cilkrts_stack_frame *sf) {
  // Does not supporting slow path yet
  __cilkrts_worker *w = __cilkrts_get_tls_worker();
  __cilkrts_alert(ALERT_CFRAME, "[%d]: (__cilkrts_enter_frame) frame %p\n", w->self, sf);
  
  sf->flags = CILK_FRAME_VERSION;
  sf->call_parent = w->current_stack_frame; 

  WHEN_CILK_DEBUG(sf->magic = CILK_STACKFRAME_MAGIC);
  // WHEN_CILK_DEBUG(sf->debug_call_parent = w->current_stack_frame);

  sf->worker = w;
  w->current_stack_frame = sf;
}

// This function is actually inlined by the compiler, so user code actually
// use the version that the compiler knows of
// This version is only used in invoke-main.c
void __cilkrts_enter_frame_fast(__cilkrts_stack_frame * sf) {
  __cilkrts_worker * w = __cilkrts_get_tls_worker();
  __cilkrts_alert(ALERT_CFRAME, "[%d]: (__cilkrts_enter_frame_fast) frame %p\n", w->self, sf);

  sf->flags = CILK_FRAME_VERSION;
  sf->call_parent = w->current_stack_frame; 

  WHEN_CILK_DEBUG(sf->magic = CILK_STACKFRAME_MAGIC);
  // WHEN_CILK_DEBUG(sf->debug_call_parent = w->current_stack_frame);

  sf->worker = w;
  w->current_stack_frame = sf;
}

// This function is actually inlined by the compiler, so user code actually
// use the version that the compiler knows of
// This version of the function is actually used only in tlmm-invoke-main.c
void __cilkrts_detach(__cilkrts_stack_frame * sf) {
  struct __cilkrts_worker * w = sf->worker;
  __cilkrts_alert(ALERT_CFRAME, "[%d]: (__cilkrts_detach) frame %p\n", w->self, sf);

  CILK_ASSERT(w, sf->flags & CILK_FRAME_VERSION);
  CILK_ASSERT(w, w == __cilkrts_get_tls_worker());
  CILK_ASSERT(w, w->current_stack_frame == sf);
  CILK_ASSERT(w, sf->magic == CILK_STACKFRAME_MAGIC);

  struct __cilkrts_stack_frame * parent = sf->call_parent;
  struct __cilkrts_stack_frame * volatile * tail = w->tail;
    
  Cilk_membar_StoreStore();
  // store parent at *tail, and then increment tail
  *tail++ = parent;

  CILK_ASSERT(w, tail < w->ltq_limit);
 
  w->tail = tail;
  sf->flags |= CILK_FRAME_DETACHED;
}

void __cilkrts_save_fp_ctrl_state(__cilkrts_stack_frame *sf) {
  sysdep_save_fp_ctrl_state(sf);
}

void __cilkrts_sync(__cilkrts_stack_frame *sf) {

  __cilkrts_worker *w = __cilkrts_get_tls_worker();
  __cilkrts_alert(ALERT_SYNC, "[%d]: (__cilkrts_sync) syncing frame %p\n", w->self, sf);

  CILK_ASSERT(w, sf->flags & CILK_FRAME_VERSION);
  CILK_ASSERT(w, sf->magic == CILK_STACKFRAME_MAGIC);

  if(sf->worker != w) {
    fprintf(stderr, "[%d]: sf: %p, sf->worker: %p, w: %p.\n", w->self, sf, sf->worker, w);
    fprintf(stderr, "[%d]: test: %d.\n", w->self, w->l->test);
  }
  CILK_ASSERT(w, sf == w->current_stack_frame);
  CILK_ASSERT(w, sf->worker == w);

  if( Cilk_sync(w, sf) == SYNC_READY ) {
    __cilkrts_alert(ALERT_SYNC, "[%d]: (__cilkrts_sync) synced frame %p!\n", w->self, sf);
    // The Cilk_sync restores the original rsp stored in sf->ctx
    // if this frame is ready to sync.
    sysdep_longjmp_to_sf(sf);
  } else {
    __cilkrts_alert(ALERT_SYNC, "[%d]: (__cilkrts_sync) waiting to sync frame %p!\n", w->self, sf);
    longjmp_to_runtime(w);                        
  }
}

// This function is actually inlined by the compiler, so user code actually
// use the version that the compiler knows of
// This version of the function is actually used only in invoke-main.c
void __cilkrts_pop_frame(__cilkrts_stack_frame * sf) {
  __cilkrts_worker * w = sf->worker;
  __cilkrts_alert(ALERT_CFRAME, "[%d]: (__cilkrts_pop_frame) frame %p\n", w->self, sf);

  CILK_ASSERT(w, sf->flags & CILK_FRAME_VERSION);
  CILK_ASSERT(w, w == __cilkrts_get_tls_worker());
  w->current_stack_frame = sf->call_parent;
  sf->call_parent = 0;
}

void __cilkrts_leave_frame(__cilkrts_stack_frame * sf) {

  __cilkrts_worker * w = __cilkrts_get_tls_worker();
  __cilkrts_alert(ALERT_CFRAME, "[%d]: (__cilkrts_leave_frame) leaving frame %p\n", w->self, sf);

  CILK_ASSERT(w, sf->flags & CILK_FRAME_VERSION);
  CILK_ASSERT(w, sf->worker == w);
  WHEN_CILK_DEBUG(sf->magic = ~CILK_STACKFRAME_MAGIC);

  if(sf->flags & CILK_FRAME_DETACHED) { // if this frame is detached
    w->tail--;
    Cilk_membar_StoreLoad();

    if( w->exc > w->tail ) {
      // this may not return if last work item has been stolen
      Cilk_exception_handler(); 
    }
    
    CILK_ASSERT(w, *(w->tail) == w->current_stack_frame);

  } else {
    // We use else instead of if here, because a detached frame 
    // would never need to call Cilk_set_return, which performs the 
    // return protocol of a full frame back to its parent frame 
    // when the full frame is called (not spawned).  A spawned full 
    // frame returning is done via a different protocol, which is 
    // triggered in Cilk_exception_handler. 
    // Similarly, a detached frame cannot be the last frame (otherwise
    // the frame that spawned it would be the last frame).

    if(sf->flags & CILK_FRAME_STOLEN) { // if this frame has a full frame
      // leaving a full frame, need to get the full frame for its call
      // parent back onto the deque
      __cilkrts_alert(ALERT_RETURN, "[%d]: (__cilkrts_leave_frame) parent is call_parent!\n", w->self);
      Cilk_set_return(w);
      CILK_ASSERT(w, w->current_stack_frame->flags & CILK_FRAME_VERSION);
    }
  }
}
