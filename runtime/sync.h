#ifndef _SYNC_H
#define _SYNC_H

#include "worker.h"
#include "stack_frame.h"

#define SYNC_READY 0
#define SYNC_NOT_READY 1

int Cilk_sync(__cilkrts_worker *const ws, 
              __cilkrts_stack_frame *frame);

#endif
