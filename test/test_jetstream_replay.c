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
    int saw_segment;
    int saw_block;
    int saw_list;
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
    state->saw_segment =
        url && strstr(url, "/xrpc/network.bsky.jetstream.getSegment") != NULL;
    state->saw_block =
        url && strstr(url, "/xrpc/network.bsky.jetstream.getBlock") != NULL;
    state->saw_list =
        url && strstr(url, "/xrpc/network.bsky.jetstream.listSegments") != NULL;
    const int content_type_ok =
        content_type && strcmp(content_type, "application/json") == 0;
    const int body_ok = body && body_len == strlen(body) &&
                        strstr(body, "\"afterSeq\":7") != NULL;
    state->saw_json = content_type_ok && body_ok;
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
    if (state->saw_segment || state->saw_block) {
        free(out->body);
        out->body = dup_text("raw-archive-bytes");
        if (!out->body) return WF_ERR_ALLOC;
        out->body_len = strlen(out->body);
    }
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

static void expect_parse_error(const char *json, size_t len) {
    wf_jetstream_replay_plan_page page = {0};
    const wf_status status = wf_jetstream_replay_plan_parse(json, len, &page);
    WF_CHECK(status == WF_ERR_PARSE);
    wf_jetstream_replay_plan_page_free(&page);
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
    expect_parse_error(bad_order, sizeof(bad_order) - 1u);

    const char missing_stats[] =
        "{\"plannedThroughSeq\":100,\"sealedTipSeq\":100,\"segments\":[]}";
    expect_parse_error(missing_stats, sizeof(missing_stats) - 1u);

    const char bad_checksum[] =
        "{\"plannedThroughSeq\":1,\"sealedTipSeq\":1,\"segments\":["
        "{\"name\":\"x.jss\",\"index\":0,\"checksum\":\"not-a-checksum\","
        "\"minSeq\":1,\"maxSeq\":1,\"mode\":\"segment\"}],"
        "\"stats\":{\"segmentsExamined\":1,\"segmentsMatched\":1,"
        "\"blocksMatched\":0,\"entries\":1}}";
    expect_parse_error(bad_checksum, sizeof(bad_checksum) - 1u);

    const char bad_blocks[] =
        "{\"plannedThroughSeq\":1,\"sealedTipSeq\":1,\"segments\":["
        "{\"name\":\"x.jss\",\"index\":0,\"checksum\":\"0123456789abcdef\","
        "\"minSeq\":1,\"maxSeq\":1,\"mode\":\"blocks\","
        "\"blocks\":[{\"first\":2,\"last\":1}]}],"
        "\"stats\":{\"segmentsExamined\":1,\"segmentsMatched\":1,"
        "\"blocksMatched\":1,\"entries\":1}}";
    expect_parse_error(bad_blocks, sizeof(bad_blocks) - 1u);

    char *oversized = malloc(WF_JETSTREAM_REPLAY_MAX_RESPONSE_BYTES + 1u);
    WF_CHECK(oversized != NULL);
    if (oversized) {
        memset(oversized, ' ', WF_JETSTREAM_REPLAY_MAX_RESPONSE_BYTES + 1u);
        expect_parse_error(oversized,
                           WF_JETSTREAM_REPLAY_MAX_RESPONSE_BYTES + 1u);
        free(oversized);
    }
}

