/**
 * test_mock_pds.c — integration test for the SDK's XRPC transport against
 * a real local HTTP server (libmicrohttpd-backed mock PDS).
 *
 * Fully offline and deterministic: the mock PDS binds an ephemeral port and
 * serves canned JSON, so no external network is touched. Built only when
 * WOLFRAM_BUILD_TEST_HTTPD=ON.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/xrpc.h"
#include "mock_pds.h"
#include "test.h"

#include <cJSON.h>

static int
json_str_eq(const char *json, const char *key, const char *expect) {
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return 0;
    }
    cJSON *field = cJSON_GetObjectItemCaseSensitive(root, key);
    int ok = cJSON_IsString(field) && strcmp(field->valuestring, expect) == 0;
    cJSON_Delete(root);
    return ok;
}

int main(void) {
    int port = 0;
    wf_mock_pds *pds = NULL;

    WF_CHECK(wf_mock_pds_start(&pds, &port) == WF_OK);
    WF_CHECK(pds != NULL);
    WF_CHECK(port > 0);

    const char *session_json =
        "{\"handle\":\"alice.test\",\"did\":\"did:plc:abc123\","
        "\"accessJwt\":\"eyJ.fake.access\",\"refreshJwt\":\"eyJ.fake.refresh\"}";
    const char *timeline_json =
        "{\"feed\":[{\"post\":{\"uri\":\"at://did:plc:abc123/app.bsky.feed.post/1\","
        "\"text\":\"hello offline world\"}}],\"cursor\":\"c1\"}";

    WF_CHECK(wf_mock_pds_register(pds, "com.atproto.server.createSession",
                                  session_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "app.bsky.feed.getTimeline",
                                  timeline_json) == WF_OK);

    char base_url[64];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%d", port);

    wf_xrpc_client *client = wf_xrpc_client_new(base_url);
    WF_CHECK(client != NULL);

    /* Procedure: createSession returns the canned session. */
    wf_response res = {0};
    wf_status st = wf_xrpc_procedure(client,
                                     "com.atproto.server.createSession",
                                     "{\"identifier\":\"alice.test\","
                                     "\"password\":\"hunter2\"}",
                                     &res);
    WF_CHECK(st == WF_OK);
    WF_CHECK(res.status == 200);
    WF_CHECK(res.body != NULL);
    WF_CHECK(json_str_eq(res.body, "handle", "alice.test"));
    WF_CHECK(json_str_eq(res.body, "did", "did:plc:abc123"));
    wf_response_free(&res);

    /* Query: getTimeline returns the canned feed. */
    wf_response tl = {0};
    st = wf_xrpc_query(client, "app.bsky.feed.getTimeline", NULL, &tl);
    WF_CHECK(st == WF_OK);
    WF_CHECK(tl.status == 200);
    WF_CHECK(tl.body != NULL);

    cJSON *root = cJSON_Parse(tl.body);
    WF_CHECK(root != NULL);
    if (root != NULL) {
        cJSON *feed = cJSON_GetObjectItemCaseSensitive(root, "feed");
        WF_CHECK(cJSON_IsArray(feed) && cJSON_GetArraySize(feed) == 1);
        cJSON *post = cJSON_GetObjectItemCaseSensitive(
            cJSON_GetArrayItem(feed, 0), "post");
        cJSON *text = cJSON_GetObjectItemCaseSensitive(post, "text");
        WF_CHECK(cJSON_IsString(text) &&
                 strcmp(text->valuestring, "hello offline world") == 0);
        cJSON_Delete(root);
    }
    wf_response_free(&tl);

    /* Unknown NSID yields HTTP 404 with no crash. */
    wf_response missing = {0};
    st = wf_xrpc_query(client, "com.atproto.repo.unknownEndpoint", NULL,
                       &missing);
    WF_CHECK(st == WF_ERR_HTTP);
    WF_CHECK(missing.status == 404);
    wf_response_free(&missing);

    wf_xrpc_client_free(client);
    wf_mock_pds_free(pds);

    WF_TEST_SUMMARY();
}
