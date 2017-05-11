#include "rts_rand.h"

/***********************************************************
 *  Internal random number generator.
 ***********************************************************/
unsigned int rts_rand(__cilkrts_worker *const ws) {

    ws->l->rand_next = ws->l->rand_next * 1103515245 + 12345;
    // XXX: ??? Why is >> 16 important??
    return (ws->l->rand_next >> 16);
}

void rts_srand(__cilkrts_worker *const ws, unsigned int seed) {

    ws->l->rand_next = seed;
}