static void test_parse_manifest(void) {
    const char json[] =
        "{\"segments\":[{\"name\":\"seg_0000000000.jss\",\"index\":0,"
        "\"sizeBytes\":193462065,\"eventCount\":2569479,"
        "\"checksum\":\"0123456789abcdef\",\"minSeq\":1,\"maxSeq\":20,"
        "\"minWitnessedAt\":10,\"maxWitnessedAt\":20}]}";
    wf_jetstream_replay_manifest manifest = {0};
    WF_CHECK(wf_jetstream_replay_manifest_parse(json, sizeof(json) - 1u,
                                                &manifest) == WF_OK);
    WF_CHECK(manifest.segments_count == 1u);
    WF_CHECK(manifest.segments[0].size_bytes == 193462065u);
    WF_CHECK(manifest.segments[0].event_count == 2569479u);
    WF_CHECK(strcmp(manifest.segments[0].checksum, "0123456789abcdef") == 0);
    wf_jetstream_replay_manifest_free(&manifest);

    const char invalid[] =
        "{\"segments\":[{\"name\":\"x\",\"index\":0,\"sizeBytes\":1,"
        "\"eventCount\":1,\"checksum\":\"bad\",\"minSeq\":1,"
        "\"maxSeq\":1,\"minWitnessedAt\":1,\"maxWitnessedAt\":1}]}";
    WF_CHECK(wf_jetstream_replay_manifest_parse(invalid, sizeof(invalid) - 1u,
                                                &manifest) == WF_ERR_PARSE);
    wf_jetstream_replay_manifest_free(&manifest);

    const char bad_order[] =
        "{\"segments\":["
        "{\"name\":\"a\",\"index\":1,\"sizeBytes\":1,\"eventCount\":1,"
        "\"checksum\":\"0123456789abcdef\",\"minSeq\":1,\"maxSeq\":2,"
        "\"minWitnessedAt\":1,\"maxWitnessedAt\":2},"
        "{\"name\":\"b\",\"index\":0,\"sizeBytes\":1,\"eventCount\":1,"
        "\"checksum\":\"fedcba9876543210\",\"minSeq\":3,\"maxSeq\":4,"
        "\"minWitnessedAt\":3,\"maxWitnessedAt\":4}]}";
    WF_CHECK(wf_jetstream_replay_manifest_parse(
                 bad_order, sizeof(bad_order) - 1u, &manifest) == WF_ERR_PARSE);
    wf_jetstream_replay_manifest_free(&manifest);
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

    wf_response response = {0};
    WF_CHECK(wf_jetstream_replay_get_segment(client, "seg_0000000000.jss",
                                             &response) == WF_OK);
    WF_CHECK(state.saw_segment &&
             response.body_len == strlen("raw-archive-bytes"));
    wf_response_free(&response);
    WF_CHECK(wf_jetstream_replay_get_block(client, "seg_0000000000.jss", 7u,
                                           &response) == WF_OK);
    WF_CHECK(state.saw_block &&
             response.body_len == strlen("raw-archive-bytes"));
    wf_response_free(&response);
    WF_CHECK(wf_jetstream_replay_get_segment(client, "", &response) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_jetstream_replay_list_segments(client, &response) == WF_OK);
    WF_CHECK(state.saw_list);
    wf_response_free(&response);
    wf_xrpc_client_free(client);
}

static void put_u32(unsigned char *out, size_t *at, uint32_t value) {
    for (unsigned int i = 0u; i < 4u; ++i)
        out[(*at)++] = (unsigned char)(value >> (i * 8u));
}

static void put_u64(unsigned char *out, size_t *at, uint64_t value) {
    for (unsigned int i = 0u; i < 8u; ++i)
        out[(*at)++] = (unsigned char)(value >> (i * 8u));
}

