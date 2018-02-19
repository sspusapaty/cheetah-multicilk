#ifndef _CILK_INTERNAL_H
#define _CILK_INTERNAL_H

// Includes
#include <stdint.h>
#include "common.h"
#include "global_state.h"
#include "jmpbuf.h"

//===============================================
// Cilk stack frame related defs
//===============================================

/**
 * Every spawning function has a frame descriptor.  A spawning function
 * is a function that spawns or detaches.  Only spawning functions
 * are visible to the Cilk runtime.
 */
struct __cilkrts_stack_frame {
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

//===============================================
// Worker definition 
//===============================================

// Forward declaration
typedef struct local_state local_state;
typedef struct __cilkrts_stack_frame **CilkShadowStack;

// Actual declaration
struct local_state {
  __cilkrts_stack_frame **shadow_stack;

  int provably_good_steal;
  unsigned int rand_next;

  jmpbuf rts_ctx;
  struct cilk_fiber * fiber_to_free;
  volatile unsigned int magic;
  int test;
};

// Actual definitions
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
