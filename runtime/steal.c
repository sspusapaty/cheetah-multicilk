/***********************************************************
 *         Work-stealing and related functions
 ***********************************************************/
/* 
 * For stealing we use a Dekker-like protocol, that achieves
 * mutual exclusion using shared memory.
 *
 * (Modified by ANGE)
 *
 * Thief:
 *   lock
 *   E++          ; signal intention to steal, sending an exception
 *   (implicit MEMBAR)
 *   If (H+1 >= T)  ; if we cannot steal
 *     E--        ; retract the exception
 *     give up
 *   else steal (that is, H++, etc)
 *   unlock
 *
 * The victim does the normal exception-handling mechanism:
 *
 *   T--
 *   --MEMBAR-- ensure T write occurs before E is read.
 *   if (E>=T)
 *     
 *
 */

/*
 * Do the thief part of Dekker's protocol.  Return 1 upon success,
 * 0 otherwise.  The protocol fails when the victim already popped
 * T so that E=T.
 */
static int do_dekker_on(struct __cilkrts_worker *const ws, 
                        struct __cilkrts_worker *const victim_ws, 
                        Closure *cl) {

    Closure_assert_ownership(ws, cl);

    increment_exception_pointer(ws, victim_ws, cl);
    Cilk_membar_StoreLoad(); 

    /* 
     * ANGE: the thief won't steal from this victim if there is only one
     * frame on cl's stack
     */
    if(WS_HEAD(victim_ws) >= WS_TAIL(victim_ws)) {
        decrement_exception_pointer(ws, victim_ws, cl);
        return 0;
    }

    return 1;
}

static void Closure_make_ready(Closure *cl) {

    cl->status = CLOSURE_READY;
}

/*
 * suspend protocol 
 */

/*
 * ANGE: ws must have locks on closure and its own deque. 
 * Ws first sets cl from RUNNING to SUSPENDED, then removes closure 
 * cl from the ready deque.  Since this function is called from 
 * promote_child (steal), the thief's stack is not remapped yet, so we can't
 * access the oldest frame nor the fields of the frame.  Hence, this is a
 * separate and distinctly different function from Closure_suspend (which 
 * suspend a closure owned by the worker with appropriate stack mapping). 
 */
static void Closure_suspend_victim(struct __cilkrts_worker *const ws, 
                                   int victim, Closure *cl) {

    Closure *cl1;

    Closure_checkmagic(ws, cl);
    Closure_assert_ownership(ws, cl);
    deque_assert_ownership(ws, victim);

    CILK_ASSERT(ws, cl->status == CLOSURE_RUNNING);
    CILK_ASSERT(ws, cl == USE_PARAMETER_WS(invoke_main) 
                    || cl->spawn_parent || cl->call_parent);

    cl->status = CLOSURE_SUSPENDED;

    Cilk_event(ws, EVENT_SUSPEND);
    cl1 = deque_xtract_bottom(ws, victim);
    USE_UNUSED(cl1); /* prevent warning when ASSERT is defined as nothing */

    CILK_ASSERT(ws, cl == cl1);
}

static void Closure_suspend(struct __cilkrts_worker *const ws, Closure *cl) {

    Closure *cl1;

    Closure_checkmagic(ws, cl);
    Closure_assert_ownership(ws, cl);
    deque_assert_ownership(ws, ws->self);

    CILK_ASSERT(ws, cl->status == CLOSURE_RUNNING);
    CILK_ASSERT(ws, cl == USE_PARAMETER_WS(invoke_main) 
                    || cl->spawn_parent || cl->call_parent);
    CILK_ASSERT(ws, cl->frame != NULL);
    CILK_ASSERT(ws, __cilkrts_stolen(cl->frame));
    CILK_ASSERT(ws, cl->frame->worker->self == ws->self);

    cl->status = CLOSURE_SUSPENDED;
    cl->frame->worker = (struct __cilkrts_worker *) NOBODY;

    Cilk_event(ws, EVENT_SUSPEND);
    cl1 = deque_xtract_bottom(ws, ws->self);
    USE_UNUSED(cl1); /* prevent warning when ASSERT is defined as nothing */

    CILK_ASSERT(ws, cl == cl1);
}

/* 
 * ANGE: Cannot call this function until we have done the stack remapping
 * for the stolen closure.
 * This return the oldest frame in stacklet that has not been promoted to
 * full frame (i.e., never been stolen), or the closest detached frame 
 * if nothing in this stacklet has been promoted. 
 */
static inline struct __cilkrts_stack_frame * 
oldest_non_stolen_frame_in_stacklet(struct __cilkrts_stack_frame *head) {

    struct __cilkrts_stack_frame *cur = head;
    while(cur && (cur->flags & CILK_FRAME_DETACHED) == 0 && 
          cur->call_parent && __cilkrts_stolen(cur->call_parent) == 0) {
        cur = cur->call_parent;
    }

    return cur;
}

/* 
 * ANGE: Cannot call this function until we have done the stack remapping
 * for the stolen closure 
 */
