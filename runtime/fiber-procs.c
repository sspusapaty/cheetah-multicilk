#include "fiber-procs.h"

char* sysdep_reset_jump_buffers_for_resume(cilk_fiber* fiber,
                                           __cilkrts_stack_frame *sf) {
  CILK_ASSERT(fiber);
  char* new_stack_base = fiber->m_stack_base - 256;
    
  // Whatever correction we choose, align the final stack top.
  // This alignment seems to be necessary in particular on 32-bit
  // Linux, and possibly Mac. (Is 32-byte alignment is sufficient?)
  /* 256-byte alignment. Why not? */
  const uintptr_t align_mask = ~(256 -1);
  new_stack_base = (char*)((size_t)new_stack_base & align_mask);
  void* sp = (void*) new_stack_base;
                 // get_sp_for_executing_sf(fiber->m_stack_base);
  SP(sf) = sp;

  /* Debugging: make sure stack is accessible. */
  ((volatile char *)sp)[-1];

  // Adjust the saved_sp to account for the SP we're about to run.  This will
  // allow us to track fluctations in the stack

  // MAK: Avoiding w. cilk2c hax
  // __cilkrts_take_stack(ff, sp);
  return sp;
}


void sysdep_longjmp_to_sf(char* new_sp, __cilkrts_stack_frame *sf) {

  // Set the stack pointer.
  SP(sf) = new_sp;

#ifdef RESTORE_X86_FP_STATE
  // Restore the floating point state that was set in this frame at the
  // last spawn.
  //
  // This feature is only available in ABI 1 or later frames, and only
  // needed on IA64 or Intel64 processors.
  restore_x86_fp_state(sf);
#endif

  __builtin_longjmp(sf->ctx, 1);
}

void fiber_proc_to_resume_user_code_for_random_steal(cilk_fiber *fiber) {
  __cilkrts_stack_frame* sf = fiber->resume_sf;
  __cilkrts_worker* ws = sf->worker;
  Closure *t;

  CILK_ASSERT(sf);

  // When we pull the resume_sf out of the fiber to resume it, clear
  // the old value.
  fiber->resume_sf = NULL;

  deque_lock_self(ws);
  t = deque_peek_bottom(ws, ws->self); 
  deque_unlock_self(ws);

  CILK_ASSERT(ws == fiber->owner);

  // Also, this function needs to be wrapped into a try-catch block
  // so the compiler generates the appropriate exception information
  // in this frame.
    
  // TBD: IS THIS HANDLER IN THE WRONG PLACE?  Can we longjmp out of
  // this function (and does it matter?)
    
  char* new_sp = sysdep_reset_jump_buffers_for_resume(fiber, sf);

  // longjmp to user code.  Don't process exceptions here,
  // because we are resuming a stolen frame.
  sysdep_longjmp_to_sf(new_sp, sf);
  /*NOTREACHED*/
  // Intel's C compiler respects the preceding lint pragma
}
