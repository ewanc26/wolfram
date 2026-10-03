/*
 * post_display.h — display-oriented views of typed feed posts.
 *
 * Thin clients (3DS, Wii U) must not parse app.bsky record JSON themselves.
 * These helpers turn a `wf_agent_post_view` (whose `record` and `embed` are raw
 * cJSON subtrees) into plain owned C data: post text, validated facets, an
 * embed summary, and the "reposted by" name. All input is treated as hostile
 * server data: missing or oddly-shaped fields yield empty/NONE results rather
 * than errors or crashes.
 *
 * Also declares wf_agent_fetch_public for fetching avatars/CDN images without
 * the session's credentials.
 */

#ifndef WOLFRAM_POST_DISPLAY_H
#define WOLFRAM_POST_DISPLAY_H

#include "wolfram/feed_typed.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum wf_facet_kind {
    WF_FACET_LINK = 1, /* target = uri */
    WF_FACET_MENTION,  /* target = did */
    WF_FACET_TAG       /* target = tag (without '#') */
} wf_facet_kind;

/* A validated richtext facet. [byte_start, byte_end) are byte offsets into
 * wf_post_display.text, on UTF-8 boundaries, within bounds, non-empty. */
typedef struct wf_post_facet {
    wf_facet_kind kind;
    size_t byte_start;
    size_t byte_end;
    char *target;
} wf_post_facet;

typedef enum wf_post_embed_kind {
    WF_EMBED_NONE = 0,
    WF_EMBED_IMAGES,
    WF_EMBED_VIDEO,
    WF_EMBED_EXTERNAL,
    WF_EMBED_RECORD,
    WF_EMBED_RECORD_WITH_MEDIA,
    WF_EMBED_UNKNOWN
} wf_post_embed_kind;

/* Owned display data. Zero-initialise before use is not required; the fill
 * function resets it. Release with wf_post_display_free. */
typedef struct wf_post_display {
    char *text; /* UTF-8; "" when the post has none (never NULL on WF_OK) */
    char *created_at;      /* record createdAt, or NULL */
    wf_post_facet *facets; /* sorted by byte_start, non-overlapping */
    size_t facet_count;
    size_t image_count; /* images embed, or the media of recordWithMedia */
    wf_post_embed_kind embed_kind;
    char *external_title; /* EXTERNAL, or media of RECORD_WITH_MEDIA; or NULL */
    char *external_uri;
    char *quote_uri; /* only when the quoted record is viewable; else NULL */
    char *quote_author_handle;
    char *quote_text;
    int is_reply; /* record carries a reply ref */
} wf_post_display;

/* Fill `out` from `post`. Facets are validated and sorted: those whose byte
 * range is out of bounds, empty/inverted, overlaps an earlier (by start) kept
 * facet, or is not on UTF-8 boundaries are dropped; a facet's first known
 * feature (link/mention/tag with a non-empty target) is used and unknown
 * feature types are skipped. At most 512 facets are considered. Embed fields
 * are read from the embed *view* shape (app.bsky.embed.*#view).
 *
 * Returns WF_ERR_INVALID_ARG on NULL arguments, WF_ERR_ALLOC on allocation
 * failure (out is left reset), WF_OK otherwise — including for a post with no
 * record or embed. */
wf_status wf_agent_post_view_display(const wf_agent_post_view *post,
                                     wf_post_display *out);

/* Free everything owned by `d` and zero it. NULL-safe. */
void wf_post_display_free(wf_post_display *d);

/* If `item` is a repost (reasonRepost), set *out_name to a malloc'd copy of the
 * reposter's display name, falling back to their handle. *out_name is NULL
 * (and WF_OK returned) when the item is not a repost or the reposter has
 * neither name. Free with free(). WF_ERR_INVALID_ARG on NULL arguments. */
wf_status wf_agent_feed_item_reposted_by(const wf_agent_feed_item *item,
                                         char **out_name);

/* GET an absolute https:// URL (avatar, thumbnail, other public CDN content)
 * WITHOUT any Authorization/DPoP header.
 *
 * The request is made on a private snapshot of the agent's data-plane client
 * settings: the agent's CA bundle (wf_agent_set_ca_bundle) and TLS RNG hook
 * (wf_agent_set_tls_rng) are honoured, the client's own state is neither
 * mutated nor shared, and credentials are never attached, so it may run on a
 * worker thread alongside other requests. Only https is accepted — for the URL
 * and for every redirect hop — with at most 3 redirects. The body is capped at
 * `max_bytes` (must be > 0); a longer body aborts with WF_ERR_NETWORK.
 *
 * Returns WF_ERR_INVALID_ARG for NULL arguments, max_bytes == 0 or a non-https
 * URL; WF_ERR_NETWORK on transport failure; WF_ERR_HTTP for a non-2xx status.
 * On WF_OK and WF_ERR_HTTP `out` is populated and must be freed with
 * wf_response_free. */
wf_status wf_agent_fetch_public(wf_agent *agent, const char *url,
                                size_t max_bytes, wf_response *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_POST_DISPLAY_H */
