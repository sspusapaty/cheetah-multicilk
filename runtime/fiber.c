#include "fiber.h"

cilk_fiber * cilk_fiber_allocate_from_thread() {
  return NULL;
}

int cilk_fiber_deallocate_from_thread(cilk_fiber * fiber) {
  return 0;
}

void cilk_fiber_set_owner(cilk_fiber * fiber, __cilkrts_worker * owner)  {
  // pass
}
