#ifndef _TLS_H
#define _TLS_H

#include "worker.h"
#include "fiber.h"

void __cilkrts_init_tls_variables(void);

void * __cilkrts_get_current_thread_id(void);

__cilkrts_worker * __cilkrts_get_tls_worker();

// cilk_fiber * __cilkrts_get_tls_cilk_fiber(void);

void __cilkrts_set_tls_worker(__cilkrts_worker *w);

// void __cilkrts_set_tls_cilk_fiber(cilk_fiber* fiber);

#endif
