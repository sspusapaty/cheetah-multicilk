#ifndef _LOCAL_STATE_H
#define _LOCAL_STATE_H

// Forward declaration
typedef struct local_state local_state;
typedef struct __cilkrts_stack_frame **CilkShadowStack;

// Includes
#include "stack_frame.h"
#include "jmpbuf.h"
#include "fiber.h"

// Actual declaration
struct local_state {
    CilkShadowStack shadow_stack;
  
    size_t deque_depth;
    int provably_good_steal;

    unsigned int rand_next;

    // cilk_fiber * runtime_fiber;
    jmpbuf rts_ctx;
    cilk_fiber * fiber_to_free;
  
    volatile unsigned int magic;
};
#endif
