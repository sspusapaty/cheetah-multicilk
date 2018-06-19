#ifndef _CONFIG_H 
#define _CONFIG_H 

#define __CILKRTS_VERSION 0x0
#define __CILKRTS_ABI_VERSION 0x1

#define CILK_DEBUG 1
#define CILK_STATS 1

#define CILK_CACHE_LINE 64
#define CILK_CACHE_LINE_PAD  char __dummy[CILK_CACHE_LINE]

#define PROC_SPEED_IN_GHZ 2.2
#define PAGE_SIZE 4096
#define MIN_NUM_PAGES_PER_STACK 4 
#define DEFAULT_STACKSIZE 0x100000 // 1 MBytes

#define DEFAULT_NPROC 0             // 0 for # of cores available
#define DEFAULT_DEQ_DEPTH 1024
#define DEFAULT_STACK_SIZE 0x100000 // 1 MBytes
#define DEFAULT_FIBER_POOL_CAP  128 // initial per-worker fiber pool capacity 
#endif // _CONFIG_H 
