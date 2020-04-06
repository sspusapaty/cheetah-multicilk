#ifdef REDUCER_MODULE
#include "cilkred_map.h"

// =================================================================
// small helper functions
// =================================================================

static inline void swap_views(ViewInfo *v1, ViewInfo *v2) {
    ViewInfo tmp = *v1;
    *v1 = *v2;
    *v2 = tmp;
}

static inline void swap_vals(ViewInfo *v1, ViewInfo *v2) {
    void *val = v1->val;
    v1->val = v2->val;
    v2->val = val;
}

static inline void clear_view(ViewInfo *view) {
    __cilkrts_hyperobject_base *key = view->key;

    if (key != NULL) {
        key->__c_monoid.destroy_fn(key, view->val);    // calls destructor
        key->__c_monoid.deallocate_fn(key, view->val); // free the memory
    }
    view->key = NULL;
    view->val = NULL;
}

// =================================================================
// helper functions that operate on a SPA map
// =================================================================

void cilkred_map_log_id(__cilkrts_worker *const w, cilkred_map *this_map,
                        uint16_t id) {
    CILK_ASSERT(w, this_map->num_of_logs <= ((this_map->spa_cap / 2) + 1));
    CILK_ASSERT(w, this_map->num_of_vinfo <= this_map->spa_cap);

    if (this_map->num_of_vinfo == this_map->spa_cap) {
        cilkrts_bug(w, "SPA resize not supported yet!");
    }

    if (this_map->num_of_logs < (this_map->spa_cap / 2)) {
        this_map->log[this_map->num_of_logs++] = id;
    } else if (this_map->num_of_logs == (this_map->spa_cap / 2)) {
        this_map->num_of_logs++; // invalidate the log
    }

    this_map->num_of_vinfo++;
}

void cilkred_map_unlog_id(__cilkrts_worker *const w, cilkred_map *this_map,
                          uint16_t id) {
    CILK_ASSERT(w, this_map->num_of_logs <= ((this_map->spa_cap / 2) + 1));
    CILK_ASSERT(w, this_map->num_of_vinfo <= this_map->spa_cap);

    this_map->num_of_vinfo--;
    if (this_map->num_of_vinfo == 0) {
        this_map->num_of_logs = 0; // now we can reset the log
    }
}

/** @brief Return element mapped to 'key' or null if not found. */
ViewInfo *cilkred_map_lookup(cilkred_map *this_map,
                             __cilkrts_hyperobject_base *key) {
    ViewInfo *ret = this_map->vinfo + key->__id_num;
    if (ret->key == NULL && ret->val == NULL) {
        return NULL;
    }

    return ret;
}

/**
 * Construct an empty reducer map from the memory pool associated with the
 * given worker.  This reducer map must be destroyed before the worker's
 * associated global context is destroyed.
 *
 * @param w __cilkrts_worker the cilkred_map is being created for.
 *
 * @return Pointer to the initialized cilkred_map.
 */
cilkred_map *cilkred_map_make_map(__cilkrts_worker *w) {
    CILK_ASSERT_G(w);
    cilkrts_alert(ALERT_REDUCE, w,
                  "(cilkred_map_make_map) creating a cilkred_map");

    cilkred_map *h;

    h = (cilkred_map *)malloc(sizeof(*h));

    // MAK: w is not NULL
    h->spa_cap = 64;
    h->num_of_vinfo = 0;
    h->num_of_logs = 0;
    h->vinfo = (ViewInfo *)malloc(h->spa_cap * sizeof(ViewInfo));
    h->log = (uint16_t *)malloc((h->spa_cap / 2) * sizeof(uint16_t));
    h->merging = false;

    cilkrts_alert(ALERT_REDUCE, w,
                  "(cilkred_map_make_map) created cilkred_map %p", h);

    return h;
}

/**
 * Destroy a reducer map.  The map must have been allocated from the worker's
 * global context and should have been allocated from the same worker.
 *
 * @param w __cilkrts_worker the cilkred_map was created for.
 * @param h The cilkred_map to be deallocated.
 */
