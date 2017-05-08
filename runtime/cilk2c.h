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

void __cilkrts_set_stolen(__cilkrts_stack_frame *sf);

void __cilkrts_set_unsynced(__cilkrts_stack_frame *sf);

void __cilkrts_set_synced(__cilkrts_stack_frame *sf);

/* Returns nonzero if the frame is not synched. */
int __cilkrts_unsynced(__cilkrts_stack_frame *sf);

/* Returns nonzero if the frame has been stolen. */
int __cilkrts_stolen(__cilkrts_stack_frame *sf);

/* Returns nonzero if the frame is synched. */
int __cilkrts_synced(__cilkrts_stack_frame *sf);

/* Returns nonzero if the frame has never been stolen. */
int __cilkrts_not_stolen(__cilkrts_stack_frame *sf);

void __cilkrts_enter_frame(__cilkrts_stack_frame *sf);

void __cilkrts_enter_frame_fast(__cilkrts_stack_frame * sf);

void __cilkrts_detach(__cilkrts_stack_frame * self);

void __cilkrts_sync(__cilkrts_stack_frame *sf);

void __cilkrts_pop_frame(__cilkrts_stack_frame * sf);

void __cilkrts_leave_frame(__cilkrts_stack_frame * sf);

#endif
