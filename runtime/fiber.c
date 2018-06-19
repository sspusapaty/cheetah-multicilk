#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>

#include "fiber.h"
#include "cilk-internal.h"

//===============================================================
// This file maintains fiber-related function that requires
// the internals of a fiber.  The management of the fiber pools
// in fiber-pool.c, which calls the public functions implemented
// in this file.
//===============================================================

#define ROUND_TO_PAGE_SIZE(size) ((size+PAGE_SIZE) & ~(PAGE_SIZE-1))
extern __attribute__((noreturn)) void invoke_main();

//===============================================================
// Private helper functions
//===============================================================

static void make_stack(struct cilk_fiber * f, int stack_size) {

    char* p;
    // We've already validated that the stack size is page-aligned and
    // is a reasonable value.  No need to do any extra rounding here.
    size_t rounded_stack_size = stack_size;

    if (rounded_stack_size < MIN_NUM_PAGES_PER_STACK * PAGE_SIZE) {
        // If the specified stack size is too small, round up to
        // MIN_NUM_PAGES_PER_STACK.  We need 2 extra for the guard pages.
        rounded_stack_size = MIN_NUM_PAGES_PER_STACK * PAGE_SIZE;
    } else {
        rounded_stack_size = ROUND_TO_PAGE_SIZE(stack_size);
    }

    p = (char*)mmap(0, rounded_stack_size,
            PROT_READ|PROT_WRITE,
            MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK|MAP_GROWSDOWN,
            -1, 0);
    if (MAP_FAILED == p) {
        __cilkrts_bug("Cilk: stack mmap failed\n");
        // For whatever reason (probably ran out of memory), mmap() failed.
        // There is no stack to return, so the program loses parallelism.
        f->m_stack = NULL;
        f->m_stack_base = NULL;

        return;
    }

    // mprotect guard pages.
    mprotect(p + rounded_stack_size - PAGE_SIZE, PAGE_SIZE, PROT_NONE);
    mprotect(p, PAGE_SIZE, PROT_NONE);

    // m_stack points to the beginning of stack, including mprotected page
    // m_stack_base points to the usable portion where the stack grows downward
    f->m_stack = p;
    f->m_stack_base = p + rounded_stack_size - PAGE_SIZE;
}

static void free_stack(struct cilk_fiber * f) {

    if (f->m_stack) {
        size_t rounded_stack_size = f->m_stack_base - f->m_stack + PAGE_SIZE;
        if (munmap(f->m_stack, rounded_stack_size) < 0)
            __cilkrts_bug("Cilk: stack munmap failed\n");
    }
}

static void fiber_init(struct cilk_fiber * fiber) {
    fiber->m_stack = NULL;
    fiber->m_stack_base = NULL;
    fiber->owner = NULL;
}

/*
 * Restore the floating point state that is stored in a stack frame at each
 * spawn.  This should be called each time a frame is resumed.
 *
 * Only valid for IA32 and Intel64 processors.
 */
static void restore_x86_fp_state (__cilkrts_stack_frame *sf) {
    // Assume cpu supports sse
    asm volatile ("ldmxcsr %0" : : "m" (sf->mxcsr));
    asm volatile ("fnclex\n\t" "fldcw %0" : : "m" (sf->fpcsr));
}


//===============================================================
// Supported public functions
//===============================================================

void sysdep_save_fp_ctrl_state(__cilkrts_stack_frame *sf) {
    // Assume cpu supports sse
    asm volatile ("stmxcsr %0" : "=m" (sf->mxcsr));
    asm volatile ("fnstcw %0" : "=m" (sf->fpcsr));
}

