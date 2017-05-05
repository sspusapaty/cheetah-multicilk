#ifndef _CILK_MUTEX_H
#define _CILK_MUTEX_H

// Forward declaration
typedef union Cilk_mutex Cilk_mutex;

// Includes
#include <pthread.h>

#define USE_SPINLOCK 0

#if USE_SPINLOCK
union Cilk_mutex {
     volatile int memory;
     pthread_spinlock_t posix;
};
#else
union Cilk_mutex {
     volatile int memory;
     pthread_mutex_t posix;
};
#endif

void Cilk_mutex_init(Cilk_mutex *lock);

void Cilk_mutex_wait(Cilk_mutex *lock);

void Cilk_mutex_signal(Cilk_mutex *lock);

int Cilk_mutex_try(Cilk_mutex *lock);

void Cilk_mutex_destroy(Cilk_mutex *lock);
#endif
