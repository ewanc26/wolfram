/*
 * label_typed.h — owned typed parser for com.atproto.label query/get
 * responses, plus an agent convenience wrapper that persists fetched labels.
 *
 * `wf_label_parse_query` parses the raw JSON body returned by
 * `com.atproto.label.queryLabels` (and the structurally identical
 * `com.atproto.label.getLabels`) into an owned array of `wf_mod_label`
 * structs. Callers free the result with `wf_mod_labels_free` (moderation.h).
 *
 * This mirrors the typed-parser conventions used by feed_typed.h:
 *   - `wf_status` error codes (WF_ERR_INVALID_ARG, WF_ERR_PARSE,
 *     WF_ERR_ALLOC, WF_OK)
 *   - ownership via cJSON_DetachItemFromObject / strdup
 *   - full cleanup on the first error (no partial leaks)
 *
 * `wf_agent_query_labels_typed` issues queryLabels through the agent's
 * authenticated client, parses the response, and persists each label into the
 * agent's attached store (when WOLFRAM_BUILD_STORE is enabled). The parsed
 * labels are returned in `out`/`out_count` regardless of persistence.
 *
 * `wf_label_to_mod_label` / `wf_label_parse_subscribe` bridge the
 * `com.atproto.label.subscribeLabels` stream into the moderation engine's
 * `wf_mod_label` representation. `wf_label_to_mod_label` converts a single
 * parsed #label (as produced by the subscribeLabels decoder / `wf_label`)
 * into an owned `wf_mod_label`; `wf_label_parse_subscribe` parses a whole
 * `#labels` frame into an owned array. `wf_agent_subscribe_labels_typed`
 * is the agent-level consumption wrapper: it resolves the labeler service,
 * syncs auth, and delegates to the streaming `wf_label_subscribe_start`,
 * dispatching each label to the caller as an owned `wf_mod_label`.
 */

#ifndef WOLFRAM_LABEL_TYPED_H
#define WOLFRAM_LABEL_TYPED_H

#include "wolfram/agent.h"
#include "wolfram/label.h"
#include "wolfram/moderation.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parse a queryLabels/getLabels JSON body into owned wf_mod_label structs.
 * Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON
 * or a missing `labels` array, WF_ERR_ALLOC on allocation failure, WF_OK on
 * success. On success `out`/`out_count` own caller-freed memory (pass to
 * wf_mod_labels_free); on error they are left zeroed. */
wf_status wf_label_parse_query(const char *json, size_t len,
                               wf_mod_label **out, size_t *out_count);

/* Fetch labels through an agent's authenticated client, parse them, and
 * persist each into the agent's attached store (best-effort, when a store is
 * attached). `out`/`out_count` receive the parsed labels (always owned by the
 * caller and freed with wf_mod_labels_free) regardless of persistence. NULL
 * agent/out or a NULL/empty `uris` returns WF_ERR_INVALID_ARG. */
wf_status wf_agent_query_labels_typed(wf_agent *agent,
                                       const char *const *uris, size_t uri_count,
                                       const char *const *sources,
                                       size_t source_count,
                                       wf_mod_label **out, size_t *out_count);

/* Convert one parsed com.atproto.label.defs#label (as produced by the
 * subscribeLabels decoder and surfaced through `wf_label`) into an owned
 * `wf_mod_label` suitable for the moderation engine. `dst` must be
 * zero-initialised by the caller; on WF_OK every non-NULL pointer it holds
 * is heap-owned and freed either field-by-field or via wf_mod_labels_free.
 *
 * Returns WF_ERR_INVALID_ARG on NULL inputs or a label with no `val` (not
 * representable); WF_ERR_ALLOC on allocation failure (`dst` is left zeroed);
 * WF_OK otherwise. Optional fields are carried across: `cid`/`has_cid`,
 * `exp`, `neg`, and `ver` (truncated to `int`); `sig` has no moderation
 * representation and is dropped. */
wf_status wf_label_to_mod_label(const wf_label *src, wf_mod_label *dst);

/* Parse a com.atproto.label.subscribeLabels `#labels` frame (the JSON streamed
 * over the WebSocket, e.g. a wf_label_message of type WF_LABEL_MESSAGE_LABELS)
 * into an owned array of wf_mod_label structs. Mirrors wf_label_parse_query but
 * accepts the streaming frame shape `{ "seq": N, "labels": [ ... ] }`. Labels
 * without a `val` are skipped (not representable).
 *
 * Error semantics mirror the underlying frame parser: WF_ERR_INVALID_ARG on
 * NULL inputs or a JSON object with no recognised `$type`; WF_ERR_PARSE on
 * malformed JSON or when the recognised frame is not a `#labels` message;
 * WF_ERR_ALLOC on allocation failure. WF_OK on success. On success
 * `out`/`out_count` own caller-freed memory (release with wf_mod_labels_free);
 * on error they are left zeroed. */
wf_status wf_label_parse_subscribe(const char *json, size_t len,
                                  wf_mod_label **out, size_t *out_count);

/* Agent convenience: subscribe to a labeler's label stream through the agent.
 *
 * `service` is the labeler's base service URL (http/https/ws/wss) OR a `did:`
 * that the agent resolves to its `AtprotoLabeler` service endpoint via its
 * identity client. `cursor`/`has_cursor` resume from the last seen sequence
 * number. Each label is dispatched to `on_label` as an owned `wf_mod_label`
 * (freed by the wrapper immediately after the callback returns).
 *
 * The call blocks until the subscription is stopped (wf_label_subscribe_stop), a
 * fatal initial-connect error occurs, or the service closes cleanly. Returns
 * WF_ERR_INVALID_ARG on NULL agent/service/on_label or a negative cursor;
 * transport/connection errors propagate from wf_label_subscribe_start. The
 * agent's auth is synced (wf_agent_sync_auth) before connecting. */
typedef void (*wf_agent_label_sub_cb)(const wf_mod_label *label, void *userdata);
wf_status wf_agent_subscribe_labels_typed(wf_agent *agent,
    const char *service, int64_t cursor, int has_cursor,
    wf_agent_label_sub_cb on_label, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_LABEL_TYPED_H */
