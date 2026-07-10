/**
 * server.h — AT Protocol server account management.
 *
 * Thin wrappers over the com.atproto.server lexicon endpoints: server
 * description, account creation, app-password management, and account
 * lifecycle operations (delete / password reset). These are low-level
 * XRPC calls that build and parse JSON directly.
 *
 * Every heap-allocated output has a matching `_free` function documented
 * next to it. Output strings are owned by the caller and must be released
 * with the appropriate free function. The free functions are safe to call
 * with NULL.
 */

#ifndef WOLFRAM_SERVER_H
#define WOLFRAM_SERVER_H

#include "wolfram/wolfram.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Output of com.atproto.server.describeServer. */
typedef struct wf_server_description {
    char  *did;                       /* server DID */
    int    invite_code_required;      /* -1 if not present in response */
    int    phone_verification_required; /* -1 if not present in response */
    char **available_user_domains;    /* NULL if none; array of domain strings */
    size_t available_user_domains_count;
    char  *links_privacy_policy;      /* NULL if not present */
    char  *links_terms_of_service;    /* NULL if not present */
    char  *contact_email;             /* NULL if not present */
} wf_server_description;

/**
 * Fetch server metadata.
 * Calls GET com.atproto.server.describeServer.
 *
 * On WF_OK, `out` is populated and must be released with
 * wf_server_describe_free.
 */
wf_status wf_server_describe(wf_xrpc_client *client, wf_server_description *out);

/** Free a wf_server_description. Safe to call with NULL. */
void wf_server_describe_free(wf_server_description *desc);

/** Input for com.atproto.server.createAccount. */
typedef struct wf_server_create_account_input {
    const char *handle;          /* required */
    const char *email;           /* optional */
    const char *password;        /* optional */
    const char *invite_code;     /* optional */
    const char *recovery_key;    /* optional */
} wf_server_create_account_input;

/** Output of com.atproto.server.createAccount. */
typedef struct wf_server_create_account_result {
    char *access_jwt;
    char *refresh_jwt;
    char *handle;
    char *did;
    char *did_doc;          /* raw JSON string of the DID document */
} wf_server_create_account_result;

/**
 * Create a new account.
 * Calls POST com.atproto.server.createAccount.
 *
 * On WF_OK, `out` is populated and must be released with
 * wf_server_create_account_result_free.
 */
wf_status wf_server_create_account(wf_xrpc_client *client,
                                   const wf_server_create_account_input *input,
                                   wf_server_create_account_result *out);

/** Free a wf_server_create_account_result. Safe to call with NULL. */
void wf_server_create_account_result_free(wf_server_create_account_result *result);

/** Input for com.atproto.server.createAppPassword. */
typedef struct wf_server_create_app_password_input {
    const char *name;       /* required */
    int         privileged; /* optional; non-zero requests a privileged password */
} wf_server_create_app_password_input;

/** A single app password. */
typedef struct wf_server_app_password {
    char *name;
    char *password;     /* NULL for entries from listAppPasswords (never returned) */
    char *created_at;
    int   privileged;   /* -1 if not present in response */
} wf_server_app_password;

/**
 * Create a new app password.
 * Calls POST com.atproto.server.createAppPassword.
 *
 * On WF_OK, `out` is populated and must be released with
 * wf_server_app_password_free.
 */
wf_status wf_server_create_app_password(wf_xrpc_client *client,
                                        const wf_server_create_app_password_input *input,
                                        wf_server_app_password *out);

/** Free a wf_server_app_password. Safe to call with NULL. */
void wf_server_app_password_free(wf_server_app_password *pwd);

/** Output of com.atproto.server.listAppPasswords. */
typedef struct wf_server_app_password_list {
    wf_server_app_password *passwords;
    size_t                  password_count;
} wf_server_app_password_list;

/**
 * List existing app passwords.
 * Calls GET com.atproto.server.listAppPasswords.
 *
 * On WF_OK, `out` is populated and must be released with
 * wf_server_app_password_list_free.
 */
