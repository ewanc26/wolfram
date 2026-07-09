/*
 * unspecced_trends_typed.h — owned typed parsers + agent wrappers for the
 * `app.bsky.unspecced` TRENDS / SUGGESTED USERS / THREAD V2 endpoints.
 *
 * This module is distinct from unspecced_typed.h: it covers the trends view,
 * suggested-user feeds, suggested generators, and the flat thread-v2 item
 * arrays, none of which unspecced_typed.h parses.
 *
 * Conventions mirror actor_typed.h / feedgen_typed.h / labeler_typed.c:
 * wf_status error codes, static strdup/set_string/reset helpers, ownership via
 * cJSON_DetachItemFromObject, and a matching `_free` for every owned list.
 *
 * Endpoint coverage (generated lex wrappers required to exist in atproto_lex.h):
 *   - app.bsky.unspecced.getTrends           -> wf_unspecced_trend_list
 *   - app.bsky.unspecced.getSuggestedUsers   -> wf_agent_actor_list
 *   - app.bsky.unspecced.getSuggestedFeeds   -> wf_agent_generator_view_list
 *   - app.bsky.unspecced.getPostThreadV2     -> wf_unspecced_thread_v2
 *   - app.bsky.unspecced.getPostThreadOtherV2-> wf_unspecced_thread_v2
 */

#ifndef WOLFRAM_UNSPECCED_TRENDS_TYPED_H
#define WOLFRAM_UNSPECCED_TRENDS_TYPED_H

#include "wolfram/agent.h"
#include "wolfram/actor_typed.h"
#include "wolfram/feedgen_typed.h"
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A single trend view (app.bsky.unspecced.defs#trendView). Core fields are
 * owned copies; `actors` reuses the shared wf_agent_actor_list (profileViewBasic
 * array) and `extra` captures any trailing fields. */
typedef struct wf_unspecced_trend_view {
    char *topic;
    char *display_name;
    char *link;
    char *started_at;
    int has_post_count;
    int64_t post_count;
    char *status;
    char *category;
    wf_agent_actor_list actors;   /* owned profileViewBasic list; free via actors */
    cJSON *extra;                 /* owned detached subtree; NULL absent */
} wf_unspecced_trend_view;

/* A list of trend views (app.bsky.unspecced.getTrends). */
typedef struct wf_unspecced_trend_list {
    wf_unspecced_trend_view *trends;
    size_t trend_count;
} wf_unspecced_trend_list;

/* A flat thread item (app.bsky.unspecced.getPostThreadV2 / OtherV2). `value` is
 * the arbitrary record/parent/reply object, kept as an owned detached cJSON
 * node so the parser stays bounded regardless of shape. */
typedef struct wf_unspecced_thread_item_v2 {
    char *uri;
    int has_depth;
    int64_t depth;
    cJSON *value;                 /* owned detached subtree; NULL absent */
} wf_unspecced_thread_item_v2;

/* A flat thread (app.bsky.unspecced.getPostThreadV2 / OtherV2). `threadgate` is
 * the optional threadgateView kept as an owned detached subtree; the boolean
 * records whether additional replies are available via getPostThreadOtherV2. */
typedef struct wf_unspecced_thread_v2 {
    wf_unspecced_thread_item_v2 *items;
    size_t item_count;
    cJSON *threadgate;            /* owned detached subtree; NULL absent */
    int has_other_replies;
} wf_unspecced_thread_v2;

/* Parse a getTrends JSON body ("trends" array of trendView) into owned structs.
 * Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or a
 * missing/invalid array, WF_ERR_ALLOC on allocation failure, WF_OK on success.
 * On any error `out` is left fully reset (no partial leaks). */
wf_status wf_unspecced_parse_trends(const char *json, size_t json_len,
                                    wf_unspecced_trend_list *out);

/* Free a parsed trend list and every owned field it holds (also safe on a
 * reset/zeroed list). */
void wf_unspecced_trend_list_free(wf_unspecced_trend_list *list);

/* Parse a getSuggestedUsers JSON body ("actors" array of profileView) into an
 * owned actor list. Reuses wf_agent_parse_actors ownership semantics. */
wf_status wf_unspecced_parse_suggested_users(const char *json, size_t json_len,
                                             wf_agent_actor_list *out);

/* Parse a getSuggestedFeeds JSON body ("feeds" array of generatorView) into an
 * owned generator-view list. Reuses wf_agent_parse_generators. */
wf_status wf_unspecced_parse_suggested_feeds(const char *json, size_t json_len,
                                             wf_agent_generator_view_list *out);

/* Parse a getPostThreadV2 / getPostThreadOtherV2 JSON body ("thread" array of
 * threadItem) into owned structs. `threadgate` is captured from the top-level
 * field when present. Same ownership/error rules as wf_unspecced_parse_trends. */
wf_status wf_unspecced_parse_thread_v2(const char *json, size_t json_len,
                                       wf_unspecced_thread_v2 *out);

/* Free a parsed thread-v2 list and every owned field it holds (also safe on a
 * reset/zeroed list). */
void wf_unspecced_thread_v2_free(wf_unspecced_thread_v2 *list);

/* Typed high-level wrappers — issue the corresponding lex call (syncing auth
 * first) and parse the JSON body into `out`. On success `out` is owned by the
 * caller (free with the matching `_free`); on error it is left reset. Each
 * returns WF_ERR_INVALID_ARG on a NULL agent or missing required arguments. */
wf_status wf_agent_get_trends_typed(wf_agent *agent, int limit,
                                    wf_unspecced_trend_list *out);

wf_status wf_agent_get_suggested_users_typed(wf_agent *agent,
                                             const char *category_or_null,
                                             int limit, wf_agent_actor_list *out);

wf_status wf_agent_get_suggested_feeds_typed(wf_agent *agent, int limit,
                                             wf_agent_generator_view_list *out);

wf_status wf_agent_get_post_thread_v2_typed(wf_agent *agent, const char *anchor,
                                            int above, int below,
                                            int branching_factor,
                                            const char *sort_or_null,
                                            wf_unspecced_thread_v2 *out);

wf_status wf_agent_get_post_thread_other_v2_typed(wf_agent *agent,
                                                  const char *anchor,
                                                  wf_unspecced_thread_v2 *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_UNSPECCED_TRENDS_TYPED_H */
