#include "cilk_mutex.h"

void Cilk_mutex_init(Cilk_mutex *lock) {
#if USE_SPINLOCK
  pthread_spin_init(&(lock->posix), PTHREAD_PROCESS_PRIVATE);
#else
  pthread_mutex_init(&(lock->posix), NULL);
#endif
}

void Cilk_mutex_wait(Cilk_mutex *lock) {
#if USE_SPINLOCK
  pthread_spin_lock(&(lock->posix));
#else
  pthread_mutex_lock(&(lock->posix));
#endif
}

void Cilk_mutex_signal(Cilk_mutex *lock) {
#if USE_SPINLOCK
  pthread_spin_unlock(&(lock->posix));
#else
  pthread_mutex_unlock(&(lock->posix));
#endif
}

int Cilk_mutex_try(Cilk_mutex *lock) {
#if USE_SPINLOCK
  if (pthread_spin_trylock(&(lock->posix)) == 0) {
    return 1;
  } else {
    return 0;
  }
#else
  if (pthread_mutex_trylock(&(lock->posix)) == 0) {
    return 1;
  } else {
    return 0;
  }
#endif
}

void Cilk_mutex_destroy(Cilk_mutex *lock) {
#if USE_SPINLOCK
  pthread_spin_destroy(&(lock->posix));
#else
  pthread_mutex_destroy(&(lock->posix));
#endif
}
