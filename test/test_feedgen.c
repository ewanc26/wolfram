/*
 * test_feedgen.c — offline tests for the getFeedSkeleton builder.
 *
 * Covers: basic build, de-duplication, invalid-URI rejection, cursor
 * pagination across pages, and empty input. No network access.
 */

#include "wolfram/feedgen.h"
#include "wolfram/syntax.h"

#include "test.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

#define URI_A "at://did:plc:alice/app.bsky.feed.post/aaa111"
#define URI_B "at://did:plc:bob/app.bsky.feed.post/bbb222"
#define URI_C "at://did:plc:carol/app.bsky.feed.post/ccc333"
#define URI_D "at://did:plc:dave/app.bsky.feed.post/ddd444"

/* Parse the produced JSON and return the feed array length, or -1 on error. */
static int feed_len(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    cJSON *feed = cJSON_GetObjectItemCaseSensitive(root, "feed");
    int n = (feed && cJSON_IsArray(feed)) ? cJSON_GetArraySize(feed) : -1;
    cJSON_Delete(root);
    return n;
}

static int feed_contains(const char *json, const char *uri) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return 0;
    cJSON *feed = cJSON_GetObjectItemCaseSensitive(root, "feed");
    int found = 0;
    if (feed && cJSON_IsArray(feed)) {
        cJSON *it;
        cJSON_ArrayForEach(it, feed) {
            cJSON *post = cJSON_GetObjectItemCaseSensitive(it, "post");
            if (cJSON_IsString(post) && strcmp(post->valuestring, uri) == 0) {
                found = 1;
                break;
            }
        }
    }
    cJSON_Delete(root);
    return found;
}

static int has_cursor(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return 0;
    cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "cursor");
    int has = (cur && cJSON_IsString(cur));
    cJSON_Delete(root);
    return has;
}

int main(void) {
    /* ---- invalid args ---- */
    char *js = NULL, *cur = NULL;
    wf_status s = wf_feedgen_build_skeleton(NULL, 0, 0, NULL, &js, &cur);
    WF_CHECK(s == WF_ERR_INVALID_ARG);
    s = wf_feedgen_build_skeleton(NULL, 3, 10, NULL, &js, &cur);
    WF_CHECK(s == WF_ERR_INVALID_ARG);
    s = wf_feedgen_build_skeleton(NULL, 0, 10, NULL, NULL, &cur);
    WF_CHECK(s == WF_ERR_INVALID_ARG);

    /* ---- validate_candidates ---- */
    const char *good[] = {URI_A, URI_B};
    size_t bad = 0;
    s = wf_feedgen_validate_candidates(good, 2, &bad);
    WF_CHECK(s == WF_OK);

    const char *with_bad[] = {URI_A, "not-a-uri", URI_B};
    s = wf_feedgen_validate_candidates(with_bad, 3, &bad);
    WF_CHECK(s == WF_ERR_INVALID_ARG);
    WF_CHECK(bad == 1);

    s = wf_feedgen_validate_candidates(NULL, 0, &bad);
    WF_CHECK(s == WF_OK);

    /* ---- basic build (no pagination needed) ---- */
    const char *three[] = {URI_A, URI_B, URI_C};
    s = wf_feedgen_build_skeleton(three, 3, 50, NULL, &js, &cur);
    WF_CHECK(s == WF_OK);
    WF_CHECK(js != NULL);
    WF_CHECK(feed_len(js) == 3);
    WF_CHECK(feed_contains(js, URI_A));
    WF_CHECK(feed_contains(js, URI_B));
    WF_CHECK(feed_contains(js, URI_C));
    WF_CHECK(cur == NULL);
    wf_feedgen_skeleton_free(js, cur);
    js = cur = NULL;

    /* ---- de-duplication (duplicate appears once, order preserved) ---- */
    const char *duped[] = {URI_A, URI_B, URI_A, URI_C, URI_B};
    s = wf_feedgen_build_skeleton(duped, 5, 50, NULL, &js, &cur);
    WF_CHECK(s == WF_OK);
    WF_CHECK(feed_len(js) == 3);
    /* first item must be URI_A (input order preserved through dedup) */
    {
        cJSON *root = cJSON_Parse(js);
        cJSON *feed = cJSON_GetObjectItemCaseSensitive(root, "feed");
        cJSON *first = cJSON_GetArrayItem(feed, 0);
        cJSON *post = cJSON_GetObjectItemCaseSensitive(first, "post");
        WF_CHECK(strcmp(post->valuestring, URI_A) == 0);
        cJSON_Delete(root);
    }
    WF_CHECK(cur == NULL);
    wf_feedgen_skeleton_free(js, cur);
    js = cur = NULL;

    /* ---- invalid-URI rejection at build time ---- */
    s = wf_feedgen_build_skeleton(with_bad, 3, 50, NULL, &js, &cur);
    WF_CHECK(s == WF_ERR_INVALID_ARG);
    WF_CHECK(js == NULL && cur == NULL);

    /* ---- cursor pagination (limit smaller than count) ---- */
    const char *four[] = {URI_A, URI_B, URI_C, URI_D};
    s = wf_feedgen_build_skeleton(four, 4, 2, NULL, &js, &cur);
    WF_CHECK(s == WF_OK);
    WF_CHECK(feed_len(js) == 2);
    WF_CHECK(feed_contains(js, URI_A));
    WF_CHECK(feed_contains(js, URI_B));
    WF_CHECK(has_cursor(js));
    WF_CHECK(cur != NULL && strcmp(cur, "2") == 0);

    char *js2 = NULL, *cur2 = NULL;
    s = wf_feedgen_build_skeleton(four, 4, 2, cur, &js2, &cur2);
    WF_CHECK(s == WF_OK);
    WF_CHECK(feed_len(js2) == 2);
    WF_CHECK(feed_contains(js2, URI_C));
    WF_CHECK(feed_contains(js2, URI_D));
    WF_CHECK(!has_cursor(js2));
    WF_CHECK(cur2 == NULL);
    wf_feedgen_skeleton_free(js, cur);
    wf_feedgen_skeleton_free(js2, cur2);

    /* ---- malformed cursor rejected ---- */
    s = wf_feedgen_build_skeleton(four, 4, 2, "abc", &js, &cur);
    WF_CHECK(s == WF_ERR_INVALID_ARG);
    wf_feedgen_skeleton_free(js, cur);

    /* ---- empty input -> {"feed":[]} no cursor ---- */
    s = wf_feedgen_build_skeleton(NULL, 0, 10, NULL, &js, &cur);
    WF_CHECK(s == WF_OK);
    WF_CHECK(js != NULL);
    WF_CHECK(feed_len(js) == 0);
    WF_CHECK(!has_cursor(js));
    WF_CHECK(cur == NULL);
    wf_feedgen_skeleton_free(js, cur);

    WF_TEST_SUMMARY();
}
