#include <cilk/cilk.h>
#include <stdio.h>
#include <stdlib.h>
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


int fib(int n) {
    int x, y;

    if(n < 2) {
        return n;
    }
    
    x = fib(n - 1);
    y = fib(n - 2);
    return x+y;
}


int fiber_debug(int n) {
    int x, y;

    if(n < 2) {
        return n;
    }
    
    x = cilk_spawn fib(n - 2);
    y = fib(n - 1);

    cilk_sync;
    
    return x + y;
}


int main(int argc, char * args[]) {
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
        res = fiber_debug(n);
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
