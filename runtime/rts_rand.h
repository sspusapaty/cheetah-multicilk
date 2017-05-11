#ifndef _RTS_RAND_H
#define _RTS_RAND_H

#include "worker.h"

unsigned int rts_rand(__cilkrts_worker *const ws);

void rts_srand(__cilkrts_worker *const ws, unsigned int seed);

#endif
