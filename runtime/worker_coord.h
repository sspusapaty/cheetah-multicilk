#ifndef _WORKER_COORD_H
#define _WORKER_COORD_H

// Routines for coordinating workers, specifically, putting workers to sleep and
// waking workers when execution enters and leaves cilkified regions.

#include <limits.h>

#ifdef __linux__
#include <errno.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "global.h"

#define USER_USE_FUTEX 1
#ifdef __linux__
#define USE_FUTEX USER_USE_FUTEX
#else
#define USE_FUTEX 0
#endif

#if USE_FUTEX
//=========================================================
// Primitive futex operations.
//=========================================================
#define errExit(msg)                                                           \
    do {                                                                       \
        perror(msg);                                                           \
        exit(EXIT_FAILURE);                                                    \
    } while (false)

// Convenience wrapper for futex syscall.
static inline long futex(_Atomic uint32_t *uaddr, int futex_op, uint32_t val,
                  const struct timespec *timeout, uint32_t *uaddr2,
                  uint32_t val3) {
    return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

// Wait for the futex pointed to by `futexp` to become 1.
static inline void fwait(_Atomic uint32_t *futexp) {
    // We don't worry about spurious wakeups here, since we ensure that all
    // calls to fwait are contained in their own loops that effectively check
    // for spurious wakeups.
    long s = futex(futexp, FUTEX_WAIT_PRIVATE, 0, NULL, NULL, 0);
    if (__builtin_expect(s == -1 && errno != EAGAIN, false))
        errExit("futex-FUTEX_WAIT");
}

// Set the futex pointed to by `futexp` to 1, and wake up 1 thread waiting on
// that futex.
static inline void fpost(_Atomic uint32_t *futexp) {
    atomic_store_explicit(futexp, 1, memory_order_release);
    long s = futex(futexp, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
    if (s == -1)
        errExit("futex-FUTEX_WAKE");
}

// Set the futex pointed to by `futexp` to 1, and wake up all threads waiting on
// that futex.
static inline void fbroadcast(_Atomic uint32_t *futexp) {
    atomic_store_explicit(futexp, 1, memory_order_release);
    long s = futex(futexp, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
    if (s == -1)
        errExit("futex-FUTEX_WAKE");
}

//=========================================================
// Operations to control worker behavior using futexes and corresponding flags.
//=========================================================

// Called by a worker thread.  Causes the worker thread to wait on the given
// flag-futex pair.
static inline void worker_wait(volatile atomic_bool *flag,
                        _Atomic uint32_t *flag_futex) {
    while (!atomic_load_explicit(flag, memory_order_acquire)) {
        fwait(flag_futex);
    }
}

// Start all workers waiting on the given flag-futex pair.
static inline void worker_start_broadcast(volatile atomic_bool *flag,
                                   _Atomic uint32_t *flag_futex) {
    atomic_store_explicit(flag, 1, memory_order_release);
    fbroadcast(flag_futex);
}

// Reset the given flag-futex pair, so that workers will eventually resume
// waiting on that flag-futex pair.
static inline void worker_clear_start(volatile atomic_bool *flag,
                               _Atomic uint32_t *flag_futex) {
    atomic_store_explicit(flag, 0, memory_order_relaxed);
    atomic_store_explicit(flag_futex, 0, memory_order_relaxed);
}
#else
//=========================================================
// Operations to control worker behavior using pthread condition variables and
// corresponding flags.
//=========================================================

// Called by a worker thread.  Causes the worker thread to wait on the given
// flag and associated mutex and condition variable.
static inline void worker_wait(volatile atomic_bool *flag,
                        pthread_mutex_t *flag_lock,
                        pthread_cond_t *flag_cond_var) {
    pthread_mutex_lock(flag_lock);
    while (!atomic_load_explicit(flag, memory_order_acquire)) {
        pthread_cond_wait(flag_cond_var, flag_lock);
    }
    pthread_mutex_unlock(flag_lock);
}

// Start all workers waiting on the given flag and associated mutex and
// condition variable.
static inline void worker_start_broadcast(volatile atomic_bool *flag,
                                   pthread_mutex_t *flag_lock,
                                   pthread_cond_t *flag_cond_var) {
    pthread_mutex_lock(flag_lock);
    atomic_store_explicit(flag, 1, memory_order_release);
    pthread_cond_broadcast(flag_cond_var);
    pthread_mutex_unlock(flag_lock);
}

// Reset given flag, so that workers will eventually resume waiting on that
// flag.
static inline void worker_clear_start(volatile atomic_bool *start) {
    atomic_store_explicit(start, 0, memory_order_relaxed);
}
#endif

//=========================================================
// Common internal interface for managing execution of workers.
//=========================================================

// Called by a thief thread.  Causes the thief thread to wait for a signal to
// start work-stealing.
static inline void thief_wait(global_state *g) {
#if USE_FUTEX
    worker_wait(&g->start_thieves, &g->start_thieves_futex);
#else
    worker_wait(&g->start_thieves, &g->start_thieves_lock,
                &g->start_thieves_cond_var);
#endif
}

// Called by a root-worker thread, that is, the worker w where w->self ==
// g->exiting_worker.  Causes the root-worker thread to wait for a signal to
// start work-stealing.
static inline void root_worker_wait(global_state *g, const uint32_t id) {
    _Atomic uint32_t *root_worker_p = &g->start_root_worker;
#if USE_FUTEX
    while (id == atomic_load_explicit(root_worker_p, memory_order_acquire)) {
        long s = futex(root_worker_p, FUTEX_WAIT_PRIVATE, id, NULL, NULL, 0);
        if (__builtin_expect(s == -1 && errno != EAGAIN, false))
            errExit("futex-FUTEX_WAIT");
    }
#else
    pthread_mutex_t *root_worker_lock = &g->start_root_worker_lock;
    pthread_mutex_lock(root_worker_lock);
    while (id == atomic_load_explicit(root_worker_p, memory_order_acquire)) {
        pthread_cond_wait(&g->start_root_worker_cond_var, root_worker_lock);
    }
    pthread_mutex_unlock(root_worker_lock);
#endif
}

// Signal the thief threads to start work-stealing (or terminate, if
// g->terminate == 1).
static inline void wake_thieves(global_state *g) {
#if USE_FUTEX
    worker_start_broadcast(&g->start_thieves, &g->start_thieves_futex);
#else
    worker_start_broadcast(&g->start_thieves, &g->start_thieves_lock,
                           &g->start_thieves_cond_var);
#endif
}

// Signal the root-worker thread to start work-stealing (or terminate, if
// g->terminate == 1).
static inline void wake_root_worker(global_state *g, uint32_t val) {
    _Atomic uint32_t *root_worker_p = &g->start_root_worker;
#if USE_FUTEX
    atomic_store_explicit(root_worker_p, val, memory_order_release);
    long s = futex(root_worker_p, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
    if (s == -1)
        errExit("futex-FUTEX_WAKE");
#else
    pthread_mutex_t *root_worker_lock = &g->start_root_worker_lock;
    pthread_mutex_lock(root_worker_lock);
    atomic_store_explicit(root_worker_p, val, memory_order_release);
    pthread_cond_signal(&g->start_root_worker_cond_var);
    pthread_mutex_unlock(root_worker_lock);
#endif
}

// Try to signal the root-worker thread to start work-stealing (or terminate, if
// g->terminate == 1).  If the current root-worker value is *old_val, then wake
// up the root worker.  Otherwise, just exit; in this latter case, another
// worker must have woken up the root worker already.
static inline void try_wake_root_worker(global_state *g, uint32_t *old_val,
                                        uint32_t new_val) {
    _Atomic uint32_t *root_worker_p = &g->start_root_worker;
#if USE_FUTEX
    if (atomic_compare_exchange_strong(root_worker_p, old_val, new_val)) {
        long s = futex(root_worker_p, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
        if (s == -1)
            errExit("futex-FUTEX_WAKE");
    }
#else
    pthread_mutex_t *root_worker_lock = &g->start_root_worker_lock;
    pthread_mutex_lock(root_worker_lock);
    if (*old_val == atomic_load_explicit(root_worker_p, memory_order_acquire)) {
        atomic_store_explicit(root_worker_p, new_val, memory_order_release);
        pthread_cond_signal(&g->start_root_worker_cond_var);
    }
    pthread_mutex_unlock(root_worker_lock);
#endif
}

// Routines to update global flags to prevent workers from re-entering the
// work-stealing loop.  Note that we don't wait for the workers to exit the
// work-stealing loop, since its more efficient to allow that to happen
// eventually.

// Reset global state to make thief threads sleep for signal to start
// work-stealing again.
static inline void sleep_thieves(global_state *g) {
#if USE_FUTEX
    worker_clear_start(&g->start_thieves, &g->start_thieves_futex);
#else
    worker_clear_start(&g->start_thieves);
#endif
}

// Routines to control the cilkified state.

static inline void set_cilkified(global_state *g) {
    // Set g->cilkified = 1, indicating that the execution is now cilkified.
    atomic_store_explicit(&g->cilkified, 1, memory_order_release);
#if USE_FUTEX
    atomic_store_explicit(&g->cilkified_futex, 0, memory_order_release);
#endif
}

// Mark the computation as no longer cilkified and signal the thread that
// originally cilkified the execution.
static inline void signal_uncilkified(global_state *g) {
#if USE_FUTEX
    atomic_store_explicit(&g->cilkified, 0, memory_order_release);
    fpost(&g->cilkified_futex);
#else
    pthread_mutex_lock(&(g->cilkified_lock));
    atomic_store_explicit(&g->cilkified, 0, memory_order_release);
    pthread_cond_signal(&g->cilkified_cond_var);
    pthread_mutex_unlock(&(g->cilkified_lock));
#endif
}

// Wait on g->cilkified to be set to 0, indicating the end of the Cilkified
// region.
static inline void wait_while_cilkified(global_state *g) {
#if USE_FUTEX
    while (atomic_load_explicit(&g->cilkified, memory_order_acquire)) {
        fwait(&g->cilkified_futex);
    }
#else
    // TODO: Convert pthread_mutex_lock, pthread_mutex_unlock, and
    // pthread_cond_wait to cilk_* equivalents.
    pthread_mutex_lock(&(g->cilkified_lock));

    // There may be a *very unlikely* scenario where the Cilk computation has
    // already been completed before even starting to wait.  In that case, do
    // not wait and continue directly.  Also handle spurious wakeups with a
    // 'while' instead of an 'if'.
    while (atomic_load_explicit(&g->cilkified, memory_order_acquire)) {
        pthread_cond_wait(&(g->cilkified_cond_var), &(g->cilkified_lock));
    }

    pthread_mutex_unlock(&(g->cilkified_lock));
#endif
}


#endif /* _WORKER_COORD_H */
