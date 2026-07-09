/*
 * temp_typed.h — owned typed parsers for the com.atproto.temp namespace
 * (account / signup helper flows) plus convenience agent wrappers.
 *
 * Endpoints covered (wire format per the atproto lexicon, authoritative):
 *   - com.atproto.temp.checkHandleAvailability  (query, params: handle[,email,birthDate])
 *   - com.atproto.temp.checkSignupQueue          (query, no params)
 *   - com.atproto.temp.fetchLabels               (query, params: since?,limit?)
 *   - com.atproto.temp.dereferenceScope          (query, params: scope)
 *   - com.atproto.temp.requestPhoneVerification  (procedure, input: phoneNumber)
 *   - com.atproto.temp.revokeAccountCredentials  (procedure, input: account)
 *   - com.atproto.temp.addReservedHandle         (procedure, input: handle)
 *
 * Conventions mirror feed_typed.h / actor_typed.h: wf_status error codes,
 * static strdup/set_string/reset helpers, ownership via cJSON_DetachItemFromObject,
 * and a matching `_free` for every owned struct. The raw generated lex wrappers
 * (wf_lex_com_atproto_temp_*) live in atproto_lex.h and are NOT edited here.
 *
 * NOTE on the agent wrappers: the lexicon input for revokeAccountCredentials is
 * `account` (an at-identifier), which the helper signature below does not carry;
 * that wrapper therefore returns WF_ERR_INVALID_ARG with a TODO rather than a
 * fabricated success (see AGENTS.md principle 3). checkSignupQueue's `closed`
 * out-param and fetchLabels' `did_pointers` are not part of the current lexicon
 * and are documented at their call sites.
 */

#ifndef WOLFRAM_TEMP_TYPED_H
#define WOLFRAM_TEMP_TYPED_H

#include "wolfram/agent.h"
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Owned typed result structs ----------------------------------------- */

/* A single suggested handle from checkHandleAvailability#resultUnavailable. */
typedef struct wf_temp_handle_suggestion {
    char *handle;                       /* suggested handle */
    char *method;                       /* opaque suggestion method */
} wf_temp_handle_suggestion;

/* Result of com.atproto.temp.checkHandleAvailability.
 * `available` is derived: 1 when the result union is resultAvailable, 0 when
 * resultUnavailable (in which case `suggestions` may be populated). `raw_result`
 * keeps the full union object as an owned detached cJSON subtree so callers can
 * inspect anything the parser does not surface explicitly. */
typedef struct wf_temp_check_handle_availability {
    char *handle;                       /* echo of the requested handle */
    int available;                      /* 1 available, 0 unavailable */
    wf_temp_handle_suggestion *suggestions;
    size_t suggestion_count;
    cJSON *raw_result;                  /* owned detached result union subtree */
} wf_temp_check_handle_availability;

/* Result of com.atproto.temp.checkSignupQueue. */
typedef struct wf_temp_check_signup_queue {
    int activated;                      /* required: account activated */
    int has_place_in_queue;
    int64_t place_in_queue;
    int has_estimated_time_ms;
    int64_t estimated_time_ms;
} wf_temp_check_signup_queue;

/* Result of com.atproto.temp.fetchLabels. `labels` owns the detached "labels"
 * array (items are com.atproto.label.defs#label); callers inspect via cJSON. */
typedef struct wf_temp_fetch_labels {
    cJSON *labels;                      /* owned detached "labels" array */
} wf_temp_fetch_labels;

/* Result of com.atproto.temp.dereferenceScope. */
typedef struct wf_temp_dereference_scope {
    char *scope;                        /* full oauth permission scope */
} wf_temp_dereference_scope;

/* --- Parsers (decode a raw JSON response body into owned structs) -------- */

/* parse JSON, required fields missing/invalid => WF_ERR_PARSE. On error `out`
 * is left reset. Free with wf_temp_check_handle_availability_free. */
wf_status wf_temp_check_handle_availability_parse(
    const char *json, size_t json_len,
    wf_temp_check_handle_availability *out);
void wf_temp_check_handle_availability_free(
    wf_temp_check_handle_availability *v);

