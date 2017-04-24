#include "fiber.h"
#include "common.h"

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
      __cilkrts_bug("Cilk: stack munmap failed error %d\n");
  }
}

cilk_fiber * cilk_fiber_allocate_from_thread() {
  cilk_fiber * f = (cilk_fiber *) malloc(sizeof(cilk_fiber));
  return f;
}


cilk_fiber * cilk_fiber_allocate_from_heap() {
  cilk_fiber * f = (cilk_fiber *) malloc(sizeof(cilk_fiber));
  make_stack(f, 1000000);
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

void cilk_fiber_set_owner(cilk_fiber * fiber, __cilkrts_worker * owner)  {
  // pass
}