wf_status wf_server_list_app_passwords(wf_xrpc_client *client,
                                       wf_server_app_password_list *out);

/** Free a wf_server_app_password_list. Safe to call with NULL. */
void wf_server_app_password_list_free(wf_server_app_password_list *list);

/** Input for com.atproto.server.revokeAppPassword. */
typedef struct wf_server_revoke_app_password_input {
    const char *name;   /* required */
} wf_server_revoke_app_password_input;

/**
 * Revoke an app password by name.
 * Calls POST com.atproto.server.revokeAppPassword.
 */
wf_status wf_server_revoke_app_password(wf_xrpc_client *client,
                                        const wf_server_revoke_app_password_input *input);

/** Input for com.atproto.server.deleteAccount. */
typedef struct wf_server_delete_account_input {
    const char *did;         /* required */
    const char *password;    /* required */
    const char *token;       /* required; email confirmation token */
} wf_server_delete_account_input;

/**
 * Delete the authenticated account.
 * Calls POST com.atproto.server.deleteAccount.
 */
wf_status wf_server_delete_account(wf_xrpc_client *client,
                                   const wf_server_delete_account_input *input);

/** Input for com.atproto.server.requestPasswordReset. */
typedef struct wf_server_request_password_reset_input {
    const char *email;   /* required */
} wf_server_request_password_reset_input;

/**
 * Request an account password reset.
 * Calls POST com.atproto.server.requestPasswordReset.
 */
wf_status wf_server_request_password_reset(wf_xrpc_client *client,
                                           const wf_server_request_password_reset_input *input);

/** Input for com.atproto.server.resetPassword. */
typedef struct wf_server_reset_password_input {
    const char *reset_token;  /* required */
    const char *new_password; /* required */
} wf_server_reset_password_input;

/**
 * Reset an account password using a reset token.
 * Calls POST com.atproto.server.resetPassword.
 */
wf_status wf_server_reset_password(wf_xrpc_client *client,
                                    const wf_server_reset_password_input *input);

/* ------------------------------------------------------------------ */
/* Additional com.atproto.server account-management operations.         */
/* ------------------------------------------------------------------ */

/** Input for com.atproto.server.createInviteCode. */
typedef struct wf_server_create_invite_code_input {
    int64_t use_count;       /* required; how many times the code may be used */
    const char *for_account; /* optional; restrict to a specific account DID */
} wf_server_create_invite_code_input;

/** Output of com.atproto.server.createInviteCode. */
typedef struct wf_server_create_invite_code_result {
    char *code; /* the generated invite code */
} wf_server_create_invite_code_result;

/**
 * Create a single invite code.
 * Calls POST com.atproto.server.createInviteCode.
 *
 * On WF_OK, `out` is populated and must be released with
 * wf_server_create_invite_code_result_free.
 */
wf_status wf_server_create_invite_code(wf_xrpc_client *client,
                                       const wf_server_create_invite_code_input *input,
                                       wf_server_create_invite_code_result *out);

/** Free a wf_server_create_invite_code_result. Safe to call with NULL. */
void wf_server_create_invite_code_result_free(wf_server_create_invite_code_result *result);

/** Input for com.atproto.server.createInviteCodes. */
typedef struct wf_server_create_invite_codes_input {
    int64_t code_count;  /* required; number of codes to generate */
    int64_t use_count;   /* required; uses allowed per code */
    const char *const *for_accounts; /* optional; restrict each code to accounts */
    size_t for_accounts_count;
} wf_server_create_invite_codes_input;

/** Per-account code set of com.atproto.server.createInviteCodes. */
typedef struct wf_server_invite_codes_for_account {
    char **codes;        /* NULL if none; array of code strings */
    size_t code_count;
    char *account;       /* the account DID these codes are for, or NULL */
} wf_server_invite_codes_for_account;

