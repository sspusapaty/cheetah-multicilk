#ifndef _CILK_GLOBAL_H
#define _CILK_GLOBAL_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include <stdatomic.h> /* must follow stdbool.h */

#include "debug.h"
#include "fiber.h"
#include "internal-malloc.h"
#include "mutex.h"
#include "rts-config.h"
#include "sched_stats.h"
#include "types.h"

struct __cilkrts_worker;
struct reducer_id_manager;
struct Closure;

struct rts_options {
    int64_t stacksize;
    unsigned int nproc;
    int deqdepth;
    int fiber_pool_cap;
};

struct global_state {
    /* globally-visible options (read-only after init) */
    struct rts_options options;

    /* dynamically-allocated array of deques, one per processor */
    struct ReadyDeque *deques;
    struct __cilkrts_worker **workers;
    pthread_t *threads;
    struct Closure *invoke_main;

    struct cilk_fiber_pool fiber_pool __attribute__((aligned(CILK_CACHE_LINE)));
    struct global_im_pool im_pool __attribute__((aligned(CILK_CACHE_LINE)));
    struct cilk_im_desc im_desc __attribute__((aligned(CILK_CACHE_LINE)));
    cilk_mutex im_lock; // lock for accessing global im_desc

    uint32_t frame_magic;

    volatile bool invoke_main_initialized;
    volatile atomic_bool start;
    volatile atomic_bool done;
    volatile atomic_int cilk_main_return;

    cilk_mutex print_lock; // global lock for printing messages

    int cilk_main_argc;
    char **cilk_main_args;

    struct reducer_id_manager *id_manager; /* null while Cilk is running */

    struct global_sched_stats stats;
};

/* TODO: Make this thread local, so "global" state is really the
   state of one of possibly several instantiations of Cilk. */
CHEETAH_INTERNAL struct global_state *cilkrts_global_state;

/* Allocate state and initialized embedded locks. */
CHEETAH_INTERNAL global_state *global_state_allocate();
CHEETAH_INTERNAL void global_state_init(global_state *, int argc, char *argv[]);

#endif /* _CILK_GLOBAL_H */
