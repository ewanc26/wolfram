# OAuth / DPoP (`oauth.h` + `oauth/`)

wolfram implements the AT Protocol OAuth profile (RFC 9728 protected-resource
metadata, RFC 8414 authorization-server metadata, PKCE S256, ES256 **DPoP**,
Pushed Authorization Requests, and `private_key_jwt` client auth). The public
surface is split across focused headers under `wolfram/oauth/`:

| Header | Responsibility |
|--------|----------------|
| `metadata.h` | Discover & parse protected-resource / authorization-server / client metadata. |
| `pkce.h` | Generate PKCE S256 verifier + challenge. |
| `dpop.h` | Generate ES256 DPoP keys, create DPoP proofs and `private_key_jwt` client assertions. |
| `par.h` | PAR submit, authorization-code exchange, refresh, revoke (all with DPoP nonce retry). |
| `state.h` | Owned, serializable session + authorization state. |
| `callback.h` | Validate the redirect callback (state + issuer). |
| `flow.h` | High-level `authorization_begin` / `authorization_complete` orchestration. |

`oauth.h` includes all of the above.

> Every networking call is marked `// needs network`. The DPoP key for the
> public-client flow is generated for you and round-tripped through the
> serialized authorization state, so you never handle the raw key directly in
> the high-level flow.

## 1. Metadata discovery

```c
#include "wolfram/oauth.h"
#include "wolfram/xrpc.h"

wf_xrpc_client *transport = wf_xrpc_client_new("https://bsky.social"); // needs network

// Discover the authorization server behind a protected resource (e.g. a PDS).
wf_oauth_resource_metadata resource = {0};
wf_oauth_server_metadata server = {0};
wf_status st = wf_oauth_discover(transport, "https://bsky.social",
                                 &resource, &server);                  // needs network
/* On WF_OK both `resource` and `server` are owned; free them when done. */
wf_oauth_resource_metadata_free(&resource);
wf_oauth_server_metadata_free(&server);
```

You can also fetch/parse each document independently:
`wf_oauth_resource_metadata_get` / `wf_oauth_resource_metadata_parse`,
`wf_oauth_server_metadata_get` / `wf_oauth_server_metadata_parse`,
`wf_oauth_client_metadata_get` / `wf_oauth_client_metadata_parse`.

## 2. Client metadata

A public client's `client_id` is a URL pointing at its metadata document:

```c
wf_oauth_client_metadata client = {0};
wf_oauth_client_metadata_get(transport,
    "https://my-app.example.com/client-metadata.json", &client);      // needs network
// — or parse JSON you already fetched:
// wf_oauth_client_metadata_parse(json, json_len,
//     "https://my-app.example.com/client-metadata.json", &client);
```

## 3. End-to-end high-level flow

### 3a. Begin (generate material, submit PAR, build the URL)

```c
// Public DPoP client: signing_key NULL selects the `none` *client auth method*;
// the DPoP proof key is generated internally and embedded in state_json.
wf_oauth_client_auth client_auth = {
    .client_id                   = client.client_id,
    .authorization_server_issuer = server.issuer,
    .signing_key                 = NULL,   // public client
    .key_id                      = NULL,
};

wf_oauth_authorization_begin_options opts = {
    .redirect_uri = "https://my-app.example.com/callback",
    .scope        = "atproto transition:generic",
    .login_hint   = NULL,   // e.g. a handle or DID to pre-fill
    .app_state    = NULL,
    .now          = time(NULL),   // Unix seconds; must be > 0
    .state_ttl    = 600,
};

wf_oauth_authorization_begin_result begin = {0};
wf_status st = wf_oauth_authorization_begin(transport, &server, &client,
                                            &client_auth, &opts, &begin); // needs network
if (st == WF_OK) {
    printf("redirect the user's browser to:\n%s\n", begin.authorization_url);
    // Persist begin.state_json ATOMICALLY under begin.state *before* redirecting,
    // so you can recover the DPoP key + PKCE verifier on callback.
}
wf_oauth_authorization_begin_result_free(&begin);
```

The `scope` must include `atproto`, and `redirect_uri` must be one of
`client.redirect_uris` (the function enforces both). `client_id` in `client_auth`
must equal `client.client_id`.

### 3b. Callback (validate the redirect)

Your redirect endpoint receives query parameters. Validate them against the
persisted `state` and the server's `issuer`:

```c
wf_oauth_callback_params params = {
    .response          = NULL,   // present only for JARM (unsupported)
    .state             = query_state,    // from the redirect URL
    .code              = query_code,     // from the redirect URL
    .issuer            = query_iss,      // `iss` param, may be NULL
    .error             = query_error,    // NULL on success
    .error_description = query_error_description,
};
wf_oauth_callback_result cb = {0};
wf_status st = wf_oauth_callback_validate(&params, expected_state,
                                          server.issuer,
                                          server.authorization_response_iss_parameter_supported,
                                          &cb);                          // needs network? no
if (st == WF_OK) {
    /* cb.code / cb.state are owned; consume atomically to prevent replay. */
}
wf_oauth_callback_result_free(&cb);
```

### 3c. Complete (exchange code → durable session)

```c
wf_oauth_authorization_complete_result done = {0};
wf_status st = wf_oauth_authorization_complete(
    transport, &server, &client, &client_auth,
    &params, expected_state, state_json, state_json_len,
    "https://my-app.example.com/callback",
    time(NULL), &done);                                            // needs network
if (st == WF_OK && done.error == NULL) {
    // done.session is an owned wf_oauth_session_state (issuer, subject, tokens, dpop_key).
    // done.session_json is the serialized form — persist it under done.session.subject.
    printf("logged in as %s\n", done.session.subject);
}
wf_oauth_authorization_complete_result_free(&done);
```

The completion step internally parses `state_json` (recovering the DPoP key and
PKCE verifier), validates the callback, exchanges the code at the token
endpoint (with DPoP nonce retry), and verifies the returned subject DID. For a
`private_key_jwt` client, set `client_auth.signing_key` and `client_auth.key_id`
here, matching the values serialized in the authorization state.

## 4. Lower-level building blocks

Use these directly when you need finer control (e.g. custom nonce retry
semantics, or `private_key_jwt`).

```c
// PKCE S256.
wf_oauth_pkce pkce = {0};
wf_oauth_pkce_generate(&pkce);   // pkce.verifier + pkce.challenge

// ES256 DPoP key (heap-owned; free with wf_oauth_dpop_key_free).
wf_oauth_dpop_key *dpop = NULL;
wf_oauth_dpop_key_generate(&dpop);

// Build and submit a PAR.
wf_oauth_par_request req = {
    .client_id      = client.client_id,
    .redirect_uri   = "https://my-app.example.com/callback",
    .scope          = "atproto transition:generic",
    .state          = "<random-state>",
    .code_challenge = pkce.challenge,
    .login_hint     = NULL,
};
wf_oauth_par_response par = {0};
wf_oauth_par(transport, server.pushed_authorization_request_endpoint,
             dpop, &req, &par);                                    // needs network
/* par.request_uri is the PAR handle; par.expires_in its TTL. */
wf_oauth_par_response_free(&par);

// Exchange the authorization code (DPoP nonce retry is automatic).
wf_oauth_token_response token = {0};
wf_oauth_exchange_code(transport, server.token_endpoint, dpop,
                       client.client_id, code,
                       "https://my-app.example.com/callback",
                       pkce.verifier, &token);                    // needs network
wf_oauth_token_response_validate_subject(&token, expected_did);  // confirm DID
/* token.access_token / token.refresh_token / token.expires_in ... */
wf_oauth_token_response_free(&token);
```

### Refresh and revoke

```c
// Refresh using a refresh token (DPoP nonce retry is automatic).
wf_oauth_token_response refreshed = {0};
wf_oauth_refresh(transport, server.token_endpoint, dpop, client.client_id,
                 refresh_token, expected_did, &refreshed);       // needs network
wf_oauth_token_response_free(&refreshed);

// Revoke a token (best-effort; WF_ERR_HTTP may be ignored per RFC 7009).
wf_oauth_revoke(transport, server.revocation_endpoint, dpop,
                &client_auth, token_to_revoke);                  // needs network
```

`wf_oauth_session_refresh` refreshes an existing `wf_oauth_session_state` in
place (updating its tokens), which is what the high-level `wf_auth_client` uses
automatically — see below.

### DPoP proofs and client assertions (advanced)

`wf_oauth_dpop_proof_create` builds an ES256 DPoP proof JWT for a request; the
result is heap-owned and freed with `wf_oauth_string_free`. `ath` (access-token
hash) and `nonce` are optional, but you must supply the server's latest
`DPoP-Nonce` once you have one (it is returned in the `dpop_nonce` field of a
`wf_response`). `wf_oauth_client_assertion_create` builds an RFC 7523
`private_key_jwt` for confidential clients.

```c
wf_oauth_dpop_proof_options po = {
    .http_method = "POST",
    .http_uri    = "https://bsky.social/xrpc/com.atproto.repo.createRecord",
    .nonce       = server_nonce,     // NULL until the server provides one
    .access_token = access_token,    // produces the `ath` claim
    .jti        = NULL,              // random if NULL
    .issued_at  = 0,                 // current time if <= 0
};
char *proof = NULL;
wf_oauth_dpop_proof_create(dpop, &po, &proof);   // needs network? no
/* attach `proof` as the DPoP header on the request */
wf_oauth_string_free(proof);
```

## 5. Authenticated XRPC client (`wf_auth_client`)

`wf_auth_client` wraps a plain `wf_xrpc_client` with a `wf_oauth_session_state`,
adding DPoP proofs, DPoP **nonce rotation**, and automatic **session refresh**
when the access token has expired.

```c
// session here is a wf_oauth_session_state you obtained from
// wf_oauth_authorization_complete (or restored via
// wf_oauth_session_state_parse). It owns the DPoP key.
wf_auth_client *auth = wf_auth_client_new(transport, &session, &server, &client_auth);

// Issue authenticated calls exactly like the low-level client:
wf_response resp = {0};
wf_auth_client_query(auth, "app.bsky.feed.getTimeline",
                     "limit=50", &resp);                          // needs network
/* resp.body holds the JSON. */
wf_response_free(&resp);

wf_auth_client_procedure(auth, "com.atproto.repo.createRecord",
                         "{\"repo\":\"did:plc:me\",...}", &resp);  // needs network
wf_response_free(&resp);

wf_auth_client_free(auth);   // does NOT free transport or session
```

The `wf_auth_client` does not own the `wf_xrpc_client` or the
`wf_oauth_session_state` — they must outlive it. For `private_key_jwt`, pass a
`client_auth` whose `signing_key` and `key_id` match the session's
`client_auth_key_id`.

### Persisting the session

```c
char *session_json = NULL;
wf_oauth_session_state_serialize(&session, &session_json);  // persist atomically
/* ... later ... */
wf_oauth_session_state restored = {0};
wf_oauth_session_state_parse(session_json, strlen(session_json),
                             expected_subject, &restored);
free(session_json);
wf_oauth_session_state_free(&restored);
```

## Ownership cheat-sheet

| Allocated by | Free with |
|--------------|-----------|
| `wf_oauth_resource_metadata` / `wf_oauth_server_metadata` / `wf_oauth_client_metadata` | `wf_oauth_*_metadata_free` |
| `wf_oauth_par_response` / `wf_oauth_token_response` | `wf_oauth_par_response_free` / `wf_oauth_token_response_free` |
| `wf_oauth_authorization_begin_result` | `wf_oauth_authorization_begin_result_free` |
| `wf_oauth_authorization_complete_result` | `wf_oauth_authorization_complete_result_free` |
| `wf_oauth_callback_result` | `wf_oauth_callback_result_free` |
| `wf_oauth_dpop_key *` | `wf_oauth_dpop_key_free` |
| `wf_oauth_session_state` / `wf_oauth_authorization_state` | `wf_oauth_session_state_free` / `wf_oauth_authorization_state_free` |
| Strings from `wf_oauth_dpop_proof_create`, `wf_oauth_client_assertion_create`, `wf_oauth_authorization_url_create` | `wf_oauth_string_free` |

## Function reference

Concise, self-contained examples for the four OAuth entry points most apps need.
Every networking call is marked `// needs network`. A `wf_xrpc_client` provides
the transport; the DPoP key for the public-client flow is generated for you and
round-tripped through the serialized authorization state.

### `wf_oauth_discover` (metadata discovery)

```c
#include "wolfram/oauth.h"
#include "wolfram/xrpc.h"

wf_xrpc_client *transport = wf_xrpc_client_new("https://bsky.social");

wf_oauth_resource_metadata resource = {0};
wf_oauth_server_metadata server = {0};
wf_status st = wf_oauth_discover(transport, "https://bsky.social",
                                 &resource, &server);                  // needs network
if (st != WF_OK) {
    fprintf(stderr, "discovery failed: %d\n", (int)st);
    wf_xrpc_client_free(transport);
    return 1;
}
wf_oauth_resource_metadata_free(&resource);
wf_oauth_server_metadata_free(&server);
wf_xrpc_client_free(transport);
```

### `wf_oauth_authorization_begin` (PAR)

```c
wf_oauth_client_auth client_auth = {
    .client_id                   = client.client_id,
    .authorization_server_issuer = server.issuer,
    .signing_key                 = NULL,   // public client; DPoP key generated internally
    .key_id                      = NULL,
};
wf_oauth_authorization_begin_options opts = {
    .redirect_uri = "https://my-app.example.com/callback",
    .scope        = "atproto transition:generic",
    .login_hint   = NULL,
    .app_state    = NULL,
    .now          = time(NULL),
    .state_ttl    = 600,
};

wf_oauth_authorization_begin_result begin = {0};
wf_status st = wf_oauth_authorization_begin(transport, &server, &client,
                                            &client_auth, &opts, &begin); // needs network
if (st == WF_OK) {
    printf("redirect to: %s\n", begin.authorization_url);
    /* Persist begin.state_json atomically under begin.state before redirecting. */
}
wf_oauth_authorization_begin_result_free(&begin);
```

### `wf_oauth_authorization_complete` (callback → session)

```c
wf_oauth_authorization_complete_result done = {0};
wf_status st = wf_oauth_authorization_complete(
    transport, &server, &client, &client_auth,
    &params,            // populated by wf_oauth_callback_validate
    expected_state,     // the state you persisted at begin
    state_json, state_json_len,
    "https://my-app.example.com/callback",
    time(NULL), &done);                                            // needs network
if (st == WF_OK && done.error == NULL) {
    printf("logged in as %s\n", done.session.subject);
    /* Persist done.session_json under done.session.subject. */
}
wf_oauth_authorization_complete_result_free(&done);
```

### `wf_auth_client` (authenticated XRPC)

```c
wf_auth_client *auth = wf_auth_client_new(transport, &session, &server, &client_auth);

wf_response resp = {0};
wf_status st = wf_auth_client_query(auth, "app.bsky.feed.getTimeline",
                                     "limit=50", &resp);            // needs network
if (st == WF_OK) {
    /* resp.body holds the JSON. */
}
wf_response_free(&resp);

st = wf_auth_client_procedure(auth, "com.atproto.repo.createRecord",
                               "{\"repo\":\"did:plc:me\",...}", &resp); // needs network
wf_response_free(&resp);

wf_auth_client_free(auth);   // does NOT free transport or session
```
