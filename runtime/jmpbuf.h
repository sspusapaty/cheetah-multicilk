#ifndef _JMPBUF_H
#define _JMPBUF_H

// REALLY uncomfortable here
enum CtxIndex {RBP_INDEX = 0, RIP_INDEX = 1, RSP_INDEX = 2,  
               UNUSED_INDEX1 = 3, UNUSED_INDEX2 = 4, CTX_SIZE = 5};
typedef void *__CILK_JUMP_BUFFER[5];

#endif
