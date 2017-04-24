#ifndef _RSCHED_H
#define _RSCHED_H

#include "worker.h"

void longjmp_to_runtime(__cilkrts_worker * w);

void worker_scheduler(__cilkrts_worker * w);

#endif
