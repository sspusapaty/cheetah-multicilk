#ifdef REDUCER_MODULE
#include "reducer_impl.h"
#endif
#include "cilk/hyperobject_base.h"
#include "mutex.h"

#ifdef REDUCER_MODULE
// =================================================================
// ID managers for reducers
// =================================================================

typedef struct reducer_id_manager {
    uint16_t spa_cap;
    uint16_t curr_id;
    cilk_mutex mutex; // enfore mutual exclusion on access to this desc
    int mutex_owner;  // worker id who holds the mutex
} reducer_id_manager;

static reducer_id_manager *id_manager = NULL;

static void reducer_id_manager_assert_ownership(__cilkrts_worker *const ws) {

    CILK_ASSERT(ws, id_manager->mutex_owner == ws->self);
}

static inline void reducer_id_manager_lock(__cilkrts_worker *const ws) {

    cilk_mutex_lock(&id_manager->mutex);
    id_manager->mutex_owner = ws->self;
}

static inline void reducer_id_manager_unlock(__cilkrts_worker *const ws) {

    reducer_id_manager_assert_ownership(ws);
    id_manager->mutex_owner = NOBODY;
    cilk_mutex_unlock(&id_manager->mutex);
}

static void init_reducer_id_manager(global_state *const g) {
    id_manager = (reducer_id_manager *)malloc(sizeof(reducer_id_manager));
    id_manager->spa_cap = 64;
    id_manager->curr_id = 0;

    cilk_mutex_init(&id_manager->mutex);
    id_manager->mutex_owner = NOBODY;
}

static void free_reducer_id_manager(global_state *const g) {
    CILK_ASSERT_G(id_manager->mutex_owner == NOBODY);
    cilk_mutex_destroy(&id_manager->mutex);
    free(id_manager);
}

static void reducer_id_get(__cilkrts_worker *const ws, uint16_t *id) {

    reducer_id_manager_lock(ws);
    *id = id_manager->curr_id;
    id_manager->curr_id++;
    if (id_manager->curr_id >= id_manager->spa_cap) {
        __cilkrts_bug("SPA resize not supported yet!\n");
    }
    reducer_id_manager_unlock(ws);
}

void reducer_id_free(__cilkrts_worker *const ws, uint16_t *id) {
    reducer_id_manager_lock(ws);
    // A big NOOP
    reducer_id_manager_unlock(ws);
}

// =================================================================
// Init / deinit functions
// =================================================================

void reducers_init(global_state *g) {
    __cilkrts_alert(ALERT_BOOT,
                    "[M]: (reducers_init) Initializing reducers.\n");
    init_reducer_id_manager(g);
}

void reducers_deinit(global_state *g) {
    __cilkrts_alert(ALERT_BOOT,
                    "[M]: (reducers_deinit) Cleaning up reducers.\n");
    free_reducer_id_manager(g);
}

// =================================================================
// Helper functions for interacting with reducer library and
// managing hyperobjects
// =================================================================

static cilkred_map *install_new_reducer_map(__cilkrts_worker *w) {
    cilkred_map *h;
    // MAK: w.out worker mem pools, need to reexamine
    h = cilkred_map_make_map(w);
    w->reducer_map = h;

    __cilkrts_alert(
        ALERT_REDUCE,
        "[%d]: (install_new_reducer_map) installed reducer_map %p\n", w->self,
        h);
    return h;
}

// Given a __cilkrts_hyperobject_base, return the key to that hyperobject in
// the reducer map.
void *get_hyperobject_val(__cilkrts_hyperobject_base *hb) {
    // The current implementation uses the address of the lefmost view as the
    // key.
    return ((char *)hb) + hb->__view_offset;
}

/* remove the reducer from the current reducer map.  If the reducer
   exists in maps other than the current one, the behavior is
   undefined. */
