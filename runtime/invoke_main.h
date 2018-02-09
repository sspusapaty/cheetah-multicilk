#ifndef _INVOKE_MAIN_H
#define _INVOKE_MAIN_H

#include "global_state.h"
#include "closure.h"

Closure * create_invoke_main(global_state *const g);
void spawn_cilk_main(int *res, int argc, char * args[]);
__attribute__((noreturn)) void invoke_main();
#endif
