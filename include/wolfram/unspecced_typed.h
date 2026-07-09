/*
 * unspecced_typed.h — owned typed parsers for app.bsky.unspecced endpoints.
 *
 * Parses the raw JSON bodies returned by a selection of the
 * `app.bsky.unspecced` query endpoints into owned C structs. Arbitrary
 * record shapes (e.g. the `record` field of a starter pack view) are kept as
 * owned `cJSON` subtrees (detached from the parsed document) so the parser
 * stays bounded regardless of arbitrary content. Free each list with its
 * matching `_free`.
 *
 * This mirrors the conventions in feed_typed.h / feedgen_typed.h:
 *   - `wf_status` error codes (WF_ERR_INVALID_ARG, WF_ERR_PARSE, WF_ERR_ALLOC,
 *     WF_OK)
 *   - static strdup/set_string/reset helpers local to the translation unit
 *   - ownership via `cJSON_DetachItemFromObject`
 *
 * Covered endpoints:
 *   - getTrendingTopics
 *   - getTaggedSuggestions
 *   - getSuggestionsSkeleton
 *   - getConfig
 *   - getAgeAssuranceState
 *   - getOnboardingSuggestedStarterPacks
 *   - getOnboardingSuggestedStarterPacksSkeleton
 *   - searchStarterPacksSkeleton
 */

#ifndef WOLFRAM_UNSPECCED_TYPED_H
#define WOLFRAM_UNSPECCED_TYPED_H

#include "wolfram/agent.h"
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------- Trending topics ---------------------------- */

/* A single trending topic (app.bsky.unspecced.defs#trendingTopic). */
typedef struct wf_agent_trending_topic {
    char *topic;          /* required topic string */
    char *display_name;   /* optional display name; NULL when absent */
    char *description;    /* optional description; NULL when absent */
    char *link;           /* required link */
} wf_agent_trending_topic;

/* A parsed getTrendingTopics response. `topics` and `suggested` are two
 * parallel arrays of trending topics. */
typedef struct wf_agent_trending_topics {
    wf_agent_trending_topic *topics;
    size_t topic_count;
    wf_agent_trending_topic *suggested;
    size_t suggested_count;
} wf_agent_trending_topics;

/* Parse a raw getTrendingTopics JSON body into owned structs.
 * Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON
 * or a missing `topics` array, WF_ERR_ALLOC on allocation failure, WF_OK on
 * success. On any error `out` is left fully reset (no partial leaks). */
wf_status wf_agent_parse_trending_topics(const char *json, size_t json_len,
                                         wf_agent_trending_topics *out);

/* Free a parsed trending-topics response and every owned field it holds. */
void wf_agent_trending_topics_free(wf_agent_trending_topics *list);

/* ---------------------------- Tagged suggestions -------------------------- */

/* A single tagged suggestion (app.bsky.unspecced#suggestion). */
typedef struct wf_agent_tagged_suggestion {
    char *tag;          /* category tag */
    char *subject_type; /* "actor" or "feed" */
    char *subject;      /* uri (at-uri or feed generator uri) */
} wf_agent_tagged_suggestion;

/* A parsed getTaggedSuggestions response. */
typedef struct wf_agent_tagged_suggestions {
    wf_agent_tagged_suggestion *items;
    size_t count;
} wf_agent_tagged_suggestions;

/* Parse a raw getTaggedSuggestions JSON body into owned structs.
 * Same ownership/error rules as wf_agent_parse_trending_topics, operating on
 * the `suggestions` array. */
wf_status wf_agent_parse_tagged_suggestions(const char *json, size_t json_len,
                                            wf_agent_tagged_suggestions *out);

/* Free a parsed tagged-suggestions response and every owned field it holds. */
void wf_agent_tagged_suggestions_free(wf_agent_tagged_suggestions *list);

/* ---------------------------- Suggestions skeleton ------------------------ */

/* A single suggested actor (app.bsky.unspecced.defs#skeletonSearchActor). */
typedef struct wf_agent_skeleton_actor {
    char *did;          /* did of the suggested actor */
} wf_agent_skeleton_actor;

/* A parsed getSuggestionsSkeleton response. */
typedef struct wf_agent_suggestions_skeleton {
    wf_agent_skeleton_actor *actors;
    size_t actor_count;
    char *cursor;            /* optional pagination cursor; NULL when absent */
    char *relative_to_did;   /* optional viewer-relative DID; NULL when absent */
    char *rec_id_str;        /* optional recommendation snowflake; NULL when absent */
} wf_agent_suggestions_skeleton;

