/* at a slow sync; return 0 if the sync succeeds, and 1 if suspended */
/* ANGE: The return value is opposite of what I thought */
int Cilk_sync(struct __cilkrts_worker *const ws, 
              struct __cilkrts_stack_frame *frame) {

    Closure *t;
    int res = SYNC_READY;

    Cilk_event(ws, EVENT_CILK_SYNC);
    CILK_ASSERT(ws, ws->l->magic == CILK_WS_MAGIC + ((unsigned int)ws->self));

    deque_lock_self(ws);
    t = deque_peek_bottom(ws, ws->self);

    Closure_lock(ws, t);

    /* assert we are really at the top of the stack */
    CILK_ASSERT(ws, Closure_at_top_of_stack(ws));

    // reset_closure_frame(ws, t);
    CILK_ASSERT(ws, tls->ws == ws);
    CILK_ASSERT(ws, t->status == CLOSURE_RUNNING);
    CILK_ASSERT(ws, t->frame != NULL);
    CILK_ASSERT(ws, t->frame == frame);
    CILK_ASSERT(ws, frame->worker == ws);
    CILK_ASSERT(ws, __cilkrts_stolen(t->frame));
    CILK_ASSERT(ws, t->has_cilk_callee == 0);
    // CILK_ASSERT(ws, t->frame->magic == CILK_STACKFRAME_MAGIC);

    // each sync is executed only once; since we occupy user_rmap only
    // when sync fails, the user_rmap should remain NULL at this point. 
    CILK_ASSERT(ws, t->user_rmap == (RMapPageDesc *)NULL);

    if(Closure_has_children(t)) {

        WHEN_DEBUG_VERBOSE( 
            Cilk_dprintf(ws, "Get user rmap then suspend %lx.\n", t); )
        // place holder for reducer map; the views in tlmm (if any) are updated 
        // by the last strand in Closure t before sync; need to reduce 
        // these when successful provably good steal occurs
        Cilk_enter_state(ws, STATE_CONTENT);
        t->user_rmap = tlmm_get_user_rmap(ws);
        Cilk_exit_state(ws, STATE_CONTENT);
        Closure_suspend(ws, t);
        res = SYNC_NOT_READY;

    } else {
        // ANGE: If stack is not init, it must be invoke-main.
        // This can only happen in the process of abort -- a worker never
        // been stolen / steal before can be executing the invoke-main frame.
        // (the worker who started invoke-main, actually).
        CILK_ASSERT(ws, stack_initialized(ws) ||
                        (t == USE_PARAMETER_WS(invoke_main) && ws->self == 0));
        
        __cilkrts_set_synced(t->frame);
        // restore the original rsp 
        t->frame->ctx[RSP_INDEX] = (void *) t->frame_rsp;

        /* The stack_map may not be initialized in the case of abort.
         * Since all children returned, we can reclaim the space and 
         * use the real frame rsp. 
         */ 
        t->assumed_frame_rsp = UNSET_ADDR;
        t->assumed_extra_pd = UNSET;

    }
    // the stack_map may not be initialized in the case of abort
    if( __builtin_expect(stack_initialized(ws), 1) ) {
        WHEN_DEBUG_VERBOSE(Cilk_dprintf(ws, "reset stack in sync.\n");)
        tlmm_reset_stack_mapping(ws, t);
    }
    WHEN_CILK_DEBUG(tlmm_check_stack_mapping(ws, ws->self, t, 1);)

    Closure_unlock(ws, t);
    deque_unlock_self(ws);

    if(res == SYNC_READY && t->child_rmap) {
        // it's ok that we don't have the lock on the closure and there is  
        // no need for CAS; the last child has returned, and it used a CAS
        // (which includes a memory barrier) to deposit its rmap; this 
        // worker did grab a lock since to detect that the last child has 
        // returned, the rmap must be visible to this worker.
        RMapPageDesc *child_rmap = t->child_rmap;
        t->child_rmap = (RMapPageDesc *)NULL;
        // we are not holding at lock at this point;

        tlmm_reduce_rmap_into_tlmm_as_left(ws, child_rmap);
    }

    return res;
}

/* The current stack is about to either be suspended or destroyed.  This
 * function will switch to the stack on which the scheduler is suspended and
 * resume running the scheduler within function do_work().  Upon waking up,
 * the scheduler will run the 'cont' function, using the supplied worker and
 * frame.
 */
