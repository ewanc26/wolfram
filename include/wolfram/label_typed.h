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
 */

#ifndef WOLFRAM_LABEL_TYPED_H
#define WOLFRAM_LABEL_TYPED_H

#include "wolfram/agent.h"
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

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_LABEL_TYPED_H */
