/*
 * thread_typed.h — typed (owned-struct) parser for
 * `app.bsky.feed.getPostThread` responses.
 *
 * `wf_agent_parse_thread` converts the raw JSON body returned by
 * `app.bsky.feed.getPostThread` into an owned tree of C structs. The thread
 * is recursive: a post node may have a single `parent` node and an array of
 * `replies` nodes, each of which is itself a union of `threadViewPost`,
 * `notFoundPost`, or `blockedPost`. Records and embeds are kept as owned
 * `cJSON` subtrees detached from the parsed document.
 *
 * Free the whole tree with `wf_agent_thread_free`.
 */

#ifndef WOLFRAM_THREAD_TYPED_H
#define WOLFRAM_THREAD_TYPED_H

#include "wolfram/agent.h"
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Discriminates which member of a thread node is populated. */
typedef enum {
    WF_AGENT_THREAD_KIND_POST,       /* a threadViewPost (post is valid) */
    WF_AGENT_THREAD_KIND_NOT_FOUND,  /* a notFoundPost (`uri` is valid) */
    WF_AGENT_THREAD_KIND_BLOCKED     /* a blockedPost (`uri` is valid) */
} wf_agent_thread_node_kind;

/* A `postView` (the `post` of a threadViewPost). */
typedef struct wf_agent_thread_post {
    char *uri;
    char *cid;
    wf_agent_profile_view author;
    cJSON *record;          /* owned parsed record; NULL when absent */
    cJSON *embed;           /* owned parsed embed; NULL when absent */
    int reply_count;
    int repost_count;
    int like_count;
    int quote_count;
    char *indexed_at;
    char *viewer_like;       /* viewer.like record URI, or NULL */
    char *viewer_repost;     /* viewer.repost record URI, or NULL */
} wf_agent_thread_post;

typedef struct wf_agent_thread_node wf_agent_thread_node;

/* A single node in the thread tree. `kind` selects which fields are valid. */
struct wf_agent_thread_node {
    wf_agent_thread_node_kind kind;
    wf_agent_thread_post post;       /* valid when kind == POST */
    char *uri;                       /* valid when kind != POST */
    wf_agent_thread_node *parent;    /* single parent, or NULL */
    wf_agent_thread_node *replies;   /* array of WF_AGENT_THREAD_KIND_* */
    size_t replies_count;
};

/* Top-level parsed response: the root node plus an optional cursor and the
 * optional top-level threadgate view (app.bsky.feed.defs#threadgateView:
 * uri/cid/record/lists) kept as an owned raw cJSON subtree (NULL when absent). */
typedef struct wf_agent_thread {
    wf_agent_thread_node root;
    char *cursor;
    cJSON *threadgate;   /* owned #threadgateView subtree; NULL when absent */
} wf_agent_thread;

/*
 * Parse a getPostThread JSON body of `json_len` bytes into owned structs.
 * Returns WF_OK on success, WF_ERR_INVALID_ARG for bad args, WF_ERR_PARSE for
 * malformed JSON or a missing/invalid `thread`, or WF_ERR_ALLOC on allocation
 * failure (in which case `out` is zeroed and nothing is leaked).
 */
wf_status wf_agent_parse_thread(const char *json, size_t json_len,
                                wf_agent_thread *out);

/* Free every owned field of `thread` (including the recursive node tree). */
void wf_agent_thread_free(wf_agent_thread *thread);

/* Typed high-level wrapper — issues `wf_agent_get_post_thread` and parses the
 * JSON body into `out`. On success `out` is owned by the caller (free with
 * `wf_agent_thread_free`); on error it is left reset. */
wf_status wf_agent_get_post_thread_typed(wf_agent *agent, const char *uri,
                                         int depth, wf_agent_thread *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_THREAD_TYPED_H */
