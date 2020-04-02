#ifndef _DEBUG_H
#define _DEBUG_H

#include <stdarg.h>

#include "rts-config.h"

// forward declaration for using struct global_stat
struct global_state;

#define CILK_CHECK(g, cond, complain)                                          \
    if (!cond)                                                                 \
    cilk_die_internal(g, complain)

#define ALERT_LVL 0 /* ALERT_BOOT */
#define ALERT_NONE 0x0
#define ALERT_FIBER 0x1
#define ALERT_SYNC 0x2
#define ALERT_SCHED 0x4
#define ALERT_STEAL 0x8
#define ALERT_EXCEPT 0x10
#define ALERT_RETURN 0x20
#define ALERT_BOOT 0x40
#define ALERT_CFRAME 0x80
#define ALERT_REDUCE 0x100

// Unused: compiler inlines the stack frame creation
// #define CILK_STACKFRAME_MAGIC 0xCAFEBABE

CHEETAH_INTERNAL_NORETURN void __cilkrts_bug(const char *fmt, ...);
CHEETAH_INTERNAL_NORETURN void cilk_die_internal(struct global_state *const g,
                                                 const char *complain);

#if CILK_DEBUG
void __cilkrts_alert(const int lvl, const char *fmt, ...);
#define __cilkrts_alert(lvl, fmt, ...) __cilkrts_alert(lvl, fmt, ##__VA_ARGS__)
#define WHEN_CILK_DEBUG(ex) ex

/** Standard text for failed assertion */
CHEETAH_INTERNAL extern const char *const __cilkrts_assertion_failed;
CHEETAH_INTERNAL extern const char *const __cilkrts_assertion_failed_g;

#define CILK_ASSERT(w, ex)                                                     \
    (__builtin_expect((ex) != 0, 1)                                            \
         ? (void)0                                                             \
         : __cilkrts_bug(__cilkrts_assertion_failed, w->self, __FILE__,        \
                         __LINE__, #ex))

#define CILK_ASSERT_G(ex)                                                      \
    (__builtin_expect((ex) != 0, 1)                                            \
         ? (void)0                                                             \
         : __cilkrts_bug(__cilkrts_assertion_failed_g, __FILE__, __LINE__,     \
                         #ex))

#define CILK_ABORT(w, msg)                                                     \
    __cilkrts_bug(__cilkrts_assertion_failed, w->self, __FILE__, __LINE__, msg)

#define CILK_ABORT_G(msg)                                                      \
    __cilkrts_bug(__cilkrts_assertion_failed_g, __FILE__, __LINE__, msg)

#else
#define __cilkrts_alert(lvl, fmt, ...)
#define CILK_ASSERT(w, ex)
#define CILK_ASSERT_G(ex)
#define CILK_ABORT(w, msg)
#define CILK_ABORT_G(msg)
#define WHEN_CILK_DEBUG(ex)
#endif // CILK_DEBUG

// to silence compiler warning for vars only used during debugging
#define USE_UNUSED(var) (void)(var)
#endif
