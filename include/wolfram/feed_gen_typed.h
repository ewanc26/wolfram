/*
 * feed_gen_typed.h — owned typed parsers for feed GENERATORS + feed DISCOVERY,
 * distinct from feed_typed (which covers timeline/authorFeed/posts/likes/
 * quotes/repostedBy). This module owns the generator (generatorView) shapes and
 * the discovery endpoints that return actor lists, feed lists, or post-view
 * search results.
 *
 * Conventions mirror labeler_typed.h / actor_typed.h: wf_status error codes,
 * static strdup/set_string/reset helpers, ownership via cJSON_DetachItemFromObject,
 * and a matching `_free` for every owned struct (a freed/zeroed struct frees
 * safely). Every owned string is heap-allocated; the `extra` field (where
 * present) holds an owned detached cJSON subtree of open/unbounded fields.
 *
 * Reused types (do NOT redefine):
 *   - wf_agent_profile        (agent.h) — creator of a generatorView
 *   - wf_agent_like_list      (graph_typed.h) — getLikes output
 *   - wf_agent_actor_list     (graph_typed.h) — getRepostedBy output
 *   - wf_agent_feed_list      (feed_typed.h)  — getQuotes/getActorLikes/getListFeed
 *   - wf_agent_profile_view   (agent.h)       — author of a postView
 *
 * NSIDs covered (each call uses the generated lex wrapper directly):
 *   - app.bsky.feed.getFeedGenerator        -> wf_feedgen_generator_detail
 *   - app.bsky.feed.getFeedGenerators        -> wf_feedgen_generator_list
 *   - app.bsky.feed.getActorFeeds            -> wf_feedgen_generator_list
 *   - app.bsky.feed.getSuggestedFeeds        -> wf_feedgen_generator_list
 *   - app.bsky.feed.getLikes                 -> wf_agent_like_list (reused)
 *   - app.bsky.feed.getRepostedBy            -> wf_agent_actor_list (reused)
 *   - app.bsky.feed.getQuotes                -> wf_agent_feed_list (reused)
 *   - app.bsky.feed.getActorLikes            -> wf_agent_feed_list (reused)
 *   - app.bsky.feed.searchPosts              -> wf_feedgen_search_result_list
 *   - app.bsky.feed.searchPostsV2            -> wf_feedgen_search_result_list
 *   - app.bsky.feed.getListFeed              -> wf_agent_feed_list (reused)
 *
 * app.bsky.feed.getPopularFeedGenerators is NOT covered: there is no
 * app.bsky.feed.getPopularFeedGenerators generated lex wrapper (the NSID exists
 * only as app.bsky.unspecced.getPopularFeedGenerators). See the TODO in the
 * source for the optional unspecced variant.
 */

#ifndef WOLFRAM_FEED_GEN_TYPED_H
#define WOLFRAM_FEED_GEN_TYPED_H

#include "wolfram/agent.h"
#include "wolfram/feed_typed.h"
#include "wolfram/graph_typed.h"

#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A feed generator view (app.bsky.feed.defs#generatorView). Core fields are
 * copied; `descriptionFacets`, `viewer`, `labels`, and any other fields remain
 * in the owned `extra` subtree. The creator is a profileView parsed into the
 * shared wf_agent_profile struct (freed with wf_agent_profile_free). */
typedef struct wf_feedgen_generator_view {
    char *uri;
    char *cid;
    wf_agent_profile creator;       /* owned; see note above */
    char *display_name;
    char *description;
    char *avatar;
    bool has_like_count;
    int64_t like_count;
    bool has_accepts_interactions;
    bool accepts_interactions;
    char *content_mode;
    char *indexed_at;
    cJSON *extra;                   /* owned detached subtree; NULL when empty */
} wf_feedgen_generator_view;

/* A list of generator views plus an optional cursor (feeds[] endpoints). */
typedef struct wf_feedgen_generator_list {
    wf_feedgen_generator_view *generators;
    size_t generator_count;
    char *cursor;
} wf_feedgen_generator_list;

/* A single feed generator with online/valid status (getFeedGenerator). */
typedef struct wf_feedgen_generator_detail {
    wf_feedgen_generator_view view;
    bool is_online;
    bool is_valid;
} wf_feedgen_generator_detail;

/* A search hit post view (app.bsky.feed.defs#postView). The author is a
 * profileView copied into the shared wf_agent_profile_view struct. `record` and
 * `embed` are owned detached cJSON subtrees; `viewer`, `labels`, and any other
 * fields remain in the owned `extra` subtree. */
typedef struct wf_feedgen_search_post {
    char *uri;
    char *cid;
    wf_agent_profile_view author;
    cJSON *record;                 /* owned record subtree; NULL when absent */
    cJSON *embed;                  /* owned embed subtree; NULL when absent */
    bool has_reply_count;
    int64_t reply_count;
    bool has_repost_count;
    int64_t repost_count;
    bool has_like_count;
    int64_t like_count;
    bool has_quote_count;
    int64_t quote_count;
    bool has_bookmark_count;
    int64_t bookmark_count;
    char *indexed_at;
    cJSON *extra;                  /* owned detached subtree; NULL when empty */
} wf_feedgen_search_post;

