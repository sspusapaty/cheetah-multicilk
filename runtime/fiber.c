#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>

#include "fiber.h"
#include "cilk-internal.h"

#define ROUND_TO_PAGE_SIZE(size) ((size+PAGE_SIZE) & ~((size_t)PAGE_SIZE-1))


extern __attribute__((noreturn)) void invoke_main();

static void make_stack(cilk_fiber * f, size_t stack_size) {

  char* p;
  // We've already validated that the stack size is page-aligned and
  // is a reasonable value.  No need to do any extra rounding here.
  size_t rounded_stack_size = stack_size;

  if (rounded_stack_size < MIN_NUM_PAGES_PER_STACK * (size_t)PAGE_SIZE) {
    // If the specified stack size is too small, round up to
    // MIN_NUM_PAGES_PER_STACK pages.  We need at 2 extra for the guard pages.
    rounded_stack_size = MIN_NUM_PAGES_PER_STACK * (size_t)PAGE_SIZE;
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
  // m_stack_base points to the usable portion where the stack can grow downward
  f->m_stack = p;
  f->m_stack_base = p + rounded_stack_size - PAGE_SIZE;
}

static void free_stack(cilk_fiber * f) {

  if (f->m_stack) {
    size_t rounded_stack_size = f->m_stack_base - f->m_stack + PAGE_SIZE;
    if (munmap(f->m_stack, rounded_stack_size) < 0)
      __cilkrts_bug("Cilk: stack munmap failed\n");
  }
}

static void cilk_fiber_init(cilk_fiber * fiber) {
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
  if (__builtin_cpu_supports("sse")) {
    asm volatile ("ldmxcsr %0" : : "m" (sf->mxcsr));
  }
  asm volatile ("fnclex\n\t" "fldcw %0" : : "m" (sf->fpcsr));
}

void sysdep_save_fp_ctrl_state(__cilkrts_stack_frame *sf) {
  // if (__builtin_cpu_supports("sse")) {
    asm volatile ("stmxcsr %0" : "=m" (sf->mxcsr));
  // }
  asm volatile ("fnstcw %0" : "=m" (sf->fpcsr));
}

char* sysdep_reset_jump_buffers_for_resume(cilk_fiber* fiber,
                                           __cilkrts_stack_frame *sf) {
  CILK_ASSERT_G(fiber);
  char* new_stack_base = fiber->m_stack_base - 256;
    
  // Whatever correction we choose, align the final stack top.
  // This alignment seems to be necessary in particular on 32-bit
  // Linux, and possibly Mac. (Is 32-byte alignment is sufficient?)
  /* 256-byte alignment. Why not? */
  const uintptr_t align_mask = ~(256 -1);
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
  //
  // This feature is only available in ABI 1 or later frames, and only
  // needed on IA64 or Intel64 processors.
  restore_x86_fp_state(sf);
  __builtin_longjmp(sf->ctx, 1);
}

__attribute__((noreturn))
void init_fiber_run(cilk_fiber * fiber, __cilkrts_stack_frame *sf) {

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
    invoke_main();
  }

  CILK_ASSERT_G(0); // should never get here
}

cilk_fiber * cilk_main_fiber_allocate() {
  cilk_fiber * fiber = (cilk_fiber *) malloc(sizeof(cilk_fiber));
  cilk_fiber_init(fiber);
  make_stack(fiber, DEFAULT_STACK_SIZE); // default ~1MB stack
  __cilkrts_alert(ALERT_FIBER, "[?]: Allocate fiber for main %p [%p--%p]\n", 
                  fiber, fiber->m_stack_base, fiber->m_stack);
  
  return fiber;
}

int cilk_main_fiber_deallocate(cilk_fiber * fiber) {
  __cilkrts_alert(ALERT_FIBER, "[?]: Deallocate main fiber %p [%p--%p]\n", 
                  fiber, fiber->m_stack_base, fiber->m_stack);
  free_stack(fiber);
  free(fiber);

  return 0;
}

cilk_fiber * cilk_fiber_allocate(__cilkrts_worker *w) {
  int stacksize = w->g->options.stacksize;

  cilk_fiber * fiber = (cilk_fiber *) malloc(sizeof(cilk_fiber));
  cilk_fiber_init(fiber);
  make_stack(fiber, stacksize); // default ~1MB stack

  return fiber;
}

int cilk_fiber_deallocate(cilk_fiber * fiber) {
  free_stack(fiber);
  free(fiber);
  return 0;
}

