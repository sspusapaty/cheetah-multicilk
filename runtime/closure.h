#ifndef _CLOSURE_H
#define _CLOSURE_H

// Includes
#include "stack_frame.h"
#include "cilk_mutex.h"

// Forward declaration
typedef struct Closure Closure;
typedef struct __cilkrts_stack_frame __cilkrts_stack_frame;

// Actual declaration
enum ClosureStatus { CLOSURE_RUNNING = 42, CLOSURE_SUSPENDED,
                     CLOSURE_RETURNING, CLOSURE_READY };

/* The order is important here. */
enum AbortStatus { ABORT_ALL = 30 , ALMOST_NO_ABORT, NO_ABORT};

/* 
 * the list of children is not distributed among
 * the children themselves, in order to avoid extra protocols
 * and locking.
 */
struct Closure {
    Cilk_mutex mutex;          /* mutual exclusion lock */
    WHEN_DEBUG_VERBOSE(int mutex_action;)
    WHEN_CILK_DEBUG(int mutex_owner;)

    int join_counter; /* number of spawned outstanding children */
    int has_cilk_callee; /* has called outstanding cilk child */
    enum ClosureStatus status;

    /* the called cilk child closure --- used only for abort when program
     * exit early by calling Cilk_exit */
    Closure *callee;

    Closure *call_parent; /* the "parent" closure that called */
    Closure *spawn_parent; /* the "parent" closure that spawned */

    __cilkrts_stack_frame *frame;       /* rest of the closure */
    AddrType frame_rsp;
    AddrType assumed_frame_rsp;
    enum AbortStatus abort_status;

    Closure *left_sib;  // left *spawned* sibling in the closure tree
    Closure *right_sib; // right *spawned* sibling in the closur tree
    // right most *spawned* child in the closure tree
    Closure *right_most_child; 

    /*
     * stuff related to ready deque.  These fields
     * must be managed only by the queue manager in sched.c
     *
     * ANGE: for top of the ReadyDeque, prev_ready = NULL
     *       for bottom of the ReadyDeque, next_ready = NULL
     *       next_ready pointing downward, prev_ready pointing upward 
     *
     *       top
     *  next | ^
     *       | | prev
     *       v |
     *       ...
     *  next | ^
     *       | | prev
     *       v |
     *      bottom
     */
    Closure *next_ready;
    Closure *prev_ready;

    /* miscellanea */
    unsigned int magic;
    CILK_CACHE_LINE_PAD;
};
#endif
