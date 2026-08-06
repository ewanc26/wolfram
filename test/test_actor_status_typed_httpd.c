/*
 * test_actor_status_typed_httpd.c — offline integration tests for the
 * app.bsky.actor.status agent wrappers (actor_status_typed.c). Drives a real
 * local libmicrohttpd mock PDS (mock_pds.c) end-to-end: getActorStatus/
 * getStatus read the "status" field of a getProfile response, and putStatus
 * writes a com.atproto.repo.putRecord — there is no dedicated RPC for any of
 * the three, so this is the only way to exercise them against a real
 * transport.
 *
 * Built only when WOLFRAM_BUILD_TEST_HTTPD=ON.
 */

#include "wolfram/actor_status_typed.h"
#include "wolfram/agent.h"

#include "mock_pds.h"
#include "test.h"

#include <cJSON.h>

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

int main(void) {
    wf_mock_pds *pds = NULL;
    int port = 0;
    WF_CHECK(wf_mock_pds_start(&pds, &port) == WF_OK);
    WF_CHECK(pds != NULL);
    WF_CHECK(port > 0);

    const char *session_json =
        "{\"did\":\"did:plc:abc123\",\"handle\":\"alice.test\","
        "\"accessJwt\":\"eyJ.fake.access\",\"refreshJwt\":\"eyJ.fake.refresh\","
        "\"active\":true}";
    /* getProfile with an embedded live statusView: createdAt/durationMinutes
     * live inside `record`, matching the real defs.json shape (see the
     * wf_actor_status_parse_view fix). */
    const char *profile_with_status_json =
        "{\"did\":\"did:plc:abc123\",\"handle\":\"alice.test\","
        "\"status\":{\"uri\":\"at://did:plc:abc123/app.bsky.actor.status/"
        "self\",\"cid\":\"bafyreigh\",\"status\":\"app.bsky.actor.status#"
        "live\",\"record\":{\"$type\":\"app.bsky.actor.status\","
        "\"status\":\"app.bsky.actor.status#live\","
        "\"createdAt\":\"2024-06-01T12:00:00Z\",\"durationMinutes\":30},"
        "\"isActive\":true}}";
    const char *profile_no_status_json =
        "{\"did\":\"did:plc:abc123\",\"handle\":\"alice.test\"}";
    const char *put_json =
        "{\"uri\":\"at://did:plc:abc123/app.bsky.actor.status/self\","
        "\"cid\":\"bafyreigput\"}";

    WF_CHECK(wf_mock_pds_register(pds, "com.atproto.server.refreshSession",
                                  session_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "app.bsky.actor.getProfile",
                                  profile_with_status_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "com.atproto.repo.putRecord",
                                  put_json) == WF_OK);

    char base_url[64];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%d", port);

    wf_agent *agent = wf_agent_new(base_url);
    WF_CHECK(agent != NULL);

    wf_session_data data;
    memset(&data, 0, sizeof(data));
    data.did = "did:plc:abc123";
    data.handle = "alice.test";
    data.access_jwt = "eyJ.fake.access";
    data.refresh_jwt = "eyJ.fake.refresh";
    data.active = 1;
    WF_CHECK(wf_agent_resume(agent, &data) == WF_OK);

    /* ---- getActorStatus: profile has a live status ---- */
    {
        wf_actor_status_view v = {0};
        WF_CHECK(wf_agent_get_actor_status(agent, "did:plc:abc123", &v) ==
                 WF_OK);
        WF_CHECK(v.uri && strcmp(v.uri, "at://did:plc:abc123/"
                                        "app.bsky.actor.status/self") == 0);
        WF_CHECK(v.cid && strcmp(v.cid, "bafyreigh") == 0);
        WF_CHECK(v.status &&
                 strcmp(v.status, "app.bsky.actor.status#live") == 0);
        /* The bug this test guards against: created_at/duration_minutes
         * come from the nested `record`, not the top level. */
        WF_CHECK(v.created_at &&
                 strcmp(v.created_at, "2024-06-01T12:00:00Z") == 0);
        WF_CHECK(v.has_duration_minutes && v.duration_minutes == 30);
        WF_CHECK(v.has_is_active && v.is_active);
        wf_actor_status_view_free(&v);
    }

    /* ---- getStatus: same path, using the agent's own DID ---- */
    {
        wf_actor_status_view v = {0};
        WF_CHECK(wf_agent_get_status(agent, &v) == WF_OK);
        WF_CHECK(v.status &&
                 strcmp(v.status, "app.bsky.actor.status#live") == 0);
        WF_CHECK(v.created_at &&
                 strcmp(v.created_at, "2024-06-01T12:00:00Z") == 0);
        wf_actor_status_view_free(&v);
    }

    /* ---- getActorStatus: profile has no live status -> NOT_FOUND ---- */
    {
        WF_CHECK(wf_mock_pds_register(pds, "app.bsky.actor.getProfile",
                                      profile_no_status_json) == WF_OK);
        wf_actor_status_view v = {0};
        WF_CHECK(wf_agent_get_actor_status(agent, "did:plc:abc123", &v) ==
                 WF_ERR_NOT_FOUND);
        wf_actor_status_view_free(&v); /* safe on a reset struct */
        /* restore for any later use */
        WF_CHECK(wf_mock_pds_register(pds, "app.bsky.actor.getProfile",
                                      profile_with_status_json) == WF_OK);
    }

    /* ---- putStatus: builds the record and calls putRecord ---- */
    {
        wf_actor_status in = {0};
        in.status = strdup("app.bsky.actor.status#live");
        in.created_at = strdup("2024-06-01T12:00:00Z");
        in.has_duration_minutes = true;
        in.duration_minutes = 15;

        wf_actor_status_put_result out = {0};
        WF_CHECK(wf_agent_put_status(agent, &in, &out) == WF_OK);
        WF_CHECK(out.uri && strcmp(out.uri, "at://did:plc:abc123/"
                                            "app.bsky.actor.status/self") == 0);
        WF_CHECK(out.cid && strcmp(out.cid, "bafyreigput") == 0);
        WF_CHECK(out.value != NULL);

        const char *last_nsid = NULL;
        const char *last_method = NULL;
        const char *last_body = NULL;
        WF_CHECK(wf_mock_pds_get_last_request(pds, &last_nsid, &last_method,
                                              &last_body) == WF_OK);
        WF_CHECK(last_nsid &&
                 strcmp(last_nsid, "com.atproto.repo.putRecord") == 0);
        WF_CHECK(last_body != NULL);
        if (last_body) {
            WF_CHECK(json_field_eq(last_body, NULL, "repo", "did:plc:abc123"));
            WF_CHECK(json_field_eq(last_body, NULL, "collection",
                                   "app.bsky.actor.status"));
            WF_CHECK(json_field_eq(last_body, NULL, "rkey", "self"));
            WF_CHECK(json_field_eq(last_body, "record", "status",
                                   "app.bsky.actor.status#live"));
            WF_CHECK(json_field_eq(last_body, "record", "createdAt",
                                   "2024-06-01T12:00:00Z"));
        }

        wf_actor_status_free(&in);
        wf_actor_status_put_result_free(&out);
    }

    wf_agent_free(agent);
    wf_mock_pds_free(pds);

    WF_TEST_SUMMARY();
    return 0; /* unreachable */
}
