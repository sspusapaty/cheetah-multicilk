#include "cilk-internal.h"
#include "debug.h"

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

const char *const __cilkrts_assertion_failed = "[%d]: %s:%d: cilk assertion failed: %s\n";
const char *const __cilkrts_assertion_failed_g = "[M]: %s:%d: cilk assertion failed: %s\n";

void cilk_die_internal(struct global_state *const g, const char *complain) {
    cilk_mutex_lock(&(g->print_lock));
    fprintf(stderr, "Fatal error: %s\n", complain);
    cilk_mutex_unlock(&(g->print_lock));
    exit(1);
}

void __cilkrts_bug(const char *fmt,...) {

    /* To reduce user confusion, write all user-generated output
       before the system-generated error message. */
    va_list l;
    fflush(NULL);
    va_start(l, fmt);
    vfprintf(stderr, fmt, l);
    va_end(l);
    fflush(stderr);

    assert(0); // generate core file
}

#if CILK_DEBUG
void __cilkrts_alert(const int lvl, const char *fmt,...) {

  /* To reduce user confusion, write all user-generated output
     before the system-generated error message. */
#ifndef ALERT_LVL
  va_list l;
  va_start(l, fmt);
  vfprintf(stderr, fmt, l);
  va_end(l);
#else
  if (lvl & ALERT_LVL) {
    va_list l;
    va_start(l, fmt);
    vfprintf(stderr, fmt, l);
    va_end(l);
  }
#endif
}
#endif
