#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "cilk-internal.h"
#include "debug.h"

#define MEM_LIST_SIZE  8 
#define INTERNAL_MALLOC_CHUNK_SIZE (32 * 1024)
#define SIZE_THRESH size_buckets[NUM_BUCKETS-1]

static const int size_buckets[NUM_BUCKETS] = { 64, 128, 256, 512, 1024, 2048 };

struct free_block {
    void *next;
};

//=========================================================
// Private helper functions 
//=========================================================

static inline int is_page_aligned(int size) {
    int mask = PAGE_SIZE - 1;
    return((size & mask) == 0);
}

static inline int size_to_bucket(int size) {
    for(int i=0; i < NUM_BUCKETS; i++) {
        if(size <= size_buckets[i]) {
            return i;
        }
    }
    return -1;
}

static inline int bucket_to_size(int which_bucket) {
    return size_buckets[which_bucket];
}

#if CILK_DEBUG // used only when deubgging, turns out
/* compute the length of a free list starting at pointer p */
static int free_list_length(void *p) {
    int count = 0;
    while(p) {
        count++;
        // next pointer is stored at the first 8 bytes
        p = ((struct free_block *)p)->next; 
    }
    return count;
}
#endif

/* initialize the buckets in struct cilk_im_desc */
static void init_im_buckets(struct cilk_im_desc *im_desc, 
                            int count_until_free) {
    for(int i=0; i < NUM_BUCKETS; i++) {
        struct im_bucket *bucket = &(im_desc->buckets[i]);
        bucket->free_list = NULL;
        bucket->count_until_free = count_until_free;
    }
}

//=========================================================
// Private helper functions for debugging 
//=========================================================

#if CILK_DEBUG
void internal_malloc_global_check(global_state *g) {

    struct cilk_im_desc *d = &(g->im_desc);
    int64_t total_size = d->used;
    int64_t total_malloc = d->num_malloc;

    for(int i = 0; i < g->options.nproc; i++) {
        d = &(g->workers[i]->l->im_desc);
        total_size += d->used;
        total_malloc += d->num_malloc;
    }

    // these fields must add up to 0, as they keep track of sizes and number of 
    // malloc / frees going out of / into the global pool / per-worker pool.  
    // Anything batch-freed into per-worker pool had to come from the global pool; 
    // similarly, anything batch-allocated out of the per-worker pool gets freed 
    // into the global one

    CILK_CHECK(g, (total_size == 0) && (total_malloc == 0),
               "Possible memory leak detected.\n");
}

#else
#define internal_malloc_global_check(g) 
#endif // CILK_DEBUG

//=========================================================
// Private helper functions for IM stats
//=========================================================

#if INTERNAL_MALLOC_STATS
#define FIELD_DESC "%6d"
#define PN_DESC "%7d"
#define PN_NAME_DESC "%7s"

static void init_global_im_pool_stats(struct global_im_pool_stats *stats) {
    stats->allocated = 0;
    stats->wasted = 0;
}

/*
static void print_im_stats(__cilkrts_worker *w, 
                           struct global_im_pool_stats *stats) {

    if(w) {
        fprintf(stderr, "Per-worker internal malloc stats %2d:\n\n", w->self);
    } else {
        fprintf(stderr, "Global internal malloc stats:\n\n");
    }

    fprintf(stderr, PN_NAME_DESC, "PN\\size");
    for(int i = 0; i < NUM_BUCKETS; i++) {
        fprintf(stderr, FIELD_DESC, bucket_to_size(i));
    }
    fprintf(stderr, "\n");

    fprintf(stderr, PN_NAME_DESC, "Global");
    for(int i = 0; i < NUM_BUCKETS; i++) {
        fprintf(stderr, FIELD_DESC, stats->bucket_length[i]);
    }
    fprintf(stderr, "\n");
}

static void print_global_im_stats(global_stat *g) {

    int i;
    int in_free_lists = 0;

    fprintf("\nSCHEDULER MEMORY STATISTICS:\n\n"
            "Total memory allocated for the scheduler: %d bytes\n"
            "Unfragmented memory in global pool: %ld bytes\n"
            "Memory wasted because of fragmentation: %d bytes\n"
            "Memory in free lists at end of execution (in bytes):\n",
            USE_SHARED1(im_allocated),
            (long) (USE_SHARED1(global_pool_end) -
                USE_SHARED1(global_pool_begin)),
            USE_SHARED1(im_wasted));

    fprintf(USE_PARAMETER1(infofile),
            "      PN     free-list\n"
            "  --------------------\n");

    fprintf(USE_PARAMETER1(infofile), "  Global" FORMAT,
            USE_SHARED1(global_im_info).in_free_lists);
    in_free_lists += USE_SHARED1(global_im_info).in_free_lists;

    for (i = 0; i < USE_PARAMETER1(active_size); ++i) {
        fprintf(USE_PARAMETER1(infofile), "     %3d" FORMAT, i,
                USE_PARAMETER1(im_info)[i].in_free_lists);
        in_free_lists += USE_PARAMETER1(im_info)[i].in_free_lists;
    }

    fprintf(USE_PARAMETER1(infofile),
            "  --------------------\n");

    fprintf(USE_PARAMETER1(infofile), "        " FORMAT, in_free_lists);

    if (USE_PARAMETER1(options->statlevel) >= 4)
        print_detailed_im_stats(context);
}
*/
#else
#define init_global_im_pool_stats(stats)
#define print_im_stats(w, stats)
#define print_global_im_stats(g)
#endif // INTERNAL_MALLOC_STATS

