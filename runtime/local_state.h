#ifndef _LOCAL_STATE_H
#define _LOCAL_STATE_H

// Includes
#include "stack_frame.h"

// Forward declaration
typedef struct local_state local_state;
typedef struct __cilkrts_stack_frame __cilkrts_stack_frame;
typedef struct __cilkrts_stack_frame **CilkShadowStack;

// Actual declaration
struct local_state {
    CilkShadowStack shadow_stack;
  
    size_t stackdepth;

    jmp_buf rts_env;  // the jmp_buf associated with runtime context
                      // this should be on worker's private stack (i.e. the
                      // one assigned during pthread_create)

    int provablyGoodSteal;

    unsigned int rand_next;
    int barrier_direction;

    volatile unsigned int magic;
};
#endif