/** Output of com.atproto.server.createInviteCodes. */
typedef struct wf_server_create_invite_codes_result {
    wf_server_invite_codes_for_account *accounts; /* NULL if none */
    size_t account_count;
} wf_server_create_invite_codes_result;

/**
 * Create multiple invite codes, optionally for several accounts.
 * Calls POST com.atproto.server.createInviteCodes.
 *
 * On WF_OK, `out` is populated and must be released with
 * wf_server_create_invite_codes_result_free.
 */
wf_status wf_server_create_invite_codes(wf_xrpc_client *client,
                                        const wf_server_create_invite_codes_input *input,
                                        wf_server_create_invite_codes_result *out);

/** Free a wf_server_create_invite_codes_result. Safe to call with NULL. */
void wf_server_create_invite_codes_result_free(wf_server_create_invite_codes_result *result);

/** Input for com.atproto.server.revokeInviteCodes. */
typedef struct wf_server_revoke_invite_codes_input {
    char *const *codes; /* required; codes to revoke (array of strings) */
    size_t code_count;
} wf_server_revoke_invite_codes_input;

/**
 * Revoke invite codes.
 * Calls POST com.atproto.server.revokeInviteCodes (no generated wrapper;
 * built by hand against the wire spec).
 */
wf_status wf_server_revoke_invite_codes(wf_xrpc_client *client,
                                        const wf_server_revoke_invite_codes_input *input);

/** Activate the authenticated account. Calls POST com.atproto.server.activateAccount. */
wf_status wf_server_activate_account(wf_xrpc_client *client);

/** Input for com.atproto.server.deactivateAccount. */
typedef struct wf_server_deactivate_account_input {
    const char *delete_after; /* optional; RFC 3339 timestamp recommendation */
} wf_server_deactivate_account_input;

/** Deactivate the authenticated account. Calls POST com.atproto.server.deactivateAccount. */
wf_status wf_server_deactivate_account(wf_xrpc_client *client,
                                       const wf_server_deactivate_account_input *input);

/** Input for com.atproto.server.confirmEmail. */
typedef struct wf_server_confirm_email_input {
    const char *email; /* required */
    const char *token; /* required */
} wf_server_confirm_email_input;

/** Confirm an email using a token. Calls POST com.atproto.server.confirmEmail. */
wf_status wf_server_confirm_email(wf_xrpc_client *client,
                                  const wf_server_confirm_email_input *input);

/** Output of com.atproto.server.requestEmailUpdate. */
typedef struct wf_server_request_email_update_result {
    int token_required; /* 1 if a token from requestEmailUpdate is required */
} wf_server_request_email_update_result;

/**
 * Request to update the account email.
 * Calls POST com.atproto.server.requestEmailUpdate.
 *
 * On WF_OK, `out` is populated.
 */
wf_status wf_server_request_email_update(wf_xrpc_client *client,
                                         wf_server_request_email_update_result *out);

/** Request a fresh email-confirmation message. Calls POST com.atproto.server.requestEmailConfirmation. */
wf_status wf_server_request_email_confirmation(wf_xrpc_client *client);

/** Input for com.atproto.server.updateEmail. */
typedef struct wf_server_update_email_input {
    const char *email;            /* required */
    int email_auth_factor;        /* optional; non-zero sets the flag */
    int has_email_auth_factor;    /* set non-zero when email_auth_factor is meaningful */
    const char *token;            /* optional; required if email was already confirmed */
    int has_token;                /* set non-zero when token is meaningful */
} wf_server_update_email_input;

/** Update the account email. Calls POST com.atproto.server.updateEmail. */
wf_status wf_server_update_email(wf_xrpc_client *client,
                                 const wf_server_update_email_input *input);

