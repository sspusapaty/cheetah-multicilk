#ifndef _CILK_H
#define _CILK_H

// Includes
#include "worker.h"
#include "stack_frame.h"
#include "global_state.h"
#include "local_state.h"

// Funcs
global_state * __cilkrts_init(int argc, char* argv[]);

void __cilkrts_run(global_state * g);

void __cilkrts_exit(global_state * g);
#endif
