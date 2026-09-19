#include "wolfram/jetstream_replay.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

typedef struct replay_handler_state {
    int called;
    int saw_auth;
    int saw_nsid;
    int saw_post;
    int saw_json;
} replay_handler_state;

static char *dup_text(const char *text) {
    const size_t len = strlen(text) + 1u;
    char *copy = malloc(len);
    if (copy) memcpy(copy, text, len);
    return copy;
}

static wf_status replay_handler(void *userdata, const char *method,
                                const char *url, const char *content_type,
                                const char *body, size_t body_len,
                                const wf_http_header *headers,
                                size_t header_count, wf_response *out) {
    replay_handler_state *state = userdata;
    state->called++;
    state->saw_post = method && strcmp(method, "POST") == 0;
    state->saw_nsid =
        url && strstr(url, "/xrpc/network.bsky.jetstream.planSnapshot") != NULL;
    state->saw_json =
        content_type && strcmp(content_type, "application/json") == 0 && body &&
        body_len == strlen(body) &&
        strstr(body, "\"afterSeq\":7") != NULL;
    for (size_t i = 0u; i < header_count; ++i) {
        if (headers[i].name && headers[i].value &&
            strcmp(headers[i].name, "Authorization") == 0 &&
            strcmp(headers[i].value, "Bearer test-key") == 0) {
            state->saw_auth = 1;
        }
    }

    const char response[] =
        "{\"plannedThroughSeq\":20,\"sealedTipSeq\":20,\"segments\":["
        "{\"name\":\"seg_0000000000.jss\",\"index\":0,"
        "\"checksum\":\"0123456789abcdef\",\"minSeq\":8,\"maxSeq\":20,"
        "\"mode\":\"segment\"}],\"stats\":{\"segmentsExamined\":1,"
        "\"segmentsMatched\":1,\"blocksMatched\":0,\"entries\":1}}";
    memset(out, 0, sizeof(*out));
    out->status = 200;
    out->body = dup_text(response);
    if (!out->body) return WF_ERR_ALLOC;
    out->body_len = strlen(response);
    return WF_OK;
}

static void test_request_json(void) {
    const char *kinds[] = {"commit", "sync"};
    const char *dids[] = {"did:plc:alice"};
    const char *collections[] = {"app.bsky.feed.post", "app.bsky.graph.*"};
    wf_jetstream_replay_filter filter = {
        .kinds = kinds,
        .kinds_count = 2,
        .dids = dids,
        .dids_count = 1,
        .collections = collections,
        .collections_count = 2,
        .after_seq = 40,
        .before_seq = 100,
        .has_before_seq = 1,
    };
    char *json = NULL;
    size_t len = 0u;
    WF_CHECK(wf_jetstream_replay_plan_json(&filter, &json, &len) == WF_OK);
    WF_CHECK(json != NULL);
    const char expected[] =
        "{\"kinds\":[\"commit\",\"sync\"],"
        "\"dids\":[\"did:plc:alice\"],"
        "\"collections\":[\"app.bsky.feed.post\","
        "\"app.bsky.graph.*\"],\"afterSeq\":40,\"beforeSeq\":100}";
    WF_CHECK(strcmp(json, expected) == 0);
    WF_CHECK(len == strlen(json));
    free(json);

    filter.kinds_count = 5;
    WF_CHECK(wf_jetstream_replay_filter_validate(&filter) ==
             WF_ERR_INVALID_ARG);
    filter.kinds_count = 2;
    filter.before_seq = 39;
    WF_CHECK(wf_jetstream_replay_filter_validate(&filter) ==
             WF_ERR_INVALID_ARG);
    filter.before_seq = 100;

    const char *bad_kind[] = {"wat"};
    filter.kinds = bad_kind;
    filter.kinds_count = 1;
    WF_CHECK(wf_jetstream_replay_filter_validate(&filter) ==
             WF_ERR_INVALID_ARG);

    const char *sync_only[] = {"sync"};
    filter.kinds = sync_only;
    filter.collections_count = 2;
    WF_CHECK(wf_jetstream_replay_filter_validate(&filter) ==
             WF_ERR_INVALID_ARG);

    filter.collections_count = 0;
    const char *bad_did[] = {"alice.test"};
    filter.dids = bad_did;
    WF_CHECK(wf_jetstream_replay_filter_validate(&filter) ==
             WF_ERR_INVALID_ARG);

    filter.dids_count = 0;
    filter.dids = NULL;
    filter.after_seq = UINT64_C(9007199254740992);
    WF_CHECK(wf_jetstream_replay_filter_validate(&filter) ==
             WF_ERR_INVALID_ARG);
}

