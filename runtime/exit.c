#include "cilk.h"
#include <pthread.h>


void threads_join(global_state * g) {
  for (int i = 0; i < g->active_size - 1; i++) {
    void * ret;
    
    int status = pthread_join(g->threads[i], &ret);

    if (status != 0)
      __cilkrts_bug("Cilk runtime error: thread join (%d) failed: %d\n", i, status);
    }
}

void __cilkrts_exit(global_state * g) {
  threads_join(g);
}
