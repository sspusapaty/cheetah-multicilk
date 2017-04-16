#ifndef _CILK_OPTIONS_H
#define _CILK_OPTIONS_H

// Forward declaration
typedef struct Cilk_options Cilk_options;

// Includes


// Actual declaration
struct Cilk_options {
     int nproc;
     int stackdepth;
     int statlevel;
     int yieldslice;
     char *infofile_name;  /* this should really be arch-specific */
     int dump_core;
     int alloc_batch_size;
     int alloc_batch_size_for_empty_pgs;
     int memory_locks;
     int pthread_stacksize; /* if positive, set the stacksize. */
     int perf; /* if this is set, each worker is pinned to a processor */
};
#endif
