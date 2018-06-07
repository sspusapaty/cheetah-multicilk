#ifndef _CILK_INTERNAL_H
#define _CILK_INTERNAL_H

#include <stdint.h>
#include <pthread.h>

// Includes
#include "debug.h"
#include "jmpbuf.h"
#include "rts-config.h"

#define NOBODY -1

// Forward declaration
typedef struct __cilkrts_worker __cilkrts_worker;
typedef struct __cilkrts_stack_frame __cilkrts_stack_frame;

//===============================================
// Cilk stack frame related defs
//===============================================

/**
 * Every spawning function has a frame descriptor.  A spawning function
 * is a function that spawns or detaches.  Only spawning functions
 * are visible to the Cilk runtime.
 *
 * NOTE: if you are using the Tapir compiler, you should not change
 * these fields; ok to change for hand-compiled code.
 * See Tapir compiler ABI: 
 * https://github.com/wsmoses/Tapir-LLVM/blob/cilkr/lib/Transforms/Tapir/CilkRABI.cpp
 */
struct __cilkrts_stack_frame {
    // Flags is a bitfield with values defined below. Client code
    // initializes flags to 0 before the first Cilk operation.
    uint32_t flags;

    // call_parent points to the __cilkrts_stack_frame of the closest
    // ancestor spawning function, including spawn helpers, of this frame.
    // It forms a linked list ending at the first stolen frame.
    __cilkrts_stack_frame *  call_parent;

    // The client copies the worker from TLS here when initializing
    // the structure.  The runtime ensures that the field always points
    // to the __cilkrts_worker which currently "owns" the frame.
    __cilkrts_worker * worker;

    // Before every spawn and nontrivial sync the client function
    // saves its continuation here.
    jmpbuf ctx;

    /**
     * Architecture-specific floating point state.  
     * mxcsr and fpcsr should be set when setjmp is called in client code.  
     *
     * They are for linux / x86_64 platforms only.  Note that the Win64
     * jmpbuf for the Intel64 architecture already contains this information
     * so there is no need to use these fields on that OS/architecture.
     */
    uint32_t mxcsr;
    uint16_t fpcsr;

    /**
     * reserved is not used at this time.  Client code should initialize it
     * to 0 before the first Cilk operation
     */
    uint16_t reserved;      // ANGE: leave it to make it 8-byte aligned.
    uint32_t magic;
};

//===========================================================
// Value defines for the flags field in cilkrts_stack_frame
//===========================================================

/* CILK_FRAME_STOLEN is set if the frame has ever been stolen. */
#define CILK_FRAME_STOLEN 0x01

/* CILK_FRAME_UNSYNCHED is set if the frame has been stolen and
   is has not yet executed _Cilk_sync. It is technically a misnomer in that a
   frame can have this flag set even if all children have returned. */
#define CILK_FRAME_UNSYNCHED 0x02

/* Is this frame detached (spawned)? If so the runtime needs
   to undo-detach in the slow path epilogue. */
#define CILK_FRAME_DETACHED 0x04

/* CILK_FRAME_EXCEPTION_PROBED is set if the frame has been probed in the
   exception handler first pass */
#define CILK_FRAME_EXCEPTION_PROBED 0x08

/* Is this frame receiving an exception after sync? */
#define CILK_FRAME_EXCEPTING 0x10

/* Is this the last (oldest) Cilk frame? */
#define CILK_FRAME_LAST 0x80

/* Is this frame in the epilogue, or more generally after the last
   sync when it can no longer do any Cilk operations? */
#define CILK_FRAME_EXITING 0x0100

#define CILK_FRAME_VERSION (__CILKRTS_ABI_VERSION << 24)

//===========================================================
// Helper functions for the flags field in cilkrts_stack_frame
//===========================================================

/* A frame is set to be stolen as long as it has a corresponding Closure */
static inline void __cilkrts_set_stolen(__cilkrts_stack_frame *sf) {
    sf->flags |= CILK_FRAME_STOLEN;
}

/* A frame is set to be unsynced only if it has parallel subcomputation
 * underneathe, i.e., only if it has spawned children executing on a different
 * worker 
 */
