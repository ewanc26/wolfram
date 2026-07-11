# AGENTS.md

Agentic principles and technical context for the `wolfram` repository.

## Technical philosophy

1. **Transport first**: only transport modules (`xrpc.c` and `websocket.c`) do real network I/O. Protocol modules consume those APIs.
2. **No hand-rolled crypto or hashing**: wrap `libsecp256k1` and an established SHA-256 implementation rather than writing field arithmetic or digest logic from scratch.
3. **Stubs are honest**: unimplemented functions return an error and carry a `TODO` explaining what's missing and why — never a silent no-op or a fabricated success. Unimplemented *backends/transports* (e.g. the Wii/Wii U/3DS platform stubs and `xrpc_wii.c`) return `WF_ERR_NOT_IMPLEMENTED`; unimplemented protocol functions with missing inputs return `WF_ERR_INVALID_ARG`. When the missing piece becomes available (e.g. a generated lex transport call), replace the stub with a real implementation rather than leaving it.
4. **Ownership is explicit**: every heap-allocated output has a matching `_free` function documented next to it. No hidden allocations, no implicit ownership transfer.
5. **Protocol parity**: cross-reference `bluesky-social/atproto` for wire formats (XRPC envelopes, DID documents, DAG-CBOR, MST) rather than inferring them.
6. **Pure C runtime**: the SDK and generated clients are C11. Python is permitted only for optional development-time code generation and tests; it must never become a runtime dependency.
7. **Console/multi-platform support**: support for embedded and cross-compiled targets (Nintendo Wii, Wii U, 3DS, Windows, Linux/AArch64, etc.) is parity across platforms — platform-specific APIs are isolated in `src/platform/`. The Windows target is fully implemented against the Win32 API; the Wii/Wii U/3DS targets ship honest stub backends (`WF_ERR_NOT_IMPLEMENTED`) that must be replaced with real backends before shipping.

## Code style

- **Comments are allowed and encouraged** where they aid understanding — especially next to public API declarations (ownership rules, lifetime, thread-safety), non-obvious transport/protocol details, and the `honest stub`/`TODO` notes described in the philosophy. The existing codebase uses comments pervasively; match that. Do not add noise comments that merely restate the code.
- **Atomic conventional commits**: every commit must contain exactly one logical change. Scope by module — `feat(xrpc)`, `feat(repo)`, `fix(identity)`, `fix(lexgen)`, `docs(roadmap)`, etc. Never mix unrelated changes in a single commit (e.g. do not combine a code change with a docs update). Feature work lands on a dedicated `feat/<area>` branch and is merged to `main` with `--no-ff` so the branch history is preserved. If a commit touches multiple concerns, split it into multiple sequential commits.
- **No AI co-authors**: commits must not add a `Co-authored-by:` trailer crediting an AI agent (e.g. `Co-authored-by: Claude ...` or `Co-authored-by: Kilo ...`). AI assistance is welcome, but credit for committed work goes to human authors only. Omit the trailer entirely.
- **Module layering**: transport → identity → repo → agent. New protocol surface follows the existing pattern: generated lex wrappers (`atproto_lex.{c,h}`) for the wire calls, then `*_typed.{c,h}` owning parsers/builders, then `wf_agent_*` convenience wrappers that sync auth and delegate to the generated call.
- **No commented-out code** left in place; delete dead code or move it to a test.
- Follow the surrounding file's indentation and brace style.

## Development workflow

- **Desktop builds**: `cmake -S . -B build && cmake --build build`
- **Tests**: `ctest --test-dir build`
- **Lexicon generation**: `python3 tools/wf_lexgen.py $(find lexicons -name "*.json") -o include/wolfram/atproto_lex.h --source-output src/atproto_lex.c --header-rel wolfram/atproto_lex.h`
- **Optional modules**: gated by CMake options — `WOLFRAM_BUILD_SERVER` (libmicrohttpd XRPC server), `WOLFRAM_BUILD_STORE` (SQLite persistence), `WOLFRAM_BUILD_STORE_CRYPTO` (libsodium at-rest encryption), `WOLFRAM_BUILD_TEST_HTTPD` (libmicrohttpd mock PDS for offline HTTP integration tests), `WOLFRAM_BUILD_IDN` (libidn2 internationalised-handle resolution), `WOLFRAM_BUILD_CPP` (C++ RAII wrapper `wolfram-cpp`). Platform/example/test flags: `WOLFRAM_BUILD_WII` / `_WIIU` / `_3DS` / `_WINDOWS`, `WOLFRAM_BUILD_EXAMPLES`, `WOLFRAM_BUILD_TESTS`.
- **Platform support for multi-target builds**: cross-compilation targets (Wii, Wii U, 3DS, Windows, linux-aarch64) are supported via `.devdeps/*.cmake` toolchain files and stub platform implementations in `src/platform/`. Use `-DWOLFRAM_BUILD_*` accordingly. Desktop (x86_64) still uses libcurl, OpenSSL, and pthreads.
- **When picking this back up cold**: read the `## Roadmap` section of `README.md` and `docs/roadmap.md` first — they are kept current and order the remaining work by dependency.
- **Before changing protocol behavior**: inspect `/Volumes/Storage/Developer/Local/atproto` and verify maintained upstream libraries/specifications where integration is preferable to custom code. The `rsky` Rust reference at `/Volumes/Storage/Developer/Git/rsky`, when present, is a useful cross-check but is not required.