static Closure *
setup_call_parent_closure_helper(struct __cilkrts_worker *const ws, 
                                 struct __cilkrts_worker *const victim_ws, 
                                 struct __cilkrts_stack_frame *frame,
                                 Closure *oldest) {

    Closure *call_parent, *curr_cl;

    if(oldest->frame == frame) {
        CILK_ASSERT(ws, __cilkrts_stolen(oldest->frame));
        return oldest;
    }
 
    call_parent = setup_call_parent_closure_helper(ws, victim_ws, 
                                                   frame->call_parent, oldest);
    __cilkrts_set_stolen(frame);
    curr_cl = Closure_create(ws);    
    curr_cl->frame = frame;

    CILK_ASSERT(ws, frame->worker == victim_ws); 
    curr_cl->status = CLOSURE_SUSPENDED;
    curr_cl->frame->worker = (struct __cilkrts_worker *) NOBODY;
    
    Closure_add_callee(ws, call_parent, curr_cl);

    return curr_cl;
}

/* 
 * ANGE: Cannot call this function until we have done the stack remapping
 * for the stolen closure 
 */
static void
setup_closures_in_stacklet(struct __cilkrts_worker *const ws, 
                           struct __cilkrts_worker *const victim_ws, 
                           Closure *youngest_cl) {

    Closure *call_parent;
    Closure *oldest_cl = youngest_cl->call_parent;
    struct __cilkrts_stack_frame *youngest, *oldest;  

    youngest = youngest_cl->frame; 
    oldest = oldest_non_stolen_frame_in_stacklet(youngest);

    CILK_ASSERT(ws, youngest == youngest_cl->frame);
    CILK_ASSERT(ws, youngest->worker == victim_ws); 
    CILK_ASSERT(ws, __cilkrts_not_stolen(youngest));
    CILK_ASSERT(ws, youngest_cl->frame_rsp == UNSET_ADDR);

    WHEN_CILK_DEBUG({
        struct __cilkrts_stack_frame *f = youngest;
        while(f != oldest) {
            CILK_ASSERT(ws, (f->flags & CILK_FRAME_DETACHED) == 0);
            CILK_ASSERT(ws, __cilkrts_not_stolen(f->call_parent));
            f = f->call_parent;
        }
    });
    
    CILK_ASSERT( ws, 
        (oldest_cl->frame == (struct __cilkrts_stack_frame *) NULL &&
            oldest != youngest) || 
        (oldest_cl->frame == oldest->call_parent &&
            __cilkrts_stolen(oldest_cl->frame)) );

    if( oldest_cl->frame == (struct __cilkrts_stack_frame *) NULL ) {
        CILK_ASSERT(ws, __cilkrts_not_stolen(oldest));
        CILK_ASSERT(ws, oldest->flags & CILK_FRAME_DETACHED);
        __cilkrts_set_stolen(oldest); 
        oldest_cl->frame = oldest;
    }
    CILK_ASSERT(ws, oldest->worker == victim_ws); 
    oldest_cl->frame->worker = (struct __cilkrts_worker *) NOBODY;

    call_parent = setup_call_parent_closure_helper(ws, 
                        victim_ws, youngest->call_parent, oldest_cl);
    __cilkrts_set_stolen(youngest);

    Closure_add_callee(ws, call_parent, youngest_cl);

}


/***
 * ANGE: this function guess the low watermark for which the thief needs to
 * map in order to include the stolen frame; 
 * Now that we allocate shadow frame on the stack instead of in the heap, a
 * thief stealing the frame cannot read the frame content and therefore have
 * no idea how far the stack frame extends.  Since we don't want to map while
 * holding the deque lock, the thief must guess how far down the frame
 * extends in the stack, and gather enough information from the victim
 * before releasing the lock on victim's deque.  This function guesses the
 * low watermark for the stack frame by checking either the frame addr at H+1
 * on victim's deque, the victim current stack frame (if only one entry 
 * left on deque), or the total number of pages in victim's stack (last
 * resort). 
 * 
 * Whether *(H+1) is valid can be difficult to tell, since the victim may be
 * pushing / popping frames off the deque at the same time.
 ***/ 
