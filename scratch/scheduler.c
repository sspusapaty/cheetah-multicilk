/* Return TRUE if the frame can be stolen, false otherwise */
static int dekker_protocol(__cilkrts_worker *victim)
{
  // increment_E and decrement_E are going to touch victim->exc.  The
  // currently executing worker must own victim's lock before they can
  // modify it
  ASSERT_WORKER_LOCK_OWNED(victim);

  /* ASSERT(E >= H); */

  increment_E(victim);

  /* ASSERT(E >= H + 1); */
  if (can_steal_from(victim)) {
    /* success, we can steal victim->head and set H <- H + 1
       in detach() */
    return 1;
  } else {
    /* failure, restore previous state */
    decrement_E(victim);
    return 0;    
  }
}

/* Link PARENT and CHILD in the spawn tree */
static full_frame *make_child(__cilkrts_worker *w, 
                              full_frame *parent_ff,
                              __cilkrts_stack_frame *child_sf,
                              cilk_fiber *fiber) 
{
  full_frame *child_ff = __cilkrts_make_full_frame(w, child_sf);

  child_ff->parent = parent_ff;
  push_child(parent_ff, child_ff);
    
  CILK_ASSERT(parent_ff->call_stack);
  child_ff->is_call_child = (fiber == NULL);

  /* PLACEHOLDER_FIBER is used as non-null marker indicating that
     child should be treated as a spawn child even though we have not
     yet assigned a real fiber to its parent. */
  if (fiber == PLACEHOLDER_FIBER)
    fiber = NULL; /* Parent actually gets a null fiber, for now */

  /* Child gets reducer map and stack of parent.
     Parent gets a new map and new stack. */
  child_ff->fiber_self = parent_ff->fiber_self;
  child_ff->sync_master = NULL;

  if (child_ff->is_call_child) {
    /* Cause segfault on any attempted access.  The parent gets
       the child map and stack when the child completes. */
    parent_ff->fiber_self = 0;
  } else {
    parent_ff->fiber_self = fiber;
  }

  incjoin(parent_ff);
  return child_ff;
}

/* w should be the currently executing worker.  
 * loot_sf is the youngest stack frame in the call stack being 
 *   unrolled (i.e., the most deeply nested stack frame.)
 *
 * When this method is called for a steal, loot_sf should be on a
 * victim worker which is different from w.
 * For CILK_FORCE_REDUCE, the victim worker will equal w.
 *
 * Before execution, the __cilkrts_stack_frame's have pointers from
 * older to younger, i.e., a __cilkrts_stack_frame points to parent.
 *
 * This method creates a full frame for each __cilkrts_stack_frame in
 * the call stack, with each full frame also pointing to its parent. 
 *
 * The method returns the full frame created for loot_sf, i.e., the
 * youngest full frame.
 */
static full_frame *unroll_call_stack(__cilkrts_worker *w, 
                                     full_frame *ff, 
                                     __cilkrts_stack_frame *const loot_sf)
{
  __cilkrts_stack_frame *sf = loot_sf;
  __cilkrts_stack_frame *rev_sf = 0;
  __cilkrts_stack_frame *t_sf;

  CILK_ASSERT(sf);
  /*CILK_ASSERT(sf->call_parent != sf);*/

  /* The leafmost frame is unsynched. */
  if (sf->worker != w)
    sf->flags |= CILK_FRAME_UNSYNCHED;

  /* Reverse the call stack to make a linked list ordered from parent
     to child.  sf->call_parent points to the child of SF instead of
     the parent.  */
  do {
    t_sf = (sf->flags & (CILK_FRAME_DETACHED|CILK_FRAME_STOLEN|CILK_FRAME_LAST))? 0 : sf->call_parent;
    sf->call_parent = rev_sf;
    rev_sf = sf;
    sf = t_sf;
  } while (sf);
  sf = rev_sf;

  /* Promote each stack frame to a full frame in order from parent
     to child, following the reversed list we just built. */
  make_unrunnable(w, ff, sf, sf == loot_sf, "steal 1");
  /* T is the *child* of SF, because we have reversed the list */
  for (t_sf = __cilkrts_advance_frame(sf); t_sf;
       sf = t_sf, t_sf = __cilkrts_advance_frame(sf)) {
    ff = make_child(w, ff, t_sf, NULL);
    make_unrunnable(w, ff, t_sf, t_sf == loot_sf, "steal 2");
  }

  /* XXX What if the leafmost frame does not contain a sync
     and this steal is from promote own deque? */
  /*sf->flags |= CILK_FRAME_UNSYNCHED;*/

  CILK_ASSERT(!sf->call_parent);
  return ff;
}