void __cilkrts_hyper_destroy(__cilkrts_hyperobject_base *key) {

    __cilkrts_worker *w = __cilkrts_get_tls_worker();

    if (!w)
        return;

    const char *UNSYNCED_REDUCER_MSG =
        "Destroying a reducer while it is visible to unsynced child tasks, or\n"
        "calling CILK_C_UNREGISTER_REDUCER() on an unregistered reducer.\n"
        "Did you forget a _Cilk_sync or CILK_C_REGISTER_REDUCER()?";

    cilkred_map *h = w->reducer_map;
    if (NULL == h)
        __cilkrts_bug(UNSYNCED_REDUCER_MSG); // Does not return

    if (h->merging) {
        __cilkrts_bug("User error: hyperobject used by another hyperobject");
    }

    ViewInfo *vinfo = &h->vinfo[key->__id_num];
    vinfo->key = NULL;
    vinfo->val = NULL;
    cilkred_map_unlog_id(w, h, key->__id_num);
    reducer_id_free(w, (uint16_t *)&key->__id_num);
}

void __cilkrts_hyper_create(__cilkrts_hyperobject_base *key) {
    // This function registers the specified hyperobject in the current
    // reducer map and registers the initial value of the hyperobject as the
    // leftmost view of the reducer.
    __cilkrts_worker *w = __cilkrts_get_tls_worker();

    //
    if (!w)
        return;

    cilkred_map *h = w->reducer_map;

    if (__builtin_expect(!h, 0)) {
        h = install_new_reducer_map(w);
    }

    /* Must not exist. */
    CILK_ASSERT(w, cilkred_map_lookup(h, key) == NULL);

    if (h->merging)
        __cilkrts_bug("User error: hyperobject used by another hyperobject");

    CILK_ASSERT(w, w->reducer_map == h);

    uint16_t id;
    reducer_id_get(w, &id);
    ViewInfo *vinfo = &h->vinfo[id];
    vinfo->key = key;
    vinfo->val = (char *)key + key->__view_offset; // init with left most view
    key->__id_num = id;
    cilkred_map_log_id(w, h, key->__id_num);
}

void *__cilkrts_hyper_lookup(__cilkrts_hyperobject_base *key) {
    // MAK: Requires runtime started
    __cilkrts_worker *w = __cilkrts_get_tls_worker();

    cilkred_map *h = w->reducer_map;

    if (__builtin_expect(!h, 0)) {
        h = install_new_reducer_map(w);
    }

    if (h->merging)
        __cilkrts_bug("User error: hyperobject used by another hyperobject");

    ViewInfo *vinfo = cilkred_map_lookup(h, key);
    if (vinfo == NULL) {
        vinfo = &h->vinfo[key->__id_num];

        void *val = key->__c_monoid.allocate_fn(key, key->__view_size);
        key->__c_monoid.identity_fn(key, val);
        CILK_ASSERT(w, vinfo->key == NULL && vinfo->val == NULL);

        // allocate space for the val and initialize it to identity
        vinfo->key = key;
        vinfo->val = val;
        cilkred_map_log_id(w, h, key->__id_num);
    }

    return vinfo->val;
}

void *__cilkrts_hyperobject_alloc(void *ignore, size_t bytes) {
    return malloc(bytes);
}

void __cilkrts_hyperobject_dealloc(void *ignore, void *view) { free(view); }

/* No-op destroy function */
void __cilkrts_hyperobject_noop_destroy(void *ignore, void *ignore2) {}

// =================================================================
// management of cilkred_maps
// =================================================================

// used by the scheduler so cannot be static
cilkred_map *merge_two_rmaps(__cilkrts_worker *const ws, cilkred_map *left,
                             cilkred_map *right) {
    CILK_ASSERT(ws, ws == __cilkrts_get_tls_worker());
    if (!left)
        return right;
    if (!right)
        return left;

    /* Special case, if left is leftmost, then always merge into it.
       For C reducers this forces lazy creation of the leftmost views. */
    if (cilkred_map_is_leftmost(left) ||
        cilkred_map_num_views(left) > cilkred_map_num_views(right)) {
        cilkred_map_merge(left, ws, right, MERGE_INTO_LEFT);
        return left;
    } else {
        cilkred_map_merge(right, ws, left, MERGE_INTO_RIGHT);
        return right;
    }
}
#endif
