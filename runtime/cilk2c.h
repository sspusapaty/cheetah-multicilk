#ifndef _CILK2C_H
#define _CILK2C_H

#include "cilk-internal.h"
#include <stdlib.h>

// mainly used by invoke-main.c
extern unsigned long ZERO;

// These functions are mostly inlined by the compiler, except for
// __cilkrts_leave_frame.  However, their implementations are also
// provided in cilk2c.c.  The implementations in cilk2c.c are used
// by invoke-main.c and can be used to "hand compile" cilk code.
void __cilkrts_enter_frame(__cilkrts_stack_frame *sf);
void __cilkrts_enter_frame_fast(__cilkrts_stack_frame *sf);
void __cilkrts_save_fp_ctrl_state(__cilkrts_stack_frame *sf);
void __cilkrts_detach(__cilkrts_stack_frame *self);
void __cilkrts_sync(__cilkrts_stack_frame *sf);
void __cilkrts_pop_frame(__cilkrts_stack_frame *sf);
void __cilkrts_leave_frame(__cilkrts_stack_frame *sf);
int __cilkrts_get_nworkers(void);
#endif
