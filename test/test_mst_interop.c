/**
 * test_mst_interop.c — validates wolfram's Merkle Search Tree (MST) build /
 * add / delete against the official atproto reference commit-proof vectors
 * (packages/repo/tests/commit-proof-fixtures.json, copied verbatim into
 * test/fixtures).
 *
 * Each vector defines a starting key set (all leaves share `leafValue`), the
 * expected MST root before the commit (`rootBeforeCommit`), a set of `adds`
 * and `dels`, and the expected root after applying them (`rootAfterCommit`).
 * We build the tree from `keys`, assert the root equals `rootBeforeCommit`,
 * apply adds then dels, and assert the root equals `rootAfterCommit`.
 */

#include "wolfram/repo/mst.h"
#include "wolfram/repo/cid.h"

#include "test.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WF_TEST_FIXTURE_DIR
#define WF_TEST_FIXTURE_DIR "test/fixtures"
#endif

static char *read_entire_file(const char *path, size_t *len_out) {
    FILE *fp = fopen(path, "rb");
    long size_long;
    size_t size;
    char *buffer;
    if (len_out) *len_out = 0;
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    size_long = ftell(fp);
    if (size_long < 0) { fclose(fp); return NULL; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    size = (size_t)size_long;
    buffer = malloc(size + 1);
    if (!buffer) { fclose(fp); return NULL; }
    if (size > 0 && fread(buffer, 1, size, fp) != size) {
        free(buffer); fclose(fp); return NULL;
    }
    buffer[size] = '\0';
    fclose(fp);
    if (len_out) *len_out = size;
    return buffer;
}

/* Add every key in `arr` (a cJSON array of strings) to the MST, threading
 * `root` through each call. Returns WF_OK on success. */
static wf_status add_all(wf_car *car, const wf_cid *leaf,
                          cJSON *arr, wf_cid *root) {
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsString(item)) return WF_ERR_INVALID_ARG;
        wf_cid new_root;
        wf_status st = wf_mst_add(car, root,
                                  (const unsigned char *)item->valuestring,
                                  strlen(item->valuestring), leaf, &new_root);
        if (st != WF_OK) return st;
        *root = new_root;
    }
    return WF_OK;
}

static wf_status del_all(wf_car *car, cJSON *arr, wf_cid *root) {
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsString(item)) return WF_ERR_INVALID_ARG;
        wf_cid new_root;
        wf_status st = wf_mst_delete(car, root,
                                     (const unsigned char *)item->valuestring,
                                     strlen(item->valuestring), &new_root);
        if (st != WF_OK) return st;
        *root = new_root;
    }
    return WF_OK;
}

int main(void) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/commit-proof-fixtures.json",
             WF_TEST_FIXTURE_DIR);
    char *json = read_entire_file(path, NULL);
    WF_CHECK(json != NULL);
    if (!json) WF_TEST_SUMMARY();

    cJSON *fixtures = cJSON_Parse(json);
    free(json);
    WF_CHECK(fixtures != NULL && cJSON_IsArray(fixtures));
    if (!fixtures || !cJSON_IsArray(fixtures)) WF_TEST_SUMMARY();

    int n = cJSON_GetArraySize(fixtures);
    for (int i = 0; i < n; i++) {
        cJSON *fx = cJSON_GetArrayItem(fixtures, i);
        cJSON *comment = cJSON_GetObjectItemCaseSensitive(fx, "comment");
        cJSON *leaf_val = cJSON_GetObjectItemCaseSensitive(fx, "leafValue");
        cJSON *keys = cJSON_GetObjectItemCaseSensitive(fx, "keys");
        cJSON *adds = cJSON_GetObjectItemCaseSensitive(fx, "adds");
        cJSON *dels = cJSON_GetObjectItemCaseSensitive(fx, "dels");
        cJSON *root_before =
            cJSON_GetObjectItemCaseSensitive(fx, "rootBeforeCommit");
        cJSON *root_after =
            cJSON_GetObjectItemCaseSensitive(fx, "rootAfterCommit");
        if (!comment || !leaf_val || !keys || !adds || !dels ||
            !root_before || !root_after) {
            WF_CHECK(0);
            continue;
        }

        wf_cid leaf;
        WF_CHECK(wf_cid_from_string(leaf_val->valuestring, &leaf) == WF_OK);

        wf_car car;
        memset(&car, 0, sizeof(car));
        wf_cid root = {{0}, 0};

        WF_CHECK(add_all(&car, &leaf, keys, &root) == WF_OK);
        char *before_str = wf_cid_to_string(&root);
        if (!before_str || strcmp(before_str, root_before->valuestring) != 0) {
            fprintf(stderr, "[%s] BEFORE got=%s exp=%s\n",
                    comment->valuestring, before_str ? before_str : "(null)",
                    root_before->valuestring);
        }
        WF_CHECK(before_str &&
                 strcmp(before_str, root_before->valuestring) == 0);
        free(before_str);

        WF_CHECK(add_all(&car, &leaf, adds, &root) == WF_OK);
        WF_CHECK(del_all(&car, dels, &root) == WF_OK);
        char *after_str = wf_cid_to_string(&root);
        if (!after_str || strcmp(after_str, root_after->valuestring) != 0) {
            fprintf(stderr, "[%s] AFTER got=%s exp=%s\n",
                    comment->valuestring, after_str ? after_str : "(null)",
                    root_after->valuestring);
        }
        WF_CHECK(after_str &&
                 strcmp(after_str, root_after->valuestring) == 0);
        free(after_str);

        wf_car_free(&car);
    }

    cJSON_Delete(fixtures);
    WF_TEST_SUMMARY();
}