static AddrType 
guess_frame_low_watermark(struct __cilkrts_worker *const ws,
                          struct __cilkrts_worker *const victim_ws,
                          struct __cilkrts_stack_frame *frame) {

    struct __cilkrts_stack_frame **head, **tail; 
    // Get a snapshot of the head / tail; head can't change, but tail may
    head = (struct __cilkrts_stack_frame **) WS_HEAD(victim_ws);
    tail = (struct __cilkrts_stack_frame **) WS_TAIL(victim_ws);
    AddrType mark = UNSET_ADDR;  

    // If H+1 < T, we know for sure that the frame pointer at *(H+1)
    // is a valid entry; use that as a low watermark.
    if( (head + 1) < tail ) {
        mark = (AddrType) *(head + 1);
        CILK_ASSERT(ws, (AddrType) frame > mark);

    } else if( (head + 1) == tail ) {
        // In the case where H+1 == T, since victim always sets its  
        // current_stack_frame to the next executing frame before pushing 
        // the parent (T++), we know reading the current_stack_frame gives 
        // us the right pointer
        mark = (AddrType) victim_ws->current_stack_frame;
        CILK_ASSERT(ws, (AddrType) frame >= mark);

        // Since the victim may be popping, in which case it says its
        // current_stack_frame to the next executing frame before popping
        // the tail (T--), so the current_stack_frame may already be set to 
        // the same frame pointer as the one we are trying to steal.
        if(mark == (AddrType) frame) {
            // *(H+1), or *T in this case, may or may not be valid.  If the 
            // was executing a leaf of the computation tree before it pops
            // the deque, *T could be uninitialized.  To err on the safe 
            // side, just record the entire stack suffix 
            mark = UNSET_ADDR;
        }
    } 

    // If we get to this point with the mark being UNSET, we have either 
    // H == T at this point, or H+1 == T, and the current_stack_frame points 
    // to the same frame as the one we are trying to steal.  
    if(mark == UNSET_ADDR) {
        struct __cilkrts_stack_frame **new_tail;
        new_tail = (struct __cilkrts_stack_frame **) WS_TAIL(victim_ws);

        CILK_ASSERT(ws, head <= new_tail); // victim can't pop beyound head
        
        // check the new T -- victim may be pushing again
        if((head + 1) < new_tail) { 
            mark = (AddrType) *(head + 1);
            CILK_ASSERT(ws, (AddrType) frame > mark);
        } else {
            // last resort -- nothing to refer to as a lower bound; take the
            // entire stack suffix to be safe.
            int num_pgs = (get_stack_map(ws, victim_ws->self))->num_pages;

            // subtract 4 to move it off page boundary
            mark = stack_map_index_to_higher_addr(num_pgs) - 4; 
            CILK_ASSERT(ws, (AddrType) frame > mark);
        }
    }
    CILK_ASSERT(ws, WS_HEAD(victim_ws));
    CILK_ASSERT(ws, (AddrType) frame > mark);

    return mark;
}

/***
 * promote the child frame of parent to a full closure.
 * Detach the parent and return it.
 *
 * Assumptions: the parent is running on victim, and we own
 * the locks of both parent and deque[victim].
 * The child keeps running on the same cache of the parent.
 * The parent's join counter is incremented.
 *
 * In order to promote a child frame to a closure,
 * the parent's frame must be the last in its ready queue.
 *
 * Returns the child. 
 * 
 * ANGE: I don't think this function actually detach the parent.  Someone
 *       calling this function has to do deque_xtract_top on the victim's 
 *       deque to get the parent closure.  This is the only time I can 
 *       think of, where the ready deque contains more than one frame.
 * 
 * ANGE: with shadow frames alloced on the TLMM stack, the thief cannot 
 * read the content of the frames (it's trying to steal) until it remaps 
 * its own TLMM stack accordingly.  But we don't want to remap while holding
 * the deque lock, so the thief must guess the low watermark for the frame,
 * and gather information necessary to remap its TLMM stack before 
 * releasing the lock on victim's deque.  Furthermore, the thief must 
 * delay promoting the frames in the stolen stacklet until it remaps its
 * stack.   
 ***/
