#ifndef _CILK_MUTEX_H
#define _CILK_MUTEX_H

// Includes
#include <pthread.h>

// Forward declaration
typedef union Cilk_mutex Cilk_mutex;

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

#endif
