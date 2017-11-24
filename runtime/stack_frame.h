#ifndef _STACK_FRAME_H
#define _STACK_FRAME_H

// Forward declaration
typedef struct __cilkrts_stack_frame __cilkrts_stack_frame;

// Includes
#include <stdint.h>
#include "worker.h"
#include "jmpbuf.h"


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
    jmpbuf ctx;

    // ANGE: need the follwoing fields for storing / restoring floating point
    // states.  For linux / x86_64 platforms only.
    /**
     * Architecture-specific floating point state.  mxcsr and fpcsr should be
     * set when CILK_SETJMP is called in client code.  Note that the Win64
     * jmpbuf for the Intel64 architecture already contains this information
     * so there is no need to use these fields on that OS/architecture.
     */
    uint32_t mxcsr;
    uint16_t fpcsr;         /**< @copydoc mxcsr */

    /**
     * reserved is not used at this time.  Client code should initialize it
     * to 0 before the first Cilk operation
     */
    uint16_t reserved;      // ANGE: leave it to make it 8-byte aligned.
    uint32_t magic;
};

#endif
