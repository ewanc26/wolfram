/*
 * server_typed.h — owned typed parsers + agent convenience wrappers for the
 * com.atproto.server session/account endpoints not yet exposed in a typed,
 * owned form.
 *
 * Conventions mirror labeler_typed.h / ozone_typed.h: wf_status error codes,
 * static strdup/set_string/reset helpers, ownership via cJSON_DetachItem*, an
 * owned `extra` (and where relevant `did_doc`) cJSON subtree for open/unbounded
 * fields, and a matching `_free` for every owned struct (a freed/zeroed struct
 * frees safely). Every owned string is heap-allocated.
 *
 * Parsers (wf_server_parse_*):
 *   - wf_server_parse_session_info    com.atproto.server.getSession output
 *   - wf_server_parse_account_status  com.atproto.server.checkAccountStatus out
 *   - wf_server_parse_invite_codes    com.atproto.server.getAccountInviteCodes
 *   - wf_server_parse_auth_token      com.atproto.server.getServiceAuth /
 *                                     reserveSigningKey output (token)
 *   - wf_server_parse_session_tokens  com.atproto.server.createSession /
 *                                     refreshSession output
 *   - wf_server_parse_email_update    com.atproto.server.requestEmailUpdate out
 *
 * Agent wrappers (wf_agent_*_typed): thin convenience calls layered on the
 * generated lex wrappers after syncing auth onto the agent's primary XRPC
 * client.
 */

#ifndef WOLFRAM_SERVER_TYPED_H
#define WOLFRAM_SERVER_TYPED_H

#include "wolfram/agent.h"

#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* com.atproto.server.getSession output. Core fields are copied; `did_doc`
 * holds the owned detached didDoc subtree and `extra` holds any other unknown
 * fields, both NULL when absent. */
typedef struct wf_server_session_info {
    char *did;
    char *handle;
    bool has_email;
    char *email;
    bool has_email_confirmed;
    bool email_confirmed;
    bool has_active;
    bool active;
    bool has_status;
    char *status;
    cJSON *did_doc;          /* owned detached didDoc subtree; NULL absent */
    cJSON *extra;            /* owned detached subtree of remaining fields */
} wf_server_session_info;

/* com.atproto.server.checkAccountStatus output. */
typedef struct wf_server_account_status {
    bool has_activated;
    bool activated;
    bool has_valid_did;
    bool valid_did;
    char *repo_commit;
    char *repo_rev;
    bool has_repo_blocks;
    int64_t repo_blocks;
    bool has_indexed_records;
    int64_t indexed_records;
    bool has_private_state_values;
    int64_t private_state_values;
    bool has_expected_blobs;
    int64_t expected_blobs;
    bool has_imported_blobs;
    int64_t imported_blobs;
} wf_server_account_status;

/* A single invite code (com.atproto.server.defs#inviteCode). Core fields are
 * copied; the `uses` array (and any other fields) remain in the owned `extra`
 * cJSON subtree (NULL when absent). */
typedef struct wf_server_invite_code {
    char *code;
    bool has_available;
    int64_t available;
    bool has_disabled;
    bool disabled;
    char *for_account;
    char *created_by;
    char *created_at;
    cJSON *extra;            /* owned detached subtree of unknown fields */
} wf_server_invite_code;

typedef struct wf_server_invite_code_list {
    wf_server_invite_code *codes;
    size_t code_count;
} wf_server_invite_code_list;

/* A service-auth / reserved-signing-key token (com.atproto.server.getServiceAuth
 * and reserveSigningKey output). */
typedef struct wf_server_auth_token {
    char *token;
} wf_server_auth_token;

/* com.atproto.server.createSession / refreshSession output. Core fields are
 * copied; `did_doc` holds the owned detached didDoc subtree and `extra` holds
 * any other unknown fields, both NULL when absent. */
typedef struct wf_server_session_tokens {
    char *access_jwt;
    char *refresh_jwt;
    char *handle;
    char *did;
    bool has_email;
    char *email;
    bool has_email_confirmed;
    bool email_confirmed;
    bool has_active;
    bool active;
    bool has_status;
    char *status;
    cJSON *did_doc;          /* owned detached didDoc subtree; NULL absent */
    cJSON *extra;            /* owned detached subtree of remaining fields */
} wf_server_session_tokens;

