/*
 * bookmark_typed.h — owned typed parser + agent convenience wrappers for the
 * `app.bsky.bookmark` namespace (bookmarks).
 *
 * Parses the raw JSON body returned by `app.bsky.bookmark.getBookmarks` into
 * owned C structs, and provides high-level agents wrappers over
 * `app.bsky.bookmark.createBookmark`, `app.bsky.bookmark.deleteBookmark`, and
 * `app.bsky.bookmark.getBookmarks`.
 *
 * Conventions mirror `feed_typed.h` / `actor_typed.h`:
 *   - `wf_status` error codes (WF_ERR_INVALID_ARG, WF_ERR_PARSE, WF_ERR_ALLOC,
 *     WF_OK)
 *   - ownership via `cJSON_DetachItemFromObject` for arbitrary sub-shapes
 *   - every heap-allocated output has a matching `_free` declared next to it
 *   - full cleanup on the first error (no partial leaks)
 */

#ifndef WOLFRAM_BOOKMARK_TYPED_H
#define WOLFRAM_BOOKMARK_TYPED_H

#include "wolfram/agent.h"
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A single bookmarked record view (app.bsky.bookmark.defs#bookmarkView).
 * `uri` is the at-uri of the bookmarked record (subject), `created_at` the
 * optional creation timestamp echoed by the server. */
typedef struct wf_bookmark {
    char *uri;          /* at-uri of the bookmarked record; NULL when absent */
    char *created_at;   /* RFC 3339 datetime; NULL when absent */
} wf_bookmark;

/* The parsed getBookmarks response. */
typedef struct wf_bookmark_list {
    wf_bookmark *items; /* owned array of `count` bookmarks; NULL when empty */
    size_t count;
    char *cursor;       /* pagination cursor; NULL when absent */
} wf_bookmark_list;

/* Parse a raw app.bsky.bookmark.getBookmarks JSON body into owned structs.
 * Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or
 * a missing `bookmarks` array, WF_ERR_ALLOC on allocation failure, WF_OK on
 * success. On any error `out` is left fully reset (no partial leaks). */
wf_status wf_bookmark_parse_list(const char *json, size_t len,
                                 wf_bookmark_list *out);

/* Free a parsed bookmark list and every owned string it holds. */
void wf_bookmark_list_free(wf_bookmark_list *list);

/* High-level agent wrappers. On success the owned outputs are owned by the
 * caller and must be freed (out_uri via free(), out list via
 * wf_bookmark_list_free); on error they are left reset. */

/* Create a private bookmark for `post_uri`. On success sets *out_uri to an
 * owned copy of `post_uri` (the bookmarked record's subject at-uri; the
 * createBookmark procedure echoes no record at-uri on the wire). Returns
 * WF_ERR_INVALID_ARG if `post_uri` is NULL or not a valid at-uri. `out_uri`
 * may be NULL if the caller does not need the uri. */
wf_status wf_agent_create_bookmark(wf_agent *agent, const char *post_uri,
                                   char **out_uri);

/* Delete the private bookmark for `post_uri`. Returns WF_ERR_INVALID_ARG if
 * `post_uri` is NULL or not a valid at-uri. */
wf_status wf_agent_delete_bookmark(wf_agent *agent, const char *post_uri);

/* Fetch the authenticated user's bookmarks, optionally paging with `limit`
 * (<=0 uses the server default) and `cursor` (NULL/empty to start). Parses the
 * response body into `out`. */
wf_status wf_agent_get_bookmarks_typed(wf_agent *agent, int limit,
                                       const char *cursor,
                                       wf_bookmark_list *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_BOOKMARK_TYPED_H */
