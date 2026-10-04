# Hosted OAuth node

Wolfram can build a small hosted OAuth node for clients that cannot reasonably run the AT Protocol browser OAuth flow themselves, such as Cobalt on Wii U and Indigo on Nintendo 3DS.

The node owns the browser-facing OAuth flow. The console never receives a PDS password, OAuth refresh token, or DPoP private key.

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

Put the node behind an HTTPS reverse proxy. The public URL must be the same origin used for the OAuth client metadata and callback.

The node exposes:

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

The console polls the pairing code and receives an opaque node session token. It uses that token for subsequent XRPC calls to the node. The node performs those calls against the real PDS using the stored OAuth session and DPoP.

## Security model

The pairing code is generated from cryptographically secure random bytes and expires.

The console bearer token is also generated independently and is never sent through the browser page.

The OAuth callback never displays or forwards the access or refresh tokens to the browser.

The OAuth session, DPoP key and refresh token currently live in memory inside the node process. Restarting the node invalidates active pairings and node sessions. A future persistent deployment should store OAuth state securely and protect it at rest.

For production deployments, run one node instance per state store or add shared session storage before putting multiple instances behind a load balancer.
