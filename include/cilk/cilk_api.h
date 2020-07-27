#ifndef _CILK_API_H
#define _CILK_API_H
#ifdef __cplusplus
extern "C" {
#endif

#ifdef OPENCILK_LIBRARY
#define VISIBILITY visibility("protected")
#else
#define VISIBILITY visibility("default")
#endif

extern int __cilkrts_atinit(void (*callback)(void)) __attribute__((VISIBILITY));
extern int __cilkrts_atexit(void (*callback)(void)) __attribute__((VISIBILITY));
extern int __cilkrts_get_worker_number(void)
    __attribute__((VISIBILITY, deprecated));
struct __cilkrts_worker *__cilkrts_get_tls_worker(void)
    __attribute__((VISIBILITY));

#undef VISIBILITY

#ifdef __cplusplus
}
#endif

#endif /* _CILK_API_H */
