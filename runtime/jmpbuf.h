#ifndef _JMPBUF_H
#define _JMPBUF_H

#include <setjmp.h>

// REALLY uncomfortable here
//enum CtxIndex {RBP_INDEX = 0, RIP_INDEX = 1, RSP_INDEX = 2,  
//               UNUSED_INDEX1 = 3, UNUSED_INDEX2 = 4, CTX_SIZE = 5};

//define JB_RBX    0
//define JB_RBP    1
//define JB_R12    2
//define JB_R13    3
//define JB_R14    4
//define JB_R15    5
//define JB_RSP    6
//define JB_PC    7
//define JB_SIZE (8*8)


#if defined JB_RSP
 #define JMPBUF_SP(ctx) (ctx)[0].__jmpbuf[JB_RSP]
 #define JMPBUF_FP(ctx) (ctx)[0].__jmpbuf[JB_RBP]
 #define JMPBUF_PC(ctx) (ctx)[0].__jmpbuf[JB_PC]
#elif  defined JB_SP
 #define JMPBUF_SP(ctx) (ctx)[0].__jmpbuf[JB_SP]
 #define JMPBUF_FP(ctx) (ctx)[0].__jmpbuf[JB_BP]
 #define JMPBUF_PC(ctx) (ctx)[0].__jmpbuf[JB_PC]
#endif

//typedef void *__CILK_JUMP_BUFFER[8];

#define ASM_GET_SP(osp) asm volatile ("movq %%rsp, %0": "=r" (osp))
#define ASM_SET_SP(nsp) asm volatile ("movq %0, %%rsp": : "r" (nsp))


#endif