## Current state

The SDK is broad and multi-layered; almost all of it is implemented and tested. Highlights:

- `xrpc`: libcurl query/procedure calls, encoded scalar/repeated parameters, generic HTTP GET, bearer authentication, binary blob upload (incl. video), DPoP-bound OAuth client (`auth_client`). Tested.
- `session` / `server`: PDS login, resume, refresh, logout, and full `com.atproto.server` account lifecycle (createAccount, app passwords, deactivate, email/account-delete requests, session refresh). The `server_typed` agent wrappers implement the parameterless `com.atproto.server` procedures (`requestAccountDelete`, `requestEmailUpdate`, `requestEmailConfirmation`, `refreshSession`). Tested.
- `identity` / `identity_typed` / `plc`: did:plc, did:web, handle DNS TXT (c-ares/POSIX `libresolv`/well-known fallback), `com.atproto.identity` wrappers, and DID PLC operation build/sign/submit helpers. `wf_agent_identity_rotate_handle` now wires the full handle-rotation flow: it builds the rotation operation locally (validation gate) and, given the out-of-band `requestPlcOperationSignature` token, signs it server-side via `signPlcOperation` and submits it via `submitPlcOperation`. Tested.
- `crypto`: secp256k1 (libsecp256k1) + P-256 (OpenSSL), `did:key`/multikey verification. Tested.
- `repo` / `record`: DAG-CBOR, CIDs, CAR, MST, signed v3 commits, record CRUD, diff verify/apply, operation inversion, schema-driven record encoding. Tested.
- `sync` / `sync_typed` / `sync_subscribe` / `sync_verify`: CAR download, `com.atproto.sync.*` typed wrappers, firehose `subscribeRepos` WebSocket subscription with commit verification. Tested.
- `sync_publish`: firehose event production — builds the framed `{header}{body}` CBOR messages a relay/PDS streams over WebSocket (`wf_sync_publish_event` / `wf_sync_publish_error`), the exact inverse of the `sync_subscribe` decoder, covering `commit`/`sync`/`identity`/`account`/`info` and `op:-1` error frames. Round-trip tested by `test_sync_publish`. Tested.
- `agent` / `bsky_agent`: high-level BskyAgent bundling session + xrpc + identity + agent; posts, profile, social graph, feeds, preferences, push registration, notifications, blobs, video upload, and `app.bsky.graph` write wrappers (`graph_write.{c,h}`: mute/unmute thread + actor-list, block/list/listitem/starterpack/listblock create/update/delete) tested against an offline mock PDS. Tested.
- `chat` / `chat_typed`: `chat.bsky.*` DM/group/actor/moderation write+query wrappers with chat-service endpoint resolution. Tested.
- `ozone` / `ozone_typed`: full `tools.ozone.*` typed coverage (moderation, queue, report, team, verification, signature, setting, hosting, server, safelink, communication, set value). Tested.
- `moderation`: offline decision engine (blur/alert/inform/filter) from labels, blocks, mutes, muted words, hidden posts. Tested.
- `label` / `labeler_typed` / `unspecced` / `unspecced_typed`: label subscription, labeler service coverage, and full `app.bsky.unspecced` (trends, suggested users, thread v2, etc.). Tested.
- `oauth`: discovery, PKCE S256, ES256 DPoP, PAR, callback validation, `private_key_jwt`, serializable sessions, `wf_auth_client` with DPoP nonce retry, **and OAuth resource-server token verification** (`oauth/verify.h`: `wf_oauth_verify_bearer` / `wf_oauth_verify_dpop` / `wf_oauth_verify_request` over the `wf_crypto_*` P-256/JWK/SHA-256/base64url primitives, with a `wf_oauth_dpop_replay_cache` and `wf_oauth_trusted_keys`). Tested.
- `jetstream`: filtered Jetstream WebSocket subscription with cursor reconnect/backoff and optional zstd. Tested.
- `validate` / `json` / `syntax` / `richtext`: runtime lexicon validation, generic JSON canonicalize/validate, syntax validators, rich-text facets. Tested.
- `store`: optional SQLite session + repo-mirror + persisted-label storage (`WOLFRAM_BUILD_STORE`; `WOLFRAM_BUILD_STORE_CRYPTO` adds libsodium at-rest encryption).
- `xrpc_server`: optional `libmicrohttpd`-backed XRPC server (`WOLFRAM_BUILD_SERVER`). Route registration, **Server-Sent Events (SSE) streaming** for subscription-style endpoints, and **WebSocket (RFC 6455) subscription endpoints** with per-route token-bucket rate limiting, auth middleware, CORS. Tested offline.
- `feedgen_server`: optional `libmicrohttpd`-backed feed-generator skeleton server helper (`WOLFRAM_BUILD_SERVER`) serving `app.bsky.feed.getFeedSkeleton` and `getFeedGenerator`. Tested.
- `relay_server`: optional `libmicrohttpd`-backed generic upstream→downstream WebSocket subscription relay (`WOLFRAM_BUILD_SERVER`), built on the server's WS endpoints and the libcurl WebSocket client transport. `wf_xrpc_server_register_relay` registers a WS route (e.g. `com.atproto.sync.subscribeRepos`) that, on a downstream connect, opens an upstream `ws(s)://` connection and forwards each received message byte-for-byte until either side closes, then closes downstream. Protocol-agnostic (raw frames, no parsing) so it serves `subscribeRepos`, `subscribeLabels`, or any binary subscription. Config deep-copied and freed by `wf_relay_config_free`; the `wf_relay_server` handle owns the copy and is freed by `wf_relay_server_free` after `wf_xrpc_server_free`. Tested offline (`test_relay_server`).
- `blob_store`: self-contained blob persistence + serving so wolfram can act as a PDS for blobs (`WOLFRAM_BUILD_SERVER` for the server integration; core store always built). `wf_blob_store_*` (new/free/put/get/exists) with an in-memory mode and a file-backed mode (one file per blob named by CID + a `<cid>.mime` sidecar; re-open reloads). `wf_xrpc_server_register_blob_store` registers `com.atproto.repo.uploadBlob` (procedure) and `com.atproto.sync.getBlob` (query): upload computes the blob's raw multicodec (0x55) SHA-256 CID with `wf_cid_of_bytes`, stores it, and returns the TypedBlobRef; getBlob serves the raw bytes with the stored Content-Type. The XRPC server request/response structs carry a raw POST body and a custom response Content-Type for binary blobs. Tested offline (`test_blob_store`).
- `video_typed`: owning parsers + agent wrappers for `app.bsky.video` (job status, upload limits, upload). Tested.
- `actor_prefs_typed` / `actor_status_typed` / `notification_typed` / `notification_v2_typed` / `labeler_typed` / `embed_typed` / `feed_typed` / `feedgen_typed` / `graph_typed` / `list_typed` / `thread_typed` / `bookmark_typed` / `contact_typed` / `draft_typed` / `ageassurance_typed` / `temp_typed` / `admin_typed`: owned typed parsers/builders and agent wrappers across the remaining lexicon namespaces. `actor_status_typed` keeps honest stubs for `getActorStatus`/`getStatus`/`putStatus` because the `app.bsky.actor.status` lexicon defines `main` as a `record` (no query/procedure defs). Tested.
- `lexicon` (`tools/wf_lexgen.py`): generates C declarations, recursive input encoders, endpoint wrappers, and owning output decoders. The generator always emits the definition for query/procedure endpoints that have neither an `input` schema nor `parameters`. Tested.
- `cli`: `wolfram` command-line client (login/post/get/threads/notifications/labels/moderation/profile/timeline/follow/like/repost/search/mute/thread, plus `oauth-login`/`oauth-callback`, `block`/`unblock`, `notifications update-seen`, `repo put-record`/`delete-record`/`list-records`/`describe`, `feed get`/`author`, `moderation report`; global `--json` flag for raw JSON output). Built by default.

