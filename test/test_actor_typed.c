/*
 * test_actor_typed.c — offline unit tests for the actor/profile/search/
 * suggestion typed parsers and wrapper argument validation. No network.
 */

#include "wolfram/actor_typed.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* getProfiles -> "profiles" array of profileView */
static const char *k_profiles_json =
    "{\"profiles\":["
    "{\"did\":\"did:plc:aaa\",\"handle\":\"alice.bsky.social\","
    "\"displayName\":\"Alice\"},"
    "{\"did\":\"did:plc:bbb\",\"handle\":\"bob.bsky.social\"}],"
    "\"cursor\":\"next\"}";

/* searchActors -> "actors" array of profileView */
static const char *k_search_json =
    "{\"actors\":["
    "{\"did\":\"did:plc:aaa\",\"handle\":\"alice.bsky.social\","
    "\"displayName\":\"Alice\"},"
    "{\"did\":\"did:plc:bbb\",\"handle\":\"bob.bsky.social\","
    "\"displayName\":\"Bob\"},"
    "{\"did\":\"did:plc:ccc\",\"handle\":\"carol.bsky.social\"}],"
    "\"cursor\":\"c2\"}";

/* getSuggestions -> "actors" array of profileView plus recommendation IDs */
static const char *k_suggestions_json =
    "{\"actors\":["
    "{\"did\":\"did:plc:aaa\",\"handle\":\"alice.bsky.social\"}],"
    "\"cursor\":\"c3\","
    "\"recId\":123,"
    "\"recIdStr\":\"123\"}";

/* Deprecated string recId variant (still seen in some upstream suggested-user
 * responses). */
static const char *k_suggestions_legacy_recid_json =
    "{\"actors\":["
    "{\"did\":\"did:plc:bbb\",\"handle\":\"bob.bsky.social\"}],"
    "\"recId\":\"snowflake-legacy\"}";

/* searchActorsTypeahead -> "actors" array of profileViewBasic (with viewer) */
static const char *k_typeahead_json =
    "{\"actors\":["
    "{\"did\":\"did:plc:x\",\"handle\":\"x.bsky.social\","
    "\"displayName\":\"X\",\"viewer\":{\"muted\":true}}]}";

/* getLikes -> "likes" array of {actor, createdAt, indexedAt} */
static const char *k_likes_json =
    "{\"likes\":["
    "{\"actor\":{\"did\":\"did:plc:l\",\"handle\":\"liker.bsky.social\"},"
    "\"createdAt\":\"2024-01-01T00:00:00Z\","
    "\"indexedAt\":\"2024-01-01T00:00:01Z\"}],"
    "\"cursor\":\"cl\"}";

/* getRepostedBy -> "repostedBy" array of profileView */
static const char *k_reposted_json =
    "{\"repostedBy\":[{\"did\":\"did:plc:r\",\"handle\":\"reposter.bsky.social\"}],"
    "\"uri\":\"at://did:plc:post\",\"cursor\":\"cr\"}";

/* getSuggestionsSkeleton (app.bsky.unspecced) -> "actors" of {did} */
static const char *k_skeleton_json =
    "{\"actors\":[{\"did\":\"did:plc:s\"}],\"cursor\":\"cs\"}";

