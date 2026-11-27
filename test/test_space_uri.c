/*
 * test_space_uri.c -- spaces/uri.h against test/vectors/spaces/uri.json.
 */

#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include "wolfram/spaces/uri.h"
#include "test.h"
#include "spaces_vectors.h"

static int same(const char *a, const char *b) {
    return (!a && !b) || (a && b && strcmp(a, b) == 0);
}

static void test_valid(const cJSON *vec) {
    const char *uri = vec_str(vec, "uri");
    wf_space_uri p;
    char *space = NULL;
    char *record = NULL;

    WF_CHECK(wf_space_uri_has_prefix(uri));
    WF_CHECK(wf_space_uri_parse(uri, &p) == WF_OK);
    WF_CHECK(same(p.authority, vec_str(vec, "authority")));
    WF_CHECK(same(p.type, vec_str(vec, "type")));
    WF_CHECK(same(p.skey, vec_str(vec, "skey")));
    WF_CHECK(same(p.author, vec_str(vec, "author")));
    WF_CHECK(same(p.collection, vec_str(vec, "collection")));
    WF_CHECK(same(p.rkey, vec_str(vec, "rkey")));

    /* Formatting the parts gives the same URI back. */
    WF_CHECK(wf_space_ref_format(p.authority, p.type, p.skey, &space) == WF_OK);
    if (p.author) {
        WF_CHECK(wf_space_record_uri_format(space, p.author, p.collection,
                                            p.rkey, &record) == WF_OK);
        WF_CHECK(same(record, uri));
    } else {
        WF_CHECK(same(space, uri));
    }
    free(space);
    free(record);
    wf_space_uri_free(&p);
}

static void test_skey_limits(void) {
    char skey[WF_SPACE_SKEY_MAX + 2];

    memset(skey, 'a', sizeof(skey));
    skey[WF_SPACE_SKEY_MAX] = '\0';
    WF_CHECK(wf_space_skey_is_valid(skey));
    skey[WF_SPACE_SKEY_MAX] = 'a';
    skey[WF_SPACE_SKEY_MAX + 1] = '\0';
    WF_CHECK(!wf_space_skey_is_valid(skey));
    WF_CHECK(!wf_space_skey_is_valid(""));
    WF_CHECK(!wf_space_skey_is_valid(NULL));
    WF_CHECK(wf_space_skey_is_valid("..."));
}

static void test_format_rejects(void) {
    const char *space =
        "at:space/did:web:erin.example/com.example.bookmarks/self";
    const char *record =
        "at:space/did:web:erin.example/com.example.bookmarks/self/"
        "did:web:erin.example/com.example.bookmark/abc";
    char *out = (char *)1;

    WF_CHECK(wf_space_ref_format("erin.example", "com.example.bookmarks",
                                 "self", &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(out == NULL);
    WF_CHECK(wf_space_record_uri_format(record, "did:web:erin.example",
                                        "com.example.bookmark", "abc",
                                        &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_space_record_uri_format(space, "did:web:erin.example",
                                        "com.example.bookmark", "a/b",
                                        &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(out == NULL);
}

int main(void) {
    cJSON *root = vec_load("uri.json");
    const cJSON *item;
    int valid = 0;
    int invalid = 0;

    WF_CHECK(root != NULL);
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "valid")) {
        test_valid(item);
        valid++;
    }
    cJSON_ArrayForEach(item,
                       cJSON_GetObjectItemCaseSensitive(root, "invalid")) {
        wf_space_uri p;
        WF_CHECK(wf_space_uri_parse(item->valuestring, &p) == WF_ERR_PARSE);
        WF_CHECK(p.authority == NULL);
        if (wf_test_fail_count) {
            fprintf(stderr, "  vector: %s\n", item->valuestring);
        }
        invalid++;
    }
    WF_CHECK(valid > 0 && invalid > 0);
    test_skey_limits();
    test_format_rejects();
    cJSON_Delete(root);
    WF_TEST_SUMMARY();
}
