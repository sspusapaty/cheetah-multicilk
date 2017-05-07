#ifndef _EXCEPTION_H
#define _EXCEPTION_H

#include "worker.h"
#include "closure.h"
#include "stack_frame.h"

#define EXCEPTION_INFINITY (__cilkrts_stack_frame **)(-1LL)


void increment_exception_pointer(__cilkrts_worker *const ws, 
				 __cilkrts_worker *const victim_ws, 
				 Closure *cl);

void decrement_exception_pointer(__cilkrts_worker *const ws, 
				 __cilkrts_worker *const victim_ws, 
				 Closure *cl);

void reset_exception_pointer(__cilkrts_worker *const ws, Closure *cl);


void signal_immediate_exception_to_all(__cilkrts_worker *const ws);

#endif
