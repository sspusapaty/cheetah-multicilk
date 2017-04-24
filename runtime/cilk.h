#ifndef _CILK_H
#define _CILK_H

#define SYNC_READY 0
#define SYNC_NOT_READY 1

/* CILK_FRAME_STOLEN is set if the frame has ever been stolen. */
#define CILK_FRAME_STOLEN 0x01

/* CILK_FRAME_UNSYNCHED is set if the frame has been stolen and
   is has not yet executed _Cilk_sync. It is technically a misnomer in that a
   frame can have this flag set even if all children have returned. */
#define CILK_FRAME_UNSYNCHED 0x02

/* Is this frame detached (spawned)? If so the runtime needs
   to undo-detach in the slow path epilogue. */
#define CILK_FRAME_DETACHED 0x04

/* CILK_FRAME_EXCEPTION_PROBED is set if the frame has been probed in the
   exception handler first pass */
#define CILK_FRAME_EXCEPTION_PROBED 0x08

/* Is this frame receiving an exception after sync? */
#define CILK_FRAME_EXCEPTING 0x10

/* Is this the last (oldest) Cilk frame? */
#define CILK_FRAME_LAST 0x80

/* Is this frame in the epilogue, or more generally after the last
   sync when it can no longer do any Cilk operations? */
#define CILK_FRAME_EXITING 0x0100

/* CILK_FRAME_MBZ bits must be zero ("MBZ" = "Must Be Zero") */
/* TBD: Should all unused bits be zero? If so, this value is wrong and
   should instead be computed as the compliment of the OR of all other bits.

#define CILK_FRAME_MBZ 0x7600

// Is this frame suspended? (used for debugging) 
#define CILK_FRAME_SUSPENDED 0x8000
*/

// Includes
#include "worker.h"
#include "stack_frame.h"
#include "global_state.h"
#include "local_state.h"
#include "cilk2c.h"

// Funcs
global_state * __cilkrts_init(int argc, char* argv[]);

void __cilkrts_run(global_state * g);

void __cilkrts_exit(global_state * g);
#endif
