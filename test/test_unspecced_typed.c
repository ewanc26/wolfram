/*
 * test_unspecced_typed.c — offline tests for the app.bsky.unspecced typed
 * parsers.
 *
 * Hardcodes representative JSON fixtures for a selection of endpoints, parses
 * them, asserts key fields, and frees. No network access. The agent wrappers
 * are exercised only for NULL-argument validation (they require a live agent).
 */

#include "wolfram/unspecced_typed.h"

#include "test.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
    /* ---------------- getTrendingTopics ---------------- */
    {
        const char *json =
            "{\"topics\":["
            "{\"topic\":\"#Cats\",\"displayName\":\"Cats\",\"description\":"
            "\"All about cats\",\"link\":\"at://did:plc:x/app.bsky.feed.post/cats\"},"
            "{\"topic\":\"#Dogs\",\"link\":\"at://did:plc:x/app.bsky.feed.post/dogs\"}"
            "],\"suggested\":["
            "{\"topic\":\"#Art\",\"link\":\"at://did:plc:x/app.bsky.feed.post/art\"}"
            "]}";
        wf_agent_trending_topics out = {0};
        wf_status s = wf_agent_parse_trending_topics(json, strlen(json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.topic_count == 2);
        WF_CHECK(out.suggested_count == 1);
        if (out.topic_count == 2) {
            WF_CHECK(out.topics[0].topic != NULL);
            WF_CHECK(strcmp(out.topics[0].topic, "#Cats") == 0);
            WF_CHECK(out.topics[0].display_name != NULL);
            WF_CHECK(strcmp(out.topics[0].display_name, "Cats") == 0);
            WF_CHECK(out.topics[0].description != NULL);
            WF_CHECK(out.topics[0].link != NULL);
            WF_CHECK(out.topics[1].link != NULL);
            WF_CHECK(strcmp(out.topics[1].link,
                            "at://did:plc:x/app.bsky.feed.post/dogs") == 0);
            WF_CHECK(out.topics[1].display_name == NULL);
            WF_CHECK(out.topics[1].description == NULL);
        }
        if (out.suggested_count == 1) {
            WF_CHECK(strcmp(out.suggested[0].topic, "#Art") == 0);
        }
        wf_agent_trending_topics_free(&out);
        WF_CHECK(out.topics == NULL && out.topic_count == 0);
        WF_CHECK(out.suggested == NULL && out.suggested_count == 0);
    }

    /* ---------------- getTaggedSuggestions ---------------- */
    {
        const char *json =
            "{\"suggestions\":["
            "{\"tag\":\"gaming\",\"subjectType\":\"actor\","
            "\"subject\":\"at://did:plc:gamer/app.bsky.actor.profile/self\"},"
            "{\"tag\":\"news\",\"subjectType\":\"feed\","
            "\"subject\":\"at://did:plc:news/app.bsky.feed.generator/daily\"}"
            "]}";
        wf_agent_tagged_suggestions out = {0};
        wf_status s = wf_agent_parse_tagged_suggestions(json, strlen(json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.count == 2);
        if (out.count == 2) {
            WF_CHECK(strcmp(out.items[0].tag, "gaming") == 0);
            WF_CHECK(strcmp(out.items[0].subject_type, "actor") == 0);
            WF_CHECK(strcmp(out.items[0].subject,
                            "at://did:plc:gamer/app.bsky.actor.profile/self") == 0);
            WF_CHECK(strcmp(out.items[1].subject_type, "feed") == 0);
        }
        wf_agent_tagged_suggestions_free(&out);
        WF_CHECK(out.items == NULL && out.count == 0);
    }

    /* ---------------- getSuggestionsSkeleton ---------------- */
    {
        const char *json =
            "{\"cursor\":\"next-cursor\",\"relativeToDid\":\"did:plc:viewer\","
            "\"recIdStr\":\"12345\",\"actors\":[{\"did\":\"did:plc:a\"},"
            "{\"did\":\"did:plc:b\"}]}";
        wf_agent_suggestions_skeleton out = {0};
        wf_status s = wf_agent_parse_suggestions_skeleton(json, strlen(json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.actor_count == 2);
        WF_CHECK(out.cursor != NULL);
        WF_CHECK(strcmp(out.cursor, "next-cursor") == 0);
        WF_CHECK(out.relative_to_did != NULL);
        WF_CHECK(strcmp(out.relative_to_did, "did:plc:viewer") == 0);
        WF_CHECK(out.rec_id_str != NULL);
        WF_CHECK(strcmp(out.rec_id_str, "12345") == 0);
        if (out.actor_count == 2) {
            WF_CHECK(strcmp(out.actors[0].did, "did:plc:a") == 0);
            WF_CHECK(strcmp(out.actors[1].did, "did:plc:b") == 0);
        }
        wf_agent_suggestions_skeleton_free(&out);
    }

    /* ---------------- getConfig ---------------- */
    {
        const char *json =
            "{\"checkEmailConfirmed\":true,\"liveNow\":["
            "{\"did\":\"did:plc:live\",\"domains\":[\"live.example.com\","
            "\"stream.example.com\"]},"
            "{\"did\":\"did:plc:live2\",\"domains\":[]}]}";
        wf_agent_unspecced_config out = {0};
        wf_status s = wf_agent_parse_config(json, strlen(json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.has_check_email_confirmed == 1);
        WF_CHECK(out.check_email_confirmed == 1);
        WF_CHECK(out.live_now_count == 2);
        if (out.live_now_count == 2) {
            WF_CHECK(strcmp(out.live_now[0].did, "did:plc:live") == 0);
            WF_CHECK(out.live_now[0].domain_count == 2);
            WF_CHECK(strcmp(out.live_now[0].domains[0], "live.example.com") == 0);
            WF_CHECK(strcmp(out.live_now[0].domains[1], "stream.example.com") == 0);
            WF_CHECK(strcmp(out.live_now[1].did, "did:plc:live2") == 0);
            WF_CHECK(out.live_now[1].domain_count == 0);
        }
        wf_agent_unspecced_config_free(&out);
    }

    /* ---------------- getAgeAssuranceState ---------------- */
    {
        const char *json =
            "{\"status\":\"pending\",\"lastInitiatedAt\":\"2026-01-02T03:04:05Z\"}";
        wf_agent_age_assurance_state out = {0};
        wf_status s = wf_agent_parse_age_assurance_state(json, strlen(json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.status != NULL);
        WF_CHECK(strcmp(out.status, "pending") == 0);
        WF_CHECK(out.has_last_initiated_at == 1);
        WF_CHECK(out.last_initiated_at != NULL);
        wf_agent_age_assurance_state_free(&out);
    }
    {
        /* status only, optional field absent */
        const char *json = "{\"status\":\"completed\"}";
        wf_agent_age_assurance_state out = {0};
        wf_status s = wf_agent_parse_age_assurance_state(json, strlen(json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(strcmp(out.status, "completed") == 0);
        WF_CHECK(out.has_last_initiated_at == 0);
        wf_agent_age_assurance_state_free(&out);
    }

    /* ---------------- getOnboardingSuggestedStarterPacks (full) ---------------- */
    {
        const char *json =
            "{\"starterPacks\":["
            "{\"uri\":\"at://did:plc:sp/app.bsky.graph.starterpack/aaa\","
            "\"cid\":\"bafyxxx\",\"indexedAt\":\"2026-05-06T07:08:09Z\","
            "\"creator\":{\"did\":\"did:plc:sp\",\"handle\":\"sp.bsky.social\","
            "\"displayName\":\"Starter Pack\",\"avatar\":\"https://a/av.png\"},"
            "\"record\":{\"$type\":\"app.bsky.graph.starterpack\",\"name\":\"Fun\"},"
            "\"joinedWeekCount\":12,\"joinedAllTimeCount\":345}]}";
        wf_agent_starter_pack_view_list out = {0};
        wf_status s = wf_agent_parse_onboarding_starter_packs(json, strlen(json),
                                                              &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.count == 1);
        if (out.count == 1) {
            wf_agent_starter_pack_view *v = &out.items[0];
            WF_CHECK(strcmp(v->uri,
                            "at://did:plc:sp/app.bsky.graph.starterpack/aaa") == 0);
            WF_CHECK(strcmp(v->cid, "bafyxxx") == 0);
            WF_CHECK(v->creator.did != NULL);
            WF_CHECK(strcmp(v->creator.did, "did:plc:sp") == 0);
            WF_CHECK(v->creator.handle != NULL);
            WF_CHECK(strcmp(v->creator.handle, "sp.bsky.social") == 0);
            WF_CHECK(v->record != NULL);
            WF_CHECK(cJSON_IsObject(v->record));
            WF_CHECK(v->has_joined_week_count == 1);
            WF_CHECK(v->joined_week_count == 12);
            WF_CHECK(v->joined_all_time_count == 345);
        }
        wf_agent_starter_pack_view_list_free(&out);
        WF_CHECK(out.items == NULL && out.count == 0);
    }

    /* ---------------- getOnboardingSuggestedStarterPacksSkeleton ---------------- */
    {
        const char *json =
            "{\"starterPacks\":["
            "\"at://did:plc:sp/app.bsky.graph.starterpack/aaa\","
            "\"at://did:plc:sp/app.bsky.graph.starterpack/bbb\"]}";
        wf_agent_starter_pack_skeleton_list out = {0};
        wf_status s = wf_agent_parse_onboarding_starter_packs_skeleton(
            json, strlen(json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.uri_count == 2);
        if (out.uri_count == 2) {
            WF_CHECK(strcmp(out.uris[0],
                            "at://did:plc:sp/app.bsky.graph.starterpack/aaa") == 0);
            WF_CHECK(strcmp(out.uris[1],
                            "at://did:plc:sp/app.bsky.graph.starterpack/bbb") == 0);
        }
        wf_agent_starter_pack_skeleton_list_free(&out);
    }

    /* ---------------- searchStarterPacksSkeleton ---------------- */
    {
        const char *json =
            "{\"cursor\":\"sp-cursor\",\"hitsTotal\":7,\"starterPacks\":["
            "{\"uri\":\"at://did:plc:sp/app.bsky.graph.starterpack/aaa\"}]}";
        wf_agent_search_starter_packs_list out = {0};
        wf_status s = wf_agent_parse_search_starter_packs(json, strlen(json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.count == 1);
        WF_CHECK(out.cursor != NULL);
        WF_CHECK(strcmp(out.cursor, "sp-cursor") == 0);
        WF_CHECK(out.has_hits_total == 1);
        WF_CHECK(out.hits_total == 7);
        if (out.count == 1) {
            WF_CHECK(strcmp(out.items[0].uri,
                            "at://did:plc:sp/app.bsky.graph.starterpack/aaa") == 0);
        }
        wf_agent_search_starter_packs_free(&out);
    }

    /* ---------------- error / NULL handling ---------------- */
    {
        wf_agent_trending_topics tt = {0};
        WF_CHECK(wf_agent_parse_trending_topics(NULL, 0, &tt) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_parse_trending_topics("{", 1, NULL) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_parse_trending_topics("not json", 8, &tt) ==
                 WF_ERR_PARSE);
        WF_CHECK(wf_agent_parse_trending_topics("{\"foo\":1}", 9, &tt) ==
                 WF_ERR_PARSE);

        wf_agent_tagged_suggestions ts = {0};
        WF_CHECK(wf_agent_parse_tagged_suggestions("{}", 2, &ts) == WF_ERR_PARSE);

        wf_agent_unspecced_config cfg = {0};
        WF_CHECK(wf_agent_parse_config("{\"checkEmailConfirmed\":\"x\"}",
                                       strlen("{\"checkEmailConfirmed\":\"x\"}"),
                                       &cfg) == WF_OK);

        /* agent wrapper NULL validation */
        WF_CHECK(wf_agent_get_trending_topics_typed(NULL, NULL, 0, &tt) ==
                 WF_ERR_INVALID_ARG);
        wf_agent_tagged_suggestions ts2 = {0};
        WF_CHECK(wf_agent_get_tagged_suggestions_typed(NULL, &ts2) ==
                 WF_ERR_INVALID_ARG);
        wf_agent_unspecced_config cfg2 = {0};
        WF_CHECK(wf_agent_get_config_typed(NULL, &cfg2) == WF_ERR_INVALID_ARG);
        wf_agent_search_starter_packs_list sp = {0};
        WF_CHECK(wf_agent_search_starter_packs_typed(NULL, NULL, NULL, 0, NULL,
                                                     &sp) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_search_starter_packs_typed((wf_agent *)1, NULL, NULL, 0,
                                                     NULL, &sp) == WF_ERR_INVALID_ARG);
    }

    WF_TEST_SUMMARY();
}
