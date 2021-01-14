#include <stdatomic.h>
#include <stdio.h>
#define ENABLE_CILKRTS_PEDIGREE
#include <cilk/cilk_api.h> 

__cilkrts_pedigree cilkrts_root_pedigree_node;
