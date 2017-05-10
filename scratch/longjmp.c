/**
 * This method is the first method that should execute after we've
 * switched to a scheduling fiber from user code.
 *
 * @param fiber The scheduling fiber for the current worker.
 * @param wptr  The current worker.
 */
static void enter_runtime_transition_proc(cilk_fiber *fiber)
{
  // We can execute this method for one of three reasons:
  // 1. Undo-detach finds parent stolen.
  // 2. Sync suspends frame.
  // 3. Return from Cilk entry point.
  //
  //
  // In cases 1 and 2, the frame may be truly suspended or
  // may be immediately executed by this worker after provably_good_steal.
  //
  // 
  // There is a fourth case, which can, but does not need to execute
  // this function:
  //   4. Starting up the scheduling loop on a user or
  //      system worker.  In this case, we won't have
  //      a scheduling stack function to run.
  __cilkrts_worker* w = cilk_fiber_get_owner(fiber);
  scheduling_stack_fcn_t fcn = w->l->post_suspend;

  if (fcn) {
    // Run the continuation function passed to longjmp_into_runtime

    full_frame *ff2 = w->l->frame_ff;
    __cilkrts_stack_frame *sf2 = w->l->suspended_stack;

    w->l->post_suspend = 0;
    w->l->suspended_stack = 0;

    // Conceptually, after clearing w->l->frame_ff,
    // w no longer owns the full frame ff.
    // The next time another (possibly different) worker takes
    // ownership of ff will be at a provably_good_steal on ff. 
    w->l->frame_ff = NULL;

    CILK_ASSERT(fcn);
    CILK_ASSERT(ff2);
    fcn(w, ff2, sf2);

    // After we run the scheduling stack function, we shouldn't
    // (still) not have a full frame.
    CILK_ASSERT(NULL == w->l->frame_ff);

  }
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
static inline NORETURN
cilkrts_resume(__cilkrts_stack_frame *sf, full_frame *ff)
{
    // Save the sync stack pointer, and do the bookkeeping
    char* sync_sp = ff->sync_sp;
    __cilkrts_take_stack(ff, sync_sp);  // leaves ff->sync_sp null

    sf->flags &= ~CILK_FRAME_SUSPENDED;
    // Actually longjmp to the user code.
    // We may have exceptions to deal with, since we are resuming
    // a previous-suspended frame.
    sysdep_longjmp_to_sf(sync_sp, sf, ff);
}

/**
 * Called by the user-code fiber right before resuming a full frame
 * (sf/ff).
 *
 * This method pulls sf/ff out of the worker, and then calls
 * cilkrts_resume to jump to user code.
 */
static NORETURN
user_code_resume_after_switch_into_runtime(cilk_fiber *fiber)
{
    __cilkrts_worker *w = cilk_fiber_get_owner(fiber);
    __cilkrts_stack_frame *sf;
    full_frame *ff;
    sf = w->current_stack_frame;
    ff = sf->worker->l->frame_ff;

#if FIBER_DEBUG >= 1    
    CILK_ASSERT(ff->fiber_self == fiber);
    cilk_fiber_data *fdata = cilk_fiber_get_data(fiber);
    DBGPRINTF ("%d-%p: resume_after_switch_into_runtime, fiber=%p\n",
               w->self, w, fiber);
    CILK_ASSERT(sf == fdata->resume_sf);
#endif

    // Actually jump to user code.
    cilkrts_resume(sf, ff);
 }

/* The current stack is about to either be suspended or destroyed.  This
 * function will switch to the stack on which the scheduler is suspended and
 * resume running the scheduler within function do_work().  Upon waking up,
 * the scheduler will run the 'cont' function, using the supplied worker and
 * frame.
 */
static void longjmp_into_runtime(__cilkrts_worker *w,
                     scheduling_stack_fcn_t fcn,
                     __cilkrts_stack_frame *sf)
{
    full_frame *ff, *ff2;

    CILK_ASSERT(!w->l->post_suspend);
    ff = w->l->frame_ff;

    w->l->post_suspend = fcn;
    w->l->suspended_stack = sf;

    // Current fiber is either the (1) one we are about to free,
    // or (2) it has been passed up to the parent.
    cilk_fiber *current_fiber = ( w->l->fiber_to_free ?
                                  w->l->fiber_to_free :
                                  w->l->frame_ff->parent->fiber_child );
    CILK_ASSERT(NULL == w->l->frame_ff->fiber_self);

    // Clear the sf in the current fiber for cleanliness, to prevent
    // us from accidentally resuming a bad sf.
    // Technically, resume_sf gets overwritten for a fiber when
    // we are about to resume it anyway.
    current_fiber->resume_sf = NULL;
    CILK_ASSERT(current_fiber->owner == w);

    // Set the function to execute immediately after switching to the
    // scheduling fiber, but before freeing any fibers.
    cilk_fiber_set_post_switch_proc(w->l->scheduling_fiber,
                                    enter_runtime_transition_proc);
    
    if (w->l->fiber_to_free) {
        // Case 1: we are freeing this fiber.  We never
        // resume this fiber again after jumping into the runtime.
        w->l->fiber_to_free = NULL;

        // Extra check. Normally, the fiber we are about to switch to
        // should have a NULL owner.
        CILK_ASSERT(NULL == w->l->scheduling_fiber->owner);

        cilk_fiber_remove_reference_from_self_and_resume_other(current_fiber,
                                                               &w->l->fiber_pool,
                                                               w->l->scheduling_fiber);
        // We should never come back here!
        CILK_ASSERT(0);
    } else {        
        // Case 2: We are passing the fiber to our parent because we
        // are leftmost.  We should come back later to
        // resume execution of user code.
        //
        // If we are not freeing a fiber, there we must be
        // returning from a spawn or processing an exception.  The
        // "sync" path always frees a fiber.
        // 
        // We must be the leftmost child, and by left holder logic, we
        // have already moved the current fiber into our parent full
        // frame.

        cilk_fiber_suspend_self_and_resume_other(current_fiber,
                                                 w->l->scheduling_fiber);
        // Resuming this fiber returns control back to
        // this function because our implementation uses OS fibers.
        //
        // On Unix, we could have the choice of passing the
        // user_code_resume_after_switch_into_runtime as an extra "resume_proc"
        // that resumes execution of user code instead of the
        // jumping back here, and then jumping back to user code.

        user_code_resume_after_switch_into_runtime(current_fiber);
    }
}
