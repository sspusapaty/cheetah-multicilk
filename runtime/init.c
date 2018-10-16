#define _GNU_SOURCE
#include <sched.h>

#include <pthread.h>
#include <stdlib.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#include "debug.h"
#include "fiber.h"
#include "readydeque.h"
#include "sched_stats.h"
#include "scheduler.h"

extern void cleanup_invoke_main(Closure *invoke_main);
extern int parse_command_line(struct rts_options *options, int *argc, char *argv[]);

extern int cilkg_nproc;

/* Linux only */
static int linux_get_num_proc() {
    const char *envstr = getenv("CILK_NWORKERS");
    if (envstr)
        return strtol(envstr, NULL, 0);
    return get_nprocs();
}

static global_state * global_state_init(int argc, char* argv[]) {
    __cilkrts_alert(ALERT_BOOT, "[M]: (global_state_init) Initializing global state.\n");
    global_state * g = (global_state *) malloc(sizeof(global_state));

    if(parse_command_line(&g->options, &argc, argv)) {
        // user invoked --help; quit
        free(g);
        exit(0);
    }
    
    if(g->options.nproc == 0) {
        // use the number of cores online right now 
        int available_cores = 0;
        cpu_set_t process_mask;
            //get the mask from the parent thread (master thread)
        int err = pthread_getaffinity_np (pthread_self(), sizeof(process_mask), &process_mask);
        if (0 == err)
        {
            int j;
            //Get the number of available cores (copied from os-unix.c)
            available_cores = 0;
            for (j = 0; j < CPU_SETSIZE; j++){
                if (CPU_ISSET(j, &process_mask)){
                    available_cores++;
                }
            }
        }
        const char *envstr = getenv("CILK_NWORKERS");
        g->options.nproc = (envstr ? linux_get_num_proc() : available_cores);

    }
    cilkg_nproc = g->options.nproc;

    int active_size = g->options.nproc;
    g->invoke_main_initialized = 0;
    g->start = 0;
    g->done = 0;
    cilk_mutex_init(&(g->print_lock));

    g->workers = (__cilkrts_worker **) malloc(active_size * sizeof(__cilkrts_worker *));
    g->deques = (ReadyDeque *) malloc(active_size * sizeof(ReadyDeque));
    g->threads = (pthread_t *) malloc(active_size * sizeof(pthread_t));
    cilk_internal_malloc_global_init(g); // initialize internal malloc first
    cilk_fiber_pool_global_init(g);
    cilk_global_sched_stats_init(&(g->stats));

    g->cilk_main_argc = argc;
    g->cilk_main_args = argv;

    return g;
}

static local_state *worker_local_init(global_state *g) {
    local_state * l = (local_state *) malloc(sizeof(local_state));
    l->shadow_stack = (__cilkrts_stack_frame **) 
        malloc(g->options.deqdepth * sizeof(struct __cilkrts_stack_frame *));
    for(int i=0; i < JMPBUF_SIZE; i++) { l->rts_ctx[i] = NULL; }
    l->fiber_to_free = NULL;
    l->provably_good_steal = 0;
    cilk_sched_stats_init(&(l->stats));

    return l;
}

static void deques_init(global_state * g) {
    __cilkrts_alert(ALERT_BOOT, "[M]: (deques_init) Initializing deques.\n");
    for (int i = 0; i < g->options.nproc; i++) {
        g->deques[i].top = NULL;
        g->deques[i].bottom = NULL;
        WHEN_CILK_DEBUG(g->deques[i].mutex_owner = NOBODY);
        cilk_mutex_init(&(g->deques[i].mutex));
    }
}

static void workers_init(global_state * g) {
    __cilkrts_alert(ALERT_BOOT, "[M]: (workers_init) Initializing workers.\n");
    for (int i = 0; i < g->options.nproc; i++) {
        __cilkrts_alert(ALERT_BOOT, "[M]: (workers_init) Initializing worker %d.\n", i);
        __cilkrts_worker * w = (__cilkrts_worker *) malloc(sizeof(__cilkrts_worker));
        w->self = i;
        w->g = g;
        w->l = worker_local_init(g);

        w->ltq_limit = w->l->shadow_stack + g->options.deqdepth;
        g->workers[i] = w;
        w->tail = w->l->shadow_stack + 1;
        w->head = w->l->shadow_stack + 1;
        w->exc = w->head;
        w->current_stack_frame = NULL;

        // initialize internal malloc first
        cilk_internal_malloc_per_worker_init(w);
        cilk_fiber_pool_per_worker_init(w);
    }
}

static void* scheduler_thread_proc(void * arg) {
    __cilkrts_worker * w = (__cilkrts_worker *)arg;
    long long idle = 0;
    __cilkrts_alert(ALERT_BOOT, "[%d]: (scheduler_thread_proc)\n", w->self);
    __cilkrts_set_tls_worker(w);

    while(!w->g->start) {
        usleep(1);
        idle++;
    }

    if(w->self == 0) {
        worker_scheduler(w, w->g->invoke_main);
    } else {
        worker_scheduler(w, NULL);
    }

    return 0;
}

