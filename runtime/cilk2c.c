#include <stdio.h>

#include "debug.h"

#include "cilk2c.h"
#include "cilk-internal.h"
#include "fiber.h"
#include "membar.h"
#include "scheduler.h"

// ================================================================
// This file comtains the compiler ABI, which corresponds to 
// conceptually what the compiler generates to implement Cilk code.
// They are included here in part as documentation, and in part
// allow one to write and run "hand-compiled" Cilk code.
// ================================================================

// inlined by the compiler
void __cilkrts_enter_frame(__cilkrts_stack_frame *sf) {
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    __cilkrts_alert(ALERT_CFRAME, "[%d]: (__cilkrts_enter_frame) frame %p\n", w->self, sf);

    sf->flags = CILK_FRAME_VERSION;
    sf->call_parent = w->current_stack_frame; 
    sf->worker = w;
    w->current_stack_frame = sf;
    // WHEN_CILK_DEBUG(sf->magic = CILK_STACKFRAME_MAGIC);
}

// inlined by the compiler; this implementation is only used in invoke-main.c
void __cilkrts_enter_frame_fast(__cilkrts_stack_frame * sf) {
    __cilkrts_worker * w = __cilkrts_get_tls_worker();
    __cilkrts_alert(ALERT_CFRAME, "[%d]: (__cilkrts_enter_frame_fast) frame %p\n", w->self, sf);

    sf->flags = CILK_FRAME_VERSION;
    sf->call_parent = w->current_stack_frame; 
    sf->worker = w;
    w->current_stack_frame = sf;
    // WHEN_CILK_DEBUG(sf->magic = CILK_STACKFRAME_MAGIC);
}

// inlined by the compiler; this implementation is only used in invoke-main.c
void __cilkrts_detach(__cilkrts_stack_frame * sf) {
    struct __cilkrts_worker *w = sf->worker;
    __cilkrts_alert(ALERT_CFRAME, "[%d]: (__cilkrts_detach) frame %p\n", w->self, sf);

    CILK_ASSERT(w, sf->flags & CILK_FRAME_VERSION);
    CILK_ASSERT(w, sf->worker == __cilkrts_get_tls_worker());
    CILK_ASSERT(w, w->current_stack_frame == sf);

    struct __cilkrts_stack_frame *parent = sf->call_parent;
    struct __cilkrts_stack_frame *volatile *tail = w->tail;
    CILK_ASSERT(w, (tail+1) < w->ltq_limit);

    // store parent at *tail, and then increment tail
    *tail++ = parent;
    sf->flags |= CILK_FRAME_DETACHED;
    Cilk_membar_StoreStore();
    w->tail = tail;
}

// inlined by the compiler; this implementation is only used in invoke-main.c
void __cilkrts_save_fp_ctrl_state(__cilkrts_stack_frame *sf) {
    sysdep_save_fp_ctrl_state(sf);
}

void __cilkrts_sync(__cilkrts_stack_frame *sf) {

    __cilkrts_worker *w = sf->worker;
    __cilkrts_alert(ALERT_SYNC, "[%d]: (__cilkrts_sync) syncing frame %p\n", w->self, sf);

    CILK_ASSERT(w, sf->worker == __cilkrts_get_tls_worker());
    CILK_ASSERT(w, sf->flags & CILK_FRAME_VERSION);
    CILK_ASSERT(w, sf == w->current_stack_frame);

    if( Cilk_sync(w, sf) == SYNC_READY ) {
        __cilkrts_alert(ALERT_SYNC, 
            "[%d]: (__cilkrts_sync) synced frame %p!\n", w->self, sf);
        // The Cilk_sync restores the original rsp stored in sf->ctx
        // if this frame is ready to sync.
        sysdep_longjmp_to_sf(sf);
    } else {
        __cilkrts_alert(ALERT_SYNC, 
            "[%d]: (__cilkrts_sync) waiting to sync frame %p!\n", w->self, sf);
        longjmp_to_runtime(w);                        
    }
}

// inlined by the compiler; this implementation is only used in invoke-main.c
void __cilkrts_pop_frame(__cilkrts_stack_frame * sf) {
    __cilkrts_worker * w = sf->worker;
    __cilkrts_alert(ALERT_CFRAME, "[%d]: (__cilkrts_pop_frame) frame %p\n", w->self, sf);

    CILK_ASSERT(w, sf->flags & CILK_FRAME_VERSION);
    CILK_ASSERT(w, sf->worker == __cilkrts_get_tls_worker());
    w->current_stack_frame = sf->call_parent;
    sf->call_parent = 0;
}

void __cilkrts_leave_frame(__cilkrts_stack_frame * sf) {

    __cilkrts_worker *w = sf->worker;
    __cilkrts_alert(ALERT_CFRAME, 
        "[%d]: (__cilkrts_leave_frame) leaving frame %p\n", w->self, sf);

    CILK_ASSERT(w, sf->flags & CILK_FRAME_VERSION);
    CILK_ASSERT(w, sf->worker == __cilkrts_get_tls_worker());
    // WHEN_CILK_DEBUG(sf->magic = ~CILK_STACKFRAME_MAGIC);

    if(sf->flags & CILK_FRAME_DETACHED) { // if this frame is detached
        __cilkrts_stack_frame *volatile *t = w->tail;
        --t;
        w->tail = t;
        __sync_fetch_and_add(&sf->flags, ~CILK_FRAME_DETACHED); 
        // Cilk_membar_StoreLoad(); // the sync_fetch_and_add replaced mfence
                // which is slightly more efficient.  Note that this
                // optimiation is applicable *ONLY* on i386 and x86_64
        if(__builtin_expect(w->exc > t, 0)) {
            // this may not return if last work item has been stolen
            Cilk_exception_handler(); 
        }
        // CILK_ASSERT(w, *(w->tail) == w->current_stack_frame);
    } else {
        // A detached frame would never need to call Cilk_set_return, which 
        // performs the return protocol of a full frame back to its parent
        // when the full frame is called (not spawned).  A spawned full 
        // frame returning is done via a different protocol, which is 
        // triggered in Cilk_exception_handler. 
        if(sf->flags & CILK_FRAME_STOLEN) { // if this frame has a full frame
            __cilkrts_alert(ALERT_RETURN, 
                "[%d]: (__cilkrts_leave_frame) parent is call_parent!\n", w->self);
            // leaving a full frame; need to get the full frame of its call
            // parent back onto the deque
            Cilk_set_return(w);
            CILK_ASSERT(w, w->current_stack_frame->flags & CILK_FRAME_VERSION);
        }
    }
}
