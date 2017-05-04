#ifndef _CILK2C_H
#define _CILK2C_H

#include "cilk.h"
#include <stdlib.h>

extern unsigned long ZERO;

#define HELPER_PREAMBLE                                                 \
    __cilkrts_stack_frame *sf = (__cilkrts_stack_frame *) \
                            alloca(sizeof(__cilkrts_stack_frame));

#define PREAMBLE                                                \
    alloca(ZERO);                                               \
    __cilkrts_stack_frame *sf = (__cilkrts_stack_frame *) \
                 alloca(sizeof(__cilkrts_stack_frame) );

static inline void __cilkrts_set_stolen(__cilkrts_stack_frame *sf) {
    sf->flags |= CILK_FRAME_STOLEN;
}

static inline void __cilkrts_set_unsynced(__cilkrts_stack_frame *sf) {
    sf->flags |= CILK_FRAME_UNSYNCHED;
}

static inline void __cilkrts_set_synced(__cilkrts_stack_frame *sf) {
    sf->flags &= ~CILK_FRAME_UNSYNCHED;
}

/* Returns nonzero if the frame is not synched. */
static inline int __cilkrts_unsynced(__cilkrts_stack_frame *sf) {
    return (sf->flags & CILK_FRAME_UNSYNCHED);
}

/* Returns nonzero if the frame has been stolen. */
static inline int __cilkrts_stolen(__cilkrts_stack_frame *sf) {
    return (sf->flags & CILK_FRAME_STOLEN);
}

/* Returns nonzero if the frame is synched. */
static inline int __cilkrts_synced(__cilkrts_stack_frame *sf) {
    return( (sf->flags & CILK_FRAME_UNSYNCHED) == 0 );
}

/* Returns nonzero if the frame has never been stolen. */
static inline int __cilkrts_not_stolen(__cilkrts_stack_frame *sf) {
    return( (sf->flags & CILK_FRAME_STOLEN) == 0);
}

void __cilkrts_enter_frame(__cilkrts_stack_frame *sf);

void __cilkrts_enter_frame_fast(__cilkrts_stack_frame * sf);

void __cilkrts_detach(__cilkrts_stack_frame * self);

void __cilkrts_sync(__cilkrts_stack_frame *sf);

void __cilkrts_pop_frame(__cilkrts_stack_frame * sf);

void __cilkrts_leave_frame(__cilkrts_stack_frame * sf);

#endif