/* detach the top of the deque frame from the VICTIM and install a new
   CHILD frame in its place */
static void detach_for_steal(__cilkrts_worker *w,
                             __cilkrts_worker *victim,
                             cilk_fiber* fiber)
{
  /* ASSERT: we own victim->lock */

  full_frame *parent_ff, *child_ff, *loot_ff;
  __cilkrts_stack_frame *volatile *h;
  __cilkrts_stack_frame *sf;

  w->l->team = victim->l->team;

  CILK_ASSERT(w->l->frame_ff == 0 || w == victim);

  h = victim->head;

  CILK_ASSERT(*h);

  victim->head = h + 1;

  parent_ff = victim->l->frame_ff;
  BEGIN_WITH_FRAME_LOCK(w, parent_ff) {
    /* parent no longer referenced by victim */
    decjoin(parent_ff);

    /* obtain the victim call stack */
    sf = *h;

    /* perform system-dependent normalizations */
    /*__cilkrts_normalize_call_stack_on_steal(sf);*/

    /* unroll PARENT_FF with call stack SF, adopt the youngest
       frame LOOT.  If loot_ff == parent_ff, then we hold loot_ff->lock,
       otherwise, loot_ff is newly created and we can modify it without
       holding its lock. */
    loot_ff = unroll_call_stack(w, parent_ff, sf);

    if (WORKER_USER == victim->l->type &&
	NULL == victim->l->last_full_frame) {
      // Mark this looted frame as special: only the original user worker
      // may cross the sync.
      // 
      // This call is a shared access to
      // victim->l->last_full_frame.
      set_sync_master(victim, loot_ff);
    }

    /* LOOT is the next frame that the thief W is supposed to
       run, unless the thief is stealing from itself, in which
       case the thief W == VICTIM executes CHILD and nobody
       executes LOOT. */
    if (w == victim) {
      /* Pretend that frame has been stolen */
      loot_ff->call_stack->flags |= CILK_FRAME_UNSYNCHED;
      loot_ff->simulated_stolen = 1;
    }
    else
      __cilkrts_push_next_frame(w, loot_ff);

    // After this "push_next_frame" call, w now owns loot_ff.
    child_ff = make_child(w, loot_ff, 0, fiber);

    BEGIN_WITH_FRAME_LOCK(w, child_ff) {
      /* install child in the victim's work queue, taking
	 the parent_ff's place */
      /* child is referenced by victim */
      incjoin(child_ff);

      // With this call, w is bestowing ownership of the newly
      // created frame child_ff to the victim, and victim is
      // giving up ownership of parent_ff.
      //
      // Worker w will either take ownership of parent_ff
      // if parent_ff == loot_ff, or parent_ff will be
      // suspended.
      //
      // Note that this call changes the victim->frame_ff
      // while the victim may be executing.
      make_runnable(victim, child_ff);
    } END_WITH_FRAME_LOCK(w, child_ff);
  } END_WITH_FRAME_LOCK(w, parent_ff);
}

