/*
 * test_graph_writes.c — offline integration tests for the app.bsky.graph
 * write wrappers (graph_write.c). Drives a real local libmicrohttpd mock PDS
 * (mock_pds.c) so the SDK transports end-to-end without touching the network.
 *
 * Exercises every create helper (asserts the returned uri/cid and the exact
 * request payload sent) plus the delete and mute/unmute procedures.
 *
 * Built only when WOLFRAM_BUILD_TEST_HTTPD=ON.
 */

#include "wolfram/graph_write.h"
#include "wolfram/agent.h"
#include "wolfram/syntax.h"

#include "mock_pds.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Assert a string field `key` inside object `obj` (NULL = top level) of the
 * JSON `body` equals `expect`. */
static int json_field_eq(const char *body, const char *obj, const char *key,
                         const char *expect) {
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return 0;
    }
    cJSON *cur = root;
    if (obj) {
        cJSON *o = cJSON_GetObjectItemCaseSensitive(root, obj);
        if (!cJSON_IsObject(o)) {
            cJSON_Delete(root);
            return 0;
        }
        cur = o;
    }
    cJSON *f = cJSON_GetObjectItemCaseSensitive(cur, key);
    int ok = cJSON_IsString(f) && strcmp(f->valuestring, expect) == 0;
    cJSON_Delete(root);
    return ok;
}

/* Assert a string field `key` inside object `obj` (NULL = top level) exists
 * and is a non-NULL string (value not yet checked). */
static int json_field_present(const char *body, const char *obj,
                             const char *key) {
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        return 0;
    }
    cJSON *cur = root;
    if (obj) {
        cJSON *o = cJSON_GetObjectItemCaseSensitive(root, obj);
        if (!cJSON_IsObject(o)) {
            cJSON_Delete(root);
            return 0;
        }
        cur = o;
    }
    cJSON *f = cJSON_GetObjectItemCaseSensitive(cur, key);
    int ok = cJSON_IsString(f) && f->valuestring != NULL;
    cJSON_Delete(root);
    return ok;
}

/* Assert a top-level string field of `body` equals `expect`. */
static int json_top_eq(const char *body, const char *key, const char *expect) {
    return json_field_eq(body, NULL, key, expect);
}

