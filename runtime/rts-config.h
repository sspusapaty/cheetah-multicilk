#ifndef _CONFIG_H 
#define _CONFIG_H 

#define __CILKRTS_ABI_VERSION 0x1

#define CILK_CACHE_LINE 64
#define CILK_CACHE_LINE_PAD  char __dummy[CILK_CACHE_LINE]

#define PAGE_SIZE 4096
#define MIN_NUM_PAGES_PER_STACK 4 

#define DEFAULT_STACKSIZE 0x100000 // 1 MBytes

#define DEFAULT_NPROC 16
#define DEFAULT_DEQ_DEPTH 1024
#define DEFAULT_STACK_SIZE 0x100000 // 1 MBytes
#define DEFAULT_ALLOC_BATCH 64
#define DEFAULT_MAX_NUM_FIBERS 2400 // same as Cilk Plus for now

#endif // _CONFIG_H 