static inline void __cilkrts_set_unsynced(__cilkrts_stack_frame *sf) {
    sf->flags |= CILK_FRAME_UNSYNCHED;
}

static inline void __cilkrts_set_synced(__cilkrts_stack_frame *sf) {
    sf->flags &= ~CILK_FRAME_UNSYNCHED;
}

/* Returns nonzero if the frame is not synched. */
static inline int __cilkrts_unsynced(__cilkrts_stack_frame *sf) {
    return (sf->flags & CILK_FRAME_UNSYNCHED);
}

/* Returns nonzero if the frame has been stolen. */
static inline int __cilkrts_stolen(__cilkrts_stack_frame *sf) {
    return (sf->flags & CILK_FRAME_STOLEN);
}

/* Returns nonzero if the frame is synched. */
static inline int __cilkrts_synced(__cilkrts_stack_frame *sf) {
    return( (sf->flags & CILK_FRAME_UNSYNCHED) == 0 );
}

/* Returns nonzero if the frame has never been stolen. */
static inline int __cilkrts_not_stolen(__cilkrts_stack_frame *sf) {
    return( (sf->flags & CILK_FRAME_STOLEN) == 0);
}

//===============================================
// Worker related definition 
//===============================================

// Forward declaration
typedef struct global_state global_state;
typedef struct local_state local_state;
typedef struct __cilkrts_stack_frame **CilkShadowStack;


// Actual declaration
struct rts_options {
    int nproc;
    int deqdepth;
    int stacksize;
    int alloc_batch_size;
};

#define DEFAULT_OPTIONS \
{                                                          \
    DEFAULT_NPROC,       /* num of workers to create */    \
    DEFAULT_DEQ_DEPTH,   /* num of entries in deque */     \
    DEFAULT_STACK_SIZE,  /* stack size to use for fiber */ \
    DEFAULT_ALLOC_BATCH, /* alloc_batch_size */            \
}

// Actual declaration
struct global_state {
    /* globally-visible options (read-only after init) */
    struct rts_options options;

    /*
     * this string is printed when an assertion fails.  If we just inline
     * it, apparently gcc generates many copies of the string.
     */
    const char *assertion_failed_msg;
    const char *stack_overflow_msg;

    /* dynamically-allocated array of deques, one per processor */
    struct ReadyDeque *deques;
    struct __cilkrts_worker **workers;
    pthread_t * threads;

    struct cilk_fiber_pool *fiber_pool;
    struct Closure *invoke_main;

    volatile int invoke_main_initialized;
    volatile int start;
    volatile int done;

    int cilk_main_argc;
    char **cilk_main_args;

    int cilk_main_return;
    int cilk_main_exit;
};

// Actual declaration
struct local_state {
    __cilkrts_stack_frame **shadow_stack;
    struct cilk_fiber_pool *fiber_pool;

    int provably_good_steal;
    unsigned int rand_next;

    jmpbuf rts_ctx;
    struct cilk_fiber * fiber_to_free;
    volatile unsigned int magic;
};

/**
 * NOTE: if you are using the Tapir compiler, you should not change
 * these fields; ok to change for hand-compiled code.
 * See Tapir compiler ABI: 
 * https://github.com/wsmoses/Tapir-LLVM/blob/cilkr/lib/Transforms/Tapir/CilkRABI.cpp
 **/
struct __cilkrts_worker {
    // T and H pointers in the THE protocol
    __cilkrts_stack_frame * volatile * volatile tail;
    __cilkrts_stack_frame * volatile * volatile head;
    __cilkrts_stack_frame * volatile * volatile exc;

    // Limit of the Lazy Task Queue, to detect queue overflow
    __cilkrts_stack_frame * volatile *ltq_limit;

    // Worker id.
    int32_t self;

    // Global state of the runtime system, opaque to the client.
    global_state * g;

    // Additional per-worker state hidden from the client.
    local_state * l;

    // A slot that points to the currently executing Cilk frame.
    __cilkrts_stack_frame * current_stack_frame;
};

#endif // _CILK_INTERNAL_H
