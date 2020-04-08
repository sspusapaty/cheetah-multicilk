#ifndef _CILKRED_MAP_H
#define _CILKRED_MAP_H

#ifdef OPENCILK_ABI
#define __cilk 300
#endif

#include "cilk-internal.h"
#include "debug.h"
#include <cilk/hyperobject_base.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INVALID_RMAP (cilkred_map *)(-1)

enum merge_kind {
    MERGE_UNORDERED, ///< Assertion fails
    MERGE_INTO_LEFT, ///< Merges the argument from the right into the left
    MERGE_INTO_RIGHT ///< Merges the argument from the left into the right
};
typedef enum merge_kind merge_kind;

// for each reducer, such ViewInfo is allocated in TLMM region, where the
// key is a pointer to the hyperobject_base, and the val stores the pointer
// to correpsonding view; hyperobjet_base->__tlmm_addr stores its addr
typedef struct view_info {
    void *val; // pointer to the actual view for the reducer
    // pointer to the hyperbase object for a given reducer
    __cilkrts_hyperobject_base *key;
} ViewInfo;

/**
 * Class that implements the map for reducers so we can find the
 * view for a strand.
 */
struct cilkred_map {
    uint16_t spa_cap;
    uint16_t num_of_vinfo; // max is spa_cap
    uint16_t num_of_logs;  // max is spa_cap / 2
    // SPA structure
    ViewInfo *vinfo;
    uint16_t *log;

    /** Set true if merging (for debugging purposes) */
    bool merging;
};
typedef struct cilkred_map cilkred_map;

void cilkred_map_log_id(__cilkrts_worker *const w, cilkred_map *this_map,
                        uint16_t id);
void cilkred_map_unlog_id(__cilkrts_worker *const w, cilkred_map *this_map,
                          uint16_t id);
ViewInfo *cilkred_map_lookup(cilkred_map *this_map,
                             __cilkrts_hyperobject_base *key);
/**
 * Construct an empty reducer map from the memory pool associated with the
 * given worker.  This reducer map must be destroyed before the worker's
 * associated global context is destroyed.
 *
 * @param w __cilkrts_worker the cilkred_map is being created for.
 *
 * @return Pointer to the initialized cilkred_map.
 */
cilkred_map *cilkred_map_make_map(__cilkrts_worker *w);

/**
 * Destroy a reducer map.  The map must have been allocated from the worker's
 * global context and should have been allocated from the same worker.
 *
 * @param w __cilkrts_worker the cilkred_map was created for.
 * @param h The cilkred_map to be deallocated.
 */
void cilkred_map_destroy_map(__cilkrts_worker *w, cilkred_map *h);

__cilkrts_worker *cilkred_map_merge(cilkred_map *this_map, __cilkrts_worker *w,
                                    cilkred_map *other_map, merge_kind kind);

/** @brief Test whether the cilkred_map is empty */
bool cilkred_map_is_empty(cilkred_map *this_map);

/** @brief Get number of views in the cilkred_map */
uint64_t cilkred_map_num_views(cilkred_map *this_map);

/** @brief Is the cilkred_map leftmost */
bool cilkred_map_is_leftmost(cilkred_map *this_map);

#endif
