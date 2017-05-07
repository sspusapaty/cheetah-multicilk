#include "closure.h"
#include "common.h"

void Closure_assert_ownership(__cilkrts_worker *const ws, Closure *t) {
  CILK_ASSERT(t->mutex_owner == ws->self);
}

void Closure_assert_alienation(__cilkrts_worker *const ws, Closure *t) {
  CILK_ASSERT(t->mutex_owner != ws->self);
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

/********************************************
 * Closure management
 ********************************************/

// need to be careful when calling this function --- we check whether a
// frame is set stolen (i.e., has a full frame associated with it), but note
// that the setting of this can be delayed.  A thief can steal a spawned
// frame, but it cannot fully promote it until it remaps its TLMM stack,
// because the flag field is stored in the frame on the TLMM stack.  That
// means, a frame can be stolen, in the process of being promoted, and
// mean while, the stolen flag is not set until finish_promote.
int Closure_at_top_of_stack(__cilkrts_worker *const ws) {

  return( ws->head == ws->tail && 
	  __cilkrts_stolen(ws->current_stack_frame) );
}

int Closure_has_children(Closure *cl) {

  return ( cl->has_cilk_callee || cl->join_counter != 0 );
}

inline void Closure_init(Closure *t) {
  Cilk_mutex_init(&t->mutex);

  t->frame = NULL;
  t->fiber = NULL;

  t->mutex_owner = NOBODY;
  t->owner_ready_deque = NOBODY;

  t->join_counter = 0;

  t->has_cilk_callee = 0;
  t->callee = NULL;
  
  t->call_parent = NULL;
  t->spawn_parent = NULL;

  t->left_sib = NULL;
  t->right_sib = NULL;
  t->right_most_child = NULL;
  
  t->next_ready = NULL;
  t->prev_ready = NULL;

  // t->magic = CILK_CLOSURE_MAGIC;

}

Closure *Closure_create(__cilkrts_worker *const ws) {

  Closure *new_closure = (Closure *)malloc(sizeof(Closure));
  CILK_ASSERT(new_closure != NULL);

  Closure_init(new_closure);

  return new_closure;
}

/* 
 * ANGE: same thing as Cilk_Closure_create, except this function uses system
 *       malloc, while Cilk_Closure_create uses internal_malloc.
 *       This seems to be used only for create_initial_thread from
 *       inovke-main.c.
 */
Closure *Cilk_Closure_create_malloc(__cilkrts_global_state *const g, 
                                    __cilkrts_worker *const ws) {

  Closure *new_closure = (Closure *) malloc(sizeof(Closure));
  CILK_ASSERT(new_closure != NULL);

  Closure_init(new_closure);

  return new_closure;
}

// double linking left and right; the right is always the new child
// Note that we must have the lock on the parent when invoking this function
inline void double_link_children(Closure *left, Closure *right) {
      
  if(left) {
    CILK_ASSERT_TLS(left->right_sib == (Closure *) NULL);
    left->right_sib = right;
  }

  if(right) {
    CILK_ASSERT_TLS(right->left_sib == (Closure *) NULL);
    right->left_sib = left;
  }
}

// unlink the closure from its left and right siblings
// Note that we must have the lock on the parent when invoking this function
inline void unlink_child(Closure *cl) {

  if(cl->left_sib) {
    CILK_ASSERT_TLS(cl->left_sib->right_sib == cl);
    cl->left_sib->right_sib = cl->right_sib;
  }
  if(cl->right_sib) {
    CILK_ASSERT_TLS(cl->right_sib->left_sib == cl);
    cl->right_sib->left_sib = cl->left_sib;
  }
  // used only for error checking
  cl->left_sib = (Closure *) NULL;
  cl->right_sib = (Closure *) NULL;
}

/*** 
 * Only the scheduler is allowed to alter the closure tree.  
 * Consequently, these operations are private.
 *
 * Insert the newly created child into the closure tree.
 * The child closure is newly created, which makes it the new right
 * most child of parent.  Setup the left/right sibling for this new 
 * child, and reset the parent's right most child pointer.  
 * 
 * Note that we don't need locks on the children to double link them.
 * The old right most child will not follow its right_sib link until
 * it's ready to return, and it needs lock on the parent to do so, which
 * we are holding.  The pointer to new right most child is not visible
 * to anyone yet, so we don't need to lock that, either.  
 ***/
void Closure_add_child(__cilkrts_worker *const ws,
		       Closure *parent, Closure *child) {

  /* ANGE: ws must have the lock on parent */
  Closure_assert_ownership(ws, parent);
  /* ANGE: ws must NOT have the lock on child */
  Closure_assert_alienation(ws, child);

  // setup sib links between parent's right most child and the new child
  double_link_children(parent->right_most_child, child);
  // now the new child becomes the right most child
  parent->right_most_child = child;
}

/***
 * Remove the child from the closure tree.
 * At this point we should already have reduced all rmaps that this
 * child has.  We need to unlink it from its left/right sibling, and reset
 * the right most child pointer in parent if this child is currently the 
 * right most child.  
 * 
 * Note that we need locks both on the parent and the child.
 * We always hold lock on the parent when unlinking a child, so only one
 * child gets unlinked at a time, and one child gets to modify the steal
 * tree at a time.  
 ***/
void Closure_remove_child(__cilkrts_worker *const ws,
			  Closure *parent, Closure *child) {
  CILK_ASSERT(child);
  CILK_ASSERT(parent == child->spawn_parent);

  Closure_assert_ownership(ws, parent);
  Closure_assert_ownership(ws, child);

  if( child == parent->right_most_child ) {
    CILK_ASSERT(child->right_sib == (Closure *)NULL);
    parent->right_most_child = child->left_sib;
  }
    
  unlink_child(child);
}


/*** 
 * This function is called during promote_child, when we know we have multiple 
 * frames in the stacklet, but we can't promote them yet, because the thief
 * has yet to remap its stack, so we can't access the fields in the frames.
 * We create a new closure for the new spawn_parent, and temporarily use
 * that to represent all frames in between the new spawn_parent and the 
 * old closure on top of the victim's deque.  In case where some other child
 * of the old closure returns, it needs to know that the old closure has
 * outstanding call children, so it won't resume the suspended old closure
 * by mistake.
 ***/ 
void Closure_add_temp_callee(__cilkrts_worker *const ws, 
			     Closure *caller, Closure *callee) {
  CILK_ASSERT(!(caller->has_cilk_callee));
  CILK_ASSERT(callee->spawn_parent == NULL);

  callee->call_parent = caller;
  caller->has_cilk_callee = 1;
}

void Closure_add_callee(__cilkrts_worker *const ws, 
			Closure *caller, Closure *callee) {
  CILK_ASSERT(callee->frame->call_parent == caller->frame);

  // ANGE: instead of checking has_cilk_callee, we just check if callee is
  // NULL, because we might have set the has_cilk_callee in
  // Closure_add_tmp_callee to prevent the closure from being resumed.
  CILK_ASSERT(caller->callee == NULL);
  CILK_ASSERT(callee->spawn_parent == NULL);
  CILK_ASSERT((callee->frame->flags & CILK_FRAME_DETACHED) == 0);

  callee->call_parent = caller;
  caller->callee = callee;
  caller->has_cilk_callee = 1;
}

void Closure_remove_callee(__cilkrts_worker *const ws, Closure *caller) {

  // A child is not double linked with siblings if it is called
  // so there is no need to unlink it.  
  CILK_ASSERT(caller->status == CLOSURE_SUSPENDED);
  CILK_ASSERT(caller->has_cilk_callee);
  caller->has_cilk_callee = 0;
  caller->callee = NULL;
}