static Closure *promote_child(struct __cilkrts_worker *const ws,
                              struct __cilkrts_worker *const victim_ws, 
                              Closure *cl, Closure **res) {
                            
    int pn = victim_ws->self;

    Closure_assert_ownership(ws, cl);
    WHEN_DEBUG_VERBOSE( cl->mutex_action = PROMOTE_CHILD ); 

    deque_assert_ownership(ws, pn);
    CILK_ASSERT(ws, cl->status == CLOSURE_RUNNING);
    CILK_ASSERT(ws, cl->owner_ready_deque == pn);
    CILK_ASSERT(ws, cl->next_ready == (Closure *) NULL);
    /* cl may have a call parent: it might be promoted as its containing
     * stacklet is stolen, and it's call parent is promoted into full and
     * suspended
     */
    CILK_ASSERT(ws, cl == USE_PARAMETER_WS(invoke_main) 
                    || cl->frame_rsp == UNSET_ADDR
                    || cl->spawn_parent || cl->call_parent);

    Closure *spawn_parent = NULL;
    struct __cilkrts_stack_frame **head = 
            (struct __cilkrts_stack_frame **) WS_HEAD(victim_ws);  
    struct __cilkrts_stack_frame *frame_to_steal = *head;  

    /* can't promote until we are sure nobody works on the frame */
    /* ANGE: Why does checking H <= E  ensures that nobody works on the
     * frame ??? */
    CILK_ASSERT(ws, head <= WS_EXCEPTION(victim_ws));
    /* ANGE: this must be true if we get this far */
    // Note that it can be that H == T here; victim could have done
    // T-- after the thief has done Dekker; in which case, thief gets the last
    // frame, and H == T. Victim won't be able to proceed further until 
    // the thief finishes stealing, releasing the deque lock; at which point, 
    // the victim will realize that it should return back to runtime.
    CILK_ASSERT(ws, head <= WS_TAIL(victim_ws));
    CILK_ASSERT(ws, frame_to_steal != NULL);

    // ANGE: if cl's frame is not set, the top stacklet must contain more 
    // than one frame, because the right-most (oldest) frame must be a spawn
    // helper which can only call a Cilk function.  On the other hand, if
    // cl's frame is set AND equal to the frame at *HEAD, cl must be either
    // the root frame (invoke_main) or have been stolen before. 
    if(cl->frame == frame_to_steal) {
        if(cl == USE_PARAMETER_WS(invoke_main)) {
            // this is the right frame, but it needs to be set up
            tlmm_set_closure_stack_mapping(ws, pn, cl);

        } else if(cl->frame_rsp != UNSET_ADDR) {
            // ANGE: the following conditions must hold for frames 
            // that have been stolen before. 
            CILK_ASSERT(ws, cl->frame_rsp != UNSET_ADDR); 
            CILK_ASSERT(ws, cl->begin_pd_index != UNSET);
            CILK_ASSERT(ws, cl->end_pd_index != UNSET);
            CILK_ASSERT(ws, cl->ances_on_diff_pg != UNSET_CL);
            CILK_ASSERT(ws, cl->ances_on_diff_pg->frame_rsp != UNSET_ADDR);
            CILK_ASSERT(ws, 
                (addr_to_stack_map_index(cl->ances_on_diff_pg->frame_rsp)+1)
                    == cl->begin_pd_index || 
                ((addr_to_stack_map_index(cl->ances_on_diff_pg->frame_rsp)
                    == cl->begin_pd_index) && 
                    AT_PAGE_BOUNDARY(cl->ances_on_diff_pg->frame_rsp)) );
            CILK_ASSERT(ws, 
                (addr_to_stack_map_index(cl->frame_rsp)+1) == cl->end_pd_index 
                || ((addr_to_stack_map_index(cl->frame_rsp) == cl->end_pd_index)
                     && AT_PAGE_BOUNDARY(cl->frame_rsp)) );
            CILK_ASSERT(ws, cl->call_parent || cl->spawn_parent);

        } else {
            AddrType mark; 

            mark = guess_frame_low_watermark(ws, victim_ws, frame_to_steal);
            tlmm_set_closure_stack_mapping_approx(ws, pn, cl, mark);
        }
        spawn_parent = cl;

    } else { // cl->frame could either be NULL or some older frame 
        AddrType mark; 

        // ANGE: if this is true, we must create a new Closure representing
        // the left-most frame (the one to be stolen and resume). 
        // We will then setup the rest after we perform the mapping in
        // thief's stack.
        spawn_parent = Closure_create(ws);
        spawn_parent->frame = frame_to_steal;
        spawn_parent->status = CLOSURE_RUNNING;

        // ANGE: this is only temporary; will reset this after the stack has
        // been remapped; so lets not set the callee in cl yet ... although
        // we do need to set the has_callee in cl, so that cl does not get
        // resumed by some other child performing provably good steal.
        Closure_add_temp_callee(ws, cl, spawn_parent);
        spawn_parent->call_parent = cl; 
    
        mark = guess_frame_low_watermark(ws, victim_ws, frame_to_steal);
        tlmm_set_closure_stack_mapping_approx(ws, pn, spawn_parent, mark);

        // suspend cl & remove it from deque
        Closure_suspend_victim(ws, pn, cl);  
        Closure_unlock(ws, cl);

#if DEBUG_VERBOSE
        Closure_lock_log(ws, spawn_parent, PROMOTE_CHILD);
#else
        Closure_lock(ws, spawn_parent);
#endif
        *res = spawn_parent;

        WHEN_COUNTING_STACKS({
            spawn_parent->stack = cl->stack;
            spawn_parent->stack_before_spawn = cl->stack;
            CILK_ASSERT(ws, 
                spawn_parent->stack_before_spawn_is_used_by_child == 0);
        });
    }

    CILK_ASSERT(ws, spawn_parent->has_cilk_callee == 0);
    Closure *spawn_child = Closure_create(ws);

    spawn_child->spawn_parent = spawn_parent;
    spawn_child->status = CLOSURE_RUNNING;

    /***
     * Register this child, which sets up its siblinb links.
     * We do this here intead of in finish_promote, because we must setup 
     * the sib links for the new child before its pointer escapses.
     ***/
    Closure_add_child(ws, spawn_parent, spawn_child);

    WHEN_COUNTING_STACKS({
        CILK_ASSERT(ws, spawn_parent->stack != 0);
        CILK_ASSERT(ws, spawn_parent->stack_before_spawn != 0);

        if(spawn_parent->stack_before_spawn_is_used_by_child == 0) {
            spawn_parent->stack_before_spawn_is_used_by_child = 1;
            spawn_child->stack = spawn_parent->stack_before_spawn;
            if(spawn_parent->stack == spawn_parent->stack_before_spawn) {
                spawn_parent->stack = get_stack(ws);
            }
        } else {
            spawn_child->stack = spawn_parent->stack;
            // ANGE: need to execute the continuation of parent on some stack, 
            // even before it gets stolen again -- what if it makes a C 
            // function call  
            spawn_parent->stack = get_stack(ws);
        }
        spawn_child->stack_before_spawn = spawn_child->stack;
    });

    WS_HEAD(victim_ws)++;
    // ANGE: we set this frame lazily 
    spawn_child->frame = (struct __cilkrts_stack_frame *) NULL; 

    /* insert the closure on the victim processor's deque */
    deque_add_bottom(ws, spawn_child, pn);

    /* at this point the child can be freely executed */
    return spawn_child;
}

