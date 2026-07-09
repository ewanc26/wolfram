/*
 * test_actor_prefs_typed.c — offline unit tests for the app.bsky.actor
 * preferences union parser/builder, the declaration parser/builder, and the
 * agent wrapper argument validation. No network.
 */

#include "wolfram/actor_prefs_typed.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* tiny test-only strdup (heap string used to populate the owned structs) */
static char *mkstr(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t n = strlen(s);
    char *c = (char *)malloc(n + 1);
    if (c) {
        memcpy(c, s, n + 1);
    }
    return c;
}

/* A getPreferences "preferences" array with three known types and one unknown. */
static const char *k_prefs_json =
    "["
    "{\"$type\":\"app.bsky.actor.defs#adultContentPref\",\"enabled\":true},"
    "{\"$type\":\"app.bsky.actor.defs#contentLabelPref\",\"label\":\"!\","
    "\"visibility\":\"hide\",\"labelerDid\":\"did:plc:lab\"},"
    "{\"$type\":\"app.bsky.actor.defs#labelersPref\",\"labelers\":"
    "[\"did:plc:l1\",\"did:plc:l2\"]},"
    "{\"$type\":\"app.bsky.actor.defs#mysteryPref\",\"foo\":\"bar\"}"
    "]";

/* Envelope form {"preferences":[...]} for the same content. */
static const char *k_prefs_envelope =
    "{\"preferences\":["
    "{\"$type\":\"app.bsky.actor.defs#threadViewPref\",\"sort\":\"newest\"}"
    "]}";

/* A declaration record. */
static const char *k_decl_json =
    "{\"actorType\":\"app.bsky.actor.declaration#appUser\","
    "\"since\":\"2024-01-01T00:00:00Z\",\"password\":\"secret\",\"note\":\"x\"}";