//=========================================================
// Global memory allocator
//=========================================================

static char * malloc_from_system(__cilkrts_worker *w, int size) {
    void *mem;
    if(is_page_aligned(size)) {
        mem = mmap(0, size, PROT_READ|PROT_WRITE, 
                   MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    } else {
        mem = malloc(size);
    }
    CILK_CHECK(w->g, mem, "Internal malloc running out of memory!");
    return mem;
}

static void free_to_system(void *p, int size) {
    if(is_page_aligned(size)) {
        munmap(p, size);
    } else {
        free(p);
    }
}

/**
 * Extend the global im pool.  This function is only called when the 
 * current chunk in use is not big enough to satisfy an allocation.
 * The size is already canonicalized at this point.
 */
static void extend_global_pool(__cilkrts_worker *w) {

    struct global_im_pool *im_pool = &(w->g->im_pool);
    im_pool->mem_begin = malloc_from_system(w, INTERNAL_MALLOC_CHUNK_SIZE);
    im_pool->mem_end = im_pool->mem_begin + INTERNAL_MALLOC_CHUNK_SIZE;
#if INTERNAL_MALLOC_STATS
    im_pool->stats.allocated += INTERNAL_MALLOC_CHUNK_SIZE;
#endif
    im_pool->mem_list_index++;

    if(im_pool->mem_list_index >= im_pool->mem_list_size) {
        int new_list_size = im_pool->mem_list_size + MEM_LIST_SIZE;
        im_pool->mem_list = realloc(im_pool->mem_list, 
                                    new_list_size * sizeof(*im_pool->mem_list));
        im_pool->mem_list_size = new_list_size;
        CILK_CHECK(w->g, im_pool->mem_list,
                   "Interal malloc running out of memory!");
    }
    im_pool->mem_list[im_pool->mem_list_index] = im_pool->mem_begin;
}

/**
 * Allocate a piece of memory of 'size' from global im bucket 'bucket'. 
 * The free_list is last-in-first-out.
 * The size is already canonicalized at this point.
 */
static void *global_im_alloc(__cilkrts_worker *w, 
                             int size, int which_bucket) {

    CILK_ASSERT(w, w->g);
    CILK_ASSERT(w, size <= SIZE_THRESH);
    CILK_ASSERT(w, which_bucket < NUM_BUCKETS);

    struct im_bucket *bucket = &(w->g->im_desc.buckets[which_bucket]);
    void *mem = bucket->free_list;

    WHEN_CILK_DEBUG({ // stats only kept track during debugging
        struct cilk_im_desc *im_desc = &(w->g->im_desc);
        im_desc->used += size;
        im_desc->num_malloc++;
    });
    // look at the global free list for this bucket
    if(mem) {
        bucket->free_list = ((struct free_block *) mem)->next;
        bucket->count_until_free++;
    } else {
        struct global_im_pool *im_pool = &(w->g->im_pool);
        // allocate from the global pool
        if((im_pool->mem_begin + size) > im_pool->mem_end) {
#if INTERNAL_MALLOC_STATS
            // consider the left over as waste for now
            im_pool->stats.wasted += im_pool->mem_end - im_pool->mem_begin;
#endif
            extend_global_pool(w);
        }
        mem = im_pool->mem_begin;
        im_pool->mem_begin += size;
    }

    return mem;
}

/**
 * Free a piece of memory of 'size' back to global im bucket 'bucket'. 
 * The free_list is last-in-first-out.
 * The size is already canonicalized at this point.
 */
static void global_im_free(__cilkrts_worker *w,
                           void *p, int size, int which_bucket) {

    CILK_ASSERT(w, w->g);
    CILK_ASSERT(w, size <= SIZE_THRESH);
    CILK_ASSERT(w, which_bucket < NUM_BUCKETS);
    USE_UNUSED(size);

    WHEN_CILK_DEBUG({ // stats only kept track during debugging
        struct cilk_im_desc *im_desc = &(w->g->im_desc);
        im_desc->used -= size;
        im_desc->num_malloc--;
    });
    struct im_bucket *bucket = &(w->g->im_desc.buckets[which_bucket]);
    void *next = bucket->free_list;
    ((struct free_block *)p)->next = next;
    bucket->free_list = p;
    bucket->count_until_free--;
}

static void global_im_pool_destroy(struct global_im_pool *im_pool) {
    
    for(int i=0; i < im_pool->mem_list_size; i++) {
        void *mem = im_pool->mem_list[i];
        free_to_system(mem, INTERNAL_MALLOC_CHUNK_SIZE);
        im_pool->mem_list[i] = NULL;
    }
    free(im_pool->mem_list);
    im_pool->mem_list = NULL;
    im_pool->mem_begin = im_pool->mem_end = NULL;
    im_pool->mem_list_index = -1;
    im_pool->mem_list_size = 0;
}

void cilk_internal_malloc_global_init(global_state *g) {
    cilk_mutex_init(&(g->im_lock));
    g->im_pool.mem_begin = g->im_pool.mem_end = NULL;
    g->im_pool.mem_list_index = -1;
    g->im_pool.mem_list_size = MEM_LIST_SIZE;
    g->im_pool.mem_list = malloc(MEM_LIST_SIZE * sizeof(*g->im_pool.mem_list));
    CILK_CHECK(g, g->im_pool.mem_list, "Cannot allocate mem_list");
    init_im_buckets(&g->im_desc, g->options.alloc_batch_size);
    init_global_im_pool_stats(&(g->im_pool.stats));
    WHEN_CILK_DEBUG(g->im_desc.used = 0);
    WHEN_CILK_DEBUG(g->im_desc.num_malloc = 0);
}

void cilk_internal_malloc_global_destroy(global_state *g) {
    global_im_pool_destroy(&(g->im_pool)); // free global mem blocks
    cilk_mutex_destroy(&(g->im_lock));
}

//=========================================================
// Per-worker memory allocator
//=========================================================

/**
 * Allocate a batch of memory of size 'size' from global im bucket 'bucket'
 * into per-worker im bucekt 'bucket'.
 */
static void im_allocate_batch(__cilkrts_worker *w,
                              int size, int bucket) {
    cilk_mutex_lock(&(w->g->im_lock));
    for(int i = 0; i < (w->g->options.alloc_batch_size) / 2; i++) {
        void *p = global_im_alloc(w, size, bucket);
        cilk_internal_free(w, p, size);
    }
    cilk_mutex_unlock(&(w->g->im_lock));
}

/**
 * Free a batch of memory of size 'size' from per-worker im bucket 'bucket'
 * back to global im bucekt 'bucket'.
 */
static void im_free_batch(__cilkrts_worker *w, int size, int bucket) {
    cilk_mutex_lock(&(w->g->im_lock));
    for(int i = 0; i < (w->g->options.alloc_batch_size) / 2; i++) {
        void *p = cilk_internal_malloc(w, size);
        global_im_free(w, p, size, bucket);
    }
    cilk_mutex_unlock(&(w->g->im_lock));
}
 
/*
 * Malloc returns a piece of memory at the head of the free list; last-in-first-out
 */
void * cilk_internal_malloc(__cilkrts_worker *w, int size) {

    WHEN_CILK_DEBUG(w->l->im_desc.used += size);
    WHEN_CILK_DEBUG(w->l->im_desc.num_malloc += 1);

    if(size >= SIZE_THRESH) {
        return malloc_from_system(w, size);
    }

    int which_bucket = size_to_bucket(size);
    CILK_ASSERT(w, which_bucket >= 0 && which_bucket < NUM_BUCKETS);
    int csize = bucket_to_size(which_bucket); // canonicalize the size
    struct im_bucket *bucket = &(w->l->im_desc.buckets[which_bucket]);
    void *mem;

    while(!((mem = bucket->free_list))) {
        im_allocate_batch(w, csize, which_bucket);
    }

    /* if there is a block in the free list */
    CILK_ASSERT(w, mem);
    bucket->free_list = ((struct free_block *) mem)->next;
    bucket->count_until_free++;

    return mem;
}

/*
 * Free simply returns to the free list; last-in-first-out
 */
void cilk_internal_free(__cilkrts_worker *w, void *p, int size) {

    WHEN_CILK_DEBUG(w->l->im_desc.used -= size);
    WHEN_CILK_DEBUG(w->l->im_desc.num_malloc -= 1);

    if(size > SIZE_THRESH) {
        free_to_system(p, size);
        return;
    }

    int which_bucket = size_to_bucket(size);
    CILK_ASSERT(w, which_bucket >= 0 && which_bucket < NUM_BUCKETS);
    int csize = bucket_to_size(which_bucket); // canonicalize the size
    struct im_bucket *bucket = &(w->l->im_desc.buckets[which_bucket]);

    while(bucket->count_until_free <= 0) {
        im_free_batch(w, csize, which_bucket);
    }
    ((struct free_block *)p)->next = bucket->free_list;
    bucket->free_list = p;
    bucket->count_until_free--;
}

void cilk_internal_malloc_per_worker_init(__cilkrts_worker *w) {
    init_im_buckets(&(w->l->im_desc), w->g->options.alloc_batch_size);
}

void cilk_internal_malloc_per_worker_destroy(__cilkrts_worker *w) {
#if CILK_DEBUG
    for(int i = 0; i < NUM_BUCKETS; i++) {
            struct im_bucket *bucket = &(w->l->im_desc.buckets[i]);
            int k = free_list_length(bucket->free_list);
            CILK_ASSERT(w,
                (bucket->count_until_free+k) == w->g->options.alloc_batch_size);
    }
#endif
}

