#include <pthread.h>
#include "tls.h"


static pthread_key_t worker_key, fiber_key;

void __cilkrts_init_tls_variables()
{
    int status;
    status = pthread_key_create(&worker_key, NULL);
    CILK_ASSERT (status == 0);   
    status = pthread_key_create(&fiber_key, NULL);
    CILK_ASSERT (status == 0);
}

void * __cilkrts_get_current_thread_id()
{
    return (void *)pthread_self();
}


__cilkrts_worker * __cilkrts_get_tls_worker()
{
    return (__cilkrts_worker *)pthread_getspecific(worker_key);
    
}

cilk_fiber * __cilkrts_get_tls_cilk_fiber()
{
    return (cilk_fiber *)pthread_getspecific(fiber_key);
}

void __cilkrts_set_tls_worker(__cilkrts_worker *w)
{
    int status;
    status = pthread_setspecific(worker_key, w);
    CILK_ASSERT (status == 0);
    return;
}

void __cilkrts_set_tls_cilk_fiber(cilk_fiber* fiber)
{
    int status;
    status = pthread_setspecific(fiber_key, fiber);
    CILK_ASSERT (status == 0);
    return;
}
