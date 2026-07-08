/*
 * test_ozone.c — offline unit tests for the ozone moderation helper.
 *
 * These tests never touch the network. They exercise the pure request
 * serializer (wf_ozone_emit_event_input_serialize) and the generated
 * owning output decoders (via the *_parse helpers) against locally built
 * JSON, mirroring how a caller would construct and consume these types.
 */

#include "wolfram/ozone.h"
#include "wolfram/atproto_lex.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);         \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* Build a sample emitEvent input and verify the serializer round-trips it
 * into valid JSON carrying the expected top-level keys. */
static int test_emit_event_serialize(void) {
    const char *subject_json =
        "{\"$type\":\"com.atproto.admin.defs#repoRef\","
        "\"did\":\"did:plc:subjectexample\"}";
    const char *event_json =
        "{\"$type\":\"tools.ozone.moderation.defs#modEventLabel\","
        "\"createLabelVals\":[\"!warn\"],\"negateLabelVals\":[],"
        "\"comment\":\"offline-test\"}";

    wf_lex_tools_ozone_moderation_emit_event_main_input input;
    memset(&input, 0, sizeof(input));
    input.subject.data = subject_json;
    input.subject.length = strlen(subject_json);
    input.event.data = event_json;
    input.event.length = strlen(event_json);
    input.created_by = "did:plc:moderatorexample";

    char *json = wf_ozone_emit_event_input_serialize(&input);
    CHECK(json != NULL);

    cJSON *root = cJSON_Parse(json);
    CHECK(root != NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "event") != NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "subject") != NULL);
    CHECK(cJSON_IsString(
        cJSON_GetObjectItemCaseSensitive(root, "createdBy")));
    CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(root, "createdBy")->valuestring,
                 "did:plc:moderatorexample") == 0);

    cJSON_Delete(root);
    wf_ozone_emit_event_input_free(json);
    return 0;
}

/* Verify the generated queryEvents output decoder consumes a representative
 * response body without network access. */
static int test_query_events_parse(void) {
    static const char *body =
        "{\"events\":[{\"id\":1,\"event\":{\"$type\":"
        "\"tools.ozone.moderation.defs#modEventLabel\"},\"subject\":{"
        "\"$type\":\"com.atproto.admin.defs#repoRef\"},\"subjectBlobCids\":[],"
        "\"createdBy\":\"did:plc:moderatorexample\","
        "\"createdAt\":\"2024-01-01T00:00:00Z\"}],\"cursor\":\"c1\"}";

    wf_response resp;
    memset(&resp, 0, sizeof(resp));
    resp.body = (char *)body;
    resp.body_len = strlen(body);
    resp.status = 200;

    wf_lex_tools_ozone_moderation_query_events_main_output *out = NULL;
    CHECK(wf_ozone_query_events_parse(&resp, &out) == WF_OK);
    CHECK(out != NULL);
    CHECK(out->events.count == 1);
    if (out->has_cursor) {
        CHECK(strcmp(out->cursor, "c1") == 0);
    }
    wf_lex_tools_ozone_moderation_query_events_main_output_free(out);
    return 0;
}

/* Verify the generated reporterStats output decoder consumes a body. */
static int test_get_reporter_stats_parse(void) {
    static const char *body =
        "{\"stats\":[{\"did\":\"did:plc:reporterexample\","
        "\"accountReportCount\":3,\"recordReportCount\":1,"
        "\"reportedAccountCount\":2,\"reportedRecordCount\":1,"
        "\"takendownAccountCount\":0,\"takendownRecordCount\":0,"
        "\"labeledAccountCount\":1,\"labeledRecordCount\":1}]}";

    wf_response resp;
    memset(&resp, 0, sizeof(resp));
    resp.body = (char *)body;
    resp.body_len = strlen(body);
    resp.status = 200;

    wf_lex_tools_ozone_moderation_get_reporter_stats_main_output *out = NULL;
    CHECK(wf_ozone_get_reporter_stats_parse(&resp, &out) == WF_OK);
    CHECK(out != NULL);
    CHECK(out->stats.count == 1);
    wf_lex_tools_ozone_moderation_get_reporter_stats_main_output_free(out);
    return 0;
}

int main(void) {
    CHECK(test_emit_event_serialize() == 0);
    CHECK(test_query_events_parse() == 0);
    CHECK(test_get_reporter_stats_parse() == 0);
    printf("ozone: all offline tests passed\n");
    return 0;
}
