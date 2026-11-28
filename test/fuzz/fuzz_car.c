/*
 * fuzz_car.c — the CAR reader (wf_car_parse) and the MST readers that walk a
 * parsed CAR from its roots: what a getRepo or sync.getBlocks reply reaches.
 *
 * Invariant: a CAR that parses writes back out, and the written CAR parses
 * to the same roots and blocks.
 */

#include <string.h>

#include "fuzz.h"
#include "wolfram/repo/car.h"
#include "wolfram/repo/commit.h"
#include "wolfram/repo/mst.h"

static void walk_root(wf_car *car, const wf_cid *root) {
    wf_mst_leaf *leaves = NULL;
    size_t leaf_count = 0;
    if (wf_mst_list(car, root, &leaves, &leaf_count) == WF_OK)
        wf_mst_leaf_list_free(leaves, leaf_count);

    wf_cid *cids = NULL;
    size_t cid_count = 0;
    if (wf_mst_get_all_cids(car, root, &cids, &cid_count) == WF_OK)
        wf_mst_cid_list_free(cids, cid_count);

    wf_cid found;
    (void)wf_mst_find(car, root, (const unsigned char *)"app.bsky.feed.post/a",
                      20, &found);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    wf_car car;
    memset(&car, 0, sizeof(car));
    if (wf_car_parse(data, size, &car) != WF_OK) return 0;

    for (size_t i = 0; i < car.root_count; i++) {
        wf_car_block *block = wf_car_find_block(&car, &car.roots[i]);
        if (block) {
            /* A root is a commit; its `data` is the MST root. */
            wf_commit commit;
            if (wf_commit_parse(block->data, block->data_len, &commit) == WF_OK)
                walk_root(&car, &commit.data);
        }
        walk_root(&car, &car.roots[i]);
    }

    unsigned char *out = NULL;
    size_t out_len = 0;
    if (wf_car_write(&car, &out, &out_len) == WF_OK) {
        wf_car again;
        memset(&again, 0, sizeof(again));
        wf_fuzz_check(wf_car_parse(out, out_len, &again) == WF_OK);
        wf_fuzz_check(again.root_count == car.root_count);
        wf_fuzz_check(again.block_count == car.block_count);
        for (size_t i = 0; i < car.block_count; i++) {
            wf_fuzz_check(again.blocks[i].data_len == car.blocks[i].data_len);
            wf_fuzz_check(car.blocks[i].data_len == 0 ||
                          memcmp(again.blocks[i].data, car.blocks[i].data,
                                 car.blocks[i].data_len) == 0);
        }
        wf_car_free(&again);
        free(out);
    }
    wf_car_free(&car);
    return 0;
}
