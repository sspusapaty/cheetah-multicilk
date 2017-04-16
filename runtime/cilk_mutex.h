#ifndef _CILK_MUTEX_H
#define _CILK_MUTEX_H

// Forward declaration
typedef union Cilk_mutex Cilk_mutex;

// Includes
#include <pthread.h>

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