/* 
 * ANGE: Cannot call this function until we have done the stack remapping
 * for the stolen closure 
 */
static void
fixup_stack_mapping(struct __cilkrts_worker *const ws, Closure *cl) {
    
    Closure_assert_ownership(ws, cl);
    Closure *ances = cl->call_parent;
    AddrType frame_rsp = (AddrType) cl->frame->ctx[RSP_INDEX];

    CILK_ASSERT(ws, cl != USE_PARAMETER_WS(invoke_main)); 
    CILK_ASSERT(ws, ances != (Closure *)NULL);

    // orig_from - orig_to: the pds stored in cl currently
    // from - to: the pds that should be stored in cl
    // assumed_to: index of the page after where the assumed_frame_rsp falls
    int i, j, from, to, orig_from, orig_to, assumed_to;
    size_t orig_array_size, new_array_size;

    /* After the while loop, ances will be set to the ances whose 
     * frame_rsp is set and has pd_array with size > 0 
     */
    while(ances->frame_rsp == UNSET_ADDR ||
          (ances->begin_pd_index - ances->end_pd_index) == 0) {

        if(ances->frame_rsp == UNSET_ADDR) {
            ances = (ances->call_parent == NULL) ?
                        ances->spawn_parent : ances->call_parent;
        } else {
            CILK_ASSERT(ws, ances->ances_on_diff_pg != UNSET_CL);
            ances = ances->ances_on_diff_pg;
        }
        CILK_ASSERT(ws, ances != (Closure *)NULL);
    }

    CILK_ASSERT(ws, ances->frame_rsp != UNSET_ADDR);
    CILK_ASSERT(ws, ances->begin_pd_index != UNSET_ADDR);
    CILK_ASSERT(ws, ances->end_pd_index != UNSET_ADDR);
    CILK_ASSERT(ws, cl->frame_rsp == UNSET_ADDR);

    cl->ances_on_diff_pg = ances;
    from = ances->end_pd_index;
    to = addr_to_stack_map_index(frame_rsp);

    if( !AT_PAGE_BOUNDARY(frame_rsp) ) {
        to++;
    }

    orig_from = cl->begin_pd_index;
    orig_to = cl->end_pd_index;
    cl->begin_pd_index = from;
    cl->end_pd_index = to;
    assumed_to = addr_to_stack_map_index(cl->assumed_frame_rsp) + 1;
    orig_array_size = sizeof(Page) * (orig_to - orig_from);
    new_array_size = sizeof(Page) * (to - from);

    CILK_ASSERT(ws, from >= 0 && to >= 0 && orig_from >= 0 && orig_to >= 0);
    CILK_ASSERT(ws, (orig_to == to && orig_from == from) 
                    || ((orig_to - orig_from) > (to - from)));
    CILK_ASSERT(ws, orig_from <= from);
    CILK_ASSERT(ws, orig_to >= to && orig_to >= assumed_to);
    CILK_ASSERT(ws, to >= from);
    CILK_ASSERT( ws, to >= assumed_to || to == (assumed_to-1) ); 

    // ANGE: fixup the pd_array in cl; before we actually fix frame_rsp, 
    // we always use pd_ptr to store pds and never the pds (only allow 2
    // pds).  After we know the *actual* size of pds, then we may opt for
    // using pds when there are <= 2 pds to store.
    if( (to - from) <= 2 ) {
        Page *pds = cl->pds.pd_ptr;
        if(assumed_to > to) {
            cl->assumed_extra_pd = pds[to - orig_from];
        }
        if( (to - from) > 0 ) {
            CILK_ASSERT(ws, orig_array_size != 0);
            cl->pds.pd_array[0] = pds[from-orig_from];
            if( (to-from) == 2 ) {
                cl->pds.pd_array[1] = pds[from-orig_from+1];
            }
        } else {
            cl->pds.pd_ptr = NULL;
        }
        if(orig_array_size != 0) {
            Cilk_internal_free(ws, pds, orig_array_size);        
        }
    } else {
        CILK_ASSERT( ws, new_array_size != orig_array_size ||
            ((to == orig_to) && (from == orig_from) && (to >= assumed_to)) );

        if(new_array_size != orig_array_size) {
            // get new memory - it may be the same bucket, but the internal
            // memory allocator actually checks for the specific size info,
            // and complain if the bookkeeping does not end up matching.
            Page *pds = (Page *) Cilk_internal_malloc(ws, new_array_size);
            for(i=from, j=0; i < to; i++,j++) {
                pds[j] = cl->pds.pd_ptr[i-orig_from];
            }
            if( assumed_to > to ) { // overshot 
                CILK_ASSERT(ws, (orig_to > to) && (i < orig_to) && (j < to));
                cl->assumed_extra_pd = cl->pds.pd_ptr[i-orig_from]; 
            }
            Cilk_internal_free(ws, cl->pds.pd_ptr, orig_array_size);
            cl->pds.pd_ptr = pds;

        }
    }

    /* We could have possibly overshot or undershot during 
     * tlmm_remap_stack_approx. We can never overshot by more than one 
     * page, however.  Thus we don't fix it if we overshot, and resume 
     * with that extra page in between.  We only fix it if we undershot.
     */
    CILK_ASSERT(ws, to >= assumed_to || cl->assumed_extra_pd != UNSET);

    // we undershot
    if( to > assumed_to ) {
        tlmm_remap_stack_suffix(ws, cl);
    }
    // in the case where rsp is correct or undershot, 
    // we can reset assumed_frame_rsp
    if( to >= assumed_to ) {
        cl->assumed_frame_rsp = UNSET_ADDR;
        CILK_ASSERT(ws, cl->assumed_extra_pd == UNSET);
    }

    Cilk_membar_StoreStore();
    cl->frame_rsp = frame_rsp; 
}

