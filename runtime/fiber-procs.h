#ifndef _FIBER_PROCS_H
#define _FIBER_PROCS_H

#include "fiber.h"
#include "stack_frame.h"

char* sysdep_reset_jump_buffers_for_resume(cilk_fiber* fiber,
                                           __cilkrts_stack_frame *sf);

void sysdep_longjmp_to_sf(char* new_sp, __cilkrts_stack_frame *sf);

void fiber_proc_to_resume_user_code_for_random_steal(cilk_fiber *fiber);

void cilkrts_resume(__cilkrts_stack_frame *sf, char* sync_sp);

void user_code_resume_after_switch_into_runtime(cilk_fiber *fiber);
#endif
