#include "closure.h"

void Closure_assert_ownership(__cilkrts_worker *const ws, Closure *t) {
    CILK_ASSERT(t->mutex_owner == ws->self);
}

int Closure_trylock(__cilkrts_worker *const ws, Closure *t) {
  //Closure_checkmagic(ws, t);
    int ret = Cilk_mutex_try(&(t->mutex)); 
        if(ret) {
            t->mutex_owner = ws->self;
        }
    return ret;
}

void Closure_lock(__cilkrts_worker *const ws, Closure *t) {
  //Closure_checkmagic(ws, t);
    Cilk_mutex_wait(&(t->mutex));
    t->mutex_owner = ws->self;
}

void Closure_unlock(__cilkrts_worker *const ws, Closure *t) {
  //Closure_checkmagic(ws, t);
    Closure_assert_ownership(ws, t);
    t->mutex_owner = NOBODY;
    Cilk_mutex_signal(&(t->mutex));
}
