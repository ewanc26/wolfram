# Hosted OAuth node

Wolfram can build a small hosted OAuth node for clients that cannot reasonably run the AT Protocol browser OAuth flow themselves, such as Cobalt on Wii U and Indigo on Nintendo 3DS.

The node owns the browser-facing OAuth flow. The console never receives a PDS password, OAuth refresh token, or DPoP private key.

The node is **memory-only**. It writes no session state, credentials or personal data to disk, so there is nothing to back up, export or erase at rest. A restart signs every console out.

## Build

Configure a normal hosted Wolfram build with the OAuth node enabled:

```sh
cmake -S . -B build-oauth \
  -DWOLFRAM_BUILD_OAUTH_NODE=ON \
  -DWOLFRAM_BUILD_TESTS=OFF \
  -DWOLFRAM_BUILD_EXAMPLES=OFF

cmake --build build-oauth --target wolfram-oauth-node
```

The node is deliberately unavailable in Wii U and 3DS builds.

## Run

```sh
./build-oauth/wolfram-oauth-node \
  --public-base-url https://auth.example.com \
  --listen 127.0.0.1 \
  --port 8080 \
  --client-name "My Console Client"
```

There is no state-file option. The executable refuses to start if given one.

Put the node behind an HTTPS reverse proxy. The public URL must be the same origin used for the OAuth client metadata and callback.

The node exposes:

- `GET /` and `GET /privacy` — the public privacy notice
- `GET /oauth-client-metadata.json`
- `GET /pair/<pair-code>`
- `GET /oauth/callback`
- `POST /xrpc/uk.ewancroft.oauth.begin`
- `GET /xrpc/uk.ewancroft.oauth.poll?code=<pair-code>`
- authenticated XRPC proxying for console sessions

The production Slingshot resolver is fixed to `https://slingshot.micocosm.blue` by default. A different resolver can be supplied through the node configuration API when embedding the node rather than using the standalone executable.

## Browser sign-in flow

A console submits the account handle to `uk.ewancroft.oauth.begin`.

The node:

1. Resolves the handle through Slingshot.
2. Obtains the account PDS from Slingshot's verified identity response.
3. Discovers OAuth metadata at the PDS authorization server.
4. Starts the OAuth authorization-code flow with PKCE, PAR and DPoP.
5. Returns a short-lived pairing URL to the console.

The console displays that URL and the user opens it on a phone or computer. The pairing page does not collect credentials. It links directly to the PDS authorization endpoint.

The user enters their normal PDS credentials and completes any MFA and consent screens there. The PDS redirects back to the node callback.

The node validates the OAuth state, exchanges the authorization code, and requires the OAuth subject DID to equal the DID previously resolved from the handle. Only then is the pairing marked complete.

The wire contract, reply rules and test vectors are in [oauth-pairing.md](oauth-pairing.md). The console polls the pairing code and receives an opaque node session token. It uses that token for subsequent XRPC calls to the node. The node performs those calls against the real PDS using the in-memory OAuth session and DPoP key.

## Security and data protection

The pairing code is generated from 100 bits of cryptographically secure random data and expires.

The console bearer token is generated independently and is never sent through the browser page.

The OAuth callback never displays or forwards the access or refresh tokens to the browser.

OAuth sessions, including the refresh token and DPoP private key needed to refresh access tokens, live only in process memory. They are never written to a file, volume, database or log. The node has no persistence layer and no state directory. On `SIGTERM`, a crash or a host restart, the memory is released and every session is gone.

The node holds the minimum personal data needed to proxy a request for the life of a session:

- account handle and DID
- PDS URL
- OAuth session tokens and DPoP key
- the pairing URL and short-lived OAuth state while the browser flow is pending

It does not log handles, DIDs, tokens, pairing codes, request URLs, request bodies or response bodies. The executable disables the library logger, whose error path can include a query string containing a handle or DID, and prints only its own listening line. Nothing is written to durable storage; expired sessions are freed as pairing slots are reused and the whole table is dropped on exit. A restart is therefore also the erasure mechanism: there is no durable record to delete.

At-rest protections are unnecessary because there is no at-rest copy. This does not protect against an operator of the running process: to proxy a request the node must hold the session in memory and sign with its DPoP key, so anyone who can instrument the live process can observe plaintext during an active request. Guaranteeing that the operator can never use an account requires clients to hold the OAuth session and talk to the PDS directly, without a proxy.

The public privacy notice and retention table are served at `https://oauth.croft.click/privacy` and at the root of the hostname. It names the controller, the lawful bases, the recipients (Cloudflare, the PDS and Slingshot), the in-memory retention periods, and how to exercise UK GDPR rights or complain to the ICO. Keep it in step with any deployment change; the HTML lives in [`src/node/oauth_node_privacy.h`](../src/node/oauth_node_privacy.h).

The contact address is wrapped in Cloudflare's `<!--email_off-->` markers. Without them, Cloudflare's Email Address Obfuscation rewrites the address and injects `email-decode.min.js`, which would make the page depend on a third-party script and hide the contact from visitors without JavaScript. If the zone has obfuscation enabled, keep those markers.

Pending pairings (pairing code, authorization URL, PKCE verifier and OAuth state) also stay in memory. A node restart cancels sign-ins that were already in progress on the PDS consent screen; the console gets an error and requests a new pairing link.

## Deployment hardening

Run the container with no state volume and a read-only root filesystem (only a memory-backed `/tmp`), and disable core dumps so credential memory cannot be written to disk:

```yaml
read_only: true
tmpfs:
  - /tmp
security_opt:
  - no-new-privileges:true
cap_drop:
  - ALL
ulimits:
  core: 0
```

The standalone executable also disables core dumps at startup on POSIX, marks itself non-dumpable on Linux and turns off the library logger. Keep `docker logs` collection limited; the node itself prints only its startup line, but a reverse proxy or error page added in front of it must not log request paths or query strings either.

Because sessions are memory-only, run exactly one container per host and expect every console to sign in again after a deploy. There is no shared session store.
