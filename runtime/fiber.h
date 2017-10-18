#ifndef _FIBER_H
#define _FIBER_H

typedef struct cilk_fiber cilk_fiber;

#include "jmpbuf.h"
#include "worker.h"

// Boolean flags capturing the status of the fiber.
// Each one can be set independently.
// A default fiber is constructed with a flag value of 0.
static const int RESUMABLE             = 0x01;  ///< True if the fiber is in a suspended state and can be resumed.
static const int ALLOCATED_FROM_THREAD = 0x02;  ///< True if fiber was allocated from a thread.

typedef void (*cilk_fiber_proc)(cilk_fiber*);

/*
struct cilk_fiber_pool
{
    Cilk_mutex*      lock;       ///< Mutual exclusion for pool operations 
    size_t   stack_size; ///< Size of stacks for fibers in this pool.
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

struct cilk_fiber
{
  size_t stack_size;       /**< Size of stack for fiber    */
  
  char * m_stack;
  char * m_stack_base;
  
  jmpbuf ctx;
  
  __cilkrts_worker * owner;            /**< Worker using this fiber    */
  __cilkrts_stack_frame * resume_sf;        /**< Stack frame to resume      */
  
  cilk_fiber_proc m_start_proc;        ///< Function to run on start up/reset
  cilk_fiber_proc m_post_switch_proc;  ///< Function that executes when we first switch to a new fiber from a different one.

  cilk_fiber * m_pending_remove_ref;   ///< Fiber to possibly delete on start up or resume
  //cilk_fiber_pool * m_pending_pool;    ///< Pool where m_pending_remove_ref should go if it is deleted.
  unsigned m_flags;             ///< Captures the status of this fiber. 

  volatile long    m_outstanding_references;  ///< Counts references to this fiber.
};

/** @brief Returns true if this fiber is resumable.
 *
 * A fiber is considered resumable when it is not currently being
 * executed.
 */
static inline int cilk_fiber_is_resumable(cilk_fiber * fiber) {
  return (fiber->m_flags & RESUMABLE);
}
    
/** @brief Returns true if fiber was allocated from a thread. */   
static inline int cilk_fiber_is_allocated_from_thread(cilk_fiber * fiber) {
  return (fiber->m_flags & ALLOCATED_FROM_THREAD);
}

static inline void cilk_fiber_set_owner(cilk_fiber * fiber, __cilkrts_worker * owner)  {
  fiber->owner = owner;
}

/** @brief Set the proc method to execute immediately after a switch
 * to this fiber.
 */
static inline void cilk_fiber_set_post_switch_proc(cilk_fiber * fiber, cilk_fiber_proc post_switch_proc) {
  fiber->m_post_switch_proc = post_switch_proc;
}

static inline void cilk_fiber_reset_state(cilk_fiber * fiber, cilk_fiber_proc start_proc) {
  // Setup the fiber and return.
  fiber->m_start_proc = start_proc;
}
    // For Windows, updates to the fiber reference count need to be
    // atomic, because exceptions can live on a stack that we are not
    // currently executing on.  Thus, we can update the reference
    // count of a fiber we are not currently executing on.

static inline void cilk_fiber_inc_ref_count(cilk_fiber * fiber) {
  __sync_add_and_fetch(&(fiber->m_outstanding_references), 1);
}

static inline long cilk_fiber_dec_ref_count(cilk_fiber * fiber) {
  return __sync_add_and_fetch(&(fiber->m_outstanding_references), -1);
}

static inline long cilk_fiber_sub_from_ref_count(cilk_fiber * fiber, long v) {
  return __sync_add_and_fetch(&(fiber->m_outstanding_references), -v);
}

cilk_fiber * cilk_fiber_allocate_from_thread();

cilk_fiber * cilk_fiber_allocate_from_heap();

int cilk_fiber_deallocate_from_thread(cilk_fiber * fiber);

int cilk_fiber_deallocate_to_heap(cilk_fiber * fiber);

int cilk_fiber_remove_reference(cilk_fiber * fiber);

void cilk_fiber_do_post_switch_actions(cilk_fiber * self);

void cilk_fiber_suspend_self_and_resume_other(cilk_fiber * self, cilk_fiber * other);

void cilk_fiber_remove_reference_from_self_and_resume_other(cilk_fiber * self, cilk_fiber * other);

void cilk_fiber_run(cilk_fiber * fiber);

#endif