/* A search-posts result list (searchPosts / searchPostsV2). */
typedef struct wf_feedgen_search_result_list {
    wf_feedgen_search_post *posts;
    size_t post_count;
    char *cursor;
    bool has_hits_total;
    int64_t hits_total;
} wf_feedgen_search_result_list;

/* ---- Parsers (own their outputs; full cleanup on first error) ----
 * Return WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or a
 * missing/invalid shape, WF_ERR_ALLOC on allocation failure, WF_OK on success.
 * On any error `out` is left fully reset. */

/* Parse a feed-generator list JSON body ("feeds" array + optional cursor) into
 * owned structs. Used by getFeedGenerators / getActorFeeds / getSuggestedFeeds. */
wf_status wf_feedgen_parse_generators(const char *json, size_t json_len,
                                      wf_feedgen_generator_list *out);

/* Parse a getFeedGenerator JSON body (single "view" + isOnline/isValid) into an
 * owned detail struct. */
wf_status wf_feedgen_parse_feed_generator(const char *json, size_t json_len,
                                          wf_feedgen_generator_detail *out);

/* Parse a searchPosts / searchPostsV2 JSON body ("posts" array of postView,
 * optional cursor + hitsTotal) into owned structs. */
wf_status wf_feedgen_parse_search_posts(const char *json, size_t json_len,
                                        wf_feedgen_search_result_list *out);

/* ---- Free functions (safe on a reset/zeroed struct) ---- */
void wf_feedgen_generator_view_free(wf_feedgen_generator_view *g);
void wf_feedgen_generator_list_free(wf_feedgen_generator_list *list);
void wf_feedgen_generator_detail_free(wf_feedgen_generator_detail *d);
void wf_feedgen_search_result_list_free(wf_feedgen_search_result_list *list);

/* ---- Agent convenience wrappers ----
 * Each issues the corresponding lex call against the agent's primary XRPC
 * client (after syncing auth) and parses the body into `out`. On success `out`
 * is owned by the caller (free with the matching `_free`); on error it is left
 * reset. Required inputs are validated and return WF_ERR_INVALID_ARG when
 * NULL/empty. */

/* app.bsky.feed.getFeedGenerator. `feed_uri` is the generator record AT-URI. */
wf_status wf_feedgen_get_feed_generator_typed(wf_agent *agent,
                                             const char *feed_uri,
                                             wf_feedgen_generator_detail *out);

/* app.bsky.feed.getFeedGenerators. `feeds`/`feed_count` are generator AT-URIs. */
wf_status wf_feedgen_get_feed_generators_typed(wf_agent *agent,
                                               const char *const *feeds,
                                               size_t feed_count,
                                               wf_feedgen_generator_list *out);

/* app.bsky.feed.getActorFeeds. */
wf_status wf_feedgen_get_actor_feeds_typed(wf_agent *agent, const char *actor,
                                           int limit, const char *cursor,
                                           wf_feedgen_generator_list *out);

/* app.bsky.feed.getSuggestedFeeds. */
wf_status wf_feedgen_get_suggested_feeds_typed(wf_agent *agent, int limit,
                                               const char *cursor,
                                               wf_feedgen_generator_list *out);

/* app.bsky.feed.getLikes. `cid` may be NULL. */
wf_status wf_feedgen_get_likes_typed(wf_agent *agent, const char *uri,
                                     const char *cid, int limit,
                                     const char *cursor,
                                     wf_agent_like_list *out);

/* app.bsky.feed.getRepostedBy. `cid` may be NULL. */
wf_status wf_feedgen_get_reposted_by_typed(wf_agent *agent, const char *uri,
                                           const char *cid, int limit,
                                           const char *cursor,
                                           wf_agent_actor_list *out);

/* app.bsky.feed.getQuotes. */
wf_status wf_feedgen_get_quotes_typed(wf_agent *agent, const char *uri,
                                      int limit, const char *cursor,
                                      wf_agent_feed_list *out);

/* app.bsky.feed.getActorLikes. */
wf_status wf_feedgen_get_actor_likes_typed(wf_agent *agent, const char *actor,
                                           int limit, const char *cursor,
                                           wf_agent_feed_list *out);

/* app.bsky.feed.getListFeed. `list_uri` is the list record AT-URI. */
wf_status wf_feedgen_get_list_feed_typed(wf_agent *agent, const char *list_uri,
                                         int limit, const char *cursor,
                                         wf_agent_feed_list *out);

/* app.bsky.feed.searchPosts. */
wf_status wf_feedgen_search_posts_typed(wf_agent *agent, const char *query,
                                        int limit, const char *cursor,
                                        wf_feedgen_search_result_list *out);

/* app.bsky.feed.searchPostsV2. */
wf_status wf_feedgen_search_posts_v2_typed(wf_agent *agent, const char *query,
                                           int limit, const char *cursor,
                                           wf_feedgen_search_result_list *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_FEED_GEN_TYPED_H */
