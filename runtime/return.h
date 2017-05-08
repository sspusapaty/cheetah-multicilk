#ifndef _RETURN_H
#define _RETURN_H

#include "worker.h"
#include "closure.h"

Closure * setup_call_parent_resumption(__cilkrts_worker *const ws, Closure *t);

void Cilk_set_return(__cilkrts_worker *const ws);

Closure *Closure_return(__cilkrts_worker *const ws, Closure *child);

Closure *return_value(__cilkrts_worker *const ws, Closure *t);

#endif