/* parse JSON, required `activated` missing/invalid => WF_ERR_PARSE. Struct owns
 * no heap beyond scalars, so _free is a thin reset (provided for symmetry). */
wf_status wf_temp_check_signup_queue_parse(
    const char *json, size_t json_len,
    wf_temp_check_signup_queue *out);
void wf_temp_check_signup_queue_free(wf_temp_check_signup_queue *v);

/* parse JSON, detach the "labels" array (required) as owned cJSON. Missing
 * "labels" => WF_ERR_PARSE. Free with wf_temp_fetch_labels_free. */
wf_status wf_temp_fetch_labels_parse(
    const char *json, size_t json_len,
    wf_temp_fetch_labels *out);
void wf_temp_fetch_labels_free(wf_temp_fetch_labels *v);

/* parse JSON, required `scope` missing/invalid => WF_ERR_PARSE. Free with
 * wf_temp_dereference_scope_free. */
wf_status wf_temp_dereference_scope_parse(
    const char *json, size_t json_len,
    wf_temp_dereference_scope *out);
void wf_temp_dereference_scope_free(wf_temp_dereference_scope *v);

/* --- Write-side result structs (com.atproto.temp procedures) ------------- */

/* Result of com.atproto.temp.addReservedHandle. The lexicon declares an empty
 * output object, so `ok` is 1 on a successful parse. The `handle` field is an
 * optional echo captured when the server includes one (it is not required by
 * the lexicon and may be NULL). The caller owns `handle`. */
typedef struct wf_temp_add_reserved_handle_result {
    int ok;               /* 1 when the (empty) output parsed successfully */
    char *handle;         /* optional echoed handle; owned, may be NULL */
} wf_temp_add_reserved_handle_result;

/* Result of com.atproto.temp.requestPhoneVerification. The lexicon declares no
 * output, so `ok` is 1 on a successful (empty) parse. */
typedef struct wf_temp_request_phone_verification_result {
    int ok;               /* 1 when the empty output parsed successfully */
} wf_temp_request_phone_verification_result;

/* Result of com.atproto.temp.revokeAccountCredentials. The lexicon declares no
 * output, so `ok` is 1 on a successful (empty) parse. */
typedef struct wf_temp_revoke_account_credentials_result {
    int ok;               /* 1 when the empty output parsed successfully */
} wf_temp_revoke_account_credentials_result;

/* --- Write-side parsers -------------------------------------------------- */

/* parse JSON (expected empty object). `ok` is set to 1 on success; an optional
 * echoed "handle" string is captured if present. Missing/invalid JSON =>
 * WF_ERR_PARSE. Free with wf_temp_add_reserved_handle_result_free. */
wf_status wf_temp_add_reserved_handle_parse(
    const char *json, size_t json_len,
    wf_temp_add_reserved_handle_result *out);
void wf_temp_add_reserved_handle_result_free(
    wf_temp_add_reserved_handle_result *v);

/* parse JSON (expected empty object). `ok` is set to 1 on success. Missing/
 * invalid JSON => WF_ERR_PARSE. Free with
 * wf_temp_request_phone_verification_result_free. */
wf_status wf_temp_request_phone_verification_parse(
    const char *json, size_t json_len,
    wf_temp_request_phone_verification_result *out);
void wf_temp_request_phone_verification_result_free(
    wf_temp_request_phone_verification_result *v);

/* parse JSON (expected empty object). `ok` is set to 1 on success. Missing/
 * invalid JSON => WF_ERR_PARSE. Free with
 * wf_temp_revoke_account_credentials_result_free. */
wf_status wf_temp_revoke_account_credentials_parse(
    const char *json, size_t json_len,
    wf_temp_revoke_account_credentials_result *out);
void wf_temp_revoke_account_credentials_result_free(
    wf_temp_revoke_account_credentials_result *v);

/* --- Agent convenience wrappers ----------------------------------------- */

/* Issue checkHandleAvailability and report only availability via out_available
 * (1 available, 0 unavailable). Returns WF_ERR_INVALID_ARG on NULL/empty handle
 * or out pointer. */
