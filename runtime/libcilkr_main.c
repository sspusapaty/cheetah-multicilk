#include <stdlib.h>
#include <stdio.h>

#include "cilk.h"

#undef main

int main(int argc, char* argv[]) {
  int ret;

  fprintf(stderr, "Cheetah: invoking user main.\n"); 

  global_state * g = __cilkrts_init(argc, argv);

  __cilkrts_run(g);

  __cilkrts_exit(g);
  
  ret = g->cilk_main_return;

  return ret;
}