/* Parse a raw getSuggestionsSkeleton JSON body into owned structs.
 * Same ownership/error rules, operating on the `actors` array. */
wf_status wf_agent_parse_suggestions_skeleton(const char *json, size_t json_len,
                                              wf_agent_suggestions_skeleton *out);

/* Free a parsed suggestions-skeleton response and every owned field it holds. */
void wf_agent_suggestions_skeleton_free(wf_agent_suggestions_skeleton *list);

/* --------------------------------- Config --------------------------------- */

/* A live-now configuration entry (app.bsky.unspecced#liveNowConfig). */
typedef struct wf_agent_live_now_config {
    char *did;          /* did of the account that is live */
    char **domains;     /* owned array of domain strings */
    size_t domain_count;
} wf_agent_live_now_config;

/* A parsed getConfig response. */
typedef struct wf_agent_unspecced_config {
    int has_check_email_confirmed;
    int check_email_confirmed;    /* whether the viewer's email is confirmed */
    wf_agent_live_now_config *live_now;
    size_t live_now_count;
} wf_agent_unspecced_config;

/* Parse a raw getConfig JSON body into owned structs.
 * Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON,
 * WF_ERR_ALLOC on allocation failure, WF_OK on success. */
wf_status wf_agent_parse_config(const char *json, size_t json_len,
                                wf_agent_unspecced_config *out);

/* Free a parsed config response and every owned field it holds. */
void wf_agent_unspecced_config_free(wf_agent_unspecced_config *cfg);

/* ---------------------------- Age assurance state ------------------------- */

/* A parsed getAgeAssuranceState response (app.bsky.unspecced.defs#ageAssuranceState). */
typedef struct wf_agent_age_assurance_state {
    char *status;               /* status of the age assurance process */
    char *last_initiated_at;    /* optional RFC3339 timestamp; NULL when absent */
    int has_last_initiated_at;
} wf_agent_age_assurance_state;

/* Parse a raw getAgeAssuranceState JSON body into owned structs.
 * Same ownership/error rules as wf_agent_parse_config. */
wf_status wf_agent_parse_age_assurance_state(const char *json, size_t json_len,
                                             wf_agent_age_assurance_state *out);

/* Free a parsed age-assurance-state response and every owned field it holds. */
void wf_agent_age_assurance_state_free(wf_agent_age_assurance_state *st);

/* --------------------- Onboarding starter packs (full) -------------------- */

/* A starter pack view (app.bsky.graph.defs#starterPackView), bounded to the
 * fields we need. `record` is kept as an owned cJSON subtree. */
typedef struct wf_agent_starter_pack_view {
    char *uri;
    char *cid;
    cJSON *record;              /* owned record subtree (unknown shape) */
    wf_agent_profile_view creator; /* owned creator profile view */
    char *indexed_at;
    int joined_week_count;
    int has_joined_week_count;
    int joined_all_time_count;
    int has_joined_all_time_count;
} wf_agent_starter_pack_view;

/* A parsed getOnboardingSuggestedStarterPacks response. */
typedef struct wf_agent_starter_pack_view_list {
    wf_agent_starter_pack_view *items;
    size_t count;
} wf_agent_starter_pack_view_list;

/* Parse a raw getOnboardingSuggestedStarterPacks JSON body into owned structs.
 * Same ownership/error rules, operating on the `starterPacks` array. */
wf_status wf_agent_parse_onboarding_starter_packs(const char *json, size_t json_len,
                                                  wf_agent_starter_pack_view_list *out);

/* Free a parsed starter-pack-view list and every owned field it holds. */
void wf_agent_starter_pack_view_list_free(wf_agent_starter_pack_view_list *list);

/* ------------------- Onboarding starter packs (skeleton) ------------------ */

/* A parsed getOnboardingSuggestedStarterPacksSkeleton response: an array of
 * starter-pack at-uris. */
typedef struct wf_agent_starter_pack_skeleton_list {
    char **uris;
    size_t uri_count;
} wf_agent_starter_pack_skeleton_list;

/* Parse a raw getOnboardingSuggestedStarterPacksSkeleton JSON body into owned
 * at-uri strings. Same ownership/error rules, operating on the `starterPacks`
 * array. */