int main(void) {
    wf_mock_pds *pds = NULL;
    int port = 0;
    WF_CHECK(wf_mock_pds_start(&pds, &port) == WF_OK);
    WF_CHECK(pds != NULL);
    WF_CHECK(port > 0);

    /* Canned PDS responses. refreshSession is hit by wf_agent_resume. */
    const char *session_json =
        "{\"did\":\"did:plc:abc123\",\"handle\":\"alice.test\","
        "\"accessJwt\":\"eyJ.fake.access\",\"refreshJwt\":\"eyJ.fake.refresh\","
        "\"active\":true}";
    const char *create_json =
        "{\"uri\":\"at://did:plc:abc123/app.bsky.graph.list/abcXX1\",\""
        "cid\":\"bafycidcreated\"}";
    const char *put_json =
        "{\"uri\":\"at://did:plc:abc123/app.bsky.graph.starterpack/abcXX2\",\""
        "cid\":\"bafycidput\"}";
    const char *empty_json = "{}";

    WF_CHECK(wf_mock_pds_register(pds, "com.atproto.server.refreshSession",
                                  session_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "com.atproto.repo.createRecord",
                                  create_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "com.atproto.repo.putRecord",
                                  put_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "com.atproto.repo.deleteRecord",
                                  empty_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "app.bsky.graph.muteThread",
                                  empty_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "app.bsky.graph.unmuteThread",
                                  empty_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "app.bsky.graph.muteActorList",
                                  empty_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "app.bsky.graph.unmuteActorList",
                                  empty_json) == WF_OK);

    char base_url[64];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%d", port);

    wf_agent *agent = wf_agent_new(base_url);
    WF_CHECK(agent != NULL);

    /* Install a session so the agent is "logged in" (resume triggers a refresh
     * round-trip against the mock). */
    wf_session_data data;
    memset(&data, 0, sizeof(data));
    data.did = "did:plc:abc123";
    data.handle = "alice.test";
    data.access_jwt = "eyJ.fake.access";
    data.refresh_jwt = "eyJ.fake.refresh";
    data.active = 1;
    WF_CHECK(wf_agent_resume(agent, &data) == WF_OK);
    wf_session_data sd;
    memset(&sd, 0, sizeof(sd));
    WF_CHECK(wf_agent_get_session_data(agent, &sd) == WF_OK);
    WF_CHECK(sd.did != NULL && strcmp(sd.did, "did:plc:abc123") == 0);
    wf_agent_session_data_free(&sd);

    const char *last_nsid = NULL;
    const char *last_method = NULL;
    const char *last_body = NULL;

    /* ---- createList ---- */
    {
        wf_agent_post_result out = {0};
        wf_status st = wf_agent_graph_create_list(
            agent, "app.bsky.graph.defs#curatelist", "My List", "a list", &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out.uri != NULL && out.cid != NULL);
        WF_CHECK(strncmp(out.uri, "at://", 5) == 0);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid && strcmp(last_nsid, "com.atproto.repo.createRecord") == 0);
        WF_CHECK(last_method && strcmp(last_method, "POST") == 0);
        WF_CHECK(json_top_eq(last_body, "collection", "app.bsky.graph.list"));
        WF_CHECK(json_field_eq(last_body, "record", "$type", "app.bsky.graph.list"));
        WF_CHECK(json_field_eq(last_body, "record", "name", "My List"));
        WF_CHECK(json_field_eq(last_body, "record", "purpose",
                               "app.bsky.graph.defs#curatelist"));
        WF_CHECK(json_field_eq(last_body, "record", "description", "a list"));
        WF_CHECK(json_field_present(last_body, "record", "createdAt"));
        wf_agent_post_result_free(&out);
    }

    /* ---- createListItem ---- */
    {
        wf_agent_post_result out = {0};
        wf_status st = wf_agent_graph_create_list_item(
            agent, "at://did:plc:abc123/app.bsky.graph.list/abcXX1",
            "did:plc:subject1", &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out.uri != NULL && out.cid != NULL);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(json_top_eq(last_body, "collection", "app.bsky.graph.listitem"));
        WF_CHECK(json_field_eq(last_body, "record", "subject", "did:plc:subject1"));
        WF_CHECK(json_field_eq(last_body, "record", "list",
                               "at://did:plc:abc123/app.bsky.graph.list/abcXX1"));
        wf_agent_post_result_free(&out);
    }

    /* ---- createStarterPack ---- */
    {
        wf_agent_post_result out = {0};
        wf_status st = wf_agent_graph_create_starter_pack(
            agent, "My Pack", "at://did:plc:abc123/app.bsky.graph.list/abcXX1",
            "a pack", "[{\"uri\":\"at://did:plc:abc123/app.bsky.feed.generator/feed1\"}]",
            &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out.uri != NULL && out.cid != NULL);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(json_top_eq(last_body, "collection", "app.bsky.graph.starterpack"));
        WF_CHECK(json_field_eq(last_body, "record", "name", "My Pack"));
        WF_CHECK(json_field_eq(last_body, "record", "description", "a pack"));
        WF_CHECK(json_field_eq(last_body, "record", "list",
                               "at://did:plc:abc123/app.bsky.graph.list/abcXX1"));
        cJSON *root = cJSON_Parse(last_body);
        cJSON *rec = root ? cJSON_GetObjectItemCaseSensitive(root, "record") : NULL;
        cJSON *feeds = rec ? cJSON_GetObjectItemCaseSensitive(rec, "feeds") : NULL;
        WF_CHECK(cJSON_IsArray(feeds) && cJSON_GetArraySize(feeds) == 1);
        cJSON_Delete(root);
        wf_agent_post_result_free(&out);
    }

    /* ---- createListBlock ---- */
    {
        wf_agent_post_result out = {0};
        wf_status st = wf_agent_graph_create_list_block(
            agent, "at://did:plc:abc123/app.bsky.graph.list/abcXX1", &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out.uri != NULL && out.cid != NULL);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(json_top_eq(last_body, "collection", "app.bsky.graph.listblock"));
        WF_CHECK(json_field_eq(last_body, "record", "subject",
                               "at://did:plc:abc123/app.bsky.graph.list/abcXX1"));
        wf_agent_post_result_free(&out);
    }

    /* ---- block ---- */
    {
        wf_agent_post_result out = {0};
        wf_status st = wf_agent_graph_block(agent, "did:plc:blockme", &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out.uri != NULL && out.cid != NULL);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(json_top_eq(last_body, "collection", "app.bsky.graph.block"));
        WF_CHECK(json_field_eq(last_body, "record", "subject", "did:plc:blockme"));
        wf_agent_post_result_free(&out);
    }

    /* ---- updateStarterPack (putRecord) ---- */
    {
        wf_agent_post_result out = {0};
        const char *new_rec =
            "{\"$type\":\"app.bsky.graph.starterpack\",\"name\":\"Renamed\","
            "\"list\":\"at://did:plc:abc123/app.bsky.graph.list/abcXX1\","
            "\"createdAt\":\"2026-07-09T00:00:00Z\"}";
        wf_status st = wf_agent_graph_update_starter_pack(
            agent, "abcXX2", new_rec, &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out.uri != NULL && strstr(out.uri, "starterpack") != NULL);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid && strcmp(last_nsid, "com.atproto.repo.putRecord") == 0);
        WF_CHECK(json_top_eq(last_body, "collection", "app.bsky.graph.starterpack"));
        WF_CHECK(json_top_eq(last_body, "rkey", "abcXX2"));
        wf_agent_post_result_free(&out);
    }

    /* ---- deletes ---- */
    {
        wf_status st = wf_agent_graph_delete_list(
            agent, "at://did:plc:abc123/app.bsky.graph.list/abcXX1");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid && strcmp(last_nsid, "com.atproto.repo.deleteRecord") == 0);
        WF_CHECK(json_top_eq(last_body, "collection", "app.bsky.graph.list"));
        WF_CHECK(json_top_eq(last_body, "rkey", "abcXX1"));
    }
    {
        wf_status st = wf_agent_graph_delete_list_item(
            agent, "at://did:plc:abc123/app.bsky.graph.listitem/abcXX9");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(json_top_eq(last_body, "collection", "app.bsky.graph.listitem"));
    }
    {
        wf_status st = wf_agent_graph_delete_starter_pack(
            agent, "at://did:plc:abc123/app.bsky.graph.starterpack/abcXX2");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(json_top_eq(last_body, "collection", "app.bsky.graph.starterpack"));
    }
    {
        wf_status st = wf_agent_graph_delete_list_block(
            agent, "at://did:plc:abc123/app.bsky.graph.listblock/abcXX3");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(json_top_eq(last_body, "collection", "app.bsky.graph.listblock"));
    }
    {
        wf_status st = wf_agent_graph_unblock(
            agent, "at://did:plc:abc123/app.bsky.graph.block/abcXX4");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(json_top_eq(last_body, "collection", "app.bsky.graph.block"));
    }

    /* ---- mute / unmute procedures ---- */
    {
        wf_status st = wf_agent_graph_mute_thread(
            agent, "at://did:plc:abc123/app.bsky.feed.post/root1");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid && strcmp(last_nsid, "app.bsky.graph.muteThread") == 0);
        WF_CHECK(json_top_eq(last_body, "root",
                              "at://did:plc:abc123/app.bsky.feed.post/root1"));
    }
    {
        wf_status st = wf_agent_graph_unmute_thread(
            agent, "at://did:plc:abc123/app.bsky.feed.post/root1");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid && strcmp(last_nsid, "app.bsky.graph.unmuteThread") == 0);
    }
    {
        wf_status st = wf_agent_graph_mute_actor_list(
            agent, "at://did:plc:abc123/app.bsky.graph.list/abcXX1");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid && strcmp(last_nsid, "app.bsky.graph.muteActorList") == 0);
        WF_CHECK(json_top_eq(last_body, "list",
                              "at://did:plc:abc123/app.bsky.graph.list/abcXX1"));
    }
    {
        wf_status st = wf_agent_graph_unmute_actor_list(
            agent, "at://did:plc:abc123/app.bsky.graph.list/abcXX1");
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid && strcmp(last_nsid, "app.bsky.graph.unmuteActorList") == 0);
    }

    /* ---- invalid-arg guards ---- */
    {
        wf_agent_post_result out = {0};
        WF_CHECK(wf_agent_graph_block(agent, NULL, &out) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_graph_create_list(agent, NULL, "n", NULL, &out) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_graph_create_list_item(
                     agent, "not-an-uri", "did:plc:x", &out) == WF_ERR_PARSE);
        WF_CHECK(wf_agent_graph_delete_list(
                     agent, "at://did:plc:other/app.bsky.graph.list/x") ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_graph_mute_thread(agent, "bad-uri") == WF_ERR_PARSE);
    }

    wf_agent_free(agent);
    wf_mock_pds_free(pds);

    WF_TEST_SUMMARY();
}