int main(void) {
    /* ---- parse a bare preferences array ---- */
    {
        wf_actor_preferences out = {0};
        wf_status s = wf_actor_parse_preferences(k_prefs_json,
                                                 strlen(k_prefs_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.adult_content.has_enabled && out.adult_content.enabled);
        WF_CHECK(out.content_label_count == 1);
        WF_CHECK(out.content_labels != NULL);
        WF_CHECK(out.content_labels[0].label &&
                 strcmp(out.content_labels[0].label, "!") == 0);
        WF_CHECK(out.content_labels[0].visibility &&
                 strcmp(out.content_labels[0].visibility, "hide") == 0);
        WF_CHECK(out.content_labels[0].labeler_did &&
                 strcmp(out.content_labels[0].labeler_did, "did:plc:lab") == 0);
        WF_CHECK(out.labelers.labeler_count == 2);
        WF_CHECK(out.labelers.labelers[1] &&
                 strcmp(out.labelers.labelers[1], "did:plc:l2") == 0);
        /* unknown $type preserved verbatim in extra */
        WF_CHECK(out.extra != NULL);
        if (out.extra) {
            WF_CHECK(cJSON_GetArraySize(out.extra) == 1);
            cJSON *unk = cJSON_GetArrayItem(out.extra, 0);
            cJSON *t = cJSON_GetObjectItemCaseSensitive(unk, "$type");
            WF_CHECK(cJSON_IsString(t) && t->valuestring &&
                     strcmp(t->valuestring,
                            "app.bsky.actor.defs#mysteryPref") == 0);
            cJSON *foo = cJSON_GetObjectItemCaseSensitive(unk, "foo");
            WF_CHECK(cJSON_IsString(foo) && foo->valuestring &&
                     strcmp(foo->valuestring, "bar") == 0);
        }
        wf_actor_preferences_free(&out);
        wf_actor_preferences_free(&out); /* double-free must be safe */
    }

    /* ---- parse the envelope form + round-trip via builder ---- */
    {
        wf_actor_preferences out = {0};
        wf_status s = wf_actor_parse_preferences(k_prefs_envelope,
                                                 strlen(k_prefs_envelope), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.thread_view.sort &&
                 strcmp(out.thread_view.sort, "newest") == 0);

        char *json = NULL;
        s = wf_actor_build_preferences(&out, &json);
        WF_CHECK(s == WF_OK);
        WF_CHECK(json != NULL);
        if (json) {
            wf_actor_preferences rt = {0};
            s = wf_actor_parse_preferences(json, strlen(json), &rt);
            WF_CHECK(s == WF_OK);
            WF_CHECK(rt.thread_view.sort &&
                     strcmp(rt.thread_view.sort, "newest") == 0);
            wf_actor_preferences_free(&rt);
            free(json);
        }
        wf_actor_preferences_free(&out);
    }

    /* ---- builder round-trips a rich preferences set ---- */
    {
        wf_actor_preferences p = {0};
        p.adult_content.has_enabled = true;
        p.adult_content.enabled = true;

        p.content_labels =
            (wf_actor_pref_content_label *)calloc(1, sizeof(*p.content_labels));
        p.content_label_count = 1;
        p.content_labels[0].label = mkstr("gore");
        p.content_labels[0].visibility = mkstr("warn");

        p.interests.tags = (char **)calloc(2, sizeof(char *));
        p.interests.tag_count = 2;
        p.interests.tags[0] = mkstr("art");
        p.interests.tags[1] = mkstr("music");

        p.labelers.labelers = (char **)calloc(1, sizeof(char *));
        p.labelers.labeler_count = 1;
        p.labelers.labelers[0] =
            mkstr("did:plc:lab");

        char *json = NULL;
        wf_status s = wf_actor_build_preferences(&p, &json);
        WF_CHECK(s == WF_OK);
        WF_CHECK(json != NULL);
        if (json) {
            wf_actor_preferences rt = {0};
            s = wf_actor_parse_preferences(json, strlen(json), &rt);
            WF_CHECK(s == WF_OK);
            WF_CHECK(rt.adult_content.enabled);
            WF_CHECK(rt.content_label_count == 1);
            WF_CHECK(rt.content_labels &&
                     strcmp(rt.content_labels[0].label, "gore") == 0);
            WF_CHECK(rt.interests.tag_count == 2);
            WF_CHECK(rt.labelers.labeler_count == 1);
            wf_actor_preferences_free(&rt);
            free(json);
        }
        wf_actor_preferences_free(&p);
    }

    /* ---- declaration parse / build ---- */
    {
        wf_actor_declaration out = {0};
        wf_status s = wf_actor_parse_declaration(k_decl_json,
                                                 strlen(k_decl_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.actor_type &&
                 strcmp(out.actor_type,
                        "app.bsky.actor.declaration#appUser") == 0);
        WF_CHECK(out.since &&
                 strcmp(out.since, "2024-01-01T00:00:00Z") == 0);
        WF_CHECK(out.password && strcmp(out.password, "secret") == 0);
        WF_CHECK(out.extra != NULL); /* "note" preserved */
        if (out.extra) {
            cJSON *note = cJSON_GetObjectItemCaseSensitive(out.extra, "note");
            WF_CHECK(cJSON_IsString(note) &&
                     strcmp(note->valuestring, "x") == 0);
        }

        char *json = NULL;
        s = wf_actor_build_declaration(&out, &json);
        WF_CHECK(s == WF_OK);
        WF_CHECK(json != NULL);
        if (json) {
            wf_actor_declaration rt = {0};
            s = wf_actor_parse_declaration(json, strlen(json), &rt);
            WF_CHECK(s == WF_OK);
            WF_CHECK(rt.actor_type &&
                     strcmp(rt.actor_type,
                            "app.bsky.actor.declaration#appUser") == 0);
            WF_CHECK(rt.extra != NULL);
            wf_actor_declaration_free(&rt);
            free(json);
        }
        wf_actor_declaration_free(&out);
        wf_actor_declaration_free(&out); /* double-free safe */
    }

    /* ---- invalid / NULL input handling ---- */
    {
        wf_actor_preferences p = {0};
        wf_actor_declaration d = {0};
        WF_CHECK(wf_actor_parse_preferences(NULL, 0, &p) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_actor_parse_preferences(k_prefs_json, strlen(k_prefs_json),
                                            NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_actor_parse_preferences("not json{", 9, &p) ==
                 WF_ERR_PARSE);
        WF_CHECK(wf_actor_parse_preferences("{}", 2, &p) == WF_ERR_PARSE);
        WF_CHECK(wf_actor_build_preferences(NULL, (char **)&p) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_actor_build_preferences(&p, NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_actor_parse_declaration(NULL, 0, &d) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_actor_build_declaration(NULL, (char **)&d) ==
                 WF_ERR_INVALID_ARG);
    }

    /* ---- agent wrapper argument validation (returns before network) ---- */
    {
        wf_actor_preferences p = {0};
        wf_actor_declaration d = {0};
        wf_agent_actor_list actors = {0};

        WF_CHECK(wf_agent_get_actor_prefs_typed(NULL, &p) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_put_actor_prefs_typed(NULL, &p) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_suggestions_typed(NULL, 10, NULL, &actors) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_declare_actor_typed(NULL, &d) ==
                 WF_ERR_INVALID_ARG);
        /* a non-NULL opaque agent pointer is rejected because its client is
         * required; we only pass NULL here as the struct is opaque to callers.
         * The wrappers validate agent/required args before any network use. */
    }

    WF_TEST_SUMMARY();
    return 0; /* unreachable */
}
