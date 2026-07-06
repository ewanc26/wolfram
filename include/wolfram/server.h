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

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SERVER_H */
