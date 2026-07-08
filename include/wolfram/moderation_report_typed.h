/*
 * moderation_report_typed.h — typed (owned-struct) parser + agent wrapper for
 * com.atproto.moderation.createReport (user-facing content/account reporting).
 *
 * Mirrors the conventions of actor_typed.h / chat_typed.h: owned strings,
 * an owned cJSON `value` subtree detached from the record, a matching
 * `_free`, and full cleanup on the first parse error. The agent wrapper
 * performs auth via the agent's primary XRPC client and hands the response
 * body to wf_moderation_report_record_parse.
 */

#ifndef WOLFRAM_MODERATION_REPORT_TYPED_H
#define WOLFRAM_MODERATION_REPORT_TYPED_H

#include <stddef.h>

#include "wolfram/agent.h"

#include <cJSON.h>

#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A parsed com.atproto.moderation report record, viewed as a stored record
 * (mirrors the older createReport wire shape). `uri`/`cid` are heap-owned
 * strings (free with wf_moderation_report_record_free); `value` is an owned
 * cJSON subtree detached from the record (also freed by
 * wf_moderation_report_record_free).
 *
 * NOTE: this is intentionally distinct from the `wf_moderation_report_record` type in
 * moderation_actions.h, which exposes the current createReport output envelope
 * (id / reason / subject_uri). This record view keeps the verbatim record. */
typedef struct wf_moderation_report_record {
    char *uri;
    char *cid;
    cJSON *value; /* owned record subtree */
} wf_moderation_report_record;

/* Parse a com.atproto.moderation report record from `json` (length `len`).
 * On WF_OK, `out` owns its fields and must be released with
 * wf_moderation_report_record_free. On any error the output is fully reset. */
wf_status wf_moderation_report_record_parse(const char *json, size_t len,
                                            wf_moderation_report_record *out);

/* Release a report produced by wf_moderation_report_record_parse. Safe to call
 * on a zero-initialised or already-freed report. */
void wf_moderation_report_record_free(wf_moderation_report_record *r);

/* Submit a moderation report on behalf of the logged-in agent.
 *
 * `subject_uri` is the AT-URI of the reported record (or account); when
 * `subject_cid` is non-NULL/non-empty it is included (strongRef form
 * {uri, cid}), otherwise the subject is sent as {uri}. `reason_type` is the
 * reason type token (e.g. "com.atproto.moderation.defs#reasonSpam"). The
 * response body is parsed into `out`.
 *
 * Returns WF_ERR_INVALID_ARG if `agent`, `subject_uri`, or `reason_type` is
 * NULL or empty. */
wf_status wf_agent_report(wf_agent *agent, const char *subject_uri,
                          const char *subject_cid, const char *reason_type,
                          const char *reason_subject_uri,
                          wf_moderation_report_record *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_MODERATION_REPORT_TYPED_H */