/***
 * Finishes the promotion process.  The child is already fully promoted
 * and requires no more work (we only use the given pointer to identify
 * the child).  This function does some more work on the parent to make
 * the promotion complete.
 *
 * ANGE: This includes remapping the thief's stack, promote everything along
 * the stolen stacklet into full closures, and finally fix up the stack
 * mapping if we mapped too much or too little the first time around.
 ***/
static void 
finish_promote(struct __cilkrts_worker *const ws, 
               struct __cilkrts_worker *const victim_ws,
               Closure *parent, Closure *child) {

    Closure_assert_ownership(ws, parent);
    Closure_assert_alienation(ws, child);
    CILK_ASSERT(ws, parent->has_cilk_callee == 0);

    /* the parent is still locked; we still need to update it */
    /* join counter update */
    parent->join_counter++;

    if( parent->frame_rsp == UNSET_ADDR ) {
        tlmm_remap_stack_approx(ws, parent);

        // ANGE: the "else" case applies to a closure which has its frame 
        // set, but not its frame_rsp.  These closures may have been stolen 
        // before as part of a stacklet, so its frame is set (and stolen 
        // flag is set), but its frame_rsp is not set, because it didn't 
        // spawn until now.
        if(__cilkrts_not_stolen(parent->frame)) {
            setup_closures_in_stacklet(ws, victim_ws, parent);
        }
        fixup_stack_mapping(ws, parent); 

    } else {
        tlmm_remap_stack(ws, parent);
        CILK_ASSERT(ws, __cilkrts_stolen(parent->frame) != 0);
    }
    CILK_ASSERT(ws, parent->frame->worker == victim_ws); 

    __cilkrts_set_unsynced(parent->frame);
    /* Make the parent ready */
    Closure_make_ready(parent);

    return;
}

/*
 * stealing protocol.  Tries to steal from the victim; returns a
 * stolen closure, or NULL if none.
 */