int main(void) {
    /* ---- getProfiles ---- */
    {
        wf_agent_profile_view_list out = {0};
        wf_status s = wf_agent_parse_profiles(k_profiles_json,
                                              strlen(k_profiles_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.profile_count == 2);
        WF_CHECK(out.profiles != NULL);
        WF_CHECK(out.profiles[0].did &&
                 strcmp(out.profiles[0].did, "did:plc:aaa") == 0);
        WF_CHECK(out.profiles[0].display_name &&
                 strcmp(out.profiles[0].display_name, "Alice") == 0);
        WF_CHECK(out.profiles[1].handle &&
                 strcmp(out.profiles[1].handle, "bob.bsky.social") == 0);
        WF_CHECK(out.cursor && strcmp(out.cursor, "next") == 0);
        wf_agent_profile_view_list_free(&out);
        /* double-free / freed-list must be safe */
        wf_agent_profile_view_list_free(&out);
    }

    /* ---- searchActors ---- */
    {
        wf_agent_actor_list out = {0};
        wf_status s = wf_agent_parse_actor_search(k_search_json,
                                                  strlen(k_search_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.actor_count == 3);
        WF_CHECK(out.actors[2].did &&
                 strcmp(out.actors[2].did, "did:plc:ccc") == 0);
        WF_CHECK(out.cursor && strcmp(out.cursor, "c2") == 0);
        wf_agent_actor_list_free(&out);
    }

    /* ---- getSuggestions metadata survives parsing ---- */
    {
        wf_agent_actor_list out = {0};
        wf_status s = wf_agent_parse_actor_search(k_suggestions_json,
                                                  strlen(k_suggestions_json),
                                                  &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.actor_count == 1);
        WF_CHECK(out.has_rec_id == 1);
        WF_CHECK(out.rec_id == 123);
        WF_CHECK(out.rec_id_str && strcmp(out.rec_id_str, "123") == 0);
        wf_agent_actor_list_free(&out);
    }

    /* ---- getSuggestions legacy string recId is accepted ---- */
    {
        wf_agent_actor_list out = {0};
        wf_status s = wf_agent_parse_actor_search(k_suggestions_legacy_recid_json,
                                                  strlen(k_suggestions_legacy_recid_json),
                                                  &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.actor_count == 1);
        WF_CHECK(out.has_rec_id == 0);
        WF_CHECK(out.rec_id_str &&
                 strcmp(out.rec_id_str, "snowflake-legacy") == 0);
        wf_agent_actor_list_free(&out);
    }

    /* ---- searchActorsTypeahead (profileViewBasic + detached viewer) ---- */
    {
        wf_agent_profile_view_basic_list out = {0};
        wf_status s = wf_agent_parse_actor_typeahead(k_typeahead_json,
                                                     strlen(k_typeahead_json),
                                                     &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.actor_count == 1);
        WF_CHECK(out.actors[0].did &&
                 strcmp(out.actors[0].did, "did:plc:x") == 0);
        WF_CHECK(out.actors[0].viewer != NULL);
        if (out.actors[0].viewer) {
            cJSON *muted = cJSON_GetObjectItemCaseSensitive(out.actors[0].viewer,
                                                            "muted");
            WF_CHECK(cJSON_IsBool(muted) && cJSON_IsTrue(muted));
        }
        wf_agent_profile_view_basic_list_free(&out);
    }

    /* ---- getLikes ---- */
    {
        wf_agent_actor_like_list out = {0};
        wf_status s = wf_agent_parse_actor_likes(k_likes_json,
                                                 strlen(k_likes_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.like_count == 1);
        WF_CHECK(out.likes[0].actor.did &&
                 strcmp(out.likes[0].actor.did, "did:plc:l") == 0);
        WF_CHECK(out.likes[0].created_at &&
                 strcmp(out.likes[0].created_at, "2024-01-01T00:00:00Z") == 0);
        WF_CHECK(out.likes[0].indexed_at &&
                 strcmp(out.likes[0].indexed_at, "2024-01-01T00:00:01Z") == 0);
        WF_CHECK(out.cursor && strcmp(out.cursor, "cl") == 0);
        wf_agent_actor_like_list_free(&out);
    }

    /* ---- getRepostedBy ---- */
    {
        wf_agent_actor_list out = {0};
        wf_status s = wf_agent_parse_reposted_by(k_reposted_json,
                                                 strlen(k_reposted_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.actor_count == 1);
        WF_CHECK(out.actors[0].handle &&
                 strcmp(out.actors[0].handle, "reposter.bsky.social") == 0);
        wf_agent_actor_list_free(&out);
    }

    /* ---- getSuggestionsSkeleton (re-exposed from unspecced_typed.h) ---- */
    {
        wf_agent_suggestions_skeleton out = {0};
        wf_status s = wf_agent_parse_suggestions_skeleton(k_skeleton_json,
                                                         strlen(k_skeleton_json),
                                                         &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.actor_count == 1);
        WF_CHECK(out.actors[0].did &&
                 strcmp(out.actors[0].did, "did:plc:s") == 0);
        wf_agent_suggestions_skeleton_free(&out);
    }

    /* ---- invalid / NULL input handling ---- */
    {
        wf_agent_profile_view_list plist = {0};
        wf_agent_actor_list alist = {0};
        wf_agent_profile_view_basic_list blist = {0};
        wf_agent_actor_like_list llist = {0};

        WF_CHECK(wf_agent_parse_profiles(NULL, 0, &plist) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_parse_profiles(k_profiles_json, strlen(k_profiles_json),
                                         NULL) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_parse_profiles("not json{", 9, &plist) ==
                 WF_ERR_PARSE);
        /* missing array key -> WF_ERR_PARSE */
        WF_CHECK(wf_agent_parse_profiles("{}", 2, &plist) == WF_ERR_PARSE);

        WF_CHECK(wf_agent_parse_actor_search(NULL, 0, &alist) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_parse_actor_search("nope", 4, &alist) ==
                 WF_ERR_PARSE);

        WF_CHECK(wf_agent_parse_actor_typeahead(NULL, 0, &blist) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_parse_actor_likes(NULL, 0, &llist) ==
                 WF_ERR_INVALID_ARG);

        WF_CHECK(wf_agent_parse_reposted_by(NULL, 0, &alist) ==
                 WF_ERR_INVALID_ARG);

        /* wrapper argument validation (returns before any network use) */
        wf_agent_actor_list out1 = {0};
        wf_agent_profile_view_basic_list out2 = {0};
        wf_agent_actor_like_list out3 = {0};
        const char *actors[1] = {"did:plc:aaa"};

        WF_CHECK(wf_agent_get_profiles_typed(NULL, actors, 1, 10, NULL, &out1) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_profiles_typed((wf_agent *)1, NULL, 0, 10, NULL,
                                             &out1) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_search_actors_typed(NULL, "q", 10, NULL, &out1) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_search_actors_typeahead_typed(NULL, "q", 10, &out2) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_likes_typed(NULL, "at://x", 10, NULL, &out3) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_get_reposted_by_typed(NULL, "at://x", 10, NULL,
                                                &out1) == WF_ERR_INVALID_ARG);

        /* limit must not exceed upstream maximum (100) */
        WF_CHECK(wf_agent_search_actors_typed((wf_agent *)1, "q", 101, NULL,
                                              &out1) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_search_actors_typeahead_typed((wf_agent *)1, "q", 101,
                                                       &out2) ==
                 WF_ERR_INVALID_ARG);

        /* reset/freed lists free safely */
        wf_agent_actor_list_free(&out1);
        wf_agent_profile_view_basic_list_free(&out2);
        wf_agent_actor_like_list_free(&out3);
    }

    WF_TEST_SUMMARY();
    return 0; /* unreachable */
}
