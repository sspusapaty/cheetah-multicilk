#ifndef _RSCHED_H
#define _RSCHED_H

#include "common.h"
#include "closure.h"

#define SYNC_READY 0
#define SYNC_NOT_READY 1

#define EXCEPTION_INFINITY (__cilkrts_stack_frame **)(-1LL)

void __cilkrts_init_tls_variables();
__cilkrts_worker * __cilkrts_get_tls_worker(); 
void __cilkrts_set_tls_worker(__cilkrts_worker *w); 

int Cilk_sync(__cilkrts_worker *const ws, 
              __cilkrts_stack_frame *frame);

void Cilk_set_return(__cilkrts_worker *const ws);
void Cilk_exception_handler();

__attribute__((noreturn)) void longjmp_to_runtime(__cilkrts_worker * w);
void worker_scheduler(__cilkrts_worker * ws, Closure * t);

#endif
