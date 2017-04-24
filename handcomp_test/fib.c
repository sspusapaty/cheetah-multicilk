#include <stdio.h>
#include <stdlib.h>
#include "../runtime/cilk.h"
#include "ktiming.h"

#ifndef TIMES_TO_RUN
#define TIMES_TO_RUN 1 
#endif

/* 
 * fib 39: 63245986
 * fib 40: 102334155
 * fib 41: 165580141 
 * fib 42: 267914296
 */


int fib2(int n) {
    int x, y, _tmp;

    if(n < 2) {
        return n;
    }
    
    x = fib2(n - 1);
    y = fib2(n - 2);
    return x+y;
}


static void fib_spawn_helper(int *x, int n);

int fib(int n) {
    int x, y, _tmp;

    if(n < 2) {
        return n;
    }
    
    PREAMBLE
    __cilkrts_enter_frame(sf);

    if(!setjmp(sf->ctx)) {
        fib_spawn_helper(&x, n-1);
    }

    y = fib(n - 2);

    //if(__cilkrts_unsynched(sf)) {
        if(!setjmp(sf->ctx)) {
            __cilkrts_sync(sf);
        }
	//}
    _tmp = x + y;

    __cilkrts_pop_frame(sf);
    // ANGE: the Intel CC will put a flag here 
    __cilkrts_leave_frame(sf);

    //hwmark_dec(&hwmark);
    return _tmp;
}

static void fib_spawn_helper(int *x, int n) {

    HELPER_PREAMBLE
    __cilkrts_enter_frame_fast(sf);
    __cilkrts_detach(sf);
    *x = fib(n);
    __cilkrts_pop_frame(sf);
    __cilkrts_leave_frame(sf); 
}


int cilk_main(int argc, char * args[]) {
    int i;
    int n, res;
    clockmark_t begin, end; 
    uint64_t running_time[TIMES_TO_RUN];

    if(argc != 2) {
        fprintf(stderr, "Usage: fib [<cilk-options>] <n>\n");
        exit(1);
    }
    
    n = atoi(args[1]);

    for(i = 0; i < TIMES_TO_RUN; i++) {
        begin = ktiming_getmark();
        res = fib(n);
        end = ktiming_getmark();
        running_time[i] = ktiming_diff_usec(&begin, &end);
    }

    printf("Result: %d\n", res);

    if( TIMES_TO_RUN > 10 ) 
        print_runtime_summary(running_time, TIMES_TO_RUN); 
    else 
        print_runtime(running_time, TIMES_TO_RUN); 

    return 0;
}
