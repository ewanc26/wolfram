/*
 * test_space_lthash.c -- spaces/lthash.h against
 * test/vectors/spaces/lthash.json.
 */

#include <string.h>

#include <cJSON.h>

#include "wolfram/spaces/lthash.h"
#include "test.h"
#include "spaces_vectors.h"

static int digest_is(const wf_space_lthash *hash, const char *hex) {
    unsigned char digest[WF_SPACE_DIGEST_BYTES];
    return wf_space_lthash_digest(hash, digest) == WF_OK &&
           vec_hex_eq(digest, sizeof(digest), hex);
}

int main(void) {
    cJSON *root = vec_load("lthash.json");
    const cJSON *raw;
    const cJSON *step;
    const cJSON *item;
    wf_space_lthash hash;
    wf_space_lthash copy;
    int steps = 0;

    WF_CHECK(root != NULL);
    wf_space_lthash_init(&hash);
    WF_CHECK(wf_space_lthash_is_empty(&hash));
    WF_CHECK(digest_is(&hash, vec_str(root, "empty_digest")));

    /* Raw elements, in both orders, then removed again. */
    raw = cJSON_GetObjectItemCaseSensitive(root, "raw");
    cJSON_ArrayForEach(item,
                       cJSON_GetObjectItemCaseSensitive(raw, "elements")) {
        WF_CHECK(wf_space_lthash_add(&hash, item->valuestring,
                                     strlen(item->valuestring)) == WF_OK);
    }
    WF_CHECK(digest_is(&hash, vec_str(raw, "digest")));
    wf_space_lthash_init(&copy);
    WF_CHECK(wf_space_lthash_add(&copy, "two", 3) == WF_OK);
    WF_CHECK(wf_space_lthash_add(&copy, "one", 3) == WF_OK);
    WF_CHECK(memcmp(hash.state, copy.state, sizeof(hash.state)) == 0);
    WF_CHECK(wf_space_lthash_remove(&hash, "one", 3) == WF_OK);
    WF_CHECK(!wf_space_lthash_is_empty(&hash));
    WF_CHECK(wf_space_lthash_remove(&hash, "two", 3) == WF_OK);
    WF_CHECK(wf_space_lthash_is_empty(&hash));

    /* The guide's three commits, applied as record operations. */
    cJSON_ArrayForEach(step, cJSON_GetObjectItemCaseSensitive(root, "steps")) {
        cJSON_ArrayForEach(item,
                           cJSON_GetObjectItemCaseSensitive(step, "ops")) {
            WF_CHECK(wf_space_lthash_apply(&hash, vec_str(item, "collection"),
                                           vec_str(item, "rkey"),
                                           vec_str(item, "prev"),
                                           vec_str(item, "cid")) == WF_OK);
        }
        WF_CHECK(digest_is(&hash, vec_str(step, "digest")));
        steps++;
    }
    WF_CHECK(steps == 3);

    /* Rejected operations leave the state alone. */
    copy = hash;
    WF_CHECK(wf_space_lthash_apply(&hash, "com.example.a", "k", NULL, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_space_lthash_apply(&hash, "com.example.a", "a/b", NULL,
                                   "bafy") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_space_lthash_apply(&hash, "com.example.a", "k", "x", "a/b") ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_space_lthash_add(&hash, "x", 0) == WF_ERR_INVALID_ARG);
    WF_CHECK(memcmp(hash.state, copy.state, sizeof(hash.state)) == 0);

    cJSON_Delete(root);
    WF_TEST_SUMMARY();
}