static void threads_init(global_state * g) {
    __cilkrts_alert(ALERT_BOOT, "[M]: (threads_init) Setting up threads.\n");
    for (int i = 0; i < g->options.nproc; i++) {
        int status = pthread_create(&g->threads[i],
                NULL,
                scheduler_thread_proc,
                g->workers[i]);
        if (status != 0) 
            __cilkrts_bug("Cilk: thread creation (%d) failed: %d\n", i, status);
        //Affinity setting, from cilkplus-rts
        cpu_set_t process_mask;
        //Get the mask from the parent thread (master thread)
        int err = pthread_getaffinity_np (pthread_self(), sizeof(process_mask), &process_mask);
        if (0 == err)
        {
            int j;
            //Get the number of available cores (copied from os-unix.c)
            int available_cores = 0;
            for (j = 0; j < CPU_SETSIZE; j++){
               if (CPU_ISSET(j, &process_mask)){
                   available_cores++;
               }
            }

            //Bind the worker to a core according worker id
            int workermaskid = i % available_cores;
            for (j = 0; j < CPU_SETSIZE; j++)
            {
                if (CPU_ISSET(j, &process_mask))
                {
                    if (workermaskid == 0){
                    // Bind the worker to the assigned cores
                        cpu_set_t mask;
                        CPU_ZERO(&mask);
                        CPU_SET(j, &mask);
                        int ret_val = pthread_setaffinity_np(g->threads[i], sizeof(mask), &mask);
                        if (ret_val != 0)
                        {
                            __cilkrts_bug("ERROR: Could not set CPU affinity");
                        }
                        break;
                    }
                    else{
                        workermaskid--;
                    }
                }
            }
        }
        else{
            __cilkrts_bug("Cannot get affinity mask by pthread_getaffinity_np");
        }
    }
    usleep(10);
}

global_state * __cilkrts_init(int argc, char* argv[]) {
    __cilkrts_alert(ALERT_BOOT, "[M]: (__cilkrts_init)\n");
    global_state * g = global_state_init(argc, argv);
    __cilkrts_init_tls_variables();
    workers_init(g);
    deques_init(g);
    threads_init(g);

    return g;
}

static void global_state_terminate(global_state *g) {
    cilk_fiber_pool_global_terminate(g);
    cilk_internal_malloc_global_terminate(g);
    cilk_sched_stats_print(g);
}

static void global_state_deinit(global_state *g) {
    __cilkrts_alert(ALERT_BOOT, "[M]: (global_state_deinit) Clean up global state.\n");

    cleanup_invoke_main(g->invoke_main);
    cilk_fiber_pool_global_destroy(g);
    cilk_internal_malloc_global_destroy(g); // internal malloc last
    cilk_mutex_destroy(&(g->print_lock));
    free(g->workers);
    free(g->deques);
    free(g->threads);
    free(g);
}

static void deques_deinit(global_state * g) {
    __cilkrts_alert(ALERT_BOOT, "[M]: (deques_deinit) Clean up deques.\n");
    for (int i = 0; i < g->options.nproc; i++) {
        CILK_ASSERT_G(g->deques[i].mutex_owner == NOBODY);
        cilk_mutex_destroy(&(g->deques[i].mutex));
    }
}

static void workers_terminate(global_state * g) {
    for(int i = 0; i < g->options.nproc; i++) {
        __cilkrts_worker *w = g->workers[i];
        cilk_fiber_pool_per_worker_terminate(w);
        cilk_internal_malloc_per_worker_terminate(w); // internal malloc last
    }
}

static void workers_deinit(global_state * g) {
    __cilkrts_alert(ALERT_BOOT, "[M]: (workers_deinit) Clean up workers.\n");
    for(int i = 0; i < g->options.nproc; i++) {
        __cilkrts_worker *w = g->workers[i];
        CILK_ASSERT(w, w->l->fiber_to_free == NULL);

        cilk_fiber_pool_per_worker_destroy(w);
        cilk_internal_malloc_per_worker_destroy(w); // internal malloc last
        free(w->l->shadow_stack);
        free(w->l);
        free(w);
        g->workers[i] = NULL;
    }
}

void __cilkrts_cleanup(global_state *g) {
    workers_terminate(g);
    // global_state_terminate collects and prints out stats, and thus
    // should occur *BEFORE* worker_deinit, because worker_deinit  
    // deinitializes worker-related data structures which may 
    // include stats that we care about.
    // Note: the fiber pools uses the internal-malloc, and fibers in fiber 
    // pools are not freed until workers_deinit.  Thus the stats included on 
    // internal-malloc that does not include all the free fibers.  
    global_state_terminate(g); 

    workers_deinit(g);
    deques_deinit(g);
    global_state_deinit(g);
}
