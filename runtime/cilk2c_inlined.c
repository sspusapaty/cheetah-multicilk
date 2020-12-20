// ================================================================
// This file contains the compiler ABI, which corresponds to
// conceptually what the compiler generates to implement Cilk code.
// They are included here in part as documentation, and in part
// allow one to write and run "hand-compiled" Cilk code.
// ================================================================

#include <stdatomic.h>
#include <stdio.h>
#include <unwind.h>

#include "cilk-internal.h"
#include "cilk2c.h"
#include "debug.h"
#include "fiber.h"
#include "global.h"
#include "readydeque.h"
#include "scheduler.h"

// inlined by the compiler
void __cilkrts_enter_frame(__cilkrts_stack_frame *sf) {
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    cilkrts_alert(CFRAME, w, "__cilkrts_enter_frame %p", sf);

    sf->flags = 0;
    sf->magic = frame_magic;
    sf->call_parent = w->current_stack_frame;
    sf->worker = w;
    w->current_stack_frame = sf;
    // WHEN_CILK_DEBUG(sf->magic = CILK_STACKFRAME_MAGIC);

    // Pedigree maintenance.
    if (sf->call_parent != NULL) {
        sf->parent_rank = sf->call_parent->rank++;
    } else {
        sf->parent_rank = -1;
    }
    sf->rank = 0;
}

// inlined by the compiler; this implementation is only used in invoke-main.c
void __cilkrts_enter_frame_fast(__cilkrts_stack_frame *sf) {
    __cilkrts_worker *w = __cilkrts_get_tls_worker();
    cilkrts_alert(CFRAME, w, "__cilkrts_enter_frame_fast %p", sf);

    sf->flags = 0;
    sf->magic = frame_magic;
    sf->call_parent = w->current_stack_frame;
    sf->worker = w;
    w->current_stack_frame = sf;

    // Pedigree maintenance.
    if (sf->call_parent != NULL) {
        sf->parent_rank = sf->call_parent->rank++;
    } else {
        sf->parent_rank = -1;
    }
    sf->rank = 0;
}

// inlined by the compiler; this implementation is only used in invoke-main.c
void __cilkrts_detach(__cilkrts_stack_frame *sf) {
    struct __cilkrts_worker *w = sf->worker;
    cilkrts_alert(CFRAME, w, "__cilkrts_detach %p", sf);

    CILK_ASSERT(w, CHECK_CILK_FRAME_MAGIC(w->g, sf));
    CILK_ASSERT(w, sf->worker == __cilkrts_get_tls_worker());
    CILK_ASSERT(w, w->current_stack_frame == sf);

    struct __cilkrts_stack_frame *parent = sf->call_parent;
    sf->flags |= CILK_FRAME_DETACHED;
    struct __cilkrts_stack_frame **tail =
        atomic_load_explicit(&w->tail, memory_order_relaxed);
    CILK_ASSERT(w, (tail + 1) < w->ltq_limit);

    // store parent at *tail, and then increment tail
    *tail++ = parent;
    /* Release ordering ensures the two preceding stores are visible. */
    atomic_store_explicit(&w->tail, tail, memory_order_release);
}

// inlined by the compiler; this implementation is only used in invoke-main.c
void __cilkrts_save_fp_ctrl_state(__cilkrts_stack_frame *sf) {
    sysdep_save_fp_ctrl_state(sf);
}

// inlined by the compiler; this implementation is only used in invoke-main.c
void __cilkrts_pop_frame(__cilkrts_stack_frame *sf) {
    __cilkrts_worker *w = sf->worker;
    cilkrts_alert(CFRAME, w, "__cilkrts_pop_frame %p", sf);

    CILK_ASSERT(w, CHECK_CILK_FRAME_MAGIC(w->g, sf));
    CILK_ASSERT(w, sf->worker == __cilkrts_get_tls_worker());
    /* The inlined version in the Tapir compiler uses release
       semantics for the store to call_parent, but relaxed
       order may be acceptable for both.  A thief can't see
       these operations until the Dekker protocol with a
       memory barrier has run. */
    w->current_stack_frame = sf->call_parent;
    sf->call_parent = NULL;
}
