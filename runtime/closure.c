/*************************************************************
 * Basic operations on closures
 *************************************************************/

static inline void 
Closure_checkmagic(struct __cilkrts_worker *const UNUSED(ws), 
                   Closure *UNUSED(t)) {
    CILK_ASSERT(ws, t->magic == CILK_CLOSURE_MAGIC);
}

static inline void 
Closure_clean(struct __cilkrts_worker *const ws, Closure *t) {

    // sanity checks
    CILK_ASSERT(ws, t->left_sib == (Closure *)NULL);
    CILK_ASSERT(ws, t->right_sib == (Closure *)NULL);
    CILK_ASSERT(ws, t->right_most_child == (Closure *)NULL);

    // it's possible for user_rmap to be INVALID_RMAP in parallel reduce, 
    // because it was a temp place holder for rmap from the right sibling 
    // during closure return
    CILK_ASSERT(ws, t->user_rmap == (RMapPageDesc *)NULL || 
                    (PARALLEL_REDUCE && t->user_rmap == INVALID_RMAP)); 
    CILK_ASSERT(ws, t->child_rmap == (RMapPageDesc *)NULL);
    CILK_ASSERT(ws, t->right_sib_rmap == INVALID_RMAP ||
                    t->right_sib_rmap == (RMapPageDesc *)NULL); 

    int pd_array_size = Closure_get_pd_array_size(ws, t); 

    if(pd_array_size > 2) {
        size_t num_bytes = pd_array_size * sizeof(Page);
        Cilk_internal_free(ws, t->pds.pd_ptr, num_bytes);
    }

    Cilk_mutex_destroy(ws->g, &t->mutex);
}

/* ANGE: destroy the closure and internally free it (put back to global
 * pool)
 */
static void 
Closure_destroy(struct __cilkrts_worker *const ws, Closure *t) {

    Closure_checkmagic(ws, t);

    WHEN_CILK_DEBUG(t->magic = ~CILK_CLOSURE_MAGIC);
    CILK_ASSERT(ws, ! (t->malloced));
    Closure_clean(ws, t);
   
    WHEN_COUNTING_STACKS({
        CILK_ASSERT(ws, t->stack == 0);
        CILK_ASSERT(ws, t->stack_before_spawn == 0);
        CILK_ASSERT(ws, t->stack_before_spawn_is_used_by_child == 0);
    });

    Cilk_internal_free(ws, t, sizeof(Closure));
}

/* ANGE: destroy the closure and _really_ free it (call system free) */
/* This is called for destroying the very first closure (invoke_main), since
 * at the time when the closure is created, all workers are still sleeping
 * and the closure is created by the main thread
 */
static void 
Closure_destroy_malloc(struct __cilkrts_worker *const ws, Closure *t) {

    Closure_checkmagic(ws, t);

    WHEN_CILK_DEBUG(t->magic = ~CILK_CLOSURE_MAGIC);
    CILK_ASSERT(ws, (t->malloced));
    Closure_clean(ws, t);

    WHEN_COUNTING_STACKS({
        CILK_ASSERT(ws, t->stack == 1);  // the very first closure
        CILK_ASSERT(ws, t->stack_before_spawn == 1);
        CILK_ASSERT(ws, t->stack_before_spawn_is_used_by_child == 0);
    });

    Cilk_free(t);
}

static inline int
Closure_trylock(struct __cilkrts_worker *const ws, Closure *t) {

    Closure_checkmagic(ws, t);
    int ret = Cilk_mutex_try(ws->g, &(t->mutex)); 
    WHEN_CILK_DEBUG(
        if(ret) {
            t->mutex_owner = ws->self;
        }
    )
    return ret;
}

#if DEBUG_VERBOSE
/* ANGE: These functions are useful when doing performance debugging -- it
 * prints out messages when the Closure lock that the worker is trying to 
 * acquire is contented.  
 */