/* com.atproto.server.requestEmailUpdate output. */
typedef struct wf_server_email_update_request {
    bool token_required;
} wf_server_email_update_request;

/* ---- Parsers (own their outputs; full cleanup on first error) ----
 * Return WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or a
 * missing/invalid shape, WF_ERR_ALLOC on allocation failure, WF_OK on success.
 * On any error `out` is left fully reset. */

wf_status wf_server_parse_session_info(const char *json, size_t json_len,
                                       wf_server_session_info *out);
wf_status wf_server_parse_account_status(const char *json, size_t json_len,
                                         wf_server_account_status *out);
wf_status wf_server_parse_invite_codes(const char *json, size_t json_len,
                                       wf_server_invite_code_list *out);
wf_status wf_server_parse_auth_token(const char *json, size_t json_len,
                                     wf_server_auth_token *out);
wf_status wf_server_parse_session_tokens(const char *json, size_t json_len,
                                         wf_server_session_tokens *out);
wf_status wf_server_parse_email_update(const char *json, size_t json_len,
                                       wf_server_email_update_request *out);

/* ---- Free functions (safe on a reset/zeroed struct) ---- */
void wf_server_session_info_free(wf_server_session_info *s);
void wf_server_account_status_free(wf_server_account_status *s);
void wf_server_invite_code_list_free(wf_server_invite_code_list *l);
void wf_server_auth_token_free(wf_server_auth_token *t);
void wf_server_session_tokens_free(wf_server_session_tokens *s);
void wf_server_email_update_request_free(wf_server_email_update_request *r);

/* ---- Agent convenience wrappers ----
 * Each issues the corresponding lex call against the agent's primary XRPC
 * client (after syncing auth) and parses the body into `out`. On success `out`
 * is owned by the caller (free with the matching `_free`); on error it is left
 * reset. Required inputs are validated and return WF_ERR_INVALID_ARG when
 * NULL/empty. */

/* com.atproto.server.getSession */
wf_status wf_agent_get_session_typed(wf_agent *agent,
                                    wf_server_session_info *out);

/* com.atproto.server.getServiceAuth. `exp_or_0` <= 0 means "omit exp";
 * `lxm_or_null` may be NULL to omit the lxm binding. */
wf_status wf_agent_get_service_auth_typed(wf_agent *agent, const char *aud,
                                         int64_t exp_or_0,
                                         const char *lxm_or_null,
                                         wf_server_auth_token *out);

/* com.atproto.server.checkAccountStatus */
wf_status wf_agent_check_account_status_typed(wf_agent *agent,
                                             wf_server_account_status *out);

/* com.atproto.server.getAccountInviteCodes */
wf_status wf_agent_get_account_invite_codes_typed(wf_agent *agent,
                                                 int include_used,
                                                 int create_available,
                                                 wf_server_invite_code_list *out);

/* com.atproto.server.reserveSigningKey. `did_or_null` may be NULL. */
wf_status wf_agent_reserve_signing_key_typed(wf_agent *agent,
                                            const char *did_or_null,
                                            wf_server_auth_token *out);

/* com.atproto.server.requestAccountDelete (procedure, no output) */
wf_status wf_agent_request_account_delete_typed(wf_agent *agent);

/* com.atproto.server.requestEmailUpdate */
wf_status wf_agent_request_email_update_typed(
    wf_agent *agent, wf_server_email_update_request *out);

/* com.atproto.server.requestEmailConfirmation (procedure, no output) */
wf_status wf_agent_request_email_confirmation_typed(wf_agent *agent);

/* com.atproto.server.requestPasswordReset (procedure, input {email}) */
wf_status wf_agent_request_password_reset_typed(wf_agent *agent,
                                               const char *email);

/* com.atproto.server.createSession */
wf_status wf_agent_create_session_typed(wf_agent *agent, const char *identifier,
                                       const char *password,
                                       const char *auth_factor_token_or_null,
                                       wf_server_session_tokens *out);

/* com.atproto.server.refreshSession */
wf_status wf_agent_refresh_session_typed(wf_agent *agent,
                                        wf_server_session_tokens *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SERVER_TYPED_H */