/* ------------------------------------------------------------------ */
/* Service-auth (service JWT) token issuance.                          */
/*                                                                     */
/* The counterpart to com.atproto.server.getServiceAuth: instead of    */
/* *requesting* a signed token from a PDS, these helpers let a          */
/* self-hosted PDS *mint* one locally by signing a compact JWT with its */
/* own repo signing key. The resulting token authorizes calls to        */
/* labeler / ozone / feedgen services (inter-service auth).             */
/*                                                                      */
/* Wire format mirrors @atproto/xrpc-server `createServiceJwt`:         */
/*   header  { "typ": "JWT", "alg": <ES256|ES256K> }                    */
/*   payload { iat, iss, aud, exp, [lxm], jti, [nuance] }               */
/* signed (ES256 for P-256, ES256K for secp256k1) over                  */
/* `base64url(header) "." base64url(payload)`; the 64-byte compact       */
/* (r||s, low-S) signature is base64url-appended. See                    */
/* /atproto/packages/xrpc-server/src/auth.ts.                           */
/* ------------------------------------------------------------------ */

/**
 * Input describing a service-auth token to mint. Only `iss` and `aud` are
 * required; the remaining fields are optional. All strings are borrowed for
 * the duration of the call (the caller keeps ownership).
 */
typedef struct wf_service_auth_request {
    const char *iss;    /* required; issuer — the PDS/account DID */
    const char *aud;    /* required; target service DID or `did#serviceId` */
    int64_t     exp;    /* optional; expiry (unix seconds). 0 => iat + 60s */
    const char *lxm;    /* optional; lexicon (XRPC) NSID to bind to; NULL to omit */
    const char *nuance; /* optional; extra opaque claim; NULL to omit */
} wf_service_auth_request;

/**
 * Mint a signed service-auth JWT.
 *
 * `key` is the PDS's repo signing key (P-256 => alg ES256, secp256k1 =>
 * alg ES256K). `iat` is the current unix time; `exp` defaults to `iat + 60`
 * when `req->exp` is <= 0. A fresh random `jti` (16 bytes, hex) is generated
 * on every call so repeated tokens are unique.
 *
 * On WF_OK, *out_token is a heap-allocated NUL-terminated JWT string owned by
 * the caller; release it with free(). On error *out_token is left NULL.
 */
wf_status wf_server_create_service_auth(const wf_service_auth_request *req,
                                        const wf_signing_key *key,
                                        char **out_token);

/**
 * Claims decoded from a verified service-auth JWT. Owned strings are heap
 * allocated; free with wf_service_auth_claims_free. `lxm`/`nuance` are NULL
 * when the corresponding claim is absent.
 */
typedef struct wf_service_auth_claims {
    char   *alg;    /* JWT header "alg" */
    char   *iss;    /* payload "iss" */
    char   *aud;    /* payload "aud" */
    int64_t exp;    /* payload "exp" (unix seconds) */
    int64_t iat;    /* payload "iat" (unix seconds); 0 if absent */
    char   *jti;    /* payload "jti"; NULL if absent */
    char   *lxm;    /* payload "lxm"; NULL if absent */
    char   *nuance; /* payload "nuance"; NULL if absent */
} wf_service_auth_claims;

/** Free a wf_service_auth_claims. Safe to call with NULL. */
void wf_service_auth_claims_free(wf_service_auth_claims *claims);

/**
 * Verify and decode a service-auth JWT.
 *
 * `signing_key_didkey` is the issuer's public verification key as a
 * `did:key:z...` (or bare multibase `z...`) string — i.e. the value returned
 * by wf_signing_key_public_didkey for the minting key. The signature is
 * validated with wf_verify (ES256 / ES256K, low-S), and the token is rejected
 * when expired relative to `now_unix` (pass 0 to use the current wall clock).
 *
 * Returns WF_ERR_INVALID_ARG on NULL/misshaped input, WF_ERR_PARSE on a
 * malformed JWT / expired token / bad signature, WF_ERR_ALLOC on OOM, and
 * WF_OK on success (in which case *out is owned by the caller).
 */
wf_status wf_server_verify_service_auth(const char *token,
                                        const char *signing_key_didkey,
                                        int64_t now_unix,
                                        wf_service_auth_claims *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SERVER_H */