static inline void
Closure_lock_log(struct __cilkrts_worker *const ws, Closure *t, int my_action) {

    if( Closure_trylock(ws, t) == 0 ) {
        print_closure_lock_blocked_usage(ws, t, my_action, NULL);
        /* try lock failed; now wait. */
        Cilk_mutex_wait(ws->g, ws, &(t->mutex));
        print_closure_lock_unblocked_usage(ws, t, my_action, NULL);
    }

    /* have gotten the lock at this point */
    CILK_ASSERT(ws, t->mutex_action == NONE);
    WHEN_CILK_DEBUG( t->mutex_owner = ws->self; )
    t->mutex_action = my_action;
}
#endif

static inline void
Closure_lock(struct __cilkrts_worker *const ws, Closure *t) {

    Closure_checkmagic(ws, t);
    Cilk_mutex_wait(ws->g, ws, &(t->mutex));
    WHEN_CILK_DEBUG(t->mutex_owner = ws->self);
}

static inline void 
Closure_unlock(struct __cilkrts_worker *const ws, Closure *t) {

    Closure_checkmagic(ws, t);
    Closure_assert_ownership(ws, t);
    WHEN_DEBUG_VERBOSE(t->mutex_action = NONE);
    WHEN_CILK_DEBUG(t->mutex_owner = NOBODY);
    Cilk_mutex_signal(ws->g, &(t->mutex));
}

/********************************************
 * Closure management
 ********************************************/

// need to be careful when calling this function --- we check whether a
// frame is set stolen (i.e., has a full frame associated with it), but note
// that the setting of this can be delayed.  A thief can steal a spawned
// frame, but it cannot fully promote it until it remaps its TLMM stack,
// because the flag field is stored in the frame on the TLMM stack.  That
// means, a frame can be stolen, in the process of being promoted, and
// mean while, the stolen flag is not set until finish_promote.
static inline int 
Closure_at_top_of_stack(struct __cilkrts_worker *const ws) {

    return( WS_HEAD(ws) == WS_TAIL(ws) && 
            __cilkrts_stolen(ws->current_stack_frame) );
}

static inline int Closure_has_children(Closure *cl) {

    return ( cl->has_cilk_callee || cl->join_counter != 0 );
}

static inline void 
Closure_init(struct __cilkrts_global_state *const g, 
             struct __cilkrts_worker *const UNUSED(ws), 
             Closure *new_closure) {

    Cilk_mutex_init(g, &new_closure->mutex);

    new_closure->cp = (Cilk_time) 0;
    new_closure->work = (Cilk_time) 0;

    new_closure->next_ready = (Closure *) NULL;
    new_closure->prev_ready = (Closure *) NULL;
    CLOSURE_ABORT(new_closure) = NO_ABORT;

    new_closure->join_counter = 0;

    new_closure->ances_on_diff_pg = UNSET_CL;
    new_closure->begin_pd_index = UNSET; 
    new_closure->end_pd_index = UNSET; 

    new_closure->has_cilk_callee = 0;
    new_closure->call_parent = (Closure *) NULL;
    new_closure->spawn_parent = (Closure *) NULL;
    new_closure->callee = (Closure *) NULL;
    new_closure->frame = (struct __cilkrts_stack_frame *) NULL;
    new_closure->frame_rsp = UNSET_ADDR;
    new_closure->assumed_frame_rsp = UNSET_ADDR;
    new_closure->assumed_extra_pd = UNSET;

    new_closure->child_rmap = (RMapPageDesc *) NULL;
    new_closure->right_sib_rmap = (RMapPageDesc *) NULL;
    new_closure->user_rmap = (RMapPageDesc *) NULL;
    
    new_closure->left_sib = (Closure *) NULL;
    new_closure->right_sib = (Closure *) NULL;
    new_closure->right_most_child = (Closure *) NULL;

    WHEN_COUNTING_STACKS({
        new_closure->stack = 0;
        new_closure->stack_before_spawn = 0;    
        new_closure->stack_before_spawn_is_used_by_child = 0;    
    });

    WHEN_CILK_DEBUG({
        new_closure->magic = CILK_CLOSURE_MAGIC;
        new_closure->owner_ready_deque = NOBODY;
        new_closure->mutex_owner = NOBODY;
    });

    Cilk_event(ws, EVENT_CLOSURE_CREATE);
}

