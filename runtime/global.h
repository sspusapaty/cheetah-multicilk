#ifndef _CILK_GLOBAL_H
#define _CILK_GLOBAL_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include <stdatomic.h> /* must follow stdbool.h */

#include "debug.h"
#include "fiber.h"
#include "internal-malloc-impl.h"
#include "mutex.h"
#include "rts-config.h"
#include "sched_stats.h"
#include "types.h"

struct __cilkrts_worker;
struct reducer_id_manager;
struct Closure;

// clang-format off
#define DEFAULT_OPTIONS                                            \
    {                                                              \
        DEFAULT_STACK_SIZE,     /* stack size to use for fiber */  \
        DEFAULT_NPROC,          /* num of workers to create */     \
        DEFAULT_REDUCER_LIMIT,  /* num of simultaneous reducers */ \
        DEFAULT_DEQ_DEPTH,      /* num of entries in deque */      \
        DEFAULT_FIBER_POOL_CAP, /* alloc_batch_size */             \
        DEFAULT_FORCE_REDUCE,   /* whether to force self steal and reduce */\
    }
// clang-format on

struct rts_options {
    size_t stacksize;
    unsigned int nproc;
    unsigned int reducer_cap;
    unsigned int deqdepth;
    unsigned int fiber_pool_cap;
    unsigned int force_reduce;
};

struct global_state {
    /* globally-visible options (read-only after init) */
    struct rts_options options;

    unsigned int nworkers; /* size of next 3 arrays */
    struct __cilkrts_worker **workers;
    /* dynamically-allocated array of deques, one per processor */
    struct ReadyDeque *deques;
    pthread_t *threads;
    struct Closure *invoke_main;

    struct cilk_fiber_pool fiber_pool __attribute__((aligned(CILK_CACHE_LINE)));
    struct global_im_pool im_pool __attribute__((aligned(CILK_CACHE_LINE)));
    struct cilk_im_desc im_desc __attribute__((aligned(CILK_CACHE_LINE)));
    cilk_mutex im_lock; // lock for accessing global im_desc

    volatile bool invoke_main_initialized;
    volatile atomic_bool start;
    volatile atomic_bool done;
    volatile atomic_int cilk_main_return;
    volatile atomic_uint reducer_map_count;

    cilk_mutex print_lock; // global lock for printing messages

    int cilk_main_argc;
    char **cilk_main_args;

    struct reducer_id_manager *id_manager; /* null while Cilk is running */

    struct global_sched_stats stats;
};

CHEETAH_INTERNAL global_state *global_state_init(int argc, char *argv[]);

CHEETAH_INTERNAL void for_each_worker(global_state *,
                                      void (*)(__cilkrts_worker *, void *),
                                      void *data);
CHEETAH_INTERNAL void for_each_worker_rev(global_state *,
                                          void (*)(__cilkrts_worker *, void *),
                                          void *data);

#endif /* _CILK_GLOBAL_H */
