/*
 * feedgen_typed.h — owned typed parser for feed-generator endpoints.
 *
 * Parses the raw JSON bodies returned by:
 *   - app.bsky.feed.getActorFeeds    -> `feeds` array of generatorView
 *   - app.bsky.feed.getFeedGenerator -> single generatorView + isOnline/isValid
 *   - app.bsky.feed.getFeed          -> `feed` array of feedViewPost (reuses
 *                                       wf_agent_parse_feed from feed_typed)
 *   - app.bsky.feed.getActorLikes    -> `feed` array of feedViewPost (reuses
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

/* Forward declarations of the feed_typed.h feedViewPost structs (full
 * definitions live in feed_typed.h). getFeed returns feedViewPost items — the
 * SAME shape as getTimeline/getAuthorFeed — so its item/list types are aliases
 * for the shared feed_typed structs. Only pointer usage appears in this header,
 * so forward declarations of the tagged structs are sufficient and avoid the
 * agent.h <-> feed_typed.h include cycle. */
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

/* A single feed-generator record with online/valid status
 * (app.bsky.feed.getFeedGenerator returns {view, isOnline, isValid}). */
typedef struct wf_agent_generator_detail {
    wf_agent_generator_view view;   /* owned generatorView */
    int is_online;
    int is_valid;
} wf_agent_generator_detail;

/* An item returned by app.bsky.feed.getFeed. getFeed returns feedViewPost
 * items (post/reason/reply/feedContext/reqId) — the SAME shape used by
 * getTimeline/getAuthorFeed — so this is an alias for the feed_typed structs
 * and is parsed with the shared feedViewPost parser (wf_agent_parse_feed). */
typedef struct wf_agent_feed_item wf_agent_feed_view;

/* A parsed list of feedViewPost items (app.bsky.feed.getFeed). Alias for the
 * timeline/author-feed list type so the two stay consistent. */
typedef struct wf_agent_feed_list wf_agent_feed_view_list;

/* Parse a raw getActorFeeds JSON body into owned generator views.
 * Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON
 * or a missing `feeds` array, WF_ERR_ALLOC on allocation failure, WF_OK on
 * success. On any error `out` is left fully reset (no partial leaks). */
wf_status wf_agent_parse_generators(const char *json, size_t json_len,
                                    wf_agent_generator_view_list *out);

/* Free a parsed generator-view list and every owned field it holds. */
void wf_agent_generator_view_list_free(wf_agent_generator_view_list *list);

/* Parse a raw getFeedGenerator JSON body ({view, isOnline, isValid}) into an
 * owned generatorView detail. Same ownership/error rules as the parsers above,
 * reusing the generatorView parser for `view`. */
wf_status wf_agent_parse_feed_generator(const char *json, size_t json_len,
                                        wf_agent_generator_detail *out);

/* Free a parsed generator detail and every owned field it holds. */
void wf_agent_generator_detail_free(wf_agent_generator_detail *out);

/* Parse a raw getFeed JSON body into owned feedViewPost items.
 * Same ownership/error rules as wf_agent_parse_generators, operating on the
 * `feed` array of feedViewPost (delegates to wf_agent_parse_feed). */
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

/* getFeedGenerator (returns a single generatorView + isOnline + isValid). This
 * is SEPARATE from getFeed (which returns feedViewPost items). */
wf_status wf_agent_get_feed_generator_typed(wf_agent *agent,
                                            const char *feed_uri,
                                            wf_agent_generator_detail *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_FEEDGEN_TYPED_H */