static Closure *Closure_create(struct __cilkrts_worker *const ws) {

    Closure *new_closure = (Closure *)Cilk_internal_malloc(ws, sizeof(Closure));
    CILK_CHECK(new_closure, (ws->g, ws, "can't allocate closure\n"));
    WHEN_CILK_DEBUG(new_closure->malloced = 0;)
    WHEN_DEBUG_VERBOSE(new_closure->mutex_action = NONE;)
    WHEN_DEBUG_VERBOSE( Cilk_dprintf(ws, "Create new cl %lx.\n", new_closure); )

    Closure_init(ws->g, ws, new_closure);

    return new_closure;
}

/*
 * ANGE: Not used anymore
static Closure *
Closure_create_and_suspend(__cilkrts_worker *const ws, __cilkrts_stack_frame *frame) {

    CILK_ASSERT(ws, frame != NULL);
    CILK_ASSERT(ws, frame->magic == CILK_STACKFRAME_MAGIC);

    Closure *new = Cilk_internal_malloc(ws, sizeof(Closure));
    CILK_CHECK(new, (ws->g, ws, "can't allocate closure\n"));
    WHEN_CILK_DEBUG(new->malloced = 0;)
    WHEN_CILK_DEBUG(new->mutex_action = NONE;)

    Closure_init(ws->g,ws, new);
    new->status = CLOSURE_SUSPENDED;
    new->frame = frame;

    return new;
} */

/* 
 * ANGE: same thing as Cilk_Closure_create, except this function uses system
 *       malloc, while Cilk_Closure_create uses internal_malloc.
 *       This seems to be used only for create_initial_thread from
 *       inovke-main.c.
 */
Closure *Cilk_Closure_create_malloc(struct __cilkrts_global_state *const g, 
                                    struct __cilkrts_worker *const ws) {

    Closure *new_closure = (Closure *) Cilk_malloc(sizeof(Closure));
    CILK_CHECK(new_closure, (g, ws, "can't allocate closure\n"));
    WHEN_CILK_DEBUG(new_closure->malloced = 1;)

    Closure_init(g, ws, new_closure);

    return new_closure;
}

// double linking left and right; the right is always the new child
// Note that we must have the lock on the parent when invoking this function
static inline void 
double_link_children(Closure *left, Closure *right) {
      
    if(left) {
        CILK_ASSERT_TLS(left->right_sib == (Closure *) NULL);
        left->right_sib = right;
    }

    if(right) {
        CILK_ASSERT_TLS(right->left_sib == (Closure *) NULL);
        right->left_sib = left;
    }
}

// unlink the closure from its left and right siblings
// Note that we must have the lock on the parent when invoking this function
static inline void unlink_child(Closure *cl) {

    if(cl->left_sib) {
        CILK_ASSERT_TLS(cl->left_sib->right_sib == cl);
        cl->left_sib->right_sib = cl->right_sib;
    }
    if(cl->right_sib) {
        CILK_ASSERT_TLS(cl->right_sib->left_sib == cl);
        cl->right_sib->left_sib = cl->left_sib;
    }
    WHEN_CILK_DEBUG({ // used only for error checking
        cl->left_sib = (Closure *) NULL;
        cl->right_sib = (Closure *) NULL;
    })
}

/*** 
 * Only the scheduler is allowed to alter the closure tree.  
 * Consequently, these operations are private.
 *
 * Insert the newly created child into the closure tree.
 * The child closure is newly created, which makes it the new right
 * most child of parent.  Setup the left/right sibling for this new 
 * child, and reset the parent's right most child pointer.  
 * 
 * Note that we don't need locks on the children to double link them.
 * The old right most child will not follow its right_sib link until
 * it's ready to return, and it needs lock on the parent to do so, which
 * we are holding.  The pointer to new right most child is not visible
 * to anyone yet, so we don't need to lock that, either.  
 ***/