static void random_steal(__cilkrts_worker *w)
{
  __cilkrts_worker *victim = NULL;
  cilk_fiber *fiber = NULL;
  int n;
  int success = 0;
  int32_t victim_id;

  // Nothing's been stolen yet. When true, this will flag
  // setup_for_execution_pedigree to increment the pedigree
  w->l->work_stolen = 0;

  /* If the user has disabled stealing (using the debugger) we fail */
  if (__builtin_expect(w->g->stealing_disabled, 0))
    return;

  CILK_ASSERT(w->l->type == WORKER_SYSTEM || w->l->team == w);

  /* If there is only one processor work can still be stolen.
     There must be only one worker to prevent stealing. */
  CILK_ASSERT(w->g->total_workers > 1);

  /* pick random *other* victim */
  n = myrand(w) % (w->g->total_workers - 1);
  if (n >= w->self)
    ++n;

  // If we're replaying a log, override the victim.  -1 indicates that
  // we've exhausted the list of things this worker stole when we recorded
  // the log so just return.  If we're not replaying a log,
  // replay_get_next_recorded_victim() just returns the victim ID passed in.
  n = replay_get_next_recorded_victim(w, n);
  if (-1 == n)
    return;

  victim = w->g->workers[n];

    
  /* Verify that we can get a stack.  If not, no need to continue. */
  fiber = cilk_fiber_allocate(&w->l->fiber_pool);
    


  if (NULL == fiber) {
#if FIBER_DEBUG >= 2
    fprintf(stderr, "w=%d: failed steal because we could not get a fiber\n",
	    w->self);
#endif        
    return;
  }

  /* do not steal from self */
  CILK_ASSERT (victim != w);

  /* Execute a quick check before engaging in the THE protocol.
     Avoid grabbing locks if there is nothing to steal. */
  if (!can_steal_from(victim)) {
        
        
    int ref_count = cilk_fiber_remove_reference(fiber, &w->l->fiber_pool);
    // Fibers we use when trying to steal should not be active,
    // and thus should not have any other references.
    CILK_ASSERT(0 == ref_count);
        
    return;
  }
    
  /* Attempt to steal work from the victim */
  if (worker_trylock_other(w, victim)) {
    if (w->l->type == WORKER_USER && victim->l->team != w) {

      // Fail to steal if this is a user worker and the victim is not
      // on this team.  If a user worker were allowed to steal work
      // descended from another user worker, the former might not be
      // done with its work by the time it was needed to resume and
      // unbind.  Therefore, user workers are not permitted to change
      // teams.

      // There is no race on the victim's team because the victim cannot
      // change its team until it runs out of work to do, at which point
      // it will try to take out its own lock, and this worker already
      // holds it.
            

    } else if (victim->l->frame_ff) {
      // A successful steal will change victim->frame_ff, even
      // though the victim may be executing.  Thus, the lock on
      // the victim's deque is also protecting victim->frame_ff.
      if (dekker_protocol(victim)) {
	int proceed_with_steal = 1; // optimistic

	// If we're replaying a log, verify that this the correct frame
	// to steal from the victim
	if (! replay_match_victim_pedigree(w, victim))
	  {
	    // Abort the steal attempt. decrement_E(victim) to
	    // counter the increment_E(victim) done by the
	    // dekker protocol
	    decrement_E(victim);
	    proceed_with_steal = 0;
	  }

	if (proceed_with_steal)
	  {
                    
	    success = 1;
	    detach_for_steal(w, victim, fiber);
	    victim_id = victim->self;

#if REDPAR_DEBUG >= 1
	    fprintf(stderr, "Wkr %d stole from victim %d, fiber = %p\n",
		    w->self, victim->self, fiber);
#endif

	    // The use of victim->self contradicts our
	    // classification of the "self" field as 
	    // local.  But since this code is only for
	    // debugging, it is ok.
	    DBGPRINTF ("%d-%p: Stealing work from worker %d\n"
		       "            sf: %p, call parent: %p\n",
		       w->self, GetCurrentFiber(), victim->self,
		       w->l->next_frame_ff->call_stack,
		       w->l->next_frame_ff->call_stack->call_parent);
                    
	  }  // end if(proceed_with_steal)
      }
    }
    worker_unlock_other(w, victim);
  }

  // Record whether work was stolen.  When true, this will flag
  // setup_for_execution_pedigree to increment the pedigree
  w->l->work_stolen = success;

  if (0 == success) {
    // failed to steal work.  Return the fiber to the pool.
        
    int ref_count = cilk_fiber_remove_reference(fiber, &w->l->fiber_pool);
    // Fibers we use when trying to steal should not be active,
    // and thus should not have any other references.
    CILK_ASSERT(0 == ref_count);
        
  }
  else
    {
      // Since our steal was successful, finish initialization of
      // the fiber.
      cilk_fiber_reset_state(fiber,
			     fiber_proc_to_resume_user_code_for_random_steal);
      // Record the pedigree of the frame that w has stolen.
      // record only if CILK_RECORD_LOG is set
      replay_record_steal(w, victim_id);
    }
}

/**
 * A single "check" to find work, either on our queue or through a
 * steal attempt.  This method checks our local queue once, and
 * performs one steal attempt.
 */
