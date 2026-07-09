/*
 * test_actor_status_typed.c — offline unit tests for the app.bsky.actor.status
 * typed parsers, builder, and wrapper argument validation. No network.
 */

#include "wolfram/actor_status_typed.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A stored app.bsky.actor.status record. */
static const char *k_record_json =
    "{\"$type\":\"app.bsky.actor.status\","
    "\"status\":\"app.bsky.actor.status#live\","
    "\"createdAt\":\"2024-06-01T12:00:00Z\","
    "\"durationMinutes\":30,"
    "\"embed\":{\"$type\":\"app.bsky.embed.external\"},"
    "\"customField\":\"kept\"}";

/* A statusView (app.bsky.actor.defs#statusView) plus task-described envelope
 * fields when present. */
static const char *k_view_json =
    "{\"uri\":\"at://did:plc:abc/app.bsky.actor.status/self\","
    "\"cid\":\"bafyreigh\","
    "\"status\":\"app.bsky.actor.status#live\","
    "\"createdAt\":\"2024-06-01T12:00:00Z\","
    "\"durationMinutes\":30,"
    "\"expiresAt\":\"2024-06-01T12:30:00Z\","
    "\"isActive\":true,\"isDisabled\":false,"
    "\"actor\":\"did:plc:abc\",\"lastUpdated\":\"2024-06-01T12:00:05Z\","
    "\"viewerState\":{\"muted\":false},"
    "\"embed\":{\"$type\":\"app.bsky.embed.external\"}}";

/* Build a record JSON with cJSON and parse it back (round-trip via builder). */
static char *build_sample_record(void) {
    cJSON *embed = cJSON_CreateObject();
    cJSON_AddStringToObject(embed, "$type", "app.bsky.embed.external");
    char *json = NULL;
    wf_status s = wf_actor_status_build_record(
        "2024-06-01T12:00:00Z", "app.bsky.actor.status#live", embed, &json);
    cJSON_Delete(embed);
    (void)s;
    return json;
}

int main(void) {
    /* ---- parse record ---- */
    {
        wf_actor_status rec = {0};
        wf_status s = wf_actor_status_parse_record(k_record_json,
                                                    strlen(k_record_json),
                                                    &rec);
        WF_CHECK(s == WF_OK);
        WF_CHECK(rec.status &&
                 strcmp(rec.status, "app.bsky.actor.status#live") == 0);
        WF_CHECK(rec.created_at &&
                 strcmp(rec.created_at, "2024-06-01T12:00:00Z") == 0);
        WF_CHECK(rec.has_duration_minutes && rec.duration_minutes == 30);
        WF_CHECK(rec.embed != NULL);
        WF_CHECK(rec.extra != NULL); /* customField retained */
        wf_actor_status_free(&rec);
        wf_actor_status_free(&rec); /* idempotent */
    }

    /* ---- parse view ---- */
    {
        wf_actor_status_view v = {0};
        wf_status s = wf_actor_status_parse_view(k_view_json,
                                                  strlen(k_view_json), &v);
        WF_CHECK(s == WF_OK);
        WF_CHECK(v.uri &&
                 strcmp(v.uri,
                        "at://did:plc:abc/app.bsky.actor.status/self") == 0);
        WF_CHECK(v.cid && strcmp(v.cid, "bafyreigh") == 0);
        WF_CHECK(v.status &&
                 strcmp(v.status, "app.bsky.actor.status#live") == 0);
        WF_CHECK(v.created_at &&
                 strcmp(v.created_at, "2024-06-01T12:00:00Z") == 0);
        WF_CHECK(v.has_duration_minutes && v.duration_minutes == 30);
        WF_CHECK(v.expires_at &&
                 strcmp(v.expires_at, "2024-06-01T12:30:00Z") == 0);
        WF_CHECK(v.has_is_active && v.is_active);
        WF_CHECK(v.has_is_disabled && !v.is_disabled);
        WF_CHECK(v.actor && strcmp(v.actor, "did:plc:abc") == 0);
        WF_CHECK(v.last_updated &&
                 strcmp(v.last_updated, "2024-06-01T12:00:05Z") == 0);
        WF_CHECK(v.viewer_state != NULL);
        WF_CHECK(v.embed != NULL);
        if (v.viewer_state) {
            cJSON *muted = cJSON_GetObjectItemCaseSensitive(v.viewer_state,
                                                            "muted");
            WF_CHECK(cJSON_IsBool(muted) && !cJSON_IsTrue(muted));
        }
        wf_actor_status_view_free(&v);
        wf_actor_status_view_free(&v); /* idempotent */
    }

    /* ---- builder round-trips ---- */
    {
        char *json = build_sample_record();
        WF_CHECK(json != NULL);
        if (json) {
            wf_actor_status rec = {0};
            wf_status s = wf_actor_status_parse_record(json, strlen(json),
                                                        &rec);
            WF_CHECK(s == WF_OK);
            WF_CHECK(rec.status &&
                     strcmp(rec.status, "app.bsky.actor.status#live") == 0);
            WF_CHECK(rec.created_at &&
                     strcmp(rec.created_at, "2024-06-01T12:00:00Z") == 0);
            WF_CHECK(rec.embed != NULL);
            wf_actor_status_free(&rec);
            free(json);
        }
    }

    /* ---- invalid / NULL input handling ---- */
    {
        wf_actor_status rec = {0};
        wf_actor_status_view v = {0};
        wf_actor_status_put_result res = {0};

        WF_CHECK(wf_actor_status_parse_record(NULL, 0, &rec) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_actor_status_parse_record(k_record_json,
                                               strlen(k_record_json),
                                               NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_actor_status_parse_record("not json{", 9, &rec) ==
                 WF_ERR_PARSE);

        WF_CHECK(wf_actor_status_parse_view(NULL, 0, &v) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_actor_status_parse_view("nope", 4, &v) == WF_ERR_PARSE);

        /* builder: missing required inputs */
        char *out = NULL;
        WF_CHECK(wf_actor_status_build_record(NULL, "x", NULL, &out) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_actor_status_build_record("2024-06-01T00:00:00Z", NULL,
                                              NULL, &out) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_actor_status_build_record("2024-06-01T00:00:00Z", "x",
                                              NULL, NULL) == WF_ERR_INVALID_ARG);

        /* agent wrappers: honest stubs return WF_ERR_INVALID_ARG.
         * NULL-required-input validation returns the same code. */
        WF_CHECK(wf_agent_get_actor_status(NULL, "did:plc:abc", &v) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_actor_status((wf_agent *)1, NULL, &v) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_actor_status((wf_agent *)1, "did:plc:abc",
                                           NULL) == WF_ERR_INVALID_ARG);
        /* even with valid inputs the generated binding is absent -> stub */
        WF_CHECK(wf_agent_get_actor_status((wf_agent *)1, "did:plc:abc", &v) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_get_status(NULL, &v) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_status((wf_agent *)1, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_status((wf_agent *)1, &v) == WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_put_status(NULL, &rec, &res) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_put_status((wf_agent *)1, NULL, &res) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_put_status((wf_agent *)1, &rec, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_put_status((wf_agent *)1, &rec, &res) ==
                 WF_ERR_INVALID_ARG);

        wf_actor_status_free(&rec);
        wf_actor_status_view_free(&v);
        wf_actor_status_put_result_free(&res);
    }

    WF_TEST_SUMMARY();
    return 0; /* unreachable */
}
