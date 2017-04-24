#include "cilk2c.h"

#include "tls.h"
//#include "common.h"
#include "membar.h"
#include "sched.h"
// This function is actually inlined by the compiler, so user code actually
// use the version that the compiler knows of (i.e., Cilk Plus version); 
void __cilkrts_enter_frame(__cilkrts_stack_frame *sf) {
  // MK: not supporting slow path yet
  __cilkrts_worker *ws = __cilkrts_get_tls_worker();
  __cilkrts_alert("Worker %d: entering frame %p\n", ws->self, sf);
  /*

    if(ws == 0) { // slow path, rare
    ws = __cilkrts_bind_thread(); 
    sf->flags = CILK_FRAME_LAST;
    } else { 
    sf->flags = 0;
    }
  */
  
  sf->flags = 0;
  sf->call_parent = ws->current_stack_frame; 

  // WHEN_CILK_DEBUG(sf->magic = CILK_STACKFRAME_MAGIC);
  // WHEN_CILK_DEBUG(sf->debug_call_parent = ws->current_stack_frame;)

  sf->worker = ws;
  ws->current_stack_frame = sf;
}

// This function is actually inlined by the compiler, so user code actually
// use the version that the compiler knows of (i.e., Cilk Plus version); 
// this version of the function is actually used only in tlmm-invoke-main.c
void __cilkrts_enter_frame_fast(__cilkrts_stack_frame * sf) {
  __cilkrts_worker * ws = __cilkrts_get_tls_worker();
  __cilkrts_alert("Worker %d: entering frame %p... fast!\n", ws->self, sf);

  sf->flags = 0;
  sf->call_parent = ws->current_stack_frame; 

  // WHEN_CILK_DEBUG(sf->magic = CILK_STACKFRAME_MAGIC);
  // WHEN_CILK_DEBUG(sf->debug_call_parent = ws->current_stack_frame;)

  sf->worker = ws;
  ws->current_stack_frame = sf;
}

// This function is actually inlined by the compiler, so user code actually
// use the version that the compiler knows of (i.e., Cilk Plus version); 
// this version of the function is actually used only in tlmm-invoke-main.c
void __cilkrts_detach(__cilkrts_stack_frame * self) {
  struct __cilkrts_worker * ws = self->worker;
  __cilkrts_alert("Worker %d: detaching frame %p\n", ws->self, self);

  CILK_ASSERT(ws == __cilkrts_get_tls_worker());
  CILK_ASSERT(ws->current_stack_frame == self);
  // CILK_ASSERT(ws, self->magic == CILK_STACKFRAME_MAGIC);

  struct __cilkrts_stack_frame * parent = self->call_parent;
  struct __cilkrts_stack_frame * volatile * tail = ws->tail;
    
  Cilk_membar_StoreStore();
  // store parent at *tail, and then increment tail
  *tail++ = parent;

  CILK_ASSERT(tail < ws->ltq_limit);
 
  ws->tail = tail;
  self->flags |= CILK_FRAME_DETACHED;
}

void __cilkrts_sync(__cilkrts_stack_frame *sf) {

  __cilkrts_worker *ws = __cilkrts_get_tls_worker();
  __cilkrts_alert("Worker %d: syncing frame %p\n", ws->self, sf);
    
  // CILK_ASSERT(ws, sf->magic == CILK_STACKFRAME_MAGIC);
  CILK_ASSERT(sf->worker == ws);
  CILK_ASSERT(sf == ws->current_stack_frame);

  if( 1 /*Cilk_sync(ws, sf) == SYNC_READY*/ ) {
    // ANGE: the Cilk_sync restores the original rsp in sf->ctx[RSP_INDEX]
    // if this frame is ready to sync.
    //stack_switch( (AddrType)sf->ctx[RBP_INDEX],
    //(AddrType)sf->ctx[RSP_INDEX], (AddrType)sf->ctx[RIP_INDEX] );
  } else {
    longjmp_to_runtime(ws);                        
  }
}

// This function is actually inlined by the compiler, so user code actually
// use the version that the compiler knows of (i.e., Cilk Plus version); 
// this version of the function is actually used only in tlmm-invoke-main.c
void __cilkrts_pop_frame(__cilkrts_stack_frame * sf) {
  __cilkrts_worker * ws = sf->worker;
  __cilkrts_alert("Worker %d: popping frame %p\n", ws->self, sf);

  CILK_ASSERT(ws == __cilkrts_get_tls_worker());
  ws->current_stack_frame = sf->call_parent;
  sf->call_parent = 0;
}

void __cilkrts_leave_frame(__cilkrts_stack_frame * sf) {
  __cilkrts_worker * ws = __cilkrts_get_tls_worker();
  __cilkrts_alert("Worker %d: leaving frame %p\n", ws->self, sf);

  CILK_ASSERT(sf->worker == ws);
  // WHEN_CILK_DEBUG(sf->magic = ~CILK_STACKFRAME_MAGIC);

  if(sf->flags & CILK_FRAME_DETACHED) { // if this frame is detached
    ws->tail--;
    Cilk_membar_StoreLoad();

    /* MK: Not yet
    if( ws->exc > ws->tail ) {
      // this may not return if last work item has been stolen
      Cilk_exception_handler(); 
    }
    */
    
    CILK_ASSERT(*(ws->tail) == ws->current_stack_frame);

  } /* MK: Not yet else {
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
      Cilk_set_return(ws); 
    }
  }
    */
}
