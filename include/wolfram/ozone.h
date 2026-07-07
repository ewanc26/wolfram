/*
 * ozone.h — moderation-service / labeler helper.
 *
 * A small, transport-backed convenience layer over the local atproto
 * `tools/ozone/*` lexicons. It reuses the low-level XRPC client
 * (wolfram/xrpc.h) rather than performing any network I/O itself, so all
 * real socket work stays isolated in xrpc.c per the transport-first rule.
 *
 * The module exposes:
 *   - wf_labeler_build_service_record : an in-memory builder for the
 *     app.bsky.labeler.service record (no network).
 *   - wf_ozone_report_subject         : file a moderation report
 *     (com.atproto.moderation.createReport).
 *   - wf_ozone_query_statuses         : tools.ozone.moderation.queryStatuses.
 *   - wf_ozone_get_label_defs         : tools.ozone.moderation.getLabelDefinitions.
 *
 * Ownership: every heap-allocated output is documented at its declaration and
 * has a matching free() (free() for the JSON string, wf_response_free() for
 * wf_response outputs). Nothing is freed on the caller's behalf.
 */

#ifndef WOLFRAM_OZONE_H
#define WOLFRAM_OZONE_H

#include "wolfram/xrpc.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NSIDs used by this module. */
#define WF_OZONE_QUERY_STATUSES_NSID       "tools.ozone.moderation.queryStatuses"
#define WF_OZONE_GET_LABEL_DEFS_NSID       "tools.ozone.moderation.getLabelDefinitions"
#define WF_OZONE_GET_SUGGESTIONS_NSID      "tools.ozone.moderation.getSuggestions"
#define WF_ATPROTO_CREATE_REPORT_NSID      "com.atproto.moderation.createReport"

/*
 * Build an `app.bsky.labeler.service` record value as a JSON string, in
 * memory. This never touches the network.
 *
 * `labeler_did` is the DID of the account that will run the labeler and is
 * required. `creator` (optional, may be NULL) is recorded on the service
 * record; `created_at` (optional, may be NULL) is an RFC 3339 timestamp —
 * when NULL the builder fills in the current UTC time.
 *
 * The record advertises a `policies.labelValueDefinitions` array referencing
 * well-known atproto label values (!no-unauthenticated, !hide, spam, porn,
 * sexual, nudity, ...). The caller may edit or extend these after building.
 *
 * On success returns a NUL-terminated JSON string owned by the caller; free
 * it with free(). Returns NULL on allocation failure or when `labeler_did` is
 * NULL.
 */
char *wf_labeler_build_service_record(const char *labeler_did,
                                      const char *creator,
                                      const char *created_at);

/*
 * File a moderation report about a repo or a specific record.
 *
 * `repo_did` identifies the subject account. When both `collection` and
 * `rkey` are non-NULL the subject is a
 * `com.atproto.repo.strongRef` (at://did/collection/rkey); otherwise the
 * subject is a `com.atproto.admin.defs#repoRef` (the whole account).
 *
 * `reason_type` is a com.atproto.moderation.defs#reasonType string (e.g.
 * "com.atproto.moderation.defs#reasonViolation"); `reason` is free-form
 * context (may be NULL).
 *
 * On WF_OK, *out is populated with the created report and must be released
 * with wf_response_free. Returns WF_ERR_INVALID_ARG for NULL client/did/
 * reason_type, or a transport error from wf_xrpc_procedure.
 *
 * NOTE: record subjects are built without a `cid`. Production callers that
 * need a verified record reference should extend the request JSON with the
 * record CID before sending.
 */
wf_status wf_ozone_report_subject(wf_xrpc_client *client,
                                  const char *repo_did,
                                  const char *collection,
                                  const char *rkey,
                                  const char *reason_type,
                                  const char *reason,
                                  wf_response *out);

/*
 * Query moderation subject statuses via tools.ozone.moderation.queryStatuses.
 *
 * `subjects` is an array of `n` atproto URIs (or DIDs) to fetch statuses for;
 * pass NULL/`n == 0` to query the default queue. Each subject is sent as a
 * repeated `subject` query parameter.
 *
 * On WF_OK, *out is populated and must be released with wf_response_free.
 * Returns WF_ERR_INVALID_ARG for NULL client/out, or a transport error.
 */
wf_status wf_ozone_query_statuses(wf_xrpc_client *client,
                                  const char **subjects,
                                  size_t n,
                                  wf_response *out);

/*
 * Fetch label value definitions via tools.ozone.moderation.getLabelDefinitions
 * (falling back to getSuggestions when definitions are unavailable). `uris` is
 * an array of `n` labeler service record URIs; each is sent as a repeated
 * `uris` query parameter.
 *
 * On WF_OK, *out is populated and must be released with wf_response_free.
 * Returns WF_ERR_INVALID_ARG for NULL client/out, or a transport error.
 */
wf_status wf_ozone_get_label_defs(wf_xrpc_client *client,
                                  const char **uris,
                                  size_t n,
                                  wf_response *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_OZONE_H */