wf_status wf_agent_parse_onboarding_starter_packs_skeleton(const char *json,
                                                           size_t json_len,
                                                           wf_agent_starter_pack_skeleton_list *out);

/* Free a parsed starter-pack-skeleton list and every owned field it holds. */
void wf_agent_starter_pack_skeleton_list_free(wf_agent_starter_pack_skeleton_list *list);

/* ----------------------- Search starter packs (skeleton) ------------------ */

/* A single search hit (app.bsky.unspecced.defs#skeletonSearchStarterPack). */
typedef struct wf_agent_search_starter_pack {
    char *uri;          /* at-uri of the starter pack */
} wf_agent_search_starter_pack;

/* A parsed searchStarterPacksSkeleton response. */
typedef struct wf_agent_search_starter_packs_list {
    wf_agent_search_starter_pack *items;
    size_t count;
    char *cursor;            /* optional pagination cursor; NULL when absent */
    int has_hits_total;
    int hits_total;          /* optional count of search hits */
} wf_agent_search_starter_packs_list;

/* Parse a raw searchStarterPacksSkeleton JSON body into owned structs.
 * Same ownership/error rules, operating on the `starterPacks` array. */
wf_status wf_agent_parse_search_starter_packs(const char *json, size_t json_len,
                                              wf_agent_search_starter_packs_list *out);

/* Free a parsed search-starter-packs response and every owned field it holds. */
void wf_agent_search_starter_packs_free(wf_agent_search_starter_packs_list *list);

/* --------------------------- Typed agent wrappers ------------------------- */

/* Typed high-level wrappers — issue the corresponding agent call and parse the
 * JSON body into `out`. On success `out` is owned by the caller (free with the
 * matching `_free`); on error it is left reset.
 *
 * `viewer` may be NULL to omit the viewer DID parameter. `limit` <= 0 omits the
 * limit parameter. `cursor` may be NULL. */

wf_status wf_agent_get_trending_topics_typed(wf_agent *agent, const char *viewer,
                                             int limit,
                                             wf_agent_trending_topics *out);

wf_status wf_agent_get_tagged_suggestions_typed(wf_agent *agent,
                                                wf_agent_tagged_suggestions *out);

wf_status wf_agent_get_suggestions_skeleton_typed(wf_agent *agent,
                                                  const char *viewer, int limit,
                                                  const char *cursor,
                                                  const char *relative_to_did,
                                                  wf_agent_suggestions_skeleton *out);

wf_status wf_agent_get_config_typed(wf_agent *agent,
                                    wf_agent_unspecced_config *out);

wf_status wf_agent_get_age_assurance_state_typed(wf_agent *agent,
                                                 wf_agent_age_assurance_state *out);

wf_status wf_agent_get_onboarding_suggested_starter_packs_typed(
    wf_agent *agent, int limit, wf_agent_starter_pack_view_list *out);

/* app.bsky.unspecced.getSuggestedStarterPacks — hydrated starter-pack views
 * ("starterPacks" array of graph.defs#starterPackView), identical output shape
 * to getOnboardingSuggestedStarterPacks, so it reuses
 * wf_agent_parse_onboarding_starter_packs. On success `out` is owned by the
 * caller (free with wf_agent_starter_pack_view_list_free); on error it is left
 * reset. Returns WF_ERR_INVALID_ARG on a NULL agent/out. */
wf_status wf_agent_get_suggested_starter_packs_typed(
    wf_agent *agent, int limit, wf_agent_starter_pack_view_list *out);

wf_status wf_agent_get_onboarding_suggested_starter_packs_skeleton_typed(
    wf_agent *agent, const char *viewer, int limit,
    wf_agent_starter_pack_skeleton_list *out);

wf_status wf_agent_search_starter_packs_typed(
    wf_agent *agent, const char *q, const char *viewer, int limit,
    const char *cursor, wf_agent_search_starter_packs_list *out);

/* ------------------------------------------------------------------ */
/* Unspecced — XRPC-level convenience wrappers                        */
/* ------------------------------------------------------------------ */

