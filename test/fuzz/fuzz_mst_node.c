/*
 * fuzz_mst_node.c — one MST node block (wf_mst_node_parse), as read from a
 * CAR or a firehose commit's blocks.
 */

#include <string.h>

#include "fuzz.h"
#include "wolfram/repo/mst.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    wf_cid cid;
    memset(&cid, 0, sizeof(cid));
    wf_mst_node node;
    if (wf_mst_node_parse(data, size, &cid, &node) != WF_OK) return 0;
    for (size_t i = 0; i < node.count; i++)
        (void)wf_mst_key_layer(node.entries[i].key, node.entries[i].key_len);
    wf_mst_node_free(&node);
    return 0;
}
