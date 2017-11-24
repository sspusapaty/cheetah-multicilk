#include "common.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#define CILK_DEBUG 0
#define ALERT_LVL ALERT_SCHED 


const char *const __cilkrts_assertion_failed = "%s:%d: cilk assertion failed: %s\n";

void __cilkrts_bug(const char *fmt,...) {

    /* To reduce user confusion, write all user-generated output
       before the system-generated error message. */
    va_list l;
    fflush(NULL);
    va_start(l, fmt);
    vfprintf(stderr, fmt, l);
    va_end(l);
    fflush(stderr);

    exit(1);
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

#else
void __cilkrts_alert(const int lvl, const char *fmt,...) { }
#endif // CILK_DEBUG
