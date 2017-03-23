#ifndef _STACK_FRAME_H
#define _STACK_FRAME_H

// Includes


// Forward declaration
typedef struct __cilkrts_stack_frame __cilkrts_stack_frame;

/**
 * Every spawning function has a frame descriptor.  A spawning function
 * is a function that spawns or detaches.  Only spawning functions
 * are visible to the Cilk runtime.
 */
struct __cilkrts_stack_frame
{
    // Flags is a bitfield with values defined below. Client code
    /// initializes flags to 0 before the first Cilk operation.
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
    __CILK_JUMP_BUFFER ctx;
};

#endif