static Closure *Closure_steal(struct __cilkrts_worker *const ws, int victim) {

    Closure *res = (Closure *) NULL;
    Closure *cl, *child;
    struct __cilkrts_worker *victim_ws;

    WHEN_PERFCTR_TIMING( struct perfctr_sum_ctrs begin; )
    WHEN_PERFCTR_TIMING( struct perfctr_sum_ctrs end; )

    Cilk_event(ws, EVENT_STEAL_ATTEMPT);
    WHEN_COUNTING_STEALS(
        USE_PARAMETER_WS(steal_attempt_count)[ws->self]++;
    );

    if( deque_trylock(ws, victim) == 0 ) {
        return NULL; 
    }

    cl = deque_peek_top(ws, victim);
    
    if (cl) {
        if( Closure_trylock(ws, cl) == 0 ) {
            deque_unlock(ws, victim);
            return NULL;
        }

        WHEN_PERFCTR_TIMING( Cilk_perfctr_read(ws->self, &begin); )
        WHEN_DEBUG_VERBOSE( cl->mutex_action = STEAL ); 

        /* do not steal aborting closures */
        if (Closure_being_aborted(ws, cl)) {
            Cilk_event(ws, EVENT_STEAL_ABORT);
            goto give_up;
        }

        victim_ws = USE_PARAMETER_WS(workers)[victim];

        switch (cl->status) {
            case CLOSURE_READY:
                Cilk_die_internal(ws->g, ws, 
                                  "Bug: ready closure in ready deque\n");
                break;

            case CLOSURE_RUNNING:
                /* send the exception to the worker */
                if (do_dekker_on(ws, victim_ws, cl)) {
                    /* 
                     * if we could send the exception, promote
                     * the child to a full closure, and steal
                     * the parent
                     */
                    child = promote_child(ws, victim_ws, cl, &res);

                    /* detach the parent */
                    // ANGE: the top of the deque could have changed in the
                    // else case.
                    if(res == (Closure *) NULL) {
                        res = deque_xtract_top(ws, victim);
                        CILK_ASSERT(ws, cl == res);
                    }
                    Closure_assert_ownership(ws, res);
                    WHEN_DEBUG_VERBOSE( Cilk_dprintf(ws, 
                        "Stolen %lx from %d, top of deque was %lx.\n",
                        res, victim, cl); )

                    CILK_ASSERT(ws, res->begin_pd_index != UNSET);
                    CILK_ASSERT(ws, res->end_pd_index != UNSET);

                    WHEN_CILK_DEBUG( 
                        /* don't check the last_index_shared in victim's
                         * stack, since we didn't set it here */
                        tlmm_check_stack_mapping(ws, victim, res, 0); )
                    deque_unlock(ws, victim);

                    // at this point, more steals can happen from the victim.
                    finish_promote(ws, victim_ws, res, child);

                    CILK_ASSERT(ws, res->right_most_child == child);
                    CILK_ASSERT(ws, res->frame->worker == victim_ws); 
                    Closure_unlock(ws, res);

                    // this is turned on only when TLMM_DEBUG is true
                    check_tlmm_rmap_region_empty(ws);

                    Cilk_event(ws, EVENT_STEAL);

                    WHEN_COUNTING_STEALS(
                        USE_PARAMETER_WS(steal_succ_count)[ws->self]++;)

                } else {
                    Cilk_event(ws, EVENT_STEAL_NO_DEKKER);
                    goto give_up;
                }

                break;

            case CLOSURE_SUSPENDED:
                Cilk_die_internal(ws->g, ws, 
                                  "Bug: suspended closure in ready deque\n");
                break;

            case CLOSURE_RETURNING:
                /* ok, let it leave alone */
                Cilk_event(ws, EVENT_STEAL_RETURNING);

            give_up:
                // MUST unlock the closure before the queue;
                // see rule D in the file PROTOCOLS
                Closure_unlock(ws, cl);
                deque_unlock(ws, victim);
                break;

            default:
                Cilk_die_internal(ws->g, ws, 
                                  "Bug: unknown closure status\n");
        }

    } else {
        deque_unlock(ws, victim);
        Cilk_event(ws, EVENT_STEAL_EMPTY_DEQUE);
    }

    WHEN_PERFCTR_TIMING({
        if(res) {
            Cilk_perfctr_read(ws->self, &end);
            Cilk_perfctr_record_steal(ws->self, &begin, &end);
        }
    });

    return res;
}

static Closure *provably_good_steal_maybe(struct __cilkrts_worker *const ws, 
                                          Closure *parent) {

    Closure *res = (Closure *) NULL;

    Closure_assert_ownership(ws, parent);

    if (!Closure_has_children(parent) &&
        parent->status == CLOSURE_SUSPENDED) {

        CILK_ASSERT(ws, parent->frame != NULL);
        CILK_ASSERT(ws, 
            parent->frame->worker == (struct __cilkrts_worker *) NOBODY);

        /* do a provably-good steal; this is *really* simple */
        res = parent;
        ws->l->provablyGoodSteal = 1;
        res->frame->worker = ws;

        /* All children returned, so we can reclaim the space and 
         *  use the real frame rsp. 
         */ 
        res->assumed_frame_rsp = UNSET_ADDR;
        res->assumed_extra_pd = UNSET;

        Cilk_event(ws, EVENT_PROVABLY_GOOD_STEAL);

        WHEN_COUNTING_STEALS(
            USE_PARAMETER_WS(provably_good_steal)[ws->self]++;)


        /* debugging stuff */
        CILK_ASSERT(ws, parent->owner_ready_deque == NOBODY);

        __cilkrts_set_synced(parent->frame);
        Closure_make_ready(parent);

    } else {
        ws->l->provablyGoodSteal = 0;
    }

    /* ANGE: If provably-good steal is successful, reset stack mapping 
     * with real rsp; otherwise, reset stack mapping with assumed rsp.
     * If the frame is aborted, we may get to this point without 
     * ever being stolen (stack not initialized), and in which case, 
     * we should just continue without setting last_index_shared. 
     */
    if( __builtin_expect(stack_initialized(ws), 1) ) {
        WHEN_DEBUG_VERBOSE(
            Cilk_dprintf(ws, "reset stack in provably good steal.\n"); )
        tlmm_reset_stack_mapping(ws, parent);
    }

    return res;
}


