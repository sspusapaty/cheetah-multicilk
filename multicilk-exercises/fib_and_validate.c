#include <cilk/cilk_c11_threads.h>
#include "util.h"
typedef struct {
    int n;
} args;

const int fib_mem[] = {0,1,1,2,3,5,8,13};

int fib( int n ) {
    
    if (n < 2) return fib_mem[n];
    
    int x = cilk_spawn fib(n-1);
    int y = fib(n-2);
    
    cilk_sync;

    return x+y;
}

typedef struct {
    int n;
    int fib;
} entry;

entry buffer[2];
pthread_mutex_t queueMutex;

int produce( void* arg ) {
    int max = ((args*)arg)->n;
    
    for (int n = 1; n <= max; ++n) {
        int data = fib(n);
        entry e;
        e.n = n;
        e.fib = data;
        
        int i = 0;
        while (1) {
            if (buffer[i].n == -1) {
                pthread_mutex_lock(&queueMutex);
                buffer[i] = e;
                pthread_mutex_unlock(&queueMutex);
                break;
            }
            i = (i+1)%2;
        }

    }

    return 0;
}

int consume( void* arg ) {
    int max = ((args*)arg)->n;
    while (1) {
        int i = 0;
        entry data;
        while (1) {
            if (buffer[i].n != -1) {
                pthread_mutex_lock(&queueMutex);
                data = buffer[i];
                buffer[i].n = -1;
                buffer[i].n = -1;
                pthread_mutex_unlock(&queueMutex);
                break;
            }
            i = (i+1)%2;
        }

        printf("Consumer decided fib(%d) is correct(%d)!\n",data.n,fib(data.n) == data.fib);
        if (data.n == max) break;
    }
    return 0;
}

int main(int argc, char** argv) {
    THREAD_PRINT("hello world from main!\n");
    
    // get cilk runtime configs
    cilk_config_t cfg1 = cilk_thrd_config_from_env("CILK_CONFIG1");
    cilk_config_t cfg2 = cilk_thrd_config_from_env("CILK_CONFIG2");
    CFG_PRINT(cfg1);
    CFG_PRINT(cfg2);

    pthread_t prod, cons;
    int n = 10;
    if (argc == 2) {
        n = atoi(argv[1]);
    }

    // initialize buffer
    pthread_mutex_init(&queueMutex, NULL);
    for (int i = 0; i < 2; ++i) {
        buffer[i].n = -1;
        buffer[i].fib = -1;
    }

    args x = {.n = n};
    // create producer and consumer with new cilk runtime
    int err = cilk_thrd_create(cfg1, &prod, produce, &x);
    int err2 = cilk_thrd_create(cfg2, &cons, consume, &x);
    if (err || err2) {
        printf("thrd creation not successful!\n");
        return -1;
    }
    
    cilk_thrd_join(prod, NULL);
    cilk_thrd_join(cons, NULL);
    
    return 0;
}
