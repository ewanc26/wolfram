/*
 * moderation_report_typed.h — typed (owned-struct) parser + agent wrapper for
 * com.atproto.moderation.createReport (user-facing content/account reporting).
 *
 * Mirrors the conventions of actor_typed.h / chat_typed.h: owned strings,
 * an owned cJSON `subject` subtree, a matching
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

/* Owned current createReport output envelope. */
typedef struct wf_moderation_report_record {
    int64_t id;
    char *reason_type;
    char *reason;
    cJSON *subject;
    char *reported_by;
    char *created_at;
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
 * Legacy convenience wrapper. `subject_uri` is either a DID (repoRef) or an
 * AT URI (strongRef, requiring `subject_cid`). `reason_subject_uri` has no
 * wire field and must be NULL.
 *
 * Returns WF_ERR_INVALID_ARG if `agent`, `subject_uri`, or `reason_type` is
 * NULL or empty. */
wf_status wf_agent_report(wf_agent *agent, const char *subject_uri,
                          const char *subject_cid, const char *reason_type,
                          const char *reason_subject_uri,
                          wf_moderation_report_record *out);

/* Exact createReport wrapper. Supply exactly one subject form: subject_did,
 * or subject_uri plus subject_cid. Optional mod_tool_meta_json requires a
 * non-empty mod_tool_name and must encode one JSON value. */
wf_status wf_agent_report_typed(wf_agent *agent,
                                const char *subject_did,
                                const char *subject_uri,
                                const char *subject_cid,
                                const char *reason_type,
                                const char *reason,
                                const char *mod_tool_name,
                                const char *mod_tool_meta_json,
                                wf_moderation_report_record *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_MODERATION_REPORT_TYPED_H */
