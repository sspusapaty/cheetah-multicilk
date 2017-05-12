#include "fiber.h"
#include "common.h"
#include "tls.h"

#include <sys/mman.h>
#include <stdlib.h>

void make_stack(cilk_fiber * f, size_t stack_size)
{
  char* p;
  // We've already validated that the stack size is page-aligned and
  // is a reasonable value.  No need to do any extra rounding here.
  size_t rounded_stack_size = stack_size;
  f->stack_size = stack_size;

  // Normally, we have already validated that the stack size is
  // aligned to 4K.  In the rare case that pages are huge though, we
  // need to do some extra checks.
  if (rounded_stack_size < 3 * (size_t)PAGE_SIZE) {
    // If the specified stack size is too small, round up to 3
    // pages.  We need at least 2 extra for the guard pages.
    rounded_stack_size = 3 * (size_t)PAGE_SIZE;
  }
  else {
    // Otherwise, the stack size is large enough, but might not be
    // a multiple of page size.  Round up to nearest multiple of
    // s_page_size, just to be safe.
    size_t remainder = rounded_stack_size % PAGE_SIZE;
    if (remainder) {
      rounded_stack_size += PAGE_SIZE - remainder;
    }
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

  f->m_stack = p;
  f->m_stack_base = p + rounded_stack_size - PAGE_SIZE;
}


void free_stack(cilk_fiber * f)
{
  if (f->m_stack) {
    size_t rounded_stack_size = f->m_stack_base - f->m_stack + PAGE_SIZE;
    if (munmap(f->m_stack, rounded_stack_size) < 0)
      __cilkrts_bug("Cilk: stack munmap failed\n");
  }
}

void cilk_fiber_set_resumable(cilk_fiber * fiber, int state) {
  fiber->m_flags = state ?  (fiber->m_flags | RESUMABLE) : (fiber->m_flags & (~RESUMABLE));
}

void cilk_fiber_set_allocated_from_thread(cilk_fiber * fiber, int state) {
  fiber->m_flags = state ?  (fiber->m_flags | ALLOCATED_FROM_THREAD) : (fiber->m_flags & (~ALLOCATED_FROM_THREAD));
}

// Jump to resume other fiber.  We may or may not come back.
void cilk_fiber_resume_other(cilk_fiber * other)
{
  if (cilk_fiber_is_resumable(other)) {
    cilk_fiber_set_resumable(other, 0);
    // Resume by longjmp'ing to the place where we suspended.
    __builtin_longjmp(other->ctx, 1);
  }
  else {
    // Otherwise, we've never ran this fiber before.  Start the
    // proc method.
    cilk_fiber_run(other);
  }
}

void cilk_fiber_init(cilk_fiber * fiber) {
  fiber->m_stack = NULL;
  fiber->m_stack_base = NULL;
  
  fiber->owner = NULL;
  fiber->resume_sf = NULL;
  
  fiber->m_start_proc = NULL;
  fiber->m_post_switch_proc = NULL;
  
  fiber->m_flags = 0;
}

// ----------

cilk_fiber * cilk_fiber_allocate_from_thread() {
  cilk_fiber * f = (cilk_fiber *) malloc(sizeof(cilk_fiber));
  cilk_fiber_init(f);
  
  cilk_fiber_set_allocated_from_thread(f, 1);
  __cilkrts_set_tls_cilk_fiber(f);
  __cilkrts_alert(4, "Allocated fiber %p/%p\n", f, f->m_stack_base);
  return f;
}

cilk_fiber * cilk_fiber_allocate_from_heap() {
  cilk_fiber * f = (cilk_fiber *) malloc(sizeof(cilk_fiber));
  cilk_fiber_init(f);
  
  make_stack(f, 4000000); // ~4MB stack

  __cilkrts_alert(4, "Allocated fiber %p/%p\n", f, f->m_stack_base);
  
  return f;
}

int cilk_fiber_deallocate_from_thread(cilk_fiber * fiber) {
  free(fiber);
  return 0;
}

int cilk_fiber_deallocate_from_heap(cilk_fiber * fiber) {
  free_stack(fiber);
  free(fiber);
  return 0;
}

void cilk_fiber_run(cilk_fiber * fiber) {
  // Only fibers created from a pool have a proc method to run and execute. 
  CILK_ASSERT(fiber->m_start_proc);
  CILK_ASSERT(!cilk_fiber_is_allocated_from_thread(fiber));
  CILK_ASSERT(!cilk_fiber_is_resumable(fiber));

  // TBD: This setjmp/longjmp pair simply changes the stack pointer.
  // We could probably replace this code with some assembly.
  //if (! setjmp(fiber->ctx))
  //{
  char * rsp;
  char * rbp;
  ASM_GET_SP(rsp);
  ASM_GET_FP(rbp);
  // Calculate the size of the current stack frame (i.e., this
  // run() function.  
  size_t frame_size = (size_t)rbp - (size_t)rsp;
  // (size_t)JMPBUF_FP(fiber->ctx) - (size_t)JMPBUF_SP(fiber->ctx);

  // Macs require 16-byte alignment.  Do it always because it just
  // doesn't matter
  if (frame_size & (16-1))
    frame_size += 16 - (frame_size  & (16-1));

  // Assert that we are getting a reasonable frame size out of
  // it.  If this run() function is using more than 4096 bytes
  // of space for its local variables / any state that spills to
  // registers, something is probably *very* wrong here...
  //
  // 4096 bytes just happens to be a number that seems "large
  // enough" --- for an example GCC 32-bit compilation, the
  // frame size was 48 bytes.
  CILK_ASSERT(frame_size < 4096);

  // Change stack pointer to fiber stack.  Offset the
  // calculation by the frame size, so that we've allocated
  // enough extra space from the top of the stack we are
  // switching to for any temporaries required for this run()
  // function.
  // JMPBUF_SP(fiber->ctx) = fiber->m_stack_base - frame_size;
  rsp = fiber->m_stack_base - frame_size;

  // longjmp(fiber->ctx, 1);
  ASM_SET_SP(rsp);
  //}

  // Note: our resetting of the stack pointer is valid only if the
  // compiler has not saved any temporaries onto the stack for this
  // function before the longjmp that we still care about at this
  // point.
    
  // Verify that 1) 'this' is still valid and 2) '*this' has not been
  // corrupted.
  //CILK_ASSERT(magic_number == m_magic);

  // If the fiber that switched to me wants to be deallocated, do it now.
  cilk_fiber_do_post_switch_actions(fiber);

  // Now call the user proc on the new stack
  fiber->m_start_proc(fiber);

  // alloca() to force generation of frame pointer.  The argument to alloca
  // is contrived to prevent the compiler from optimizing it away.  This
  // code should never actually be executed.
  int* dummy = (int*) alloca((sizeof(int) + (size_t) fiber->m_start_proc) & 0x1);
  *dummy = 0xface;

  // User proc should never return.
  __cilkrts_bug("Should not get here");
}

void cilk_fiber_do_post_switch_actions(cilk_fiber * self)
{
  if (self->m_post_switch_proc) 
    {
      cilk_fiber_proc proc = self->m_post_switch_proc;
      self->m_post_switch_proc = NULL;
      proc(self);
    }

  /* WTF Emacs */
  /* if (m_pending_remove_ref) */
  /* { */
  /*     m_pending_remove_ref->remove_reference(m_pending_pool); */

  /*     // Even if we don't free it,  */
  /*     m_pending_remove_ref = NULL; */
  /*     m_pending_pool   = NULL; */
  /* } */
}

void cilk_fiber_suspend_self_and_resume_other(cilk_fiber * self, cilk_fiber * other)
{
#if FIBER_DEBUG >=1
  fprintf(stderr, "suspend_self_and_resume_other: self =%p, other=%p [owner=%p, resume_sf=%p]\n",
	  self, other, other->owner, other->resume_sf);
#endif
  __cilkrts_alert(3, "[%d owner]: switching from fiber %p to %p\n", self->owner->self, self, other);

  // Pass along my owner.
  other->owner = self->owner;
  self->owner  = NULL;

  // Change this fiber to resumable.
  CILK_ASSERT(!cilk_fiber_is_resumable(self));
  cilk_fiber_set_resumable(self, 1);

  // cilk_fiber_sysdep* self = self->sysdep();
  // self->suspend_self_and_resume_other_sysdep(other->sysdep());
    
  __cilkrts_set_tls_cilk_fiber(other);

  // CILK_ASSERT(this->is_resumable());

  // Jump to the other fiber.  We expect to come back.
  if (! __builtin_setjmp(self->ctx)) {
    cilk_fiber_resume_other(other);
  }

  // Return here when another fiber resumes me.
  // If the fiber that switched to me wants to be deallocated, do it now.
  cilk_fiber_do_post_switch_actions(self);
}