static void test_decode_columnar_block(void) {
    const size_t block_size = 4u + 8u + 8u + 8u + 1u + 1u + 2u + 1u + 1u + 4u +
                              19u + 13u + 2u + 4u + 3u;
    unsigned char *block = calloc(1u, block_size);
    WF_CHECK(block != NULL);
    if (!block) return;
    size_t at = 0u;
    put_u32(block, &at, 1u);
    put_u64(block, &at, 42u);
    put_u64(block, &at, 1000u);
    put_u64(block, &at, 0u);
    block[at++] = 1u;  /* create */
    block[at++] = 19u; /* collection length */
    block[at++] = 13u;
    block[at++] = 0u; /* DID length */
    block[at++] = 2u; /* rkey */
    block[at++] = 4u; /* rev */
    put_u32(block, &at, 3u);
    memcpy(block + at, "app.bsky.feed.post", 19u);
    at += 19u;
    memcpy(block + at, "did:plc:alice", 13u);
    at += 13u;
    memcpy(block + at, "rk", 2u);
    at += 2u;
    memcpy(block + at, "rev1", 4u);
    at += 4u;
    memcpy(block + at, "abc", 3u);
    at += 3u;
    wf_jetstream_replay_event *events = NULL;
    size_t count = 0u;
    WF_CHECK(wf_jetstream_replay_block_decode(block, block_size, &events,
                                              &count) == WF_OK);
    WF_CHECK(count == 1u && events != NULL);
    if (events) {
        WF_CHECK(events[0].seq == 42u && events[0].kind == 1u);
        WF_CHECK(strcmp(events[0].collection, "app.bsky.feed.post") == 0);
        WF_CHECK(strcmp(events[0].did, "did:plc:alice") == 0);
        WF_CHECK(events[0].payload_len == 3u &&
                 memcmp(events[0].payload, "abc", 3u) == 0);
    }
    wf_jetstream_replay_events_free(events, count);
    block[0] = 0xffu;
    block[1] = 0xffu;
    block[2] = 0xffu;
    block[3] = 0xffu;
    WF_CHECK(wf_jetstream_replay_block_decode(block, block_size, &events,
                                              &count) != WF_OK);
    free(block);
}

static void test_parse_segment_header(void) {
    unsigned char header[256] = {0};
    memcpy(header, "jss0", 4u);
    size_t at = 4u;
    put_u64(header, &at, 99u); /* checksum */
    header[at++] = 1u;
    header[at++] = 0u;        /* version */
    put_u32(header, &at, 2u); /* blocks */
    put_u32(header, &at, 4u); /* events */
    put_u32(header, &at, 1u); /* unique DIDs */
    put_u64(header, &at, 10u);
    put_u64(header, &at, 20u);
    put_u64(header, &at, 100u);
    put_u64(header, &at, 200u);
    put_u64(header, &at, 256u);
    put_u64(header, &at, 300u);
    put_u64(header, &at, 340u);
    put_u64(header, &at, 380u);
    put_u64(header, &at, 420u);
    wf_jetstream_replay_segment_header parsed = {0};
    WF_CHECK(wf_jetstream_replay_segment_header_parse(header, sizeof(header),
                                                      &parsed) == WF_OK);
    WF_CHECK(parsed.version == 1u && parsed.block_count == 2u &&
             parsed.min_seq == 10u && parsed.max_seq == 20u &&
             parsed.block_index_offset == 420u);
    header[0] = 'x';
    WF_CHECK(wf_jetstream_replay_segment_header_parse(header, sizeof(header),
                                                      &parsed) == WF_ERR_PARSE);
}

static void test_block_frame_bounds(void) {
    unsigned char frame[12] = {0};
    size_t at = 0u;
    put_u64(frame, &at, 4u);
    memcpy(frame + at, "test", 4u);
    const void *block = NULL;
    size_t block_len = 0u;
    size_t next = 0u;
    WF_CHECK(wf_jetstream_replay_block_frame(frame, sizeof(frame), 0u, &block,
                                             &block_len, &next) == WF_OK);
    WF_CHECK(block_len == 4u && next == 12u && memcmp(block, "test", 4u) == 0);
    memset(frame, 0, 8u);
    WF_CHECK(wf_jetstream_replay_block_frame(frame, sizeof(frame), 0u, &block,
                                             &block_len, &next) != WF_OK);
}

int main(void) {
    test_request_json();
    test_parse_plan();
    test_parse_manifest();
    test_offline_xrpc();
    test_decode_columnar_block();
    test_parse_segment_header();
    test_block_frame_bounds();
    WF_TEST_SUMMARY();
}
