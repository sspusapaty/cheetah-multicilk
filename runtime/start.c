#include "cilk.h"
#include "invoke_main.h"
#include "membar.h"

#include <stdio.h>




void main_thread_init(global_state * g) {
  __cilkrts_alert(ALERT_BOOT, "[M]: (main_thread_init) Setting up main thread's closure.\n");

  g->invoke_main = create_invoke_main(g);
  // ANGE: the order here is important!
  Cilk_membar_StoreStore();
  g->start = 1;
}

void threads_join(global_state * g) {
  for (int i = 0; i < g->active_size; i++) {
    void * ret;
    
    int status = pthread_join(g->threads[i], &ret);

    if (status != 0)
      __cilkrts_bug("Cilk runtime error: thread join (%d) failed: %d\n", i, status);
    }
  __cilkrts_alert(ALERT_BOOT, "[M]: (threads_join) All workers joined!\n");
}

void __cilkrts_run(global_state * g) {
  main_thread_init(g);
  
  // Sleeping
  threads_join(g);
}
