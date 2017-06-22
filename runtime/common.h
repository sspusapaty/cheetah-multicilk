#ifndef _COMMON_H
#define _COMMON_H

#define NOBODY -1

#define PAGE_SIZE 4096

#define CILK_CACHE_LINE 64

#define CILK_CACHE_LINE_PAD  char __dummy[CILK_CACHE_LINE]

#define ALERT_FIBER  0x1
#define ALERT_SYNC   0x2
#define ALERT_SCHED  0x4
#define ALERT_STEAL  0x8
#define ALERT_EXCEPT 0x10
#define ALERT_RETURN 0x20
#define ALERT_BOOT   0x40
#define ALERT_CFRAME 0x80

void __cilkrts_bug(const char *fmt,...);
void __cilkrts_alert(const int lvl, const char *fmt,...);

/** Standard text for failed assertion */
extern const char *const __cilkrts_assertion_failed;

#define CILK_ASSERT(ex)                                                 \
    (__builtin_expect((ex) != 0, 1) ? (void)0 :                         \
     __cilkrts_bug(__cilkrts_assertion_failed, __FILE__, __LINE__,  #ex))

#define CILK_ASSERT_MSG(ex, msg)                                        \
    (__builtin_expect((ex) != 0, 1) ? (void)0 :                         \
     __cilkrts_bug(__cilkrts_assertion_failed, __FILE__, __LINE__,      \
                   #ex "\n    " msg))


#endif