void cilkred_map_destroy_map(__cilkrts_worker *w, cilkred_map *h) {
    if (!h) {
        return;
    }
    cilkrts_alert(ALERT_REDUCE, w,
                  "(cilkred_map_destroy_map) freeing cilkred_map %p", h);
    free(h->vinfo);
    h->vinfo = NULL;
    free(h->log);
    h->log = NULL;
    free(h);

    cilkrts_alert(ALERT_REDUCE, w,
                  "(cilkred_map_destroy_map) freed cilkred_map %p\n", h);
}

__cilkrts_worker *cilkred_map_merge(cilkred_map *this_map, __cilkrts_worker *w,
                                    cilkred_map *other_map, merge_kind kind) {
    cilkrts_alert(ALERT_REDUCE, w,
                  "(cilkred_map_merge) merging %p into %p, order %d", other_map,
                  this_map, kind);
    // Remember the current stack frame.
    // __cilkrts_stack_frame *current_sf = w->current_stack_frame;
    this_map->merging = true;
    other_map->merging = true;

    // Merging to the leftmost view is a special case because every leftmost
    // element must be initialized before the merge.
    // CILK_ASSERT(w, !other_map->is_leftmost /* || kind == MERGE_UNORDERED */);
    // bool merge_to_leftmost = (this_map->is_leftmost);

    if (other_map->num_of_vinfo == 0)
        return w; // A no-op

    int i;
    __cilkrts_hyperobject_base *key;

    if (other_map->num_of_logs <= (other_map->spa_cap / 2)) {
        uint16_t vindex;

        for (i = 0; i < (int)other_map->num_of_logs; i++) {

            vindex = other_map->log[i];
            key = other_map->vinfo[vindex].key;

            if (this_map->vinfo[vindex].key != NULL) {
                CILK_ASSERT(w, key == this_map->vinfo[vindex].key);
                if (kind == MERGE_INTO_RIGHT) { // other_map is the left val
                    swap_vals(&other_map->vinfo[vindex],
                              &this_map->vinfo[vindex]);
                }
                // updated val is stored back into the left
                key->__c_monoid.reduce_fn(key, this_map->vinfo[vindex].val,
                                          other_map->vinfo[vindex].val);
                clear_view(&other_map->vinfo[vindex]);
            } else {
                CILK_ASSERT(w, this_map->vinfo[vindex].val == NULL);
                swap_views(&other_map->vinfo[vindex], &this_map->vinfo[vindex]);
                cilkred_map_log_id(w, this_map, vindex);
            }
        }

    } else {
        for (i = 0; i < other_map->spa_cap; i++) {
            if (other_map->vinfo[i].key != NULL) {
                key = other_map->vinfo[i].key;

                if (this_map->vinfo[i].key != NULL) {
                    CILK_ASSERT(w, key == this_map->vinfo[i].key);
                    if (kind == MERGE_INTO_RIGHT) { // other_map is the left val
                        swap_vals(&other_map->vinfo[i], &this_map->vinfo[i]);
                    }
                    // updated val is stored back into the left
                    key->__c_monoid.reduce_fn(key, this_map->vinfo[i].val,
                                              other_map->vinfo[i].val);
                    clear_view(&other_map->vinfo[i]);
                } else { // the 'this_map' page does not contain view
                    CILK_ASSERT(w, this_map->vinfo[i].val == NULL);
                    // transfer the key / val over
                    swap_views(&other_map->vinfo[i], &this_map->vinfo[i]);
                    cilkred_map_log_id(w, this_map, i);
                }
            }
        }
    }
    other_map->num_of_vinfo = 0;
    other_map->num_of_logs = 0;

    // this_map->is_leftmost = this_map->is_leftmost || other_map->is_leftmost;
    this_map->merging = false;
    other_map->merging = false;
    // cilkred_map_destroy_map(w, other_map);
    return w;
}

/** @brief Test whether the cilkred_map is empty */
bool cilkred_map_is_empty(cilkred_map *this_map) {
    return this_map->num_of_vinfo == 0;
}

/** @brief Get number of views in the cilkred_map */
uint64_t cilkred_map_num_views(cilkred_map *this_map) {
    return this_map->num_of_vinfo;
}

/** @brief Is the cilkred_map leftmost */
bool cilkred_map_is_leftmost(cilkred_map *this_map) { return false; }
#endif