// this can return false negative --- if the parent closure's lock is not
// available, we just assume that someone is working on the parent closure
// and return false
static inline int 
is_last_child_returning(struct __cilkrts_worker *const ws, Closure *child) {
    
    int res = 0;
    Closure *parent = child->spawn_parent;

    CILK_ASSERT(ws, parent);
    if( Closure_trylock(ws, parent) ) { // lock acquired
        res = (parent->status == CLOSURE_SUSPENDED) && 
              (parent->join_counter == 1) && (parent->has_cilk_callee == 0);
        Closure_unlock(ws, parent);
    }
    return res; 
}

#endif // PARALLEL_REDUCE


/***
 * Return protocol for a spawned child.
 *
 * If any reducer is accessed by the child closure, we need to reduce the
 * reducer views with the child's right_sib_rmap, and its left sibling's
 * right_sib_rmap (or parent's child_rmap if it's the left most child)
 * before we unlink the child from its sibling closure list.
 *
 * When we modify the sibling links (left_sib / right_sib), we always lock
 * the parent and the child.  When we retrieve the reducer maps from left
 * sibling or parent from their place holders (right_sib_rmap / child_rmap), 
 * we always lock the closure from whom we are getting the rmap from.  
 * The locking order is always parent first then child, right child first, 
 * then left.
 * 
 * Once we have done the reduce operation, we try to deposit the rmap from
 * the child to either it's left sibling's right_sib_rmap or parent's
 * child_rmap.  Note that even though we have performed the reduce, by the
 * time we deposit the rmap, the child's left sibling may have changed, 
 * or child may become the new left most child.  Similarly, the child's
 * right_sib_rmap may have something new again.  If that's the case, we
 * need to do the reduce again (in deposit_reducer_map).
 * 
 * This function returns a closure to be executed next, or NULL if none.
 * The child must not be locked by ourselves, and be in no deque.
 ***/
static Closure *
Closure_return(struct __cilkrts_worker *const ws, Closure *child) {

    Closure *parent;
    Closure *res = (Closure *) NULL;

    int tlmm_has_views; 

    WHEN_CILK_DEBUG( int last_child = 0; )

    CILK_ASSERT(ws, child);
    CILK_ASSERT(ws, child->join_counter == 0);
    CILK_ASSERT(ws, child->status == CLOSURE_RETURNING);
    CILK_ASSERT(ws, child->owner_ready_deque == NOBODY);
    Closure_assert_alienation(ws, child);

    CILK_ASSERT(ws, child->has_cilk_callee == 0);
    CILK_ASSERT(ws, child->call_parent == NULL);
    CILK_ASSERT(ws, child->spawn_parent != NULL);

    CILK_ASSERT(ws, child->child_rmap == NULL && child->user_rmap == NULL);

    WHEN_DEBUG_VERBOSE( Cilk_dprintf(ws, 
        "Closure return, child %lx returning back to parent %lx.\n", 
        child, child->spawn_parent) );

    Cilk_enter_state(ws, STATE_MERGE);

    Cilk_exit_state(ws, STATE_MERGE);


    WHEN_DEBUG_VERBOSE( Cilk_dprintf(ws, 
                    "Now doing the actual return child %lx.\n", child) );

    parent = child->spawn_parent;

    // At this point the status is as follows: the child is in no deque 
    // and unlocked.  However, the child is still linked with its siblings, 
#if DEBUG_VERBOSE
    Closure_lock_log(ws, parent, CLOSURE_RETURN_THE_PARENT);
#else
    Closure_lock(ws, parent);
#endif

    CILK_ASSERT(ws, parent->status != CLOSURE_RETURNING);
    CILK_ASSERT(ws, parent->frame != NULL);
    // CILK_ASSERT(ws, parent->frame->magic == CILK_STACKFRAME_MAGIC);

#if DEBUG_VERBOSE
    Closure_lock_log(ws, child, CLOSURE_RETURN_THE_CHILD);
#else
    Closure_lock(ws, child);
#endif
    
    Closure_remove_child(ws, parent, child);

    // If the frame is aborted, we may get to this point without ever being 
    // stolen, and we should just continue without setting last_index_shared. 
    CILK_ASSERT(ws, stack_initialized(ws) ||
                    CLOSURE_ABORT(child) == ABORT_ALL);


    /* update critical path and work */
    /*
    WHEN_CILK_TIMING({
        if(ws->l->cp_hack > parent->cp)
            parent->cp = ws->l->cp_hack;
        parent->work += ws->l->work_hack;
    }); */

    /* now the child is no longer needed */
    Closure_unlock(ws, child);
    Closure_destroy(ws, child);

    /* 
     * the two fences ensure dag consistency (Backer)
     */
    CILK_ASSERT(ws, parent->join_counter > 0);
    Cilk_fence();
    --parent->join_counter;
    Cilk_fence();

    res = provably_good_steal_maybe(ws, parent);
    Closure_unlock(ws, parent);

    // if this is the last child, provably good steal must succeed
    CILK_ASSERT(ws, last_child == 0 || res);


    return res;
}
