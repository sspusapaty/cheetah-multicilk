#include <stdio.h> 
#include <stdlib.h> 

#include "cilk-internal.h"
#include "debug.h"
#include "fiber.h"
#include "mutex.h"

// Whent the pool becomes full (empty), free (allocate) this fraction 
// of the pool back to (from) parent / the OS.
#define BATCH_FRACTION 4
#define GLOBAL_POOL_RATIO 4 // make global pool this much larger


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
static void fiber_pool_stat_init(__cilkrts_worker *w, 
                                 struct cilk_fiber_pool *pool) {
    pool->stats.num_allocated = 0;
    pool->stats.num_deallocated = 0;
    pool->stats.max_num_free = 0;
    pool->stats.batch_allocated = 0;
    pool->stats.batch_deallocated = 0;
}

static void fiber_pool_stat_print(__cilkrts_worker *w, 
                                  struct cilk_fiber_pool *pool) {
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
static void fiber_pool_allocate_batch(__cilkrts_worker *w,
                                      struct cilk_fiber_pool *pool, 
                                      int num_to_allocate); 
static void fiber_pool_free_batch(__cilkrts_worker *w,
                                  struct cilk_fiber_pool *pool,
                                  int num_to_free); 

/* Helper function for initializing fiber pool */
static void fiber_pool_init(struct cilk_fiber_pool *pool, 
                            int64_t stacksize,
                            int bufsize,
                            struct cilk_fiber_pool *parent,
                            int is_shared) {
    if(is_shared) {
        pool->lock = malloc(sizeof(*pool->lock));
        cilk_mutex_init(pool->lock);
        WHEN_CILK_DEBUG(pool->mutex_owner = NOBODY);
    } else { 
        pool->lock = NULL;
    }
    pool->stack_size = stacksize;
    pool->parent = parent;
    pool->max_size = bufsize;
    pool->fibers = malloc(bufsize * sizeof(*pool->fibers));
}

/* Helper function for destroying fiber pool */
static void fiber_pool_destroy(struct cilk_fiber_pool *pool) {

    if(pool->lock) {
        CILK_ASSERT_G(pool->mutex_owner == NOBODY);
        cilk_mutex_destroy(pool->lock); 
        free(pool->lock);
        pool->lock = NULL;
    }
    free(pool->fibers);
    pool->parent = NULL;
    pool->fibers = NULL;
}

static inline void fiber_pool_assert_ownership(__cilkrts_worker *w, 
                                               struct cilk_fiber_pool *pool) {
    CILK_ASSERT(w, pool->lock == NULL || pool->mutex_owner == w->self);
}

static inline void fiber_pool_assert_alienation(__cilkrts_worker *w, 
                                                struct cilk_fiber_pool *pool) {
    CILK_ASSERT(w, pool->lock == NULL || pool->mutex_owner != w->self);
}

static inline void fiber_pool_lock(__cilkrts_worker *w, 
                                   struct cilk_fiber_pool *pool) {
    if(pool->lock) {
        fiber_pool_assert_alienation(w, pool);
        cilk_mutex_lock(pool->lock); 
        WHEN_CILK_DEBUG(pool->mutex_owner = w->self);
    }
}

static inline void fiber_pool_unlock(__cilkrts_worker *w, 
                                     struct cilk_fiber_pool *pool) {
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
static void fiber_pool_increase_capacity(__cilkrts_worker *w, 
                                         struct cilk_fiber_pool *pool,
                                         int new_size) {

    fiber_pool_assert_ownership(w, pool);
    if(pool->max_size < new_size) {
        pool->fibers = realloc(pool->fibers, new_size * sizeof(*pool->fibers));
        pool->max_size = new_size;
    }
}

/**
 * Decrease the buffer size for the free fibers.  If the current size is
 * already smaller than the new size, do nothing.  Assume lock acquired upon
 * entry.
 */
__attribute__((unused)) // unused for now
static void fiber_pool_decrease_capacity(__cilkrts_worker *w, 
                                         struct cilk_fiber_pool *pool,
                                         int new_size) {

    fiber_pool_assert_ownership(w, pool);

    if(pool->size > new_size) {
        int diff = pool->size - new_size;
        fiber_pool_free_batch(w, pool, diff);
        CILK_ASSERT(w, pool->size == new_size);
    }
    if(pool->max_size > new_size) {
        pool->fibers = (struct cilk_fiber **)
            realloc(pool->fibers, new_size * sizeof(struct cilk_fiber *));
        pool->max_size = new_size;
    }
}

/**
 * Allocate num_to_allocate number of new fibers into the pool.
 * We will first look into the parent pool, and if the parent pool does not 
 * have enough, we then get it from the system.
 */ 
static void fiber_pool_allocate_batch(__cilkrts_worker *w,
                                      struct cilk_fiber_pool *pool, 
                                      int num_to_allocate) {
    fiber_pool_assert_ownership(w, pool);
    fiber_pool_increase_capacity(w, pool, num_to_allocate + pool->size);

    if(pool->parent) { // free into the parent within its buffer capacity
        struct cilk_fiber_pool *parent = pool->parent;
        fiber_pool_lock(w, parent);
        int from_parent = (parent->size <= num_to_allocate ? 
                                    parent->size : num_to_allocate);
        for(int i=0; i < from_parent; i++) {
            pool->fibers[pool->size++] = parent->fibers[--parent->size];
        }
#if FIBER_STATS
        parent->stats.num_allocated += from_parent;
#endif
        fiber_pool_unlock(w, parent);
        num_to_allocate -= from_parent;
    }
    if(num_to_allocate) { // if still need more
        for(int i=0; i < num_to_allocate; i++) {
            pool->fibers[pool->size++] = cilk_fiber_allocate(w);
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
static void fiber_pool_free_batch(__cilkrts_worker *w,
                                  struct cilk_fiber_pool *pool,
                                  int num_to_free) {

    fiber_pool_assert_ownership(w, pool);
    CILK_ASSERT(w, num_to_free <= pool->size);

    if(pool->parent) { // first try to free into the parent
        struct cilk_fiber_pool *parent = pool->parent;
        fiber_pool_lock(w, parent);
        int to_parent = (num_to_free <= (parent->max_size - parent->size)) ?
                num_to_free : (parent->max_size - parent->size);
        // free what we can within the capacity of the parent pool
        for(int i=0; i < to_parent; i++) {
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
        for(int i=0; i < num_to_free; i++) {
            struct cilk_fiber *fiber = pool->fibers[--pool->size];
            cilk_fiber_deallocate(w, fiber);
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
void cilk_fiber_pool_global_init(global_state *g) {

    int bufsize = GLOBAL_POOL_RATIO * 
                  (g->options.max_num_fibers / g->options.nproc);
    struct cilk_fiber_pool *pool = &(g->fiber_pool);
    fiber_pool_init(pool, g->options.stacksize, bufsize, NULL, 1/*shared*/);
    CILK_ASSERT_G(NULL != pool->fibers);
    fiber_pool_stat_init(NULL, pool);
    /* let's not preallocate for global fiber pool for now */
}
 
/* Global fiber pool clean up. */
void cilk_fiber_pool_global_destroy(global_state *g) {

    struct cilk_fiber_pool *pool = &(g->fiber_pool);
    CILK_ASSERT_G(pool->size == 0); // worker 0 should have freed everything
    fiber_pool_destroy(pool);
}

/**
 * Per-worker fiber pool initialization: should be called per worker so  
 * so that fiber comes from the core on which the worker is running on.
 */
void cilk_fiber_pool_per_worker_init(__cilkrts_worker *w) {

    int bufsize = w->g->options.max_num_fibers / w->g->options.nproc;
    struct cilk_fiber_pool *pool = &(w->l->fiber_pool);
    fiber_pool_init(pool, w->g->options.stacksize, 
                    bufsize, &(w->g->fiber_pool), 0/* private */);
    CILK_ASSERT(w, NULL != pool->fibers);
    CILK_ASSERT(w, w->g->fiber_pool.stack_size == pool->stack_size);

    fiber_pool_allocate_batch(w, pool, bufsize/BATCH_FRACTION);
    fiber_pool_stat_init(w, pool);
}
 
/* Per-worker fiber pool clean up. */
void cilk_fiber_pool_per_worker_destroy(__cilkrts_worker *w) {

    struct cilk_fiber_pool *pool = &(w->l->fiber_pool);
    struct cilk_fiber_pool *parent = &(w->g->fiber_pool);
    fiber_pool_stat_print(w, pool);
    for(int i=0; i < pool->size; i++) {
        cilk_fiber_deallocate(w, pool->fibers[i]);
    }
    fiber_pool_destroy(pool);

    // ANGE FIXME: is there a better way to do this?
    // worker 0 responsible for freeing fibers in global pool
    // need to do this here, since we can't free fibers into internal malloc
    // without having a worker pointer 
    if(w->self == 0) {
        fiber_pool_stat_print(NULL, parent);
        fiber_pool_lock(w, parent);
        fiber_pool_free_batch(w, parent, parent->size);
        fiber_pool_unlock(w, parent);
    }
}

/**
 * Allocate a fiber from this pool; if this pool is empty, 
 * allocate a batch of fibers from the parent pool (or system). 
 */
struct cilk_fiber * cilk_fiber_allocate_from_pool(__cilkrts_worker *w) {
    struct cilk_fiber_pool *pool = &(w->l->fiber_pool);
    if(pool->size == 0) {
        fiber_pool_allocate_batch(w, pool, pool->max_size/BATCH_FRACTION);
    }
    CILK_ASSERT(w, pool->size > 0);
    struct cilk_fiber *ret = pool->fibers[--pool->size];
#if FIBER_STATS
    pool->stats.num_allocated++;
#endif
    CILK_ASSERT(w, ret);
    return ret;
}

/**
 * Free fiber_to_return into this pool; if this pool is full, 
 * free a batch of fibers back into the parent pool (or system). 
 */
void cilk_fiber_deallocate_to_pool(__cilkrts_worker *w,
                                   struct cilk_fiber *fiber_to_return) {
    struct cilk_fiber_pool *pool = &(w->l->fiber_pool);
    if(pool->size == pool->max_size) {
        fiber_pool_free_batch(w, pool, pool->max_size/BATCH_FRACTION);
        CILK_ASSERT(w, 
                (pool->max_size - pool->size) >= (pool->max_size/BATCH_FRACTION));
    }
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
