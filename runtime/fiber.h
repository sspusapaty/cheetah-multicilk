#ifndef _FIBER_H
#define _FIBER_H

#include "cilk-internal.h"
#include "debug.h"
#include "mutex.h"

#define FIBER_STATS 0

// forward declaration
typedef struct cilk_fiber cilk_fiber;
typedef struct cilk_fiber_pool cilk_fiber_pool;

//===============================================================
// Struct defs used by fibers, fiber pools
//===============================================================

// Statistics on active fibers that were allocated from this pool,
struct fiber_pool_stats {
    int num_allocated;   // fibers allocated out of this pool
    int num_deallocated; // fibers freed into the pool
    int max_allocated;   // high watermark on max_allocated
};

struct cilk_fiber_pool {
    cilk_mutex *lock;                 // Mutual exclusion for pool operations 
    WHEN_CILK_DEBUG(int mutex_owner;) 
    size_t stack_size;                // Size of stacks for fibers in this pool.
    struct cilk_fiber_pool *parent;   // Parent pool.
                                      // If this pool is empty, get from parent
    // Describes inactive fibers stored in the pool.
    cilk_fiber **fibers;           // Array of max_size fiber pointers 
    unsigned max_size;             // Limit on number of fibers in pool 
    unsigned size;                 // Number of fibers currently in the pool

#if FIBER_STATS
    struct fiber_pool_stats pool_stats; // unimplemented yet
#endif
};

struct cilk_fiber {
  char * m_stack;        // stack low addr, including the mprotected page
  char * m_stack_base;   // the usable portion where it can start grow downward
  __cilkrts_worker * owner; // worker using this fiber 
};


//===============================================================
// Supported functions
//===============================================================

static inline void cilk_fiber_set_owner(cilk_fiber * fiber, __cilkrts_worker * owner) {
    fiber->owner = owner;
}

void sysdep_save_fp_ctrl_state(__cilkrts_stack_frame *sf);
char * sysdep_reset_jump_buffers_for_resume(cilk_fiber* fiber,
                                           __cilkrts_stack_frame *sf);
__attribute__((noreturn)) void sysdep_longjmp_to_sf(__cilkrts_stack_frame *sf);
__attribute__((noreturn)) void init_fiber_run(cilk_fiber * fiber, 
                                              __cilkrts_stack_frame *sf);

void cilk_fiber_pool_global_init(global_state *g, unsigned buffer_size); 
void cilk_fiber_pool_global_destroy(global_state *g);
void cilk_fiber_pool_per_worker_init(__cilkrts_worker *const w, 
                                     unsigned buffer_size, int is_shared);
void cilk_fiber_pool_per_worker_destroy(__cilkrts_worker *const w); 

// allocate / deallocate one fiber from / back to OS
cilk_fiber * cilk_fiber_allocate();
void cilk_fiber_deallocate(cilk_fiber *fiber);
// allocate / deallocate one fiber from / back to per-worker pool 
cilk_fiber * cilk_fiber_pool_allocate(__cilkrts_worker *const w);
void cilk_fiber_pool_deallocate(__cilkrts_worker *const w, cilk_fiber *fiber);
#endif
