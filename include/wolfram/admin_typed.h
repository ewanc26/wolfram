/*
 * admin_typed.h — owned typed parsers for com.atproto.admin responses, plus
 * convenience agent wrappers for PDS / service administration.
 *
 * Centralizes parsing for the com.atproto.admin namespace, whose endpoints
 * return account views, subject-status views, and invite-code views:
 *   - com.atproto.admin.getAccountInfo     -> accountView (object, not wrapped)
 *   - com.atproto.admin.getAccountInfos    -> "infos"   (accountView[])
 *   - com.atproto.admin.searchAccounts     -> "accounts" (accountView[] + cursor)
 *   - com.atproto.admin.getSubjectStatus   -> {subject, takedown?, deactivated?}
 *   - com.atproto.admin.getInviteCodes     -> "codes"   (inviteCode[] + cursor)
 *
 * Conventions mirror actor_typed.h / feed_typed.h / graph_typed.h: wf_status
 * error codes, static strdup/set_string/reset helpers per translation unit,
 * ownership via cJSON_DetachItemFromObject / cJSON_DetachItemViaPointer, and a
 * matching `_free` for every owned struct (a freed/zeroed struct frees safely).
 *
 * NOTE: all admin operations require PDS admin credentials on the agent's
 * session. The agent wrappers below issue the calls against the agent's PDS
 * XRPC client and are only meaningful for an admin-authenticated session.
 */

#ifndef WOLFRAM_ADMIN_TYPED_H
#define WOLFRAM_ADMIN_TYPED_H

#include "wolfram/agent.h"
#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A single account view (com.atproto.admin.defs#accountView). Core fields are
 * copied as owned strings; any other fields present in the response are kept
 * as an owned detached cJSON subtree in `extra` so the parser stays bounded
 * regardless of the accountView shape. */
typedef struct wf_admin_account_view {
    char *did;
    char *handle;
    char *email;
    char *indexed_at;
    bool has_invites_disabled;
    bool invites_disabled;
    char *deactivated_at;
    cJSON *extra;          /* owned detached subtree of unknown fields; NULL absent */
} wf_admin_account_view;

/* A list of account views plus an optional cursor (getAccountInfos,
 * searchAccounts). */
typedef struct wf_admin_account_view_list {
    wf_admin_account_view *accounts;
    size_t account_count;
    char *cursor;
} wf_admin_account_view_list;

/* The service-specific admin status of a subject (account/record/blob).
 * `subject` keeps the full union object as an owned detached cJSON subtree;
 * `did` is a convenience copy when the subject is a repoRef ({did}). takedown
 * and deactivated status attributes are decoded into applied/ref (ref owned). */
typedef struct wf_admin_subject_status {
    cJSON *subject;        /* owned detached subject union; NULL absent on error */
    char *did;             /* convenience: subject.repoRef.did; NULL otherwise */
    bool has_takedown;
    bool takedown_applied;
    char *takedown_ref;
    bool has_deactivated;
    bool deactivated_applied;
    char *deactivated_ref;
} wf_admin_subject_status;

/* A single invite code view (com.atproto.server.defs#inviteCode). Core fields
 * are copied as owned scalars; the `uses` array and any other fields are kept
 * as an owned detached cJSON subtree in `extra`. */
typedef struct wf_admin_invite_code {
    char *code;
    bool has_available;
    int available;
    bool has_disabled;
    bool disabled;
    char *for_account;
    char *created_by;
    char *created_at;
    cJSON *extra;          /* owned detached subtree of unknown fields; NULL absent */
} wf_admin_invite_code;

/* A list of invite codes plus an optional cursor (getInviteCodes). */
typedef struct wf_admin_invite_code_list {
    wf_admin_invite_code *codes;
    size_t code_count;
    char *cursor;
} wf_admin_invite_code_list;

/* ---- Parsers (own their outputs; full cleanup on first error) ----
 * Return WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or a
 * missing/invalid shape, WF_ERR_ALLOC on allocation failure, WF_OK on success.
 * On any error `out` is left fully reset. */

/* Parse a getAccountInfo JSON body (the accountView object directly). */
wf_status wf_admin_parse_account_view(const char *json, size_t json_len,
                                      wf_admin_account_view *out);

