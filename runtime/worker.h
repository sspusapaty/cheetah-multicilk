#ifndef _WORKER_H
#define _WORKER_H

// Forward declaration
typedef struct __cilkrts_worker __cilkrts_worker;

// Includes
#include <stdint.h>
#include "stack_frame.h"
#include "global_state.h"
#include "local_state.h"

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



#endif
