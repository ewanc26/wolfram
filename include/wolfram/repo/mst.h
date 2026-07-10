#ifndef WOLFRAM_REPO_MST_H
#define WOLFRAM_REPO_MST_H

#include <stddef.h>
#include "wolfram/xrpc.h"
#include "wolfram/repo/cid.h"
#include "wolfram/repo/car.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A decompressed entry (leaf) in an MST node. */
typedef struct wf_mst_entry {
    unsigned char *key;
    size_t key_len;
    wf_cid value;
    wf_cid subtree;
} wf_mst_entry;

/** An MST node parsed from a DAG-CBOR block. */
typedef struct wf_mst_node {
    wf_cid cid;
    unsigned layer;
    wf_cid left;
    wf_mst_entry *entries;
    size_t count;
} wf_mst_node;

unsigned wf_mst_key_layer(const unsigned char *key, size_t key_len);
wf_status wf_mst_node_parse(const unsigned char *cbor, size_t len,
                            const wf_cid *cid, wf_mst_node *out);
wf_status wf_mst_node_build(unsigned layer, const wf_cid *left,
                            wf_mst_entry *entries, size_t count,
                            wf_mst_node *out);
wf_status wf_mst_node_finalize(wf_mst_node *node, wf_car *car);
void wf_mst_node_free(wf_mst_node *node);
wf_status wf_mst_find(wf_car *car, const wf_cid *root_cid,
                      const unsigned char *key, size_t key_len,
                      wf_cid *out);
wf_status wf_mst_add(wf_car *car, const wf_cid *root_cid,
                     const unsigned char *key, size_t key_len,
                     const wf_cid *value, wf_cid *new_root);
wf_status wf_mst_delete(wf_car *car, const wf_cid *root_cid,
                        const unsigned char *key, size_t key_len,
                        wf_cid *new_root);

/** A leaf entry (record key + value CID) returned by MST traversal. */
typedef struct wf_mst_leaf {
    unsigned char *key;
    size_t key_len;
    wf_cid value;
} wf_mst_leaf;

/**
 * Callback invoked for each leaf visited during a walk. Returning a non-WF_OK
 * status aborts the walk and propagates that error to the caller.
 */
typedef wf_status (*wf_mst_walk_cb)(void *ctx, const unsigned char *key,
                                    size_t key_len, const wf_cid *value);

/**
 * In-order depth-first walk over every leaf with key >= `from_key` (or every
 * leaf when `from_key` is NULL), in ascending MST key order. The walk visits
 * left subtrees, then each entry's subtree, then the entry leaf itself.
 */
wf_status wf_mst_walk_from(wf_car *car, const wf_cid *root,
                           const unsigned char *from_key, size_t from_key_len,
                           wf_mst_walk_cb cb, void *ctx);

/**
 * Collect every leaf (key + value CID) in sorted order.
 * Ownership: *out is caller-owned; free it with wf_mst_leaf_list_free.
 */
wf_status wf_mst_list(wf_car *car, const wf_cid *root,
                      wf_mst_leaf **out, size_t *out_count);
void wf_mst_leaf_list_free(wf_mst_leaf *list, size_t count);

/**
 * Collect every leaf whose key begins with `collection`+"/" (i.e. records of
 * a given collection) in sorted order.
 * Ownership: *out is caller-owned; free it with wf_mst_leaf_list_free.
 */
wf_status wf_mst_paths(wf_car *car, const wf_cid *root,
                       const unsigned char *collection, size_t collection_len,
                       wf_mst_leaf **out, size_t *out_count);

/**
 * Collect every CID referenced by the MST — node CIDs, leaf value CIDs, and
 * intermediate subtree CIDs — each collected exactly once (the MST is a DAG).
 * Ownership: *out is caller-owned; free it with wf_mst_cid_list_free.
 */
wf_status wf_mst_get_all_cids(wf_car *car, const wf_cid *root,
                              wf_cid **out, size_t *out_count);
void wf_mst_cid_list_free(wf_cid *list, size_t count);

/**
 * Collect the set of MST node CIDs whose subtree contains at least one leaf
 * in [from_key, to_key) — a valid (over-approximating) covering proof for the
 * key range, as used by com.atproto.sync record/list queries. Pass NULL for
 * `to_key` to cover through the end of the tree.
 * Ownership: *out is caller-owned; free it with wf_mst_cid_list_free.
 */
wf_status wf_mst_get_covering_proof(wf_car *car, const wf_cid *root,
                                    const unsigned char *from_key,
                                    size_t from_key_len,
                                    const unsigned char *to_key,
                                    size_t to_key_len,
                                    wf_cid **out, size_t *out_count);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_REPO_MST_H */