static full_frame* check_for_work(__cilkrts_worker *w)
{
  full_frame *ff = NULL;
  ff = pop_next_frame(w);
  // If there is no work on the queue, try to steal some.
  if (NULL == ff) {
        
    if (w->l->type != WORKER_USER && w->l->team != NULL) {
      // At this point, the worker knows for certain that it has run
      // out of work.  Therefore, it loses its team affiliation.  User
      // workers never change teams, of course.
      __cilkrts_worker_lock(w);
      w->l->team = NULL;
      __cilkrts_worker_unlock(w);
    }

    // If we are about to do a random steal, we should have no
    // full frame...
    CILK_ASSERT(NULL == w->l->frame_ff);
    random_steal(w);
        

    // If the steal was successful, then the worker has populated its next
    // frame with the work to resume.
    ff = pop_next_frame(w);
    if (NULL == ff) {
      // Punish the worker for failing to steal.
      // No quantum for you!
      unsigned int max_fails = w->g->max_steal_failures << 1;
      if (w->l->has_stolen == 0 &&
	  w->l->steal_failure_count % max_fails == max_fails - 1) {
	// Idle briefly if the worker has never stolen anything for
	// the given grace period
	__cilkrts_idle();
      } else {
	__cilkrts_yield();
      }
      w->l->steal_failure_count++;
      if (w->l->steal_failure_count > (max_fails << 8)) {
	// Reset the flag after certain amount of failures
	// - This will reduce cpu time in top-level synched regions
	// - max_fails can be controlled by user (CILK_STEAL_FAILURES)
	w->l->has_stolen = 0;
      }
    } else {
      // Reset steal_failure_count since there is obviously still work to
      // be done.
      w->l->steal_failure_count = 0;
      w->l->has_stolen = 1;
    }
  }
  return ff;
}

/**
 * Keep stealing or looking on our queue.
 *
 * Returns either when a full frame is found, or NULL if the
 * computation is done.
 */ 
static full_frame* search_until_work_found_or_done(__cilkrts_worker *w)
{
  full_frame *ff = NULL;
  // Find a full frame to execute (either through random stealing,
  // or because we pull it off w's 1-element queue).
  while (!ff) {
    // Check worker state to figure out our next action.
    switch (worker_runnable(w))    
      {
      case SCHEDULE_RUN:             // One attempt at checking for work.
	ff = check_for_work(w);
	break;
      case SCHEDULE_WAIT:            // go into wait-mode.
            
	CILK_ASSERT(WORKER_SYSTEM == w->l->type);
	// If we are about to wait, then we better not have
	// a frame that we should execute...
	CILK_ASSERT(NULL == w->l->next_frame_ff);
	notify_children_wait(w);
	signal_node_wait(w->l->signal_node);
	// ...
	// Runtime is waking up.
	notify_children_run(w);
	w->l->steal_failure_count = 0;
            
	break;
      case SCHEDULE_EXIT:            // exit the scheduler.
	CILK_ASSERT(WORKER_USER != w->l->type);
	return NULL;
      default:
	CILK_ASSERT(0);
	abort();
      }
  }
  return ff;
}

/**
 * The body of the runtime scheduling loop.  This function executes in
 * 4 stages:
 *
 * 1. Transitions from the user code into the runtime by
 *    executing any scheduling-stack functions.
 * 2. Looks for a full frame enqueued from a successful provably
 *    good steal.
 * 3. If no full frame is found in step 2, steal until
 *    a frame is found or we are done.  If we are done, finish
 *    the scheduling loop. 
 * 4. When a frame is found, setup to resume user code.
 *    In particular, suspend the current fiber and resume the
 *    user fiber to execute the frame.
 *
 * Returns a fiber object that we should switch to after completing
 * the body of the loop, or NULL if we should continue executing on
 * this fiber.
 *
 * @pre @c current_fiber should equal @c wptr->l->scheduling_fiber
 * 
 * @param current_fiber   The currently executing (scheduling_ fiber
 * @param wptr            The currently executing worker.
 * @param return          The next fiber we should switch to.
 */
