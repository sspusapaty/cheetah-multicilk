#include <stdio.h> 
#include <stdlib.h> 

#include "debug.h"
#include "fiber.h"
#include "mutex.h"

// Whent the pool becomes full (empty), free (allocate) this fraction 
// of the pool back to (from) parent / the OS.
#define BATCH_FRACTION 4


//=========================================================================
// Currently the fiber pools are organized into two-levels, like in Hoard 
// --- per-worker private pool plus a global pool.  The per-worker private
// pool are accessed by the owner worker only and thus do not require 
// synchronization.  The global pool may be accessed concurrently and thus
// require synchronization.  Thus, the pool->lock is initialized to NULL for 
// per-worker pools and cilk_mutex for the global one.  
// 
// The per-worker pools are initlaized with some free fibers preallocated
// already and the global one starts out empty.  A worker typically acquires 
// and free fibers from / to the its per-worker pool but only allocate / free 
// batches from / to the global parent pool when necessary (i.e., buffer 
// exceeds capacity and there are fibers needed to be freed, or need fibers
// but the buffer is empty.
//
// For now, we don't ever allocate fibers into the global one --- we only use 
// the global one to load balance between per-worker pools.
//=========================================================================

//=========================================================
// Private helper functions for maintaining pool stats
//=========================================================

#if FIBER_STATS
static void fiber_pool_stat_init(__cilkrts_worker *const w, 
                                 cilk_fiber_pool *pool) {
    pool->stats.num_allocated = 0;
    pool->stats.num_deallocated = 0;
    pool->stats.max_num_free = 0;
    pool->stats.batch_allocated = 0;
    pool->stats.batch_deallocated = 0;
}

static void fiber_pool_stat_print(__cilkrts_worker *const w, 
                                  cilk_fiber_pool *pool) {
    if(w) {
        fprintf(stderr, 
            "Worker %2d fiber stats (a/d/max-free/curr-free/a-batch/d-batch): ", w->self);
    } else {
        fprintf(stderr, 
            "Global     fiber stats (a/d/max-free/curr-free/a-batch/d-batch): ");
    }
    fprintf(stderr, "%u, %u, %u, %u, %u, %u\n",
            pool->stats.num_allocated,
            pool->stats.num_deallocated,
            pool->stats.max_num_free,
            pool->size,
            pool->stats.batch_allocated,
            pool->stats.batch_deallocated);
}

#else 
#define fiber_pool_stat_init(w, pool)
#define fiber_pool_stat_print(w, pool)
#endif // FIBER_STATS

//=========================================================
// Private helper functions 
//=========================================================

// forward decl
static void fiber_pool_allocate_batch(__cilkrts_worker *const w,
                                      cilk_fiber_pool *pool, 
                                      unsigned int num_to_allocate); 
static void fiber_pool_free_batch(__cilkrts_worker *const w,
                                  cilk_fiber_pool *pool,
                                  unsigned int num_to_free); 

/* Helper function for initializing fiber pool */
static void fiber_pool_init(cilk_fiber_pool *pool, 
                            size_t stack_size,
                            unsigned int buffer_size,
                            cilk_fiber_pool *parent,
                            int is_shared) {
    pool->lock = (cilk_mutex *) malloc(sizeof(cilk_mutex));
    if(is_shared) { cilk_mutex_init(pool->lock); }
    else { pool->lock = NULL; }
    pool->stack_size = stack_size;
    pool->parent = parent;
    pool->max_size = buffer_size;
    pool->fibers = (cilk_fiber**) malloc(buffer_size * sizeof(cilk_fiber*));
}

/* Helper function for destroying fiber pool */
static void fiber_pool_destroy(cilk_fiber_pool *pool) {

    for(unsigned int i=0; i < pool->size; i++) {
        cilk_fiber_deallocate(pool->fibers[i]);
    }
    if(pool->lock) {
        cilk_mutex_unlock(pool->lock); 
        cilk_mutex_destroy(pool->lock); 
    }
    free(pool->fibers);
    pool->parent = NULL;
    pool->fibers = NULL;
}

static inline void fiber_pool_assert_ownership(__cilkrts_worker *const w, 
                                               cilk_fiber_pool *pool) {
    CILK_ASSERT(w, pool->lock == NULL || pool->mutex_owner == w->self);
}

static inline void fiber_pool_assert_alienation(__cilkrts_worker *const w, 
                                                cilk_fiber_pool *pool) {
    CILK_ASSERT(w, pool->lock == NULL || pool->mutex_owner != w->self);
}

