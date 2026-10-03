/*
 * post_view_typed.h — typed readers for the open-shaped parts of a post view.
 *
 * feed_typed.h / thread_typed.h / notification_typed.h keep a post's `record`,
 * `embed`, and a feed item's `reason` / `reply` as owned cJSON subtrees,
 * because those are open unions. A client that wants to draw a post still
 * needs a handful of fields out of them (text, facets, createdAt, the thread
 * root, a repost banner, thumbnails, a link card, a quoted post). These
 * readers take such a subtree (as found on wf_agent_post_view.record etc.) and
 * return owned, flattened structs, so a client never has to know the JSON
 * shapes or the `$type` strings.
 *
 * All readers accept NULL (yielding an empty result and WF_OK), never keep a
 * pointer into the input, and have a matching `_free` that is safe on a
 * zeroed struct. Unknown or malformed members are skipped rather than failing.
 */

#ifndef WOLFRAM_POST_VIEW_TYPED_H
#define WOLFRAM_POST_VIEW_TYPED_H

#include "wolfram/util.h"
#include <cJSON.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- app.bsky.feed.post record ---- */

typedef enum wf_post_facet_kind {
    WF_POST_FACET_LINK = 1,
    WF_POST_FACET_MENTION,
    WF_POST_FACET_TAG
} wf_post_facet_kind;

/* One richtext facet, reduced to the first feature of a known kind. The byte
 * range is reported as sent (not validated against the text). */
typedef struct wf_post_facet {
    int byte_start;
    int byte_end;
    wf_post_facet_kind kind;
    char *target; /* link uri, mention did, or tag; "" when the feature
                     carried none */
} wf_post_facet;

/* replyRef.root, when both uri and cid are present. */
typedef struct wf_post_reply_root {
    char *uri; /* NULL unless both uri and cid were strings */
    char *cid;
} wf_post_reply_root;

typedef struct wf_post_record {
    char *text;       /* NULL when absent */
    char *created_at; /* NULL when absent */
    wf_post_facet *facets;
    size_t facet_count;
    wf_post_reply_root reply_root; /* from record.reply */
} wf_post_record;

/* Read a post (or notification) record. Facets lacking an index or a features
 * array, or with no feature of a known kind, are omitted. */
wf_status wf_post_record_from_json(const cJSON *record, wf_post_record *out);
void wf_post_record_free(wf_post_record *rec);

/* Read a feedViewPost.reply object (the thread root lives there rather than in
 * the record for feed items). */
wf_status wf_post_reply_root_from_json(const cJSON *reply_ref,
                                       wf_post_reply_root *out);
void wf_post_reply_root_free(wf_post_reply_root *root);

/* ---- feedViewPost.reason ---- */

typedef struct wf_post_reason {
    int is_repost;         /* app.bsky.feed.defs#reasonRepost */
    char *by_display_name; /* NULL when absent */
    char *by_handle;       /* NULL when absent */
} wf_post_reason;

/* reasonPin and unknown reasons yield is_repost == 0. */
wf_status wf_post_reason_from_json(const cJSON *reason, wf_post_reason *out);
void wf_post_reason_free(wf_post_reason *reason);

/* ---- postView.embed ---- */

typedef struct wf_post_embed_image {
    char *thumb; /* always non-empty */
    char *alt;   /* NULL when absent */
    int width;   /* aspectRatio, 0 when absent */
    int height;
} wf_post_embed_image;

typedef struct wf_post_embed {
    char *type;       /* the embed's own $type; NULL when absent */
    char *media_type; /* $type of the media half: the nested media for
                         recordWithMedia, otherwise the same as `type` */
    wf_post_embed_image *images; /* images#view, direct or as media */
    size_t image_count;
    int has_external; /* external#view with a non-empty uri */
    char *external_uri;
    char *external_title;
    char *external_description;
    char *external_thumb; /* NULL when absent */
    /* A quoted post, only for record#view / recordWithMedia#view whose record
     * is a viewRecord (not found / blocked / detached leave this unset). */
    int has_quote;
    char *quote_author_display_name; /* NULL when absent */
    char *quote_author_handle;       /* NULL when absent */
    char *quote_text;                /* NULL when absent */
} wf_post_embed;

wf_status wf_post_embed_from_json(const cJSON *embed, wf_post_embed *out);
void wf_post_embed_free(wf_post_embed *embed);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_POST_VIEW_TYPED_H */
