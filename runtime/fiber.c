#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#ifdef __BSD__
#include <sys/cpuset.h>
#include <sys/param.h>
#endif

#include "cilk-internal.h"
#include "fiber.h"
#include "init.h"

#include <string.h> /* DEBUG */

struct cilk_fiber {
    char *alloc_low;         // first byte of mmap-ed region
    char *stack_low;         // lowest usable byte of stack
    char *stack_high;        // one byte above highest usable byte of stack
    char *alloc_high;        // last byte of mmap-ed region
    __cilkrts_worker *owner; // worker using this fiber
};

#ifndef MAP_GROWSDOWN
/* MAP_GROWSDOWN is implied on BSD */
#define MAP_GROWSDOWN 0
#endif
#ifndef MAP_STACK
/* MAP_STACK is not available on Darwin */
#define MAP_STACK 0
#endif

#define LOW_GUARD_PAGES 1
#define HIGH_GUARD_PAGES 1

//===============================================================
// This file maintains fiber-related function that requires
// the internals of a fiber.  The management of the fiber pools
// in fiber-pool.c, which calls the public functions implemented
// in this file.
//===============================================================

//===============================================================
// Private helper functions
//===============================================================

static void make_stack(struct cilk_fiber *f, size_t stack_size) {
    const int page_shift = cheetah_page_shift;
    const size_t page_size = 1U << page_shift;

    size_t stack_pages = (stack_size + page_size - 1) >> cheetah_page_shift;
    stack_pages += LOW_GUARD_PAGES + HIGH_GUARD_PAGES;

    /* Stacks must be at least MIN_NUM_PAGES_PER_STACK pages,
       a count which includes two guard pages. */
    if (stack_pages < MIN_NUM_PAGES_PER_STACK) {
        stack_pages = MIN_NUM_PAGES_PER_STACK;
    } else if (stack_pages > MAX_NUM_PAGES_PER_STACK) {
        stack_pages = MAX_NUM_PAGES_PER_STACK;
    }
    char *alloc_low = (char *)mmap(
        0, stack_pages * page_size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK | MAP_GROWSDOWN, -1, 0);
    if (MAP_FAILED == alloc_low) {
        cilkrts_bug(NULL, "Cilk: stack mmap failed");
        /* Currently unreached.  TODO: Investigate more graceful
           error handling. */
        f->alloc_low = NULL;
        f->stack_low = NULL;
        f->stack_high = NULL;
        f->alloc_high = NULL;
        return;
    }
    char *alloc_high = alloc_low + stack_pages * page_size;
    char *stack_high = alloc_high - page_size;
    char *stack_low = alloc_low + page_size;
    // mprotect guard pages.
    mprotect(alloc_low, page_size, PROT_NONE);
    mprotect(stack_high, page_size, PROT_NONE);
    f->alloc_low = alloc_low;
    f->stack_low = stack_low;
    f->stack_high = stack_high;
    f->alloc_high = alloc_high;
    if (DEBUG_ENABLED(MEMORY_SLOW))
        memset(stack_low, 0x11, stack_size);
}

static void free_stack(struct cilk_fiber *f) {
    if (f->alloc_low) {
        if (DEBUG_ENABLED(MEMORY_SLOW))
            memset(f->stack_low, 0xbb, f->stack_high - f->stack_low);
        if (munmap(f->alloc_low, f->alloc_high - f->alloc_low) < 0)
            cilkrts_bug(NULL, "Cilk: stack munmap failed");
        f->alloc_low = NULL;
        f->stack_low = NULL;
        f->stack_high = NULL;
        f->alloc_high = NULL;
    }
}

static void fiber_init(struct cilk_fiber *fiber) {
    fiber->alloc_low = NULL;
    fiber->stack_low = NULL;
    fiber->stack_high = NULL;
    fiber->alloc_high = NULL;
    fiber->owner = NULL;
}

/*
 * Restore the floating point state that is stored in a stack frame at each
 * spawn.  This should be called each time a frame is resumed.  OpenCilk
 * only saves MXCSR.  The 80387 status word is obsolete.
 */
static void sysdep_restore_fp_state(__cilkrts_stack_frame *sf) {
    /* TODO: Find a way to do this only when using floating point. */
#ifdef CHEETAH_SAVE_MXCSR
#if 1
    asm volatile("ldmxcsr %0" : : "m"(sf->mxcsr));
#else
    /* Disabled because LLVM's implementation is bad. */
    __builtin_ia32_ldmxcsr(sf->mxcsr); /* aka _mm_getcsr */
#endif
#endif

#ifdef __AVX__
    /* VZEROUPPER improves performance when mixing SSE and AVX code.
       VZEROALL would work as well here because vector registers are
       dead but takes about 10 cycles longer. */
    __builtin_ia32_vzeroupper();
#endif
}

//===============================================================
// Supported public functions
//===============================================================

void sysdep_save_fp_ctrl_state(__cilkrts_stack_frame *sf) {
#ifdef CHEETAH_SAVE_MXCSR
#if 1
    asm("stmxcsr %0" : "=m"(sf->mxcsr));
#else
    /* Disabled because LLVM's implementation is bad. */
    sf->mxcsr = __builtin_ia32_stmxcsr(); /* aka _mm_setcsr */
#endif
#endif
}

