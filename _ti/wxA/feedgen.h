/*
 * feedgen.h — server-side helper for building
 * app.bsky.feed.getFeedSkeleton responses.
 *
 * Given a list of candidate post AT-URIs, this validates each one, removes
 * duplicates (preserving input order), and paginates the result with an
 * index-based cursor, emitting the `{ feed, cursor }` JSON shape a feed
 * generator serves at getFeedSkeleton.
 *
 * This module is independent of feedgen_typed.h (which parses
 * app.bsky.feed.generator records); the two share no names.
 *
 * Ownership: wf_feedgen_build_skeleton allocates and returns two owned,
 * NUL-terminated strings (`out_json` and `out_next_cursor`) that must be
 * released with wf_feedgen_skeleton_free. `out_next_cursor` is NULL when
 * there is no subsequent page.
 */

#ifndef WOLFRAM_FEEDGEN_H
#define WOLFRAM_FEEDGEN_H

#include <stddef.h>
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Validate a list of candidate post AT-URIs.
 *
 * Each URI is checked with the AT-URI syntax validator (wf_syntax_aturi_parse,
 * which returns non-zero only for a well-formed at:// URI). On the first
 * invalid URI, returns WF_ERR_INVALID_ARG and writes its position to
 * *out_invalid_index (if out_invalid_index is non-NULL). Returns WF_OK when
 * every URI is valid (or uri_count is 0).
 *
 * NULL `uris` with non-zero `uri_count` is rejected with WF_ERR_INVALID_ARG.
 */
wf_status wf_feedgen_validate_candidates(const char *const *uris,
                                         size_t uri_count,
                                         size_t *out_invalid_index);

/*
 * Build a getFeedSkeleton response from candidate post AT-URIs.
 *
 * Behaviour:
 *   - Validates every URI; if any is invalid the whole call fails with
 *     WF_ERR_INVALID_ARG (no output is produced).
 *   - De-duplicates by exact URI string, preserving input order.
 *   - Paginates the de-duplicated list: `limit` items per page, starting
 *     after `cursor`. The cursor is a decimal offset into the de-duplicated
 *     list. An empty or NULL cursor means "start at offset 0".
 *
 * Output:
 *   - *out_json is an owned cJSON-rendered string of the form
 *     `{"feed":[{"post":"<at-uri>"}, ...], "cursor":"<next>"}` (the `cursor`
 *     key is omitted when there is no subsequent page). On empty input the
 *     result is `{"feed":[]}` with no cursor.
 *   - *out_next_cursor is an owned decimal-string cursor for the next page,
 *     or NULL when the current page contains the remainder.
 *
 * Errors:
 *   - WF_ERR_INVALID_ARG: NULL out pointers, NULL `uris` with uri_count > 0,
 *     limit == 0, an invalid candidate URI, or a malformed cursor.
 *   - WF_ERR_ALLOC: allocation / JSON-rendering failure (no output leaked).
 *
 * Both outputs are released with wf_feedgen_skeleton_free.
 */
wf_status wf_feedgen_build_skeleton(const char *const *uris, size_t uri_count,
                                    size_t limit, const char *cursor,
                                    char **out_json, char **out_next_cursor);

/* Free the two owned strings returned by wf_feedgen_build_skeleton.
 * Safe when either (or both) arguments are NULL. */
void wf_feedgen_skeleton_free(char *json, char *cursor);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_FEEDGEN_H */
