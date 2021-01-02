#ifndef _CILK_INIT_H
#define _CILK_INIT_H

#include "cilk-internal.h"

int cilk_main(int argc, char *argv[]);
CHEETAH_INTERNAL global_state *__cilkrts_init(int argc, char *argv[]);
CHEETAH_INTERNAL void __cilkrts_cleanup(global_state *);
CHEETAH_INTERNAL_NORETURN void invoke_main();
CHEETAH_INTERNAL void parse_environment();
CHEETAH_INTERNAL long env_get_int(char const *var);
CHEETAH_INTERNAL unsigned cilkg_nproc;

void __cilkrts_internal_set_nworkers(unsigned int nworkers);
void __cilkrts_internal_set_force_reduce(unsigned int force_reduce);
CHEETAH_INTERNAL void __cilkrts_start_workers(global_state *g);
CHEETAH_INTERNAL void __cilkrts_stop_workers(global_state *g);
void invoke_cilkified_root(global_state *g, __cilkrts_stack_frame *sf);
void wait_until_cilk_done(global_state *g);
__attribute__((noreturn))
void exit_cilkified_root(global_state *g, __cilkrts_stack_frame *sf);

#endif /* _CILK_INIT_H */
