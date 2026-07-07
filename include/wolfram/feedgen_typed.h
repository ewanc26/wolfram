/*
 * feedgen_typed.h — owned typed parser for feed-generator endpoints.
 *
 * Parses the raw JSON bodies returned by:
 *   - app.bsky.feed.getActorFeeds  -> `feeds` array of generatorView
 *   - app.bsky.feed.getFeed         -> `feed` array of feedView
 *   - app.bsky.feed.getActorLikes   -> `feed` array of feedViewPost (reuses
 *                                       wf_agent_parse_feed from feed_typed)
 *
 * into owned C structs. Ownership rules mirror notification.c / feed_typed.c /
 * graph_typed.c: static strdup/set_string/reset helpers local to the TU,
 * owned plain string copies, and full cleanup on the first error.
 */

#ifndef WOLFRAM_FEEDGEN_TYPED_H
#define WOLFRAM_FEEDGEN_TYPED_H

#include "wolfram/agent.h"
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration: wf_agent_feed_list is defined in feed_typed.h and used
 * here only as a pointer parameter (getActorLikes reuses the feed parser). */
typedef struct wf_agent_feed_list wf_agent_feed_list;

/* A parsed feed-generator view (app.bsky.feed.defs#generatorView). */
typedef struct wf_agent_generator_view {
    char *uri;
    char *cid;
    char *did;
    wf_agent_profile_view creator;  /* owned creator profileView */
    char *display_name;
    char *description;
    char *avatar;
    char *indexed_at;
    int like_count;
    int has_like_count;             /* whether likeCount was present */
} wf_agent_generator_view;

/* A parsed list of generator views (app.bsky.feed.getActorFeeds). */
typedef struct wf_agent_generator_view_list {
    wf_agent_generator_view *generators;
    size_t generator_count;
    char *cursor;
} wf_agent_generator_view_list;

/* A parsed custom-feed view (app.bsky.feed.defs#feedView). */
typedef struct wf_agent_feed_view {
    char *uri;
    char *cid;
    char *did;
    char *display_name;
    char *description;
    char *avatar;
    char *indexed_at;
} wf_agent_feed_view;

/* A parsed list of feed views (app.bsky.feed.getFeed). */
typedef struct wf_agent_feed_view_list {
    wf_agent_feed_view *feeds;
    size_t feed_count;
    char *cursor;
} wf_agent_feed_view_list;

/* Parse a raw getActorFeeds JSON body into owned generator views.
 * Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON
 * or a missing `feeds` array, WF_ERR_ALLOC on allocation failure, WF_OK on
 * success. On any error `out` is left fully reset (no partial leaks). */
wf_status wf_agent_parse_generators(const char *json, size_t json_len,
                                    wf_agent_generator_view_list *out);

/* Free a parsed generator-view list and every owned field it holds. */
void wf_agent_generator_view_list_free(wf_agent_generator_view_list *list);

/* Parse a raw getFeed JSON body into owned feed views.
 * Same ownership/error rules as wf_agent_parse_generators, operating on the
 * `feed` array of feedView. */
wf_status wf_agent_parse_feed_views(const char *json, size_t json_len,
                                    wf_agent_feed_view_list *out);

/* Free a parsed feed-view list and every owned field it holds. */
void wf_agent_feed_view_list_free(wf_agent_feed_view_list *list);

/* Typed high-level wrappers — issue the corresponding agent call and parse the
 * JSON body into `out`. On success `out` is owned by the caller (free with the
 * matching `_free`); on error it is left reset. */
wf_status wf_agent_get_actor_feeds_typed(wf_agent *agent, const char *actor,
                                         int limit, const char *cursor,
                                         wf_agent_generator_view_list *out);
wf_status wf_agent_get_feed_typed(wf_agent *agent, const char *feed_uri,
                                  int limit, const char *cursor,
                                  wf_agent_feed_view_list *out);
wf_status wf_agent_get_actor_likes_typed(wf_agent *agent, const char *actor,
                                         int limit, const char *cursor,
                                         wf_agent_feed_list *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_FEEDGEN_TYPED_H */
