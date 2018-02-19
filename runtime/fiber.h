#ifndef _FIBER_H
#define _FIBER_H

typedef struct cilk_fiber cilk_fiber;
typedef struct __cilkrts_worker __cilkrts_worker;
typedef struct __cilkrts_stack_frame __cilkrts_stack_frame;

/*
struct cilk_fiber_pool {
    Cilk_mutex*      lock;       ///< Mutual exclusion for pool operations 
    size_t   stack_size;         ///< Size of stacks for fibers in this pool.
    cilk_fiber_pool* parent;     ///< @brief Parent pool.
                                 ///< If this pool is empty, get from parent 

    // Describes inactive fibers stored in the pool.
    cilk_fiber**     fibers;     ///< Array of max_size fiber pointers 
    unsigned         max_size;   ///< Limit on number of fibers in pool 
    unsigned         size;       ///< Number of fibers currently in the pool

    // Statistics on active fibers that were allocated from this pool,
    // but no longer in the pool.
    int              total;      ///< @brief Fibers allocated - fiber deallocated from pool
                                 ///< total may be negative for non-root pools.
    int              high_water; ///< High water mark of total fibers
    int              alloc_max;  ///< Limit on number of fibers allocated from the heap/OS
};
*/

struct cilk_fiber {
  char * m_stack;        // stack low addr, including the mprotected page
  char * m_stack_base;   // the usable portion where it can start grow downward
  
  __cilkrts_worker * owner; // worker using this fiber 
};

static inline void cilk_fiber_set_owner(cilk_fiber * fiber, __cilkrts_worker * owner)  {
  fiber->owner = owner;
}

void sysdep_save_fp_ctrl_state(__cilkrts_stack_frame *sf); 
char* sysdep_reset_jump_buffers_for_resume(cilk_fiber* fiber,
                                           __cilkrts_stack_frame *sf);

__attribute__((noreturn)) 
void sysdep_longjmp_to_sf(__cilkrts_stack_frame *sf);

__attribute__((noreturn))
void init_fiber_run(cilk_fiber * fiber, __cilkrts_stack_frame *sf); 

cilk_fiber * cilk_main_fiber_allocate(); 
cilk_fiber * cilk_fiber_allocate(__cilkrts_worker *w);
void cilk_fiber_deallocate(cilk_fiber * fiber);
void cilk_main_fiber_deallocate(cilk_fiber * fiber);

#endif