static void test_parse_plan(void) {
    const char json[] =
        "{\"plannedThroughSeq\":90,\"sealedTipSeq\":100,\"segments\":["
        "{\"name\":\"seg_0000000000.jss\",\"index\":0,"
        "\"checksum\":\"0123456789abcdef\",\"minSeq\":1,\"maxSeq\":50,"
        "\"mode\":\"segment\"},"
        "{\"name\":\"seg_0000000001.jss\",\"index\":1,"
        "\"checksum\":\"fedcba9876543210\",\"minSeq\":51,\"maxSeq\":100,"
        "\"mode\":\"blocks\",\"blocks\":[{\"first\":7,\"last\":9},"
        "{\"first\":11,\"last\":12}]}],"
        "\"stats\":{\"segmentsExamined\":2,\"segmentsMatched\":2,"
        "\"blocksMatched\":4,\"entries\":5}}";
    wf_jetstream_replay_plan_page page = {0};
    WF_CHECK(wf_jetstream_replay_plan_parse(json, sizeof(json) - 1u, &page) ==
             WF_OK);
    WF_CHECK(page.planned_through_seq == 90u);
    WF_CHECK(page.sealed_tip_seq == 100u);
    WF_CHECK(page.segments_count == 2u);
    WF_CHECK(page.segments[0].mode == WF_JETSTREAM_REPLAY_SEGMENT_WHOLE);
    WF_CHECK(page.segments[0].name &&
             strcmp(page.segments[0].name, "seg_0000000000.jss") == 0);
    WF_CHECK(page.segments[1].mode == WF_JETSTREAM_REPLAY_SEGMENT_BLOCKS);
    WF_CHECK(page.segments[1].blocks_count == 2u);
    WF_CHECK(page.segments[1].blocks[0].first == 7u &&
             page.segments[1].blocks[0].last == 9u);
    WF_CHECK(page.stats.segments_examined == 2u);
    WF_CHECK(page.stats.entries == 5u);
    wf_jetstream_replay_plan_page_free(&page);
    WF_CHECK(page.segments == NULL && page.segments_count == 0u);

    const char bad_order[] =
        "{\"plannedThroughSeq\":101,\"sealedTipSeq\":100,\"segments\":[],"
        "\"stats\":{\"segmentsExamined\":0,\"segmentsMatched\":0,"
        "\"blocksMatched\":0,\"entries\":0}}";
    wf_status status =
        wf_jetstream_replay_plan_parse(bad_order, sizeof(bad_order) - 1u, &page);
    WF_CHECK(status == WF_ERR_PARSE);

    const char missing_stats[] =
        "{\"plannedThroughSeq\":100,\"sealedTipSeq\":100,\"segments\":[]}";
    wf_status status =
        wf_jetstream_replay_plan_parse(missing_stats, sizeof(missing_stats) - 1u, &page);
    WF_CHECK(status == WF_ERR_PARSE);

    const char bad_checksum[] =
        "{\"plannedThroughSeq\":1,\"sealedTipSeq\":1,\"segments\":["
        "{\"name\":\"x.jss\",\"index\":0,\"checksum\":\"not-a-checksum\","
        "\"minSeq\":1,\"maxSeq\":1,\"mode\":\"segment\"}],"
        "\"stats\":{\"segmentsExamined\":1,\"segmentsMatched\":1,"
        "\"blocksMatched\":0,\"entries\":1}}";
    wf_status status =
        wf_jetstream_replay_plan_parse(bad_checksum, sizeof(bad_checksum) - 1u, &page);
    WF_CHECK(status == WF_ERR_PARSE);

    const char bad_blocks[] =
        "{\"plannedThroughSeq\":1,\"sealedTipSeq\":1,\"segments\":["
        "{\"name\":\"x.jss\",\"index\":0,\"checksum\":\"0123456789abcdef\","
        "\"minSeq\":1,\"maxSeq\":1,\"mode\":\"blocks\","
        "\"blocks\":[{\"first\":2,\"last\":1}]}],"
        "\"stats\":{\"segmentsExamined\":1,\"segmentsMatched\":1,"
        "\"blocksMatched\":1,\"entries\":1}}";
    wf_status status =
        wf_jetstream_replay_plan_parse(bad_blocks, sizeof(bad_blocks) - 1u, &page);
    WF_CHECK(status == WF_ERR_PARSE);
}

static void test_offline_xrpc(void) {
    wf_xrpc_client *client = wf_xrpc_client_new("https://jetstream.example");
    WF_CHECK(client != NULL);
    if (!client) return;

    replay_handler_state state = {0};
    wf_xrpc_set_handler(client, replay_handler, &state);
    wf_xrpc_client_set_auth(client, "test-key");

    const char *collections[] = {"app.bsky.feed.post"};
    wf_jetstream_replay_filter filter = {
        .collections = collections,
        .collections_count = 1,
        .after_seq = 7,
    };
    wf_jetstream_replay_plan_page page = {0};
    WF_CHECK(wf_jetstream_replay_plan(client, &filter, &page) == WF_OK);
    WF_CHECK(state.called == 1);
    WF_CHECK(state.saw_post);
    WF_CHECK(state.saw_nsid);
    WF_CHECK(state.saw_json);
    WF_CHECK(state.saw_auth);
    WF_CHECK(page.sealed_tip_seq == 20u && page.planned_through_seq == 20u);
    WF_CHECK(page.segments_count == 1u);
    wf_jetstream_replay_plan_page_free(&page);
    wf_xrpc_client_free(client);
}

int main(void) {
    test_request_json();
    test_parse_plan();
    test_offline_xrpc();
    WF_TEST_SUMMARY();
}