static cilk_fiber* worker_scheduling_loop_body(cilk_fiber* current_fiber,
                                               void* wptr)
{
  __cilkrts_worker *w = (__cilkrts_worker*) wptr;
  CILK_ASSERT(current_fiber == w->l->scheduling_fiber);

  // Stage 1: Transition from executing user code to the runtime code.
  // We don't need to do this call here any more, because 
  // every switch to the scheduling fiber should make this call
  // using a post_switch_proc on the fiber.
  //
  //  enter_runtime_transition_proc(w->l->scheduling_fiber, wptr);

  // After Stage 1 is complete, w should no longer have
  // an associated full frame.
  CILK_ASSERT(NULL == w->l->frame_ff);

  // Stage 2.  First do a quick check of our 1-element queue.
  full_frame *ff = pop_next_frame(w);

  if (!ff) {
    // Stage 3.  We didn't find anything from our 1-element
    // queue.  Now go through the steal loop to find work. 
    ff = search_until_work_found_or_done(w);
    if (!ff) {
      CILK_ASSERT(w->g->work_done);
      return NULL;
    }
  }

  // Stage 4.  Now that we have found a full frame to work on,
  // actually execute it.
  __cilkrts_stack_frame *sf;

  // There shouldn't be any uncaught exceptions.
  //
  // In Windows, the OS catches any exceptions not caught by the
  // user code.  Thus, we are omitting the check on Windows.
  //
  // On Android, calling std::uncaught_exception with the stlport
  // library causes a seg fault.  Since we're not supporting
  // exceptions there at this point, just don't do the check
  CILKBUG_ASSERT_NO_UNCAUGHT_EXCEPTION();

  BEGIN_WITH_WORKER_LOCK(w) {
    CILK_ASSERT(!w->l->frame_ff);
    BEGIN_WITH_FRAME_LOCK(w, ff) {
      sf = ff->call_stack;
      CILK_ASSERT(sf && !sf->call_parent);
      setup_for_execution(w, ff, 0);
    } END_WITH_FRAME_LOCK(w, ff);
  } END_WITH_WORKER_LOCK(w);

  /* run it */
  //
  // Prepare to run the full frame.  To do so, we need to:
  //   (a) Execute some code on this fiber (the scheduling
  //       fiber) to set up data structures, and
  //   (b) Suspend the scheduling fiber, and resume the
  //       user-code fiber.

  // Part (a). Set up data structures.
  scheduling_fiber_prepare_to_resume_user_code(w, ff, sf);

  cilk_fiber *other = w->l->frame_ff->fiber_self;
  cilk_fiber_data* other_data = cilk_fiber_get_data(other);
  cilk_fiber_data* current_fiber_data = cilk_fiber_get_data(current_fiber);

  // I believe two cases are possible here, both of which
  // should have other_data->resume_sf as NULL.
  //
  // 1. Resuming a fiber that was previously executing
  //    user code (i.e., a provably-good-steal).
  //    In this case, resume_sf should have been
  //    set to NULL when it was suspended.
  //
  // 2. Resuming code on a steal.  In this case, since we
  //    grabbed a new fiber, resume_sf should be NULL.
  CILK_ASSERT(NULL == other_data->resume_sf);
        
#if FIBER_DEBUG >= 2
  fprintf(stderr, "W=%d: other fiber=%p, setting resume_sf to %p\n",
	  w->self, other, other_data->resume_sf);
#endif
  // Update our own fiber's data.
  current_fiber_data->resume_sf = NULL;
  // The scheduling fiber should have the right owner from before.
  CILK_ASSERT(current_fiber_data->owner == w);
  other_data->resume_sf = sf;
        

#if FIBER_DEBUG >= 3
  fprintf(stderr, "ThreadId=%p (about to suspend self resume other), W=%d: current_fiber=%p, other=%p, current_fiber->resume_sf = %p, other->resume_sf = %p\n",
	  cilkos_get_current_thread_id(),
	  w->self,
	  current_fiber, other,
	  current_fiber_data->resume_sf,
	  other_data->resume_sf);
#endif
  return other;
}

/**
 * The main scheduler function executed by a worker's scheduling
 * fiber.
 * 
 * This method is started by either a new system worker, or a user
 * worker that has stalled and just been imported into the runtime.
 */
static void worker_scheduler_function(__cilkrts_worker *w)
{
    
  worker_scheduler_init_function(w);
    
    
  // The main scheduling loop body.

  while (!w->g->work_done) {    
    // Execute the "body" of the scheduling loop, and figure
    // out the fiber to jump to next.
        
    cilk_fiber* fiber_to_resume
      = worker_scheduling_loop_body(w->l->scheduling_fiber, w);
        
        
    if (fiber_to_resume) {
      // Suspend the current fiber and resume next one.
            

      // Whenever we jump to resume user code, we stop being in
      // the runtime, and start working.
            
            
      cilk_fiber_suspend_self_and_resume_other(w->l->scheduling_fiber,
					       fiber_to_resume);
      // Return here only when this (scheduling) fiber is
      // resumed (i.e., this worker wants to reenter the runtime).

      // We've already switched from WORKING to IN_RUNTIME in
      // the runtime code that handles the fiber switch.  Thus, at
      // this point we are IN_RUNTIME already.
    }
  }

  // Finish the scheduling loop.
  worker_scheduler_terminate_function(w);
}
