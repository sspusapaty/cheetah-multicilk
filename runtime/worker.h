#ifndef _WORKER_H
#define _WORKER_H

// Includes


// Forward declaration
typedef struct __cilkrts_worker __cilkrts_worker;

struct __cilkrts_worker {
    // T and H pointers in the THE protocol
    // [shared read/write]
    __cilkrts_stack_frame * volatile * volatile tail;
    __cilkrts_stack_frame * volatile * volatile head;

    // Limit of the Lazy Task Queue, to detect queue overflow
    // [local read-only]
    __cilkrts_stack_frame * volatile *ltq_limit;

    // Worker id.
    // [local read-only]
    int32_t self;

    // Global state of the runtime system, opaque to the client.
    // [local read-only]
    global_state_t * g;

    // Additional per-worker state hidden from the client.
    // [shared read-only]
    local_state * l;

    // A slot that points to the currently executing Cilk frame.
    // [local read/write]
    __cilkrts_stack_frame * current_stack_frame;
};



#endif