char* sysdep_reset_jump_buffers_for_resume(struct cilk_fiber* fiber,
                                           __cilkrts_stack_frame *sf) {
    CILK_ASSERT_G(fiber);
    char* new_stack_base = fiber->m_stack_base - 256;

    // Whatever correction we choose, align the final stack top.
    // This alignment seems to be necessary in particular on 32-bit
    // Linux, and possibly Mac. (Is 32-byte alignment is sufficient?)
    const uintptr_t align_mask = ~(256 -1); // 256-byte alignment. 
    new_stack_base = (char*)((size_t)new_stack_base & align_mask);
    void* sp = (void*) new_stack_base;
    SP(sf) = sp;

    /* Debugging: make sure stack is accessible. */
    ((volatile char *)sp)[-1];

    return sp;
}

__attribute__((noreturn))
void sysdep_longjmp_to_sf(__cilkrts_stack_frame *sf) {

    __cilkrts_alert(ALERT_FIBER, "[%d]: longjmp to sf, BP/SP/PC: %p/%p/%p\n",
                    sf->worker->self, FP(sf), SP(sf), PC(sf));

    // Restore the floating point state that was set in this frame at the
    // last spawn.
    // This feature is only available in ABI 1 or later frames, and only
    // needed on IA64 or Intel64 processors.
    restore_x86_fp_state(sf);
    __builtin_longjmp(sf->ctx, 1);
}

__attribute__((noreturn))
void init_fiber_run(__cilkrts_worker *w, 
                    struct cilk_fiber * fiber,
                    __cilkrts_stack_frame *sf) {

    // owner of fiber not set at the moment
    __cilkrts_alert(ALERT_FIBER, "[?]: (cilk_fiber_run) starting fiber %p\n", fiber);

    if( __builtin_setjmp(sf->ctx) == 0 ) {
        size_t frame_size = (size_t)FP(sf) - (size_t)SP(sf);
        // Macs require 16-byte alignment.  
        if (frame_size & (16-1))
            frame_size += 16 - (frame_size  & (16-1));

        // Assert that we are getting a reasonable frame size out of
        // it.  If this run() function is using more than 4096 bytes
        // of space for its local variables / any state that spills to
        // registers, something is probably *very* wrong here...
        CILK_ASSERT_G(frame_size < PAGE_SIZE);

        // Change stack pointer to fiber stack.  Offset the
        // calculation by the frame size, so that we've allocated
        // enough extra space from the top of the stack we are
        // switching to for any temporaries required for this run()
        // function.
        SP(sf) = fiber->m_stack_base - frame_size;
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
    struct cilk_fiber *fiber = cilk_internal_malloc(w, sizeof(*fiber));
    fiber_init(fiber);
    make_stack(fiber, DEFAULT_STACK_SIZE); // default ~1MB stack
    __cilkrts_alert(ALERT_FIBER, "[?]: Allocate fiber %p [%p--%p]\n", 
                    fiber, fiber->m_stack_base, fiber->m_stack);
    return fiber;
}

void cilk_fiber_deallocate(__cilkrts_worker *w, struct cilk_fiber * fiber) {
    __cilkrts_alert(ALERT_FIBER, "[?]: Deallocate fiber %p [%p--%p]\n", 
                    fiber, fiber->m_stack_base, fiber->m_stack);
    free_stack(fiber);
    cilk_internal_free(w, fiber, sizeof(*fiber));
}

struct cilk_fiber *cilk_main_fiber_allocate() {
    struct cilk_fiber *fiber = malloc(sizeof(*fiber));
    fiber_init(fiber);
    make_stack(fiber, DEFAULT_STACK_SIZE); // default ~1MB stack
    __cilkrts_alert(ALERT_FIBER, "[?]: Allocate main fiber %p [%p--%p]\n", 
                    fiber, fiber->m_stack_base, fiber->m_stack);
    return fiber;
}

void cilk_main_fiber_deallocate(struct cilk_fiber * fiber) {
    __cilkrts_alert(ALERT_FIBER, "[?]: Deallocate main fiber %p [%p--%p]\n", 
                    fiber, fiber->m_stack_base, fiber->m_stack);
    free_stack(fiber);
    free(fiber);
}