wf_status wf_agent_check_handle_availability(wf_agent *agent,
                                             const char *handle,
                                             int *out_available);

/* Issue checkSignupQueue. out_activated = activated; out_place receives a
 * freshly allocated string of the place-in-queue (NULL when absent); out_closed
 * is reserved (the current lexicon exposes no queue-closed flag) and is always
 * set to 0. The caller owns *out_place (free with free()). Returns
 * WF_ERR_INVALID_ARG on NULL out pointers. */
wf_status wf_agent_check_signup_queue(wf_agent *agent,
                                      int *out_activated,
                                      int *out_closed,
                                      char **out_place);

/* Issue fetchLabels against the authenticated labeler. `did_pointers`/`count`
 * are accepted for parity but the current lexicon has no DID selector, so they
 * are currently unused. On success *out_labels is an owned cJSON array (free with
 * cJSON_Delete). Returns WF_ERR_INVALID_ARG on NULL out pointer. */
wf_status wf_agent_fetch_labels(wf_agent *agent,
                                const char *const *did_pointers,
                                size_t count,
                                cJSON **out_labels);

/* Issue requestPhoneVerification. `code` is accepted for parity but the
 * lexicon input is `phoneNumber` only, so `code` is currently unused. Returns
 * WF_ERR_INVALID_ARG on NULL/empty phone_number. */
wf_status wf_agent_request_phone_verification(wf_agent *agent,
                                             const char *phone_number,
                                             const char *code);

/* Issue revokeAccountCredentials. The atproto lexicon requires an `account`
 * (at-identifier) input that this helper's signature does not carry, so this is
 * an honest stub returning WF_ERR_INVALID_ARG with a TODO (AGENTS.md #3). */
wf_status wf_agent_revoke_account_credentials(wf_agent *agent,
                                               const char *code,
                                               const char *name,
                                               const char *description);

/* Issue revokeAccountCredentials carrying the lexicon-required `account`
 * (at-identifier) input. Returns wf_status only (the procedure returns no
 * body). */
wf_status wf_agent_revoke_account_credentials_typed(wf_agent *agent,
                                                    const char *account);

/* Issue addReservedHandle. Returns WF_ERR_INVALID_ARG on NULL/empty handle. */
wf_status wf_agent_add_reserved_handle(wf_agent *agent, const char *handle);

/* Issue dereferenceScope and return the dereferenced full scope string in
 * *out_did (the lexicon returns the full oauth permission `scope`; the helper
 * names it out_did for parity). Caller owns *out_did (free with free()).
 * Returns WF_ERR_INVALID_ARG on NULL/empty scope or out pointer. */
wf_status wf_agent_dereference_scope(wf_agent *agent,
                                      const char *scope,
                                      char **out_did);

/* Issue addReservedHandle (procedure, input: handle). *out is filled with the
 * parsed result (ok=1 on success, optional echoed handle captured when present;
 * caller frees via wf_temp_add_reserved_handle_result_free). Returns
 * WF_ERR_INVALID_ARG on NULL/empty handle, NULL agent/out. */
wf_status wf_agent_temp_add_reserved_handle_typed(
    wf_agent *agent, const char *handle,
    wf_temp_add_reserved_handle_result *out);

/* Issue requestPhoneVerification (procedure, input: phoneNumber). *out is
 * filled with the parsed result (ok=1 on success); caller frees via
 * wf_temp_request_phone_verification_result_free. Returns WF_ERR_INVALID_ARG on
 * NULL/empty phone_number, NULL agent/out. */
wf_status wf_agent_temp_request_phone_verification_typed(
    wf_agent *agent, const char *phone_number,
    wf_temp_request_phone_verification_result *out);

/* Issue revokeAccountCredentials (procedure, input: account at-identifier).
 * *out is filled with the parsed result (ok=1 on success); caller frees via
 * wf_temp_revoke_account_credentials_result_free. Returns WF_ERR_INVALID_ARG on
 * NULL/empty account, NULL agent/out. */
wf_status wf_agent_temp_revoke_account_credentials_typed(
    wf_agent *agent, const char *account,
    wf_temp_revoke_account_credentials_result *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_TEMP_TYPED_H */