char *sysdep_reset_stack_for_resume(struct cilk_fiber *fiber,
                                    __cilkrts_stack_frame *sf) {
    CILK_ASSERT_G(fiber);
    /* stack_high of the new fiber is aligned to a page size
       boundary just after usable memory.  */
    /* JFC: This may need to be more than 256 if the stolen function
       has more than 256 bytes of outgoing arguments.  I think
       Cilk++ looked at fp-sp in the stolen function.
       It should also exceed frame_size in init_fiber_run. */
    size_t align = MAX_STACK_ALIGN > 256 ? MAX_STACK_ALIGN : 256;
    char *sp = fiber->stack_high - align;
    SP(sf) = sp;

    /* Debugging: make sure stack is accessible. */
    ((volatile char *)sp)[-1];

    return sp;
}

CHEETAH_INTERNAL_NORETURN
void sysdep_longjmp_to_sf(__cilkrts_stack_frame *sf) {
    cilkrts_alert(FIBER, sf->worker, "longjmp to sf, BP/SP/PC: %p/%p/%p",
                  FP(sf), SP(sf), PC(sf));

#if defined CHEETAH_SAVE_MXCSR
    // Restore the floating point state that was set in this frame at the
    // last spawn.
    sysdep_restore_fp_state(sf);
#endif
    __builtin_longjmp(sf->ctx, 1);
}

CHEETAH_INTERNAL_NORETURN
void init_fiber_run(__cilkrts_worker *w, struct cilk_fiber *fiber,
                    __cilkrts_stack_frame *sf) {
    // owner of fiber not set at the moment
    cilkrts_alert(FIBER, w, "(cilk_fiber_run) starting fiber %p", fiber);

    /* The if-else block is a longwinded way of changing the stack pointer
       onto the fiber.  A single assembly instruction would be sufficient
       if the compiler understood it could not save stack addresses in
       registers across the operation.

       TODO 1: It would probably be simpler to write a little assembly
       language for each target.
       TODO 2: A comment in the old Cilk code said longjmp should not
       be used to return to a setjmp in the same function. */
    if (__builtin_setjmp(sf->ctx) == 0) {
        size_t frame_size = (size_t)FP(sf) - (size_t)SP(sf);
        /* This should not be needed if the original frame pointer
           is aligned, but the old Cilk code aligned the stack and
           doing it doesn't cost much. */
        frame_size =
            (frame_size + MAX_STACK_ALIGN - 1) & ~(MAX_STACK_ALIGN - 1);

        /* The stack frame should be small.  If it exceeeds 1000 bytes
           there is probably a bug in the frame size calculation, e.g.
           the compiler may have eliminated the frame pointer. */
        CILK_ASSERT_G(frame_size <= 1000);

        /* Switch to the fiber reserving frame_size bytes for this
           function's stack. */
        SP(sf) = fiber->stack_high - frame_size;
        __builtin_longjmp(sf->ctx, 1);
    } else {
        // fiber is set up; now we longjmp into invoke_main; switch sched_stats
        CILK_STOP_TIMING(w, INTERVAL_SCHED);
        CILK_START_TIMING(w, INTERVAL_WORK);
        invoke_main();
    }

    CILK_ASSERT_G(0); // should never get here
}

struct cilk_fiber *cilk_fiber_allocate(__cilkrts_worker *w) {
    struct cilk_fiber *fiber =
        cilk_internal_malloc(w, sizeof(*fiber), IM_FIBER);
    fiber_init(fiber);
    make_stack(fiber, DEFAULT_STACK_SIZE); // default ~1MB stack
    cilkrts_alert(FIBER, w, "Allocate fiber %p [%p--%p]", fiber,
                  fiber->stack_low, fiber->stack_high);
    return fiber;
}

void cilk_fiber_deallocate(__cilkrts_worker *w, struct cilk_fiber *fiber) {
    cilkrts_alert(FIBER, w, "Deallocate fiber %p [%p--%p]", fiber,
                  fiber->stack_low, fiber->stack_high);
    if (DEBUG_ENABLED_STATIC(FIBER))
        CILK_ASSERT(w, !in_fiber(fiber, w->current_stack_frame));
    free_stack(fiber);
    cilk_internal_free(w, fiber, sizeof(*fiber), IM_FIBER);
}

void cilk_fiber_deallocate_global(struct global_state *g,
                                  struct cilk_fiber *fiber) {
    cilkrts_alert(FIBER, NULL, "Deallocate fiber %p [%p--%p]", fiber,
                  fiber->stack_low, fiber->stack_high);
    free_stack(fiber);
    cilk_internal_free_global(g, fiber, sizeof(*fiber), IM_FIBER);
}

struct cilk_fiber *cilk_main_fiber_allocate() {
    struct cilk_fiber *fiber = malloc(sizeof(*fiber));
    fiber_init(fiber);
    make_stack(fiber, DEFAULT_STACK_SIZE); // default ~1MB stack
    cilkrts_alert(FIBER, NULL, "[?]: Allocate main fiber %p [%p--%p]", fiber,
                  fiber->stack_low, fiber->stack_high);
    return fiber;
}

void cilk_main_fiber_deallocate(struct cilk_fiber *fiber) {
    cilkrts_alert(FIBER, NULL, "[?]: Deallocate main fiber %p [%p--%p]", fiber,
                  fiber->stack_low, fiber->stack_high);
    free_stack(fiber);
    free(fiber);
}

int in_fiber(struct cilk_fiber *fiber, void *p) {
    void *low = fiber->stack_low, *high = fiber->stack_high;
    return p >= low && p < high;
}
