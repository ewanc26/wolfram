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

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_REPO_MST_H */