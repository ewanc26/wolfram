# Identity and server management

wolfram exposes identity and server management in two complementary layers:

- `identity.h` — DID/handle **resolution** (covered elsewhere) plus the
  `com.atproto.identity` **account-management** operations (recommended
  credentials, PLC operation signing/submit, handle update/check/verify).
- `server.h` — the `com.atproto.server` **account lifecycle** operations
  (describe, create account, app passwords, invite codes, activate/deactivate,
  and email management).

These complement the lower-level `plc.h` helpers (which build/sign/submit a raw
`plc_operation`) and the `agent.h` convenience wrappers (which wrap a subset of
the same endpoints for the authenticated session). Use this module when you need
fine-grained control — for example driving the PLC signature flow yourself,
rotating a handle, or minting invite codes.

## Prerequisites

These are thin XRPC wrappers. They take a `wf_xrpc_client *` (the transport),
not a high-level `wf_agent`:

```c
#include "wolfram/identity.h"
#include "wolfram/server.h"

wf_xrpc_client *client = wf_xrpc_client_new("https://bsky.social");
/* Authenticate with a bearer token before calling account-scoped endpoints: */
wf_xrpc_client_set_bearer(client, access_jwt);

/* ... use identity/server calls ... */

wf_xrpc_client_free(client);
```

> Many of these endpoints require an authenticated session. Pass the
> `access_jwt` obtained from `wf_agent_get_session_data`,
> `wf_server_create_account`, or a `session` login.

## Resolution (recap)

`identity.h` still owns the resolution helpers these management ops build on:

- `wf_did_resolve` / `wf_did_resolve_service` — resolve a DID (did:plc /
  did:web) to its document or a specific service endpoint.
- `wf_handle_resolve` / `wf_handle_parse_dns_txt` — resolve a handle to a DID.
- `wf_did_document_free` — free a resolved document.

## PLC signature flow (recommended credentials → sign → submit)

DID PLC writes are a three-step flow: fetch the recommended credentials, request
a signature token (emailed to the DID's account), sign the operation server-side
with that token, then submit it. `identity.h` delegates the sign/submit steps to
`plc.h`, so you can use whichever layer you prefer.

```c
/* 1. Fetch recommended DID credentials for the authenticated account. */
wf_identity_recommended_did_credentials creds = {0};
wf_status s = wf_identity_get_recommended_did_credentials(client, &creds);
/* ... inspect creds.rotation_keys[], creds.also_known_as[], etc. ... */
wf_identity_recommended_did_credentials_free(&creds);

/* 2. Request the PLC operation signature token (emailed to the DID). */
s = wf_identity_request_plc_operation_signature(client, "did:plc:abc");

/* 3. Sign the operation using the emailed token. */
wf_identity_sign_plc_operation_input in = {
    .token = "123456",
    .rotation_keys = (const char *const[]) { "did:key:..." },
    .rotation_keys_count = 1,
    .also_known_as = (const char *const[]) { "at://alice.example.com" },
    .also_known_as_count = 1,
    .verification_methods_json = "{\"atproto\":\"did:key:...\"}",
    .services_json = "{\"atproto_pds\":\"https://pds.example.com\"}",
};
wf_identity_sign_plc_operation_result out = {0};
s = wf_identity_sign_plc_operation(client, &in, &out);
/* out.operation is the signed plc_operation JSON string. */
wf_identity_sign_plc_operation_result_free(&out);

/* 4. Submit the signed operation (delegates to wf_plc_submit_operation). */
s = wf_identity_submit_plc_operation(client, out.operation);
```

## Handle update, check, and verify

```c
/* Update the authenticated account's handle (delegates to wf_plc_update_handle). */
wf_status s = wf_identity_update_handle(client, "new.handle.example.com");

/* Check whether a handle is available / correctly configured. */
wf_identity_check_handle_input chk = { .handle = "new.handle.example.com",
                                       .did = "did:plc:abc" };
wf_identity_check_handle_result res = {0};
s = wf_identity_check_handle(client, &chk, &res);
/* res.valid: 1 = valid/available, 0 = not, -1 = absent */

/* Bi-directional verify (local convenience: resolves both directions). */
int valid = 0;
s = wf_identity_verify_handle(client, "alice.example.com", &valid);
/* valid == 1 only when handle→DID and DID→handle agree. */
```

## Server: account lifecycle, app passwords, invite codes

```c
/* Describe the server (no auth required). */
wf_server_description desc = {0};
wf_status s = wf_server_describe(client, &desc);
printf("did=%s invite_required=%d\n", desc.did, desc.invite_code_required);
wf_server_describe_free(&desc);

/* Create a new account (returns access/refresh JWTs + DID document). */
wf_server_create_account_input ci = {
    .handle = "alice.bsky.social",
    .email = "alice@example.com",
    .password = "hunter2",
    .invite_code = "some-invite-code",
};
wf_server_create_account_result acct = {0};
s = wf_server_create_account(client, &ci, &acct);
wf_server_create_account_result_free(&acct);

/* App passwords. */
wf_server_create_app_password_input pi = { .name = "mobile", .privileged = 0 };
wf_server_app_password pwd = {0};
s = wf_server_create_app_password(client, &pi, &pwd);
wf_server_app_password_free(&pwd);

wf_server_app_password_list list = {0};
s = wf_server_list_app_passwords(client, &list);
wf_server_app_password_list_free(&list);

wf_server_revoke_app_password_input ri = { .name = "mobile" };
s = wf_server_revoke_app_password(client, &ri);
```

### Invite codes

```c
/* Single invite code. */
wf_server_create_invite_code_input ic = { .use_count = 5, .for_account = NULL };
wf_server_create_invite_code_result icode = {0};
wf_status s = wf_server_create_invite_code(client, &ic, &icode);
printf("invite: %s\n", icode.code);
wf_server_create_invite_code_result_free(&icode);

/* Multiple codes, optionally per account. */
wf_server_create_invite_codes_input ics = {
    .code_count = 2, .use_count = 1,
    .for_accounts = (const char *const[]) { "did:plc:a", "did:plc:b" },
    .for_accounts_count = 2,
};
wf_server_create_invite_codes_result icodes = {0};
s = wf_server_create_invite_codes(client, &ics, &icodes);
wf_server_create_invite_codes_result_free(&icodes);

/* Revoke codes. */
char *codes[] = { "code-to-revoke" };
wf_server_revoke_invite_codes_input rev = { .codes = codes, .code_count = 1 };
s = wf_server_revoke_invite_codes(client, &rev);
```

### Activate / deactivate

```c
wf_status s = wf_server_activate_account(client);

wf_server_deactivate_account_input di = { .delete_after = "2024-12-31T00:00:00Z" };
s = wf_server_deactivate_account(client, &di);
```

### Email management

```c
/* Request an email update — tells you whether a token is required. */
wf_server_request_email_update_result eu = {0};
wf_status s = wf_server_request_email_update(client, &eu);
/* eu.token_required: 1 if a token from requestEmailUpdate is required. */

/* Update the email (token required if already confirmed). */
wf_server_update_email_input ue = {
    .email = "new@example.com",
    .email_auth_factor = 1, .has_email_auth_factor = 1,
    .token = "confirm-token", .has_token = 1,
};
s = wf_server_update_email(client, &ue);

/* Confirm an email with a token, or resend the confirmation message. */
wf_server_confirm_email_input ce = { .email = "new@example.com", .token = "tok" };
s = wf_server_confirm_email(client, &ce);
s = wf_server_request_email_confirmation(client);
```

## Function reference (free functions)

All free functions are safe to call with `NULL`.

**identity.h**

- `wf_identity_recommended_did_credentials_free`
- `wf_identity_sign_plc_operation_result_free`
- `wf_did_document_free`

**server.h**

- `wf_server_describe_free`
- `wf_server_create_account_result_free`
- `wf_server_app_password_free`
- `wf_server_app_password_list_free`
- `wf_server_create_invite_code_result_free`
- `wf_server_create_invite_codes_result_free`

For the full list of `com.atproto.identity` and `com.atproto.server` calls,
browse `include/wolfram/identity.h` and `include/wolfram/server.h`. Higher-level
session-bound equivalents (e.g. `wf_agent_describe_server`,
`wf_agent_create_invite_code`, `wf_agent_activate_account`) are documented in
[agent.md](agent.md).
