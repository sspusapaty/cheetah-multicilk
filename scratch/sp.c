/*
 * @brief Returns the stack address for resuming execution of sf.
 *
 * This method takes in the top of the stack to use, and then returns
 * a properly aligned address for resuming execution of sf.
 *
 *  @param sf           -   The stack frame we want to resume executing.
 *  @param stack_base   -   The top of the stack we want to execute sf on.
 *
 */
char* get_sp_for_executing_sf(char* stack_base) {    
  char* new_stack_base = stack_base - 256;
    
  // Whatever correction we choose, align the final stack top.
  // This alignment seems to be necessary in particular on 32-bit
  // Linux, and possibly Mac. (Is 32-byte alignment is sufficient?)
  /* 256-byte alignment. Why not? */
  const uintptr_t align_mask = ~(256 -1);
  new_stack_base = (char*)((size_t)new_stack_base & align_mask);
  return new_stack_base;
}

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
