#ifndef _STACK_MANIP_H
#define _STACK_MANIP_H

#include "closure.h"
#include "stack_frame.h"

//////------ COPIED FROM CILK PLUS

/* The functions __cilkrts_put_stack and __cilkrts_take_stack keep track of
 * changes in the stack's depth between when the point at which a frame is
 * stolen and when it is resumed at a sync.  A stolen frame typically goes
 * through the following phase changes:
 *
 *   1. Suspend frame while stealing it.
 *   2. Resume stolen frame at begining of continuation
 *   3. Suspend stolen frame at a sync
 *   4. Resume frame (no longer marked stolen) after the sync
 *
 * When the frame is suspended (steps 1 and 3), __cilkrts_put_stack is called to
 * establish the stack pointer for the sync.  When the frame is resumed (steps
 * 2 and 4), __cilkrts_take_stack is called to indicate the stack pointer
 * (which may be on a different stack) at
 * the point of resume.  If the stack pointer changes between steps 2 and 3,
 * e.g., as a result of pushing 4 bytes onto the stack,
 * the offset is reflected in the value of ff->sync_sp after step 3 relative to
 * its value after step 1 (e.g., the value of ff->sync_sp after step 3 would be
 * 4 less than its value after step 1, for a down-growing stack).
 *
 * Imp detail: The actual call chains for each of these phase-change events is:
 *
 *   1. unroll_call_stack -> make_unrunnable  -> __cilkrts_put_stack
 *    --> ??
 *   2. do_work           -> __cilkrts_resume -> __cilkrts_take_stack
 *    --> ??
 *   3. do_sync -> disown -> make_runnable    -> __cilkrts_put_stack
 *    --> __cilkrts_c_sync -> execute_reductions_for_sync -> __cilkrts_put_stack
 *   4. __cilkrts_resume                      -> __cilkrts_take_stack
 *    --> ??
 *
 * (The above is a changeable implementation detail.  The resume, sequence, in
 * particular, is more complex on some operating systems.)
 */

/**
 * @brief Records the stack pointer within the @c sf stack frame as the
 * current stack pointer at the point of suspending full frame @c ff.
 *
 * @pre @c ff->sync_sp must be either null or contain the result of a prior call to
 *      @c __cilkrts_take_stack().
 * @pre If @c ff->sync_sp is not null, then @c SP(sf) must refer to the same stack as
 *      the @c sp argument to the prior call to @c __cilkrts_take_stack().
 * 

 * @post If @c ff->sync_sp was null before the call, then @c
 *       ff->sync_sp will be set to @c SP(sf).
 * @post Otherwise, @c ff->sync_sp will be restored to the value it had just prior
 *       to the last call to @c __cilkrts_take_stack(), except offset by any change
 *       in the stack pointer between the call to @c __cilkrts_take_stack() and
 *       this call to @c __cilkrts_put_stack().
 *
 * @param ff The full frame that is being suspended.
 * @param sf The @c __cilkrts_stack_frame that is being suspended.  The stack
 *   pointer will be taken from the jmpbuf contained within this
 *   @c __cilkrts_stack_frame.
 */
void __cilkrts_put_stack(Closure *ff, __cilkrts_stack_frame *sf);

/**
 * @brief Records the stack pointer @c sp as the stack pointer at the point of
 * resuming execution on full frame @c ff.
 *
 * The value of @c sp may be on a different stack than the original
 * value recorded for the stack pointer using __cilkrts_put_stack().
 *
 * @pre  @c ff->sync_sp must contain a value set by @c __cilkrts_put_stack().
 *
 * @post @c ff->sync_sp contains an *integer* value used to compute a change in the
 *       stack pointer upon the next call to @c __cilkrts_take_stack().
 * @post If @c sp equals @c ff->sync_sp, then @c ff->sync_sp is set to null.
 *
 * @param ff The full frame that is being resumed.
 * @param sp The stack pointer for the stack the function is being resumed on.
 */
void __cilkrts_take_stack(Closure *ff, void *sp);

#endif
