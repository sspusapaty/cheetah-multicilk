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
  
    size_t stackdepth;

    __CILK_JUMP_BUFFER rts_env;  // the jmp_buf associated with runtime context
                      // this should be on worker's private stack (i.e. the
                      // one assigned during pthread_create)

    int provablyGoodSteal;

    unsigned int rand_next;
    int barrier_direction;

    cilk_fiber * runtime_fiber;
    cilk_fiber * user_fiber;
  
    volatile unsigned int magic;
};
#endif
