#ifndef _MEMBAR_H
#define _MEMBAR_H

/***********************************************************\
 * memory barriers
 \***********************************************************/

#define CILK_MB() asm volatile("mfence" ::: "memory");
#define CILK_RB() asm volatile("lfence" : : : "memory")
#define CILK_RMB() CILK_MB()
#define CILK_WMB() asm volatile("" : : : "memory")

/*
 *  Ensure that all previous memory operations are completed before
 *  continuing.
 */
static inline void Cilk_fence(void) { CILK_MB(); }

/*
 *  Ensure that all previous reads are globally visible before any
 *  future reads become visible.
 */
static inline void Cilk_membar_LoadLoad(void) { CILK_RB(); }

/*
 *  Ensure that all previous writes are globally visible before any
 *  future writes become visible.
 */
static inline void Cilk_membar_StoreStore(void) { CILK_WMB(); }

/*
 *  Ensure that all previous writes are globally visible before any
 *  future reads are performed.
 */
static inline void Cilk_membar_StoreLoad(void) { CILK_RMB(); }

#endif
