//----- * -----

void __cilkrts_put_stack(full_frame *ff, __cilkrts_stack_frame *sf) {
  /* When suspending frame ff prior to stealing it, __cilkrts_put_stack is
   * used to store the stack pointer for eventual sync.  When suspending
   * frame ff prior to a sync, __cilkrts_put_stack is called to re-establish
   * the sync stack pointer, offsetting it by any change in the stack depth
   * that occured between the spawn and the sync.
   * Although it is not usually meaningful to add two pointers, the value of
   * ff->sync_sp at the time of this call is really an integer, not a
   * pointer.
   */
  ptrdiff_t sync_sp_i = (ptrdiff_t) ff->sync_sp;
  char* sp = (char*) SP(sf);

  ff->sync_sp = sp + sync_sp_i;
}

void __cilkrts_take_stack(full_frame *ff, void *sp) {
  /* When resuming the parent after a steal, __cilkrts_take_stack is used to
   * subtract the new stack pointer from the current stack pointer, storing
   * the offset in ff->sync_sp.  When resuming after a sync,
   * __cilkrts_take_stack is used to subtract the new stack pointer from
   * itself, leaving ff->sync_sp at zero (null).  Although the pointers being
   * subtracted are not part of the same contiguous chunk of memory, the
   * flat memory model allows us to subtract them and get a useable offset.
   */
  ptrdiff_t sync_sp_i = ff->sync_sp - (char*) sp;

  ff->sync_sp = (char *) sync_sp_i;
}

//----- * -----
/* W becomes the owner of F and F can be stolen from W */
void make_runnable(__cilkrts_worker *w, full_frame *ff) {
  w->l->frame_ff = ff;

  /* CALL_STACK is invalid (the information is stored implicitly in W) */
  ff->call_stack = 0;
}



void __cilkrts_make_unrunnable_sysdep(__cilkrts_worker *w,
                                      full_frame *ff,
                                      __cilkrts_stack_frame *sf,
                                      int is_loot,
                                      const char *why) {
  (void)w; /* unused */
  sf->except_data = 0;

  if (is_loot) {
    // MAK: Never used
    //if (ff->frame_size == 0)
    //ff->frame_size = __cilkrts_get_frame_size(sf);

    // Null loot's sp for debugging purposes (so we'll know it's not valid)
    SP(sf) = 0;
  }
}

/*
 * The worker parameter is unused, except for print-debugging purposes.
 */
void make_unrunnable(__cilkrts_worker *w,
		     full_frame *ff,
		     __cilkrts_stack_frame *sf,
		     int is_loot,
		     const char *why) {
  /* CALL_STACK becomes valid again */
  ff->call_stack = sf;

  if (sf) {
    sf->flags |= CILK_FRAME_STOLEN | CILK_FRAME_SUSPENDED;
    sf->worker = 0;

    if (is_loot) {
      __cilkrts_put_stack(ff, sf);
      // MAK: replaces __cilkrts_make_unrunnable_sysdep(w, ff, sf, is_loot, why);

      SP(sf) = 0;
    }
  }
}


//----- 2 -----

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
char* get_sp_for_executing_sf(char* stack_base,
			      full_frame *ff,
			      __cilkrts_stack_frame *sf) {    
  char* new_stack_base = stack_base - 256;
    
  // Whatever correction we choose, align the final stack top.
  // This alignment seems to be necessary in particular on 32-bit
  // Linux, and possibly Mac. (Is 32-byte alignment is sufficient?)
  /* 256-byte alignment. Why not? */
  const uintptr_t align_mask = ~(256 -1);
  new_stack_base = (char*)((size_t)new_stack_base & align_mask);
  return new_stack_base;
}

void sysdep_longjmp_to_sf(char* new_sp,
			  __cilkrts_stack_frame *sf) {

  // Set the stack pointer.
  SP(sf) = new_sp;

  //MAK: RESTORE_X86_FP_STATE

  __builtin_longjmp(sf->ctx, 1);
}

//----- 1 -----

char* sysdep_reset_jump_buffers_for_resume(cilk_fiber* fiber,
                                           full_frame *ff,
                                           __cilkrts_stack_frame *sf) {

  CILK_ASSERT(fiber);
  void* sp = (void*)get_sp_for_executing_sf(cilk_fiber_get_stack_base(fiber), ff, sf);
  SP(sf) = sp;

  /* Debugging: make sure stack is accessible. */
  ((volatile char *)sp)[-1];

  // Adjust the saved_sp to account for the SP we're about to run.  This will
  // allow us to track fluctations in the stack
  __cilkrts_take_stack(ff, sp);
  return sp;
}

/**
 * Method called to jump back to executing user code.
 *
 * A normal return from the runtime back to resuming user code calls
 * this method.  A computation executed using force_reduce also calls
 * this method to return to user code.
 *
 * This function should not contain any code that depends on a fiber.
 * In a force-reduce case, the user worker may not have a fiber.  In
 * the force-reduce case, we call this method directly instead of
 * calling @c user_code_resume_after_switch_into_runtime.
 */
inline void
cilkrts_resume(__cilkrts_stack_frame *sf, full_frame *ff) {
  // Save the sync stack pointer, and do the bookkeeping
  char* sync_sp = ff->sync_sp;
  __cilkrts_take_stack(ff, sync_sp);  // leaves ff->sync_sp null

  sf->flags &= ~CILK_FRAME_SUSPENDED;
  // Actually longjmp to the user code.
  // We may have exceptions to deal with, since we are resuming
  // a previous-suspended frame.
  sysdep_longjmp_to_sf(sync_sp, sf);
}

//----- 0 -----

/**
 * @brief cilk_fiber_proc that resumes user code after a successful
 * random steal.

 * This function longjmps back into the user code whose state is
 * stored in cilk_fiber_get_data(fiber)->resume_sf.  The stack pointer
 * is adjusted so that the code resumes on the specified fiber stack
 * instead of its original stack.
 *
 * This method gets executed only on a fiber freshly allocated from a
 * pool.
 *
 * @param fiber   The fiber being used to resume user code.
 * @param arg     Unused.
 */
void fiber_proc_to_resume_user_code_for_random_steal(cilk_fiber *fiber) {
  cilk_fiber_data *data = cilk_fiber_get_data(fiber);
  __cilkrts_stack_frame* sf = data->resume_sf;
  full_frame *ff;

  CILK_ASSERT(sf);

  // When we pull the resume_sf out of the fiber to resume it, clear
  // the old value.
  data->resume_sf = NULL;
  CILK_ASSERT(sf->worker == data->owner);
  ff = sf->worker->l->frame_ff;

  char* new_sp = sysdep_reset_jump_buffers_for_resume(fiber, ff, sf);

  sf->flags &= ~CILK_FRAME_SUSPENDED;

  // longjmp to user code.  Don't process exceptions here,
  // because we are resuming a stolen frame.
  sysdep_longjmp_to_sf(new_sp, sf);
  /*NOTREACHED*/
  // Intel's C compiler respects the preceding lint pragma
}

/**
 * Called by the user-code fiber right before resuming a full frame
 * (sf/ff).
 *
 * This method pulls sf/ff out of the worker, and then calls
 * cilkrts_resume to jump to user code.
 */
void user_code_resume_after_switch_into_runtime(cilk_fiber *fiber) {
  __cilkrts_worker *w = cilk_fiber_get_owner(fiber);
  __cilkrts_stack_frame *sf;
  full_frame *ff;
  sf = w->current_stack_frame;
  ff = sf->worker->l->frame_ff;

  // Actually jump to user code.
  cilkrts_resume(sf, ff);
}
