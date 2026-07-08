/*
 * actor_typed.h — owned typed parsers for actor / profile / search /
 * suggestion responses, plus convenience agent wrappers.
 *
 * Centralizes parsing for the many AT Protocol endpoints that return arrays of
 * `profileView` / `profileViewBasic` shapes:
 *   - app.bsky.actor.getProfiles            -> "profiles"  (profileView)
 *   - app.bsky.actor.searchActors           -> "actors"    (profileView)
 *   - app.bsky.actor.searchActorsTypeahead  -> "actors"    (profileViewBasic)
 *   - app.bsky.unspecced.getSuggestionsSkeleton -> "actors" (did list + cursor)
 *   - app.bsky.feed.getLikes                -> "likes"     ({actor, createdAt, indexedAt})
 *   - app.bsky.feed.getRepostedBy           -> "repostedBy" (profileView)
 *
 * The `wf_agent_profile_view` struct (app.bsky.actor.defs#profileView core
 * fields) is reused from agent.h rather than redefined. A new
 * `wf_agent_profile_view_basic` (profileViewBasic) is declared here; its
 * arbitrary viewerState is kept as an owned detached cJSON subtree so the
 * parser stays bounded regardless of viewer shape.
 *
 * Conventions mirror feed_typed.h / graph_typed.h: wf_status error codes,
 * static strdup/set_string/reset helpers per translation unit, ownership via
 * cJSON_DetachItemFromObject, and a matching `_free` for every owned list.
 */

#ifndef WOLFRAM_ACTOR_TYPED_H
#define WOLFRAM_ACTOR_TYPED_H

/* agent.h transitively includes graph_typed.h (which defines the shared
 * wf_agent_actor_list type), so we do NOT include graph_typed.h directly here:
 * including it ahead of agent.h would define wf_agent_actor_list only after
 * agent.h's own include of graph_typed.h has already pulled moderation_typed.h,
 * which itself needs wf_agent_actor_list. Going through agent.h guarantees the
 * type is defined before any dependent header is processed. */
#include "wolfram/agent.h"
#include <cJSON.h>

/* The actor suggestions skeleton (app.bsky.unspecced.getSuggestionsSkeleton)
 * shares this typed-list ownership model; its parser and wrapper are declared
 * in unspecced_typed.h and re-exposed here so callers need only include
 * actor_typed.h. */
#include "wolfram/unspecced_typed.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal basic profile view (app.bsky.actor.defs#profileViewBasic). Reuses
 * the same four core fields as wf_agent_profile_view; an arbitrary viewerState
 * subtree is kept as an owned detached cJSON node. */
typedef struct wf_agent_profile_view_basic {
    char *did;
    char *handle;
    char *display_name;
    char *avatar;
    cJSON *viewer;          /* owned detached viewerState subtree; NULL absent */
} wf_agent_profile_view_basic;

/* A list of basic profile views plus an optional cursor (searchActorsTypeahead,
 * getSuggestionsSkeleton). wf_agent_actor_list is defined in graph_typed.h. */
typedef struct wf_agent_profile_view_basic_list {
    wf_agent_profile_view_basic *actors;
    size_t actor_count;
    char *cursor;
} wf_agent_profile_view_basic_list;

/* A list of full profile views from getProfiles (distinct "profiles" key). */
typedef struct wf_agent_profile_view_list {
    wf_agent_profile_view *profiles;
    size_t profile_count;
    char *cursor;
} wf_agent_profile_view_list;

/* A like/repost entry (getLikes / getRepostedBy): an actor plus timestamps. */
typedef struct wf_agent_actor_like_item {
    wf_agent_profile_view actor;
    char *created_at;
    char *indexed_at;
} wf_agent_actor_like_item;

typedef struct wf_agent_actor_like_list {
    wf_agent_actor_like_item *likes;
    size_t like_count;
    char *cursor;
} wf_agent_actor_like_list;

/* Parse a getProfiles JSON body ("profiles" array of profileView) into owned
 * structs. Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed
 * JSON or a missing/invalid array, WF_ERR_ALLOC on allocation failure, WF_OK on
 * success. On any error `out` is left fully reset. */
wf_status wf_agent_parse_profiles(const char *json, size_t json_len,
                                  wf_agent_profile_view_list *out);

/* Parse a searchActors/searchProfiles JSON body ("actors" array of
 * profileView) into an owned actor list. Same ownership/error rules. */
wf_status wf_agent_parse_actor_search(const char *json, size_t json_len,
                                      wf_agent_actor_list *out);

/* Parse a searchActorsTypeahead JSON body ("actors" array of profileViewBasic)
 * into an owned basic-profile list. Same ownership/error rules. */
wf_status wf_agent_parse_actor_typeahead(const char *json, size_t json_len,
                                         wf_agent_profile_view_basic_list *out);

/* Parse a getLikes JSON body ("likes" array of {actor, createdAt, indexedAt})
 * into an owned like list. Same ownership/error rules. */
wf_status wf_agent_parse_actor_likes(const char *json, size_t json_len,
                                     wf_agent_actor_like_list *out);

/* Parse a getRepostedBy JSON body ("repostedBy" array of profileView) into an
 * owned actor list. Same ownership/error rules. */
wf_status wf_agent_parse_reposted_by(const char *json, size_t json_len,
                                     wf_agent_actor_list *out);

/* Parse an "actors" array of profileView into an owned actor list. */
wf_status wf_agent_parse_actors(const char *json, size_t json_len,
                                wf_agent_actor_list *out);

/* Parse a profileView list held under `key` (e.g. "repostedBy", "profiles")
 * into an owned actor list. Same ownership/error rules as wf_agent_parse_actors. */
wf_status wf_agent_parse_profile_views(const char *json, size_t json_len,
                                       const char *key, wf_agent_actor_list *out);

/* Free the owned contents of each list type (also safe on a reset/zeroed list). */
void wf_agent_profile_view_list_free(wf_agent_profile_view_list *list);
void wf_agent_profile_view_basic_list_free(wf_agent_profile_view_basic_list *list);
void wf_agent_actor_like_list_free(wf_agent_actor_like_list *list);
void wf_agent_actor_list_free(wf_agent_actor_list *list);

/* Typed high-level wrappers — issue the corresponding agent/lex call and parse
 * the JSON body into `out`. On success `out` is owned by the caller (free with
 * the matching `_free`); on error it is left reset. */
wf_status wf_agent_get_profiles_typed(wf_agent *agent,
                                      const char *const *actors,
                                      size_t actors_count, int limit,
                                      const char *cursor,
                                      wf_agent_actor_list *out);
wf_status wf_agent_search_actors_typed(wf_agent *agent, const char *query,
                                       int limit, const char *cursor,
                                       wf_agent_actor_list *out);
wf_status wf_agent_search_actors_typeahead_typed(wf_agent *agent,
                                                 const char *query, int limit,
                                                 wf_agent_profile_view_basic_list *out);
wf_status wf_agent_get_likes_typed(wf_agent *agent, const char *uri,
                                   int limit, const char *cursor,
                                   wf_agent_actor_like_list *out);
wf_status wf_agent_get_reposted_by_typed(wf_agent *agent, const char *uri,
                                         int limit, const char *cursor,
                                         wf_agent_actor_list *out);
/* wf_agent_get_suggestions_skeleton_typed is re-exposed from unspecced_typed.h
 * (app.bsky.unspecced.getSuggestionsSkeleton). */

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_ACTOR_TYPED_H */
