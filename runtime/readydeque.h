#ifndef _READYDEQUE_H
#define _READYDEQUE_H

// Forward declaration
typedef struct ReadyDeque ReadyDeque;

// Includes
#include "cilk_mutex.h"
#include "closure.h"

// Actual declaration
struct ReadyDeque {
     Cilk_mutex mutex;
     Cilk_mutex steal_mutex;
     WHEN_CILK_DEBUG(int mutex_owner;)
     WHEN_CILK_DEBUG(int steal_mutex_owner;)
     Closure *top, *bottom;
     CILK_CACHE_LINE_PAD;
};
#endif