static NORETURN
longjmp_into_runtime(__cilkrts_worker *w,
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
    cilk_fiber_data* fdata = cilk_fiber_get_data(current_fiber);
    CILK_ASSERT(NULL == w->l->frame_ff->fiber_self);

    // Clear the sf in the current fiber for cleanliness, to prevent
    // us from accidentally resuming a bad sf.
    // Technically, resume_sf gets overwritten for a fiber when
    // we are about to resume it anyway.
    fdata->resume_sf = NULL;
    CILK_ASSERT(fdata->owner == w);

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
        CILK_ASSERT(NULL == cilk_fiber_get_data(w->l->scheduling_fiber)->owner);

        NOTE_INTERVAL(w, INTERVAL_DEALLOCATE_RESUME_OTHER);
        cilk_fiber_remove_reference_from_self_and_resume_other(current_fiber,
                                                               &w->l->fiber_pool,
                                                               w->l->scheduling_fiber);
        // We should never come back here!
        CILK_ASSERT(0);
    }
    else {        
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

        NOTE_INTERVAL(w, INTERVAL_SUSPEND_RESUME_OTHER);

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

NORETURN __cilkrts_c_sync(__cilkrts_worker *w,
                          __cilkrts_stack_frame *sf_at_sync)
{
    full_frame *ff; 
    STOP_INTERVAL(w, INTERVAL_WORKING);
    START_INTERVAL(w, INTERVAL_IN_RUNTIME);

    // Claim: This read of w->l->frame_ff can occur without
    // holding the worker lock because when w has reached a sync
    // and entered the runtime (because it stalls), w's deque is empty
    // and no one else can steal and change w->l->frame_ff.

    ff = w->l->frame_ff;
    
    
    w = execute_reductions_for_sync(w, ff, sf_at_sync);

    longjmp_into_runtime(w, do_sync, sf_at_sync);
}

static void do_sync(__cilkrts_worker *w, full_frame *ff,
                    __cilkrts_stack_frame *sf)
{
    //int abandoned = 1;
    enum provably_good_steal_t steal_result = ABANDON_EXECUTION;

    START_INTERVAL(w, INTERVAL_SYNC_CHECK) {
        BEGIN_WITH_WORKER_LOCK_OPTIONAL(w) {

            CILK_ASSERT(ff);
            BEGIN_WITH_FRAME_LOCK(w, ff) {
                CILK_ASSERT(sf->call_parent == 0);
                CILK_ASSERT(sf->flags & CILK_FRAME_UNSYNCHED);

                // Before switching into the scheduling fiber, we should have
                // already taken care of deallocating the current
                // fiber. 
                CILK_ASSERT(NULL == ff->fiber_self);

                // Update the frame's pedigree information if this is an ABI 1
                // or later frame
                if (CILK_FRAME_VERSION_VALUE(sf->flags) >= 1)
                {
                    sf->parent_pedigree.rank = w->pedigree.rank;
                    sf->parent_pedigree.parent = w->pedigree.parent;

                    // Note that the pedigree rank needs to be updated
                    // when setup_for_execution_pedigree runs
                    sf->flags |= CILK_FRAME_SF_PEDIGREE_UNSYNCHED;
                }

                /* the decjoin() occurs in provably_good_steal() */
                steal_result = provably_good_steal(w, ff);

            } END_WITH_FRAME_LOCK(w, ff);
            // set w->l->frame_ff = NULL after checking abandoned
            if (WAIT_FOR_CONTINUE != steal_result) {
                w->l->frame_ff = NULL;
            }
        } END_WITH_WORKER_LOCK_OPTIONAL(w);
    } STOP_INTERVAL(w, INTERVAL_SYNC_CHECK);

    // Now, if we are in a replay situation and provably_good_steal() returned
    // WAIT_FOR_CONTINUE, we should sleep, reacquire locks, call
    // provably_good_steal(), and release locks until we get a value other
    // than WAIT_FOR_CONTINUE from the function.
#ifdef CILK_RECORD_REPLAY
    // We don't have to explicitly check for REPLAY_LOG below because
    // steal_result can only be set to WAIT_FOR_CONTINUE during replay
    while(WAIT_FOR_CONTINUE == steal_result)
    {
        __cilkrts_sleep();
        BEGIN_WITH_WORKER_LOCK_OPTIONAL(w)
        {
            ff = w->l->frame_ff;
            BEGIN_WITH_FRAME_LOCK(w, ff)
            {
                steal_result = provably_good_steal(w, ff);
            } END_WITH_FRAME_LOCK(w, ff);
            if (WAIT_FOR_CONTINUE != steal_result)
                w->l->frame_ff = NULL;
        } END_WITH_WORKER_LOCK_OPTIONAL(w);
    }
#endif  // CILK_RECORD_REPLAY

    return; /* back to scheduler loop */
}
