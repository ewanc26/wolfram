/*
 * feed_typed.h — owned typed parser for app.bsky.feed timeline/author-feed
 * responses.
 *
 * Parses the raw JSON body returned by `app.bsky.feed.getTimeline` (and the
 * structurally identical `app.bsky.feed.getAuthorFeed`) into owned C structs.
 * `record`, `embed`, `reason`, and `reply` are kept as owned `cJSON` subtrees
 * (detached from the parsed document) so the parser stays bounded regardless
 * of arbitrary record/embed shapes. Free the whole list with
 * `wf_agent_feed_list_free`.
 *
 * This mirrors the conventions in `agent.h` / `notification.c`:
 *   - `wf_status` error codes (WF_ERR_INVALID_ARG, WF_ERR_PARSE, WF_ERR_ALLOC,
 *     WF_OK)
 *   - static strdup/set_string/reset helpers local to the translation unit
 *   - ownership via `cJSON_DetachItemFromObject`
 */

#ifndef WOLFRAM_FEED_TYPED_H
#define WOLFRAM_FEED_TYPED_H

#include "wolfram/agent.h"
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Viewer relationship state for a post (app.bsky.feed.defs#viewerState). */
typedef struct wf_agent_feed_viewer_state {
    char *repost;          /* at-uri of the viewer's repost, or NULL */
    char *like;            /* at-uri of the viewer's like, or NULL */
    int bookmarked;
    int thread_muted;
    int reply_disabled;
    int embedding_disabled;
    int pinned;
    int has_bookmarked;        /* whether the *_disabled-style flags were set */
    int has_thread_muted;
    int has_reply_disabled;
    int has_embedding_disabled;
    int has_pinned;
} wf_agent_feed_viewer_state;

/* A single post in a feed (app.bsky.feed.defs#postView). */
typedef struct wf_agent_post_view {
    char *uri;
    char *cid;
    wf_agent_profile_view author;
    cJSON *record;              /* owned record subtree; NULL when absent */
    cJSON *embed;               /* owned embed subtree; NULL when absent */
    int reply_count;
    int repost_count;
    int like_count;
    int quote_count;
    int bookmark_count;
    int has_reply_count;
    int has_repost_count;
    int has_like_count;
    int has_quote_count;
    int has_bookmark_count;
    char *indexed_at;
    wf_agent_feed_viewer_state viewer;
} wf_agent_post_view;

/* A feed item (app.bsky.feed.defs#feedViewPost). */
typedef struct wf_agent_feed_item {
    wf_agent_post_view post;
    cJSON *reason;              /* owned reason subtree (reasonRepost/reasonPin) */
    cJSON *reply;               /* owned replyRef subtree; NULL when absent */
    char *feed_context;         /* optional context from feed generator */
    char *req_id;               /* optional per-request identifier */
} wf_agent_feed_item;

/* The parsed timeline/author-feed list. */
typedef struct wf_agent_feed_list {
    wf_agent_feed_item *items;
    size_t item_count;
    char *cursor;
} wf_agent_feed_list;

/* Parse a raw getTimeline/getAuthorFeed JSON body into owned structs.
 * Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON
 * or a missing `feed` array, WF_ERR_ALLOC on allocation failure, WF_OK on
 * success. On any error `out` is left fully reset (no partial leaks). */
wf_status wf_agent_parse_feed(const char *json, size_t json_len,
                              wf_agent_feed_list *out);

/* Free a parsed feed list and every owned subtree it holds. */
void wf_agent_feed_list_free(wf_agent_feed_list *list);

/* Typed high-level wrappers — issue the corresponding agent call and parse the
 * JSON body into `out`. On success `out` is owned by the caller and must be
 * freed with `wf_agent_feed_list_free`; on error it is left reset. */
wf_status wf_agent_get_timeline_typed(wf_agent *agent, int limit,
                                      const char *cursor,
                                      wf_agent_feed_list *out);
wf_status wf_agent_get_author_feed_typed(wf_agent *agent, const char *actor,
                                         int limit, const char *cursor,
                                         const char *filter,
                                         wf_agent_feed_list *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_FEED_TYPED_H */
