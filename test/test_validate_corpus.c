#include "wolfram/validate.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef WF_ATPROTO_LEXICONS
#define WF_ATPROTO_LEXICONS NULL
#endif

static const char *corpus_dir(void) {
    const char *env = getenv("WF_ATPROTO_LEXICONS");
    if (env && *env) return env;
    return WF_ATPROTO_LEXICONS;
}

static int corpus_available(const char *dir) {
    return dir && *dir && access(dir, R_OK | X_OK) == 0;
}

static void assert_valid(const wf_lexicon_registry *r, const char *nsid,
                          const char *json) {
    wf_validate_result res = wf_validate_record(r, nsid, json, strlen(json));
    WF_CHECK(res.success == 1 && res.errors == NULL);
    wf_validate_result_free(&res);
}

static void assert_invalid(const wf_lexicon_registry *r, const char *nsid,
                            const char *json) {
    wf_validate_result res = wf_validate_record(r, nsid, json, strlen(json));
    WF_CHECK(res.success == 0 && res.errors != NULL);
    wf_validate_result_free(&res);
}

int main(void) {
    const char *dir = corpus_dir();
    wf_lexicon_registry *r;
    wf_status load_status;

    if (!corpus_available(dir)) {
        printf("SKIP: atproto lexicon corpus not available (set WF_ATPROTO_LEXICONS)\n");
        WF_TEST_SUMMARY();
    }

    r = wf_lexicon_registry_new();
    WF_CHECK(r != NULL);

    load_status = wf_lexicon_registry_load_dir(r, dir);
    WF_CHECK(load_status == WF_OK);

    /* Valid app.bsky.feed.post */
    assert_valid(r, "app.bsky.feed.post",
                 "{\"text\":\"hello world\",\"createdAt\":\"2024-01-01T00:00:00Z\"}");

    /* Invalid app.bsky.feed.post: missing required createdAt */
    assert_invalid(r, "app.bsky.feed.post",
                   "{\"text\":\"hello world\"}");

    /* Invalid app.bsky.feed.post: wrong type for text */
    assert_invalid(r, "app.bsky.feed.post",
                   "{\"text\":123,\"createdAt\":\"2024-01-01T00:00:00Z\"}");

    /* Valid app.bsky.actor.profile (all properties optional) */
    assert_valid(r, "app.bsky.actor.profile", "{}");

    /* Valid app.bsky.graph.listitem (required: subject, list, createdAt) */
    assert_valid(r, "app.bsky.graph.listitem",
                 "{\"subject\":\"did:plc:z72i7hdynmk6r22z27h6tvur\","
                 "\"list\":\"at://did:plc:z72i7hdynmk6r22z27h6tvur/app.bsky.graph.list/3jkl0pp8sic\","
                 "\"createdAt\":\"2024-01-01T00:00:00Z\"}");

    /* Invalid app.bsky.graph.listitem: missing list */
    assert_invalid(r, "app.bsky.graph.listitem",
                   "{\"subject\":\"did:plc:z72i7hdynmk6r22z27h6tvur\","
                   "\"createdAt\":\"2024-01-01T00:00:00Z\"}");

    /* Value validation against a nested definition (app.bsky.richtext.facet#tag) */
    {
        const char *json = "{\"tag\":\"hello\"}";
        wf_validate_result res = wf_validate_value(r, "app.bsky.richtext.facet",
                                                   "tag", json, strlen(json));
        WF_CHECK(res.success == 1 && res.errors == NULL);
        wf_validate_result_free(&res);
    }

    /* Value validation: app.bsky.richtext.facet#byteSlice (integer object) */
    {
        const char *json =
            "{\"$type\":\"app.bsky.richtext.facet#byteSlice\","
             "\"byteStart\":0,\"byteEnd\":5}";
        wf_validate_result res = wf_validate_value(r, "app.bsky.richtext.facet",
                                                   "byteSlice", json, strlen(json));
        WF_CHECK(res.success == 1 && res.errors == NULL);
        wf_validate_result_free(&res);
    }

    wf_lexicon_registry_free(r);

    WF_TEST_SUMMARY();
}