#define WF_UNSPECCED_GETONBOARDINGSUGGESTEDUSERSSKELETON_NSID        "app.bsky.unspecced.getOnboardingSuggestedUsersSkeleton"
#define WF_UNSPECCED_GETPOPULARFEEDGENERATORS_NSID                   "app.bsky.unspecced.getPopularFeedGenerators"
#define WF_UNSPECCED_GETPOSTTHREADOTHERV2_NSID                       "app.bsky.unspecced.getPostThreadOtherV2"
#define WF_UNSPECCED_GETPOSTTHREADV2_NSID                            "app.bsky.unspecced.getPostThreadV2"
#define WF_UNSPECCED_GETSUGGESTEDFEEDS_NSID                          "app.bsky.unspecced.getSuggestedFeeds"
#define WF_UNSPECCED_GETSUGGESTEDFEEDSSKELETON_NSID                  "app.bsky.unspecced.getSuggestedFeedsSkeleton"
#define WF_UNSPECCED_GETSUGGESTEDONBOARDINGUSERS_NSID                "app.bsky.unspecced.getSuggestedOnboardingUsers"
#define WF_UNSPECCED_GETSUGGESTEDSTARTERPACKS_NSID                   "app.bsky.unspecced.getSuggestedStarterPacks"
#define WF_UNSPECCED_GETSUGGESTEDSTARTERPACKSSKELETON_NSID           "app.bsky.unspecced.getSuggestedStarterPacksSkeleton"
#define WF_UNSPECCED_GETSUGGESTEDUSERS_NSID                          "app.bsky.unspecced.getSuggestedUsers"
#define WF_UNSPECCED_GETSUGGESTEDUSERSFORDISCOVER_NSID               "app.bsky.unspecced.getSuggestedUsersForDiscover"
#define WF_UNSPECCED_GETSUGGESTEDUSERSFORDISCOVERSKELETON_NSID       "app.bsky.unspecced.getSuggestedUsersForDiscoverSkeleton"
#define WF_UNSPECCED_GETSUGGESTEDUSERSFOREXPLORE_NSID                "app.bsky.unspecced.getSuggestedUsersForExplore"
#define WF_UNSPECCED_GETSUGGESTEDUSERSFOREXPLORESKELETON_NSID        "app.bsky.unspecced.getSuggestedUsersForExploreSkeleton"
#define WF_UNSPECCED_GETSUGGESTEDUSERSFORSEEMORE_NSID                "app.bsky.unspecced.getSuggestedUsersForSeeMore"
#define WF_UNSPECCED_GETSUGGESTEDUSERSFORSEEMORESKELETON_NSID        "app.bsky.unspecced.getSuggestedUsersForSeeMoreSkeleton"
#define WF_UNSPECCED_GETSUGGESTEDUSERSSKELETON_NSID                  "app.bsky.unspecced.getSuggestedUsersSkeleton"
#define WF_UNSPECCED_GETTRENDS_NSID                                  "app.bsky.unspecced.getTrends"
#define WF_UNSPECCED_GETTRENDSSKELETON_NSID                          "app.bsky.unspecced.getTrendsSkeleton"
#define WF_UNSPECCED_INITAGEASSURANCE_NSID                           "app.bsky.unspecced.initAgeAssurance"
#define WF_UNSPECCED_SEARCHACTORSSKELETON_NSID                       "app.bsky.unspecced.searchActorsSkeleton"
#define WF_UNSPECCED_SEARCHPOSTSSKELETON_NSID                        "app.bsky.unspecced.searchPostsSkeleton"

wf_status wf_unspecced_get_onboarding_suggested_users_skeleton(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_onboarding_suggested_users_skeleton_main_params *params, wf_response *out);
wf_status wf_unspecced_get_onboarding_suggested_users_skeleton_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_onboarding_suggested_users_skeleton_main_output **out);

wf_status wf_unspecced_get_popular_feed_generators(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_popular_feed_generators_main_params *params, wf_response *out);
wf_status wf_unspecced_get_popular_feed_generators_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_popular_feed_generators_main_output **out);

wf_status wf_unspecced_get_post_thread_other_v2(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_post_thread_other_v2_main_params *params, wf_response *out);
wf_status wf_unspecced_get_post_thread_other_v2_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_post_thread_other_v2_main_output **out);

wf_status wf_unspecced_get_post_thread_v2(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_post_thread_v2_main_params *params, wf_response *out);
wf_status wf_unspecced_get_post_thread_v2_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_post_thread_v2_main_output **out);

