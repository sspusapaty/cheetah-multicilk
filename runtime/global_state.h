#ifndef _WORKER_H
#define _WORKER_H

// Includes


// Forward declaration
typedef struct __cilkrts_worker __cilkrts_worker;

struct global_state_t { /* COMMON_PORTABLE */

    /* Fields described as "(fixed)" should not be changed after
     * initialization.
     */

    uint16_t version;   ///< Version of this structure (fixed)

    int workers_running; ///< True when system workers have beens started */

    /// System-dependent part of the global state
    struct global_sysdep_state *sysdep;

    /// Array of worker structures.
    __cilkrts_worker **workers;

    /// USER SETTING: Per-worker fiber pool size
    int fiber_pool_size; 

    /// USER SETTING: Global fiber pool size
    int global_fiber_pool_size;

    /**
     * @brief TRUE when workers should exit scheduling loop so we can
     * shut down the runtime and free the global state.
     *
     * @note @c work_done will be checked *FREQUENTLY* in the scheduling loop
     * by idle workers.  We need to ensure that it's not in a cache line which
     * may be invalidated by other cores.  The surrounding fields are either
     * constant after initialization or not used until shutdown (stats) so we
     * should be OK.
     */
    volatile int work_done;

    /**
     * @brief USER SETTING: Maximum number of stacks the runtime will
     * allocate (apart from those created by the OS when worker
     * threads are created).
     *
     * If max_stacks == 0,there is no pre-defined maximum.
     */
    unsigned max_stacks; 

    /// Size of each stack
    size_t stack_size;

    /// Global cache for per-worker memory
    struct __cilkrts_frame_cache frame_malloc;

    /// Global fiber pool
    cilk_fiber_pool fiber_pool;

    /**
     * @brief Buffer to force max_steal_failures to appear on a
     * different cache line from the previous member variables.
     *
     * This padding is needed because max_steal_failures is read
     * constantly and other modified values in the global state will
     * cause thrashing.
     */
    char cache_buf[64];

    /**
     * @brief Maximum number of times a thread should fail to steal
     * before checking if Cilk is shutting down.
     */
    unsigned int max_steal_failures;

    /// Pointer to scheduler entry point
    void (*scheduler)(__cilkrts_worker *w);

    /**
     * @brief Buffer to force P and Q to appear on a different cache
     * line from the previous member variables.
     */
    char cache_buf_2[64];

    int P;         ///< USER SETTING: number of system workers + 1 (fixed)
    int Q;         ///< Number of user threads currently bound to workers 
};

#endif
