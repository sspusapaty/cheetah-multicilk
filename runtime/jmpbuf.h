#ifndef _JMPBUF_H
#define _JMPBUF_H

#include <stddef.h>
#include <setjmp.h>
#include "common.h"

// REALLY uncomfortable here
enum CtxIndex {RBP_INDEX = 0, RIP_INDEX = 1, RSP_INDEX = 2,  
               UNUSED_INDEX1 = 3, UNUSED_INDEX2 = 4, CTX_SIZE = 5};

/*
#define JB_RBX    0
#define JB_RBP    1
#define JB_R12    2
#define JB_R13    3
#define JB_R14    4
#define JB_R15    5
#define JB_RSP    6
#define JB_PC    7
#define JB_SIZE (8*8)


#if defined JB_RSP

#define JMPBUF_SP(ctx) (ctx)[0].__jmpbuf[JB_RSP]
#define JMPBUF_FP(ctx) (ctx)[0].__jmpbuf[JB_RBP]
#define JMPBUF_PC(ctx) (ctx)[0].__jmpbuf[JB_PC]

#elif  defined JB_SP
#define JMPBUF_SP(ctx) (ctx)[0].__jmpbuf[JB_SP]
#define JMPBUF_FP(ctx) (ctx)[0].__jmpbuf[JB_BP]
#define JMPBUF_PC(ctx) (ctx)[0].__jmpbuf[JB_PC]

#endif
*/

#define JMPBUF_SIZE 5
typedef void * jmpbuf[JMPBUF_SIZE];

    /* word 0 is frame address
     * word 1 is resume address
     * word 2 is stack address */
#define JMPBUF_FP(ctx) (ctx)[0]
#define JMPBUF_PC(ctx) (ctx)[1]
#define JMPBUF_SP(ctx) (ctx)[2]

/**
 * @brief Get frame pointer from jump buffer in__cilkrts_stack_frame.
 */
#define FP(SF) JMPBUF_FP((SF)->ctx)

/**
 * @brief Get program counter from jump buffer in__cilkrts_stack_frame.
 */
#define PC(SF) JMPBUF_PC((SF)->ctx)

/**
 * @brief Get stack pointer from jump buffer in__cilkrts_stack_frame.
 */
#define SP(SF) JMPBUF_SP((SF)->ctx)
//typedef void *__CILK_JUMP_BUFFER[8];

#define ASM_GET_SP(osp) asm volatile ("movq %%rsp, %0": "=r" (osp))
#define ASM_SET_SP(nsp) asm volatile ("movq %0, %%rsp": : "r" (nsp) : "rsp")

#define ASM_GET_FP(ofp) asm volatile ("movq %%rbp, %0": "=r" (ofp))
#define ASM_SET_FP(nfp) asm volatile ("movq %0, %%rbp": : "r" (nfp) : "rbp")


#define DUMP_STACK(lvl, wid) {char * x_bp; char * x_sp; ASM_GET_FP(x_bp); ASM_GET_SP(x_sp);  __cilkrts_alert((lvl), "[%d]: rbp: %p\n[%d]: rsp: %p\n", (wid), x_bp, (wid), x_sp);}
#endif