static inline void fiber_pool_lock(__cilkrts_worker *const w, 
                                   cilk_fiber_pool *pool) {
    if(pool->lock) {
        fiber_pool_assert_alienation(w, pool);
        cilk_mutex_lock(pool->lock); 
        WHEN_CILK_DEBUG(pool->mutex_owner = w->self);
    }
}

static inline void fiber_pool_unlock(__cilkrts_worker *const w, 
                                     cilk_fiber_pool *pool) {
    if(pool->lock) {
        fiber_pool_assert_ownership(w, pool);
        WHEN_CILK_DEBUG(pool->mutex_owner = -1);
        cilk_mutex_unlock(pool->lock);
    }
}

/**
 * Increase the buffer size for the free fibers.  If the current size is
 * already larger than the new size, do nothing.  Assume lock acquired upon
 * entry.
 */
static void fiber_pool_increase_capacity(__cilkrts_worker *const w, 
                                         cilk_fiber_pool *pool,
                                         unsigned new_size) {

    fiber_pool_assert_ownership(w, pool);

    if(pool->max_size < new_size) {
        cilk_fiber **fibers =  (cilk_fiber**) malloc(new_size * sizeof(cilk_fiber*));
        for(unsigned int i = 0; i < pool->size; i++) { // copy over free fibers
            fibers[i] = pool->fibers[i];
        }
        free(pool->fibers);
        pool->fibers = fibers;
        pool->max_size = new_size;
    }
}

/**
 * Decrease the buffer size for the free fibers.  If the current size is
 * already smaller than the new size, do nothing.  Assume lock acquired upon
 * entry.
 */
__attribute__((unused)) // unused for now
static void fiber_pool_decrease_capacity(__cilkrts_worker *const w, 
                                         cilk_fiber_pool *pool,
                                         unsigned new_size) {

    fiber_pool_assert_ownership(w, pool);

    if(pool->size > new_size) {
        unsigned int diff = pool->size - new_size;
        fiber_pool_free_batch(w, pool, diff);
        CILK_ASSERT(w, pool->size == new_size);
    }
    if(pool->max_size > new_size) {
        pool->fibers = (cilk_fiber **)
            realloc(pool->fibers, new_size * sizeof(cilk_fiber*));
        pool->max_size = new_size;
    }
}

/**
 * Allocate num_to_allocate number of new fibers into the pool.
 * We will first look into the parent pool, and if the parent pool does not 
 * have enough, we then get it from the system.
 */ 
static void fiber_pool_allocate_batch(__cilkrts_worker *const w,
                                      cilk_fiber_pool *pool, 
                                      unsigned int num_to_allocate) {
    fiber_pool_assert_ownership(w, pool);
    fiber_pool_increase_capacity(w, pool, num_to_allocate + pool->size);

    if(pool->parent) { // free into the parent within its buffer capacity
        cilk_fiber_pool *parent = pool->parent;
        fiber_pool_lock(w, parent);
        unsigned int from_parent = (parent->size <= num_to_allocate ? 
                                    parent->size : num_to_allocate);
        for(unsigned int i=0; i < from_parent; i++) {
            pool->fibers[pool->size++] = parent->fibers[--parent->size];
        }
#if FIBER_STATS
        parent->stats.num_allocated += from_parent;
#endif
        fiber_pool_unlock(w, parent);
        num_to_allocate -= from_parent;
    }
    if(num_to_allocate) { // if still need more
        for(unsigned int i=0; i < num_to_allocate; i++) {
            pool->fibers[pool->size++] = cilk_fiber_allocate();
        }
    }
#if FIBER_STATS
    pool->stats.batch_allocated++;
    if(pool->size > pool->stats.max_num_free) {
         pool->stats.max_num_free = pool->size;
    }
#endif
}

/**
 * Free num_to_free fibers from this pool back to either the parent 
 * or the system.
 */
