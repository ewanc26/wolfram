/*
 * test_agent_graph_feed.c — unit tests for the graph/feed wrapper parity
 * additions (muteActor, unmuteActor, muteActorList, unmuteActorList,
 * getSuggestedFollows, getFeedSkeleton).
 *
 * Tests input validation only (no network calls). Offline.
 */

#include "wolfram/agent.h"
#include "wolfram/atproto_lex.h"
#include "test.h"

#include <cJSON.h>

#include <string.h>
#include <stdlib.h>

int main(void) {
    /* ── muteActor / unmuteActor ──────────────────────────────────── */
    {
        WF_CHECK(wf_agent_mute_actor(NULL, "did:plc:abc")
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_unmute_actor(NULL, "did:plc:abc")
                 == WF_ERR_INVALID_ARG);

        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        /* empty actor (no network: rejected before auth) */
        WF_CHECK(wf_agent_mute_actor(agent, "") == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_unmute_actor(agent, "") == WF_ERR_INVALID_ARG);

        /* invalid at-identifier */
        WF_CHECK(wf_agent_mute_actor(agent, "not a did") == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_unmute_actor(agent, "!!!") == WF_ERR_INVALID_ARG);

        /* not logged in -> rejected before any network I/O */
        WF_CHECK(wf_agent_mute_actor(agent, "did:plc:abc")
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_unmute_actor(agent, "did:plc:abc")
                 == WF_ERR_INVALID_ARG);

        wf_agent_free(agent);
    }

    /* ── unmuteActorList (muteActorList is in moderation_actions.h) ── */
    {
        WF_CHECK(wf_agent_unmute_actor_list(NULL, "at://did:plc:abc/app.bsky.graph.list/abc")
                 == WF_ERR_INVALID_ARG);

        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        /* empty / invalid list URI */
        WF_CHECK(wf_agent_unmute_actor_list(agent, "") == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_unmute_actor_list(agent, "did:plc:abc")
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_unmute_actor_list(agent, "not an aturi")
                 == WF_ERR_INVALID_ARG);

        /* not logged in -> rejected before any network I/O */
        WF_CHECK(wf_agent_unmute_actor_list(agent,
                 "at://did:plc:abc/app.bsky.graph.list/abc")
                 == WF_ERR_INVALID_ARG);

        wf_agent_free(agent);
    }

    /* ── getSuggestedFollows ──────────────────────────────────────── */
    {
        WF_CHECK(wf_agent_get_suggested_follows(NULL, "did:plc:abc", NULL)
                 == WF_ERR_INVALID_ARG);

        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        wf_response res = {0};
        WF_CHECK(wf_agent_get_suggested_follows(agent, NULL, &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_suggested_follows(agent, "", &res)
                 == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_suggested_follows(agent, "!!!", &res)
                 == WF_ERR_INVALID_ARG);

        wf_agent_free(agent);
    }

    /* ── getFeedSkeleton ──────────────────────────────────────────── */
    {
        WF_CHECK(wf_agent_get_feed_skeleton(NULL,
                 "at://did:plc:abc/app.bsky.feed.generator/abc", 0, NULL, NULL)
                 == WF_ERR_INVALID_ARG);

        wf_agent *agent = wf_agent_new("https://example.com");
        WF_CHECK(agent != NULL);

        wf_response res = {0};
        WF_CHECK(wf_agent_get_feed_skeleton(agent, NULL, 0, NULL, &res)
                 == WF_ERR_INVALID_ARG);
        /* invalid feed URIs -> WF_ERR_PARSE (matches other at-uri wrappers) */
        WF_CHECK(wf_agent_get_feed_skeleton(agent, "", 0, NULL, &res)
                 == WF_ERR_PARSE);
        WF_CHECK(wf_agent_get_feed_skeleton(agent, "did:plc:abc", 0, NULL, &res)
                 == WF_ERR_PARSE);

        wf_agent_free(agent);
    }

    /* ── request JSON serialization produces valid JSON ───────────── */
    {
        wf_lex_app_bsky_graph_mute_actor_main_input m = { .actor = "did:plc:abc" };
        char *m_json = NULL;
        WF_CHECK(wf_lex_app_bsky_graph_mute_actor_main_input_encode_json(&m, &m_json)
                 == WF_OK);
        WF_CHECK(m_json != NULL);
        cJSON *m_root = cJSON_Parse(m_json);
        WF_CHECK(m_root != NULL);
        WF_CHECK(cJSON_GetObjectItemCaseSensitive(m_root, "actor") != NULL);
        cJSON_Delete(m_root);
        wf_lex_app_bsky_graph_mute_actor_main_json_free(m_json);

        wf_lex_app_bsky_graph_mute_actor_list_main_input ml =
            { .list = "at://did:plc:abc/app.bsky.graph.list/abc" };
        char *ml_json = NULL;
        WF_CHECK(wf_lex_app_bsky_graph_mute_actor_list_main_input_encode_json(&ml, &ml_json)
                 == WF_OK);
        WF_CHECK(ml_json != NULL);
        cJSON *ml_root = cJSON_Parse(ml_json);
        WF_CHECK(ml_root != NULL);
        WF_CHECK(cJSON_GetObjectItemCaseSensitive(ml_root, "list") != NULL);
        cJSON_Delete(ml_root);
        wf_lex_app_bsky_graph_mute_actor_list_main_json_free(ml_json);
    }

    WF_TEST_SUMMARY();
}