## Next planned work

- Exercise the gated live example test (`test_examples_live`) in CI with real credentials.
- Continue evaluating upstream C libraries for server-side infrastructure (event loop, config parsing).
- Broaden generated typed-wrapper coverage for any remaining lexicon endpoints not yet wrapped at the agent level.
- Optionally add a server-side end-to-end test that emits a built `sync_publish` frame over a `subscribeRepos`-style WebSocket route and decodes it back with the `sync_subscribe` client, and extend `sync_publish` to the `subscribeLabels` `#labels` event.

## Platform support

Cross-compilation targets for Nintendo consoles and Windows:

**Wii**: `.devdeps/wii.cmake`; client-only build, excludes OAuth, server modules, and desktop dependencies.

**Wii U**: `.devdeps/wiiu.cmake`; client-only build.

**3DS**: `.devdeps/3ds.cmake`; client-only build.

**Windows**: `.devdeps/windows.cmake`; MinGW-w64 cross-compilation.

**Linux ARM64**: `.devdeps/linux-aarch64.cmake`; AArch64 cross-compilation.

The Wii, Wii U, and 3DS targets use stub platform implementations in `src/platform/` (wii_platform.c, wiiu_platform.c, 3ds_platform.c) that return `WF_ERR_NOT_IMPLEMENTED`. The Windows target is fully implemented against the Win32 API (windows_platform.c). Replace the console stubs with real backends before shipping.

The desktop (x86_64) build includes the full suite of dependencies: libcurl, OpenSSL, pthreads, libmicrohttpd (if `WOLFRAM_BUILD_SERVER`), SQLite (if `WOLFRAM_BUILD_STORE`), libsodium (if `WOLFRAM_BUILD_STORE_CRYPTO`).