static void fiber_pool_free_batch(__cilkrts_worker *const w,
                                  cilk_fiber_pool *pool,
                                  unsigned int num_to_free) {

    fiber_pool_assert_ownership(w, pool);
    CILK_ASSERT(w, num_to_free <= pool->size);

    if(pool->parent) { // first try to free into the parent
        cilk_fiber_pool *parent = pool->parent;
        fiber_pool_lock(w, parent);
        unsigned int to_parent =
            (num_to_free <= (parent->max_size - parent->size)) ?
                num_to_free : (parent->max_size - parent->size);
        // free what we can within the capacity of the parent pool
        for(unsigned int i=0; i < to_parent; i++) {
            parent->fibers[parent->size++] = pool->fibers[--pool->size];
        }
        CILK_ASSERT(w, parent->size <= parent->max_size);
#if FIBER_STATS
    parent->stats.num_deallocated += to_parent;
    if(parent->size > parent->stats.max_num_free) {
         parent->stats.max_num_free = parent->size;
    }
#endif
        fiber_pool_unlock(w, parent);
        num_to_free -= to_parent;
    }
    if(num_to_free > 0) {
        for(unsigned int i=0; i < num_to_free; i++) {
            cilk_fiber *fiber = pool->fibers[--pool->size];
            cilk_fiber_deallocate(fiber);
        }
    }
    CILK_ASSERT(w, pool->size >= 0);
#if FIBER_STATS
    pool->stats.batch_deallocated++;
#endif
}


//=========================================================
// Supported public functions 
//=========================================================

/* Global fiber pool initialization: */
void cilk_fiber_pool_global_init(global_state *g,
                                 unsigned buffer_size) {

    cilk_fiber_pool *pool = (cilk_fiber_pool *) malloc(sizeof(cilk_fiber_pool));
    fiber_pool_init(pool, g->options.stacksize, buffer_size, NULL, 1/*shared*/);
    CILK_ASSERT(w, NULL != pool->fibers);
    fiber_pool_stat_init(NULL, pool);
    /* let's not preallocate for global fiber pool for now */
    g->fiber_pool = pool;
}
 
/* Global fiber pool clean up. */
void cilk_fiber_pool_global_destroy(global_state *g) {

    cilk_fiber_pool *pool = g->fiber_pool;
    fiber_pool_stat_print(NULL, pool);
    fiber_pool_destroy(pool);
    free(pool);
    g->fiber_pool = NULL;
}

/**
 * Per-worker fiber pool initialization: should be called per worker so  
 * so that fiber comes from the core on which the worker is running on.
 */
void cilk_fiber_pool_per_worker_init(__cilkrts_worker *const w,
                                     unsigned buffer_size,
                                     int is_shared) {

    cilk_fiber_pool *pool = (cilk_fiber_pool *) malloc(sizeof(cilk_fiber_pool));
    fiber_pool_init(pool, w->g->options.stacksize, 
                    buffer_size, w->g->fiber_pool, 0/* private */);
    CILK_ASSERT(w, NULL != pool->fibers);
    CILK_ASSERT(w, global_pool->stack_size == pool->stack_size);

    fiber_pool_allocate_batch(w, pool, buffer_size/BATCH_FRACTION);
    fiber_pool_stat_init(w, pool);
    w->l->fiber_pool = pool;
}
 
/* Per-worker fiber pool clean up. */
void cilk_fiber_pool_per_worker_destroy(__cilkrts_worker *const w) {

    cilk_fiber_pool *pool = w->l->fiber_pool;
    fiber_pool_stat_print(w, pool);
    fiber_pool_destroy(pool);
    free(pool);
    w->l->fiber_pool = NULL;
}

/**
 * Allocate a fiber from this pool; if this pool is empty, 
 * allocate a batch of fibers from the parent pool (or system). 
 */
cilk_fiber * cilk_fiber_pool_allocate(__cilkrts_worker *const w) {
    cilk_fiber_pool *pool = w->l->fiber_pool;
    if(pool->size == 0) {
        fiber_pool_allocate_batch(w, pool, pool->max_size/BATCH_FRACTION);
    }
    cilk_fiber *ret = pool->fibers[--pool->size];
#if FIBER_STATS
    pool->stats.num_allocated++;
#endif
    return ret;
}

/**
 * Free fiber_to_return into this pool; if this pool is full, 
 * free a batch of fibers back into the parent pool (or system). 
 */
void cilk_fiber_pool_deallocate(__cilkrts_worker *const w,
                                cilk_fiber *fiber_to_return) {
    cilk_fiber_pool *pool = w->l->fiber_pool;
    if(pool->size == pool->max_size) {
        fiber_pool_free_batch(w, pool, pool->max_size/BATCH_FRACTION);
    }
    CILK_ASSERT(w, pool->size <= (pool->max_size/BATCH_FRACTION));
    if(fiber_to_return) {
        pool->fibers[pool->size++] = fiber_to_return;
#if FIBER_STATS
    pool->stats.num_deallocated++;
    if(pool->size > pool->stats.max_num_free) {
         pool->stats.max_num_free = pool->size;
    }
#endif
    }
}