/* Parse a getAccountInfos JSON body ("infos" array of accountView). */
wf_status wf_admin_parse_account_infos(const char *json, size_t json_len,
                                       wf_admin_account_view_list *out);

/* Parse a searchAccounts JSON body ("accounts" array of accountView + cursor). */
wf_status wf_admin_parse_search_accounts(const char *json, size_t json_len,
                                         wf_admin_account_view_list *out);

/* Parse a getSubjectStatus JSON body. */
wf_status wf_admin_parse_subject_status(const char *json, size_t json_len,
                                        wf_admin_subject_status *out);

/* Parse a getInviteCodes JSON body ("codes" array of inviteCode + cursor). */
wf_status wf_admin_parse_invite_codes(const char *json, size_t json_len,
                                      wf_admin_invite_code_list *out);

/* ---- Free functions (safe on a reset/zeroed struct) ---- */
void wf_admin_account_view_free(wf_admin_account_view *v);
void wf_admin_account_view_list_free(wf_admin_account_view_list *l);
void wf_admin_subject_status_free(wf_admin_subject_status *s);
void wf_admin_invite_code_free(wf_admin_invite_code *c);
void wf_admin_invite_code_list_free(wf_admin_invite_code_list *l);

/* ---- Agent convenience wrappers ----
 * Each issues the corresponding com.atproto.admin lex call against the agent's
 * PDS XRPC client (after syncing auth) and parses the body into `out`. On
 * success `out` is owned by the caller (free with the matching `_free`); on
 * error it is left reset. These are PDS-admin operations requiring admin
 * credentials on the agent session. Required inputs are validated and return
 * WF_ERR_INVALID_ARG when NULL/empty. */
wf_status wf_agent_admin_get_account_info(wf_agent *agent, const char *did,
                                          wf_admin_account_view *out);
wf_status wf_agent_admin_get_account_infos(wf_agent *agent,
                                           const char *const *dids, size_t n,
                                           wf_admin_account_view_list *out);
/* searchAccounts currently only supports filtering by `email` in the lexicon,
 * so `query` is mapped onto the email parameter. */
wf_status wf_agent_admin_search_accounts(wf_agent *agent, const char *query,
                                         int limit, const char *cursor,
                                         wf_admin_account_view_list *out);
wf_status wf_agent_admin_get_subject_status(wf_agent *agent, const char *did,
                                            wf_admin_subject_status *out);
wf_status wf_agent_admin_get_invite_codes(wf_agent *agent, const char *cursor,
                                          wf_admin_invite_code_list *out);
wf_status wf_agent_admin_update_subject_status(wf_agent *agent, const char *did,
                                               int moderated, int deactivated,
                                               const char *reason);
wf_status wf_agent_admin_update_account_handle(wf_agent *agent, const char *did,
                                               const char *new_handle);
wf_status wf_agent_admin_update_account_email(wf_agent *agent, const char *did,
                                              const char *email);
wf_status wf_agent_admin_update_account_password(wf_agent *agent,
                                                 const char *did,
                                                 const char *new_password);
wf_status wf_agent_admin_update_account_signing_key(wf_agent *agent,
                                                    const char *did,
                                                    const char *signing_key_multibase);
wf_status wf_agent_admin_delete_account(wf_agent *agent, const char *did);
wf_status wf_agent_admin_enable_account_invites(wf_agent *agent);
wf_status wf_agent_admin_disable_account_invites(wf_agent *agent);
/* Legacy disableInviteCodes wrapper. `cursor` has no wire field and must be
 * NULL. Prefer wf_agent_admin_disable_invite_codes_typed. */
wf_status wf_agent_admin_disable_invite_codes(wf_agent *agent,
                                              const char *cursor);
/* Exact disableInviteCodes input. Both arrays are optional, but a non-zero
 * count requires a non-NULL array whose entries are all non-NULL. */
wf_status wf_agent_admin_disable_invite_codes_typed(
    wf_agent *agent, const char *const *codes, size_t code_count,
    const char *const *accounts, size_t account_count);
wf_status wf_agent_admin_send_email(wf_agent *agent, const char *recipient_did,
                                    const char *content, const char *subject,
                                    const char *sender_did);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_ADMIN_TYPED_H */
