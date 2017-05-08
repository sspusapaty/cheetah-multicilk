#ifndef _STEAL_H
#define _STEAL_H

#include "stack_frame.h"
#include "worker.h"
#include "closure.h"

Closure * setup_call_parent_closure_helper(__cilkrts_worker *const ws, 
					   __cilkrts_worker *const victim_ws, 
					   __cilkrts_stack_frame *frame,
					   Closure *oldest);

void setup_closures_in_stacklet(__cilkrts_worker *const ws, 
				__cilkrts_worker *const victim_ws, 
				Closure *youngest_cl);

int do_dekker_on(__cilkrts_worker *const ws, 
		 __cilkrts_worker *const victim_ws, 
		 Closure *cl);

Closure *promote_child(__cilkrts_worker *const ws,
		       __cilkrts_worker *const victim_ws, 
		       Closure *cl, Closure **res);

void finish_promote(__cilkrts_worker *const ws, 
		    __cilkrts_worker *const victim_ws,
		    Closure *parent, Closure *child);

Closure *Closure_steal(__cilkrts_worker *const ws, int victim);

Closure *provably_good_steal_maybe(__cilkrts_worker *const ws, Closure *parent);

#endif