wf_status wf_unspecced_get_suggested_feeds(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_suggested_feeds_main_params *params, wf_response *out);
wf_status wf_unspecced_get_suggested_feeds_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_suggested_feeds_main_output **out);

wf_status wf_unspecced_get_suggested_feeds_skeleton(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_suggested_feeds_skeleton_main_params *params, wf_response *out);
wf_status wf_unspecced_get_suggested_feeds_skeleton_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_suggested_feeds_skeleton_main_output **out);

wf_status wf_unspecced_get_suggested_onboarding_users(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_suggested_onboarding_users_main_params *params, wf_response *out);
wf_status wf_unspecced_get_suggested_onboarding_users_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_suggested_onboarding_users_main_output **out);

wf_status wf_unspecced_get_suggested_starter_packs(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_suggested_starter_packs_main_params *params, wf_response *out);
wf_status wf_unspecced_get_suggested_starter_packs_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_suggested_starter_packs_main_output **out);

wf_status wf_unspecced_get_suggested_starter_packs_skeleton(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_suggested_starter_packs_skeleton_main_params *params, wf_response *out);
wf_status wf_unspecced_get_suggested_starter_packs_skeleton_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_suggested_starter_packs_skeleton_main_output **out);

wf_status wf_unspecced_get_suggested_users(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_suggested_users_main_params *params, wf_response *out);
wf_status wf_unspecced_get_suggested_users_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_suggested_users_main_output **out);

wf_status wf_unspecced_get_suggested_users_for_discover(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_suggested_users_for_discover_main_params *params, wf_response *out);
wf_status wf_unspecced_get_suggested_users_for_discover_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_suggested_users_for_discover_main_output **out);

wf_status wf_unspecced_get_suggested_users_for_discover_skeleton(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_suggested_users_for_discover_skeleton_main_params *params, wf_response *out);
wf_status wf_unspecced_get_suggested_users_for_discover_skeleton_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_suggested_users_for_discover_skeleton_main_output **out);

wf_status wf_unspecced_get_suggested_users_for_explore(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_suggested_users_for_explore_main_params *params, wf_response *out);
wf_status wf_unspecced_get_suggested_users_for_explore_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_suggested_users_for_explore_main_output **out);

wf_status wf_unspecced_get_suggested_users_for_explore_skeleton(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_suggested_users_for_explore_skeleton_main_params *params, wf_response *out);
wf_status wf_unspecced_get_suggested_users_for_explore_skeleton_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_suggested_users_for_explore_skeleton_main_output **out);

wf_status wf_unspecced_get_suggested_users_for_see_more(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_suggested_users_for_see_more_main_params *params, wf_response *out);
wf_status wf_unspecced_get_suggested_users_for_see_more_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_suggested_users_for_see_more_main_output **out);

wf_status wf_unspecced_get_suggested_users_for_see_more_skeleton(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_suggested_users_for_see_more_skeleton_main_params *params, wf_response *out);
wf_status wf_unspecced_get_suggested_users_for_see_more_skeleton_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_suggested_users_for_see_more_skeleton_main_output **out);

wf_status wf_unspecced_get_suggested_users_skeleton(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_suggested_users_skeleton_main_params *params, wf_response *out);
wf_status wf_unspecced_get_suggested_users_skeleton_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_suggested_users_skeleton_main_output **out);

wf_status wf_unspecced_get_trends(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_trends_main_params *params, wf_response *out);
wf_status wf_unspecced_get_trends_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_trends_main_output **out);

wf_status wf_unspecced_get_trends_skeleton(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_get_trends_skeleton_main_params *params, wf_response *out);
wf_status wf_unspecced_get_trends_skeleton_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_get_trends_skeleton_main_output **out);

wf_status wf_unspecced_init_age_assurance(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_init_age_assurance_main_input *input, wf_response *out);

wf_status wf_unspecced_search_actors_skeleton(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_search_actors_skeleton_main_params *params, wf_response *out);
wf_status wf_unspecced_search_actors_skeleton_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_search_actors_skeleton_main_output **out);

wf_status wf_unspecced_search_posts_skeleton(wf_xrpc_client *client, const wf_lex_app_bsky_unspecced_search_posts_skeleton_main_params *params, wf_response *out);
wf_status wf_unspecced_search_posts_skeleton_parse(const wf_response *resp, wf_lex_app_bsky_unspecced_search_posts_skeleton_main_output **out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_UNSPECCED_TYPED_H */