static void Closure_add_child(struct __cilkrts_worker *const ws,
                              Closure *parent, Closure *child) {

    Closure_checkmagic(ws, parent);
    /* ANGE: ws must have the lock on parent */
    Closure_assert_ownership(ws, parent);
    Closure_checkmagic(ws, child);
    /* ANGE: ws must NOT have the lock on child */
    Closure_assert_alienation(ws, child);

    // setup sib links between parent's right most child and the new child
    double_link_children(parent->right_most_child, child);
    // now the new child becomes the right most child
    parent->right_most_child = child;

    if(child->left_sib) {
        child->loc_to_deposit_rmap = &(child->left_sib->right_sib_rmap);
    } else { // no left sib; this is the left most child
        child->loc_to_deposit_rmap = &(parent->child_rmap);
    }
}

/***
 * Remove the child from the closure tree.
 * At this point we should already have reduced all rmaps that this
 * child has.  We need to unlink it from its left/right sibling, and reset
 * the right most child pointer in parent if this child is currently the 
 * right most child.  
 * 
 * Note that we need locks both on the parent and the child.
 * We always hold lock on the parent when unlinking a child, so only one
 * child gets unlinked at a time, and one child gets to modify the steal
 * tree at a time.  
 ***/
static void Closure_remove_child(struct __cilkrts_worker *const ws,
                                 Closure *parent, Closure *child) {
    CILK_ASSERT(ws, child);
    CILK_ASSERT(ws, parent == child->spawn_parent);
    CILK_ASSERT(ws, child->right_sib_rmap == (RMapPageDesc*)NULL || 
                    child->right_sib_rmap == INVALID_RMAP);

    Closure_assert_ownership(ws, parent);
    Closure_assert_ownership(ws, child);

    if( child == parent->right_most_child ) {
        CILK_ASSERT(ws, child->right_sib == (Closure *)NULL);
        parent->right_most_child = child->left_sib;
    }

    CILK_ASSERT( ws, (child->left_sib &&  
            child->loc_to_deposit_rmap == &child->left_sib->right_sib_rmap) ||
            child->loc_to_deposit_rmap == &parent->child_rmap );

    if(child->right_sib) {
        Closure_lock(ws, child->right_sib);
        child->right_sib->loc_to_deposit_rmap = child->loc_to_deposit_rmap;
        Closure_unlock(ws, child->right_sib);
    }
    unlink_child(child);
}


/*** 
 * This function is called during promote_child, when we know we have multiple 
 * frames in the stacklet, but we can't promote them yet, because the thief
 * has yet to remap its stack, so we can't access the fields in the frames.
 * We create a new closure for the new spawn_parent, and temporarily use
 * that to represent all frames in between the new spawn_parent and the 
 * old closure on top of the victim's deque.  In case where some other child
 * of the old closure returns, it needs to know that the old closure has
 * outstanding call children, so it won't resume the suspended old closure
 * by mistake.
 ***/ 
static void Closure_add_temp_callee(struct __cilkrts_worker *const ws, 
                                    Closure *caller, Closure *callee) {
    CILK_ASSERT(ws, !(caller->has_cilk_callee));
    CILK_ASSERT(ws, callee->spawn_parent == NULL);

    callee->call_parent = caller;
    caller->has_cilk_callee = 1;
}

static void Closure_add_callee(struct __cilkrts_worker *const ws, 
                               Closure *caller, Closure *callee) {
    CILK_ASSERT(ws, callee->frame->call_parent == caller->frame);
    // CILK_ASSERT(ws, callee->frame->debug_call_parent == caller->frame);

    // ANGE: instead of checking has_cilk_callee, we just check if callee is
    // NULL, because we might have set the has_cilk_callee in
    // Closure_add_tmp_callee to prevent the closure from being resumed.
    CILK_ASSERT(ws, caller->callee == NULL);
    CILK_ASSERT(ws, callee->spawn_parent == NULL);
    CILK_ASSERT(ws, (callee->frame->flags & CILK_FRAME_DETACHED) == 0);

    callee->call_parent = caller;
    caller->callee = callee;
    caller->has_cilk_callee = 1;
}

static void Closure_remove_callee(struct __cilkrts_worker *const ws,
                                                       Closure *caller) {

    // A child is not double linked with siblings if it is called
    // so there is no need to unlink it.  
    CILK_ASSERT(ws, caller->status == CLOSURE_SUSPENDED);
    CILK_ASSERT(ws, caller->has_cilk_callee);
    caller->has_cilk_callee = 0;
    caller->callee = NULL;
}
