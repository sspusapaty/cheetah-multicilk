#include "stack_manip.h"
#include "jumbuf.h"

/**
 * Fetch the stack pointer from a __cilkrts_stack_frame.  The jmpbuf was
 * initialized before a cilk_spawn or cilk_sync.
 *
 * @param sf __cilkrts_stack_frame containing the jmpbuf.
 *
 * @return the stack pointer from the ctx.
 */
inline char *__cilkrts_get_sp(__cilkrts_stack_frame *sf)
{
    return (char *)SP(sf);
}

void __cilkrts_put_stack(Closure *ff, __cilkrts_stack_frame *sf)
{
    /* When suspending frame ff prior to stealing it, __cilkrts_put_stack is
     * used to store the stack pointer for eventual sync.  When suspending
     * frame ff prior to a sync, __cilkrts_put_stack is called to re-establish
     * the sync stack pointer, offsetting it by any change in the stack depth
     * that occured between the spawn and the sync.
     * Although it is not usually meaningful to add two pointers, the value of
     * ff->sync_sp at the time of this call is really an integer, not a
     * pointer.
     */
    ptrdiff_t sync_sp_i = (ptrdiff_t) ff->sync_sp;
    char* sp = (char*) __cilkrts_get_sp(sf);

    ff->sync_sp = sp + sync_sp_i;

    DBGPRINTF("%d-                __cilkrts_put_stack - adjust (+) sync "
              "stack of full frame %p (+sp: %p) to %p\n",
              __cilkrts_get_tls_worker()->self, ff, sp, ff->sync_sp);
}

void __cilkrts_take_stack(Closure *ff, void *sp)
{
    /* When resuming the parent after a steal, __cilkrts_take_stack is used to
     * subtract the new stack pointer from the current stack pointer, storing
     * the offset in ff->sync_sp.  When resuming after a sync,
     * __cilkrts_take_stack is used to subtract the new stack pointer from
     * itself, leaving ff->sync_sp at zero (null).  Although the pointers being
     * subtracted are not part of the same contiguous chunk of memory, the
     * flat memory model allows us to subtract them and get a useable offset.
     */
    ptrdiff_t sync_sp_i = ff->sync_sp - (char*) sp;

    ff->sync_sp = (char *) sync_sp_i;

    DBGPRINTF("%d-                __cilkrts_take_stack - adjust (-) sync "
              "stack of full frame %p to %p (-sp: %p)\n",
              __cilkrts_get_tls_worker()->self, ff, ff->sync_sp, sp);
}
