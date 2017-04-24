#include "sched.h"
#include "jmpbuf.h"
#include "tls.h"

#include <stdio.h>

void longjmp_to_runtime(__cilkrts_worker * w) {
  __cilkrts_alert("Thread of worker %d: longjmp_to_runtime\n", w->self);

  if (__builtin_expect(w->l->runtime_fiber != NULL, 1)) {
    longjmp(w->l->runtime_fiber->ctx, 0);
  } else {
    w->l->runtime_fiber = cilk_fiber_allocate_from_heap();
    cilk_fiber_set_owner(w->l->runtime_fiber, w);

    
    char * rsp = NULL;
    ASM_GET_SP(rsp);
    ASM_SET_SP(w->l->runtime_fiber->m_stack_base);
    worker_scheduler(w);
    ASM_SET_SP(rsp);
    __cilkrts_alert("Thread of worker %d: exit longjmp_to_runtime\n", w->self);
   }
}
  

void worker_scheduler(__cilkrts_worker * w) {
  __cilkrts_alert("Thread of worker %d: worker_scheduler\n", w->self);
  
  CILK_ASSERT(w == __cilkrts_get_tls_worker());
}
