# AGENTS.md

Agentic principles and technical context for the `wolfram` repository.

## Technical philosophy

1. **Transport first**: only transport modules (`xrpc.c` and `websocket.c`) do real network I/O. Protocol modules consume those APIs.
2. **No hand-rolled crypto or hashing**: wrap `libsecp256k1` and an established SHA-256 implementation rather than writing field arithmetic or digest logic from scratch.
3. **Stubs are honest**: unimplemented functions return `WF_ERR_INVALID_ARG` and carry a `TODO` explaining what's missing and why — never a silent no-op or a fabricated success. When the missing piece becomes available (e.g. a generated lex transport call), replace the stub with a real implementation rather than leaving it.
4. **Ownership is explicit**: every heap-allocated output has a matching `_free` function documented next to it. No hidden allocations, no implicit ownership transfer.
5. **Protocol parity**: cross-reference `bluesky-social/atproto` for wire formats (XRPC envelopes, DID documents, DAG-CBOR, MST) rather than inferring them.
6. **Pure C runtime**: the SDK and generated clients are C11. Python is permitted only for optional development-time code generation and tests; it must never become a runtime dependency.
7. **Console/multi-platform support**: support for embedded and cross-compiled targets (Nintendo Wii, Wii U, 3DS, Windows, etc.) is parity across platforms — platform-specific APIs are isolated in `src/platform/` with stub implementations that can be replaced with real backends before shipping.

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
- **Optional modules**: gated by CMake options: `WOLFRAM_BUILD_SERVER` (libmicrohttpd XRPC server), `WOLFRAM_BUILD_STORE` (SQLite persistence), `WOLFRAM_BUILD_STORE_CRYPTO` (libsodium at-rest encryption).
- **Platform support for multi-target builds**: cross-compilation targets (Wii, Wii U, 3DS, Windows, linux-aarch64) are supported via `.devdeps/*.cmake` toolchain files and stub platform implementations in `src/platform/`. Use `DWOLFRAM_BUILD_*` accordingly. Desktop (x86_64) still uses libcurl, OpenSSL, and pthreads.
- **When picking this back up cold**: read the `## Roadmap` section of `README.md` and `docs/roadmap.md` first — they are kept current and order the remaining work by dependency.
- **Before changing protocol behavior**: inspect `/Volumes/Storage/Developer/Local/atproto` and verify maintained upstream libraries/specifications where integration is preferable to custom code. The `rsky` Rust reference at `/Volumes/Storage/Developer/Git/rsky`, when present, is a useful cross-check but is not required.

## Current state

The SDK is broad and multi-layered; almost all of it is implemented and tested. Highlights:

- `xrpc`: libcurl query/procedure calls, encoded scalar/repeated parameters, generic HTTP GET, bearer authentication, binary blob upload (incl. video), DPoP-bound OAuth client (`auth_client`). Tested.
- `session` / `server`: PDS login, resume, refresh, logout, and full `com.atproto.server` account lifecycle. Tested.
- `identity` / `identity_typed` / `plc`: did:plc, did:web, handle DNS TXT, `com.atproto.identity` wrappers, and DID PLC operation build/sign/submit helpers. Tested.
- `crypto`: secp256k1 (libsecp256k1) + P-256 (OpenSSL), `did:key`/multikey verification. Tested.
- `repo` / `record`: DAG-CBOR, CIDs, CAR, MST, signed v3 commits, record CRUD, diff verify/apply, operation inversion, schema-driven record encoding. Tested.
- `sync` / `sync_typed` / `sync_subscribe` / `sync_verify`: CAR download, `com.atproto.sync.*` typed wrappers, firehose `subscribeRepos` WebSocket subscription with commit verification. Tested.
- `agent` / `bsky_agent`: high-level BskyAgent bundling session + xrpc + identity + agent; posts, profile, social graph, feeds, preferences, push registration, notifications, blobs, video upload, and `app.bsky.graph` write wrappers tested against an offline mock PDS. Tested.
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
- `relay_server`: optional `libmicrohttpd`-backed generic upstream→downstream WebSocket subscription relay (`WOLFRAM_BUILD_SERVER`), built on the server's WS endpoints and the libcurl WebSocket client transport. `wf_xrpc_server_register_relay` registers a WS route (e.g. `com.atproto.sync.subscribeRepos`) that, on a downstream connect, opens an upstream `ws(s)://` connection and forwards each received message byte-for-byte until either side closes, then closes downstream. Protocol-agnostic (raw frames, no parsing) so it serves `subscribeRepos`, `subscribeLabels`, or any binary subscription. Config deep-copied and freed by `wf_relay_config_free`; the `wf_relay_server` handle owns the copy and is freed by `wf_relay_server_free` after `wf_xrpc_server_free`. Tested offline.
- `video_typed`: owning parsers + agent wrappers for `app.bsky.video` (job status, upload limits, upload). Tested.
- `actor_prefs_typed` / `actor_status_typed` / `notification_typed` / `notification_v2_typed` / `labeler_typed` / `embed_typed` / `feed_typed` / `feedgen_typed` / `graph_typed` / `list_typed` / `thread_typed` / `bookmark_typed` / `contact_typed` / `draft_typed` / `ageassurance_typed` / `temp_typed` / `admin_typed`: owned typed parsers/builders and agent wrappers across the remaining lexicon namespaces. `actor_status_typed` keeps honest stubs for `getActorStatus`/`getStatus`/`putStatus` because the `app.bsky.actor.status` lexicon defines `main` as a `record` (no query/procedure defs). Tested.
- `lexicon` (`tools/wf_lexgen.py`): generates C declarations, recursive input encoders, endpoint wrappers, and owning output decoders. **Note (fixed):** the generator now always emits the definition for query/procedure endpoints that have neither an `input` schema nor `parameters`. Tested.
- `cli`: `wolfram` command-line client (login/post/get/threads/notifications/labels/moderation/profile/timeline/follow/like/repost/search/mute/thread). Built by default.

## Next planned work

- Exercise the gated live example test (`test_examples_live`) in CI with real credentials.
- Continue evaluating upstream C libraries for server-side infrastructure (event loop, config parsing).
- Broaden generated typed-wrapper coverage for any remaining lexicon endpoints not yet wrapped at the agent level.

## Platform support

Cross-compilation targets for Nintendo consoles and Windows:

**Wii**: `.devdeps/wii.cmake`; client-only build, excludes OAuth, server modules, and desktop dependencies.

**Wii U**: `.devdeps/wiiu.cmake`; client-only build.

**3DS**: `.devdeps/3ds.cmake`; client-only build.

**Windows**: `.devdeps/windows.cmake`; MinGW-w64 cross-compilation.

**Linux ARM64**: `.devdeps/linux-aarch64.cmake`; AArch64 cross-compilation.

Each target uses stub platform implementations in `src/platform/` (wii_platform.c, wiiu_platform.c, 3ds_platform.c, windows_platform.c) that return `WF_ERR_NOT_IMPLEMENTED`. Replace those with real backends before shipping.

The desktop (x86_64) build includes the full suite of dependencies: libcurl, OpenSSL, pthreads, libmicrohttpd (if `WOLFRAM_BUILD_SERVER`), SQLite (if `WOLFRAM_BUILD_STORE`), libsodium (if `WOLFRAM_BUILD_STORE_CRYPTO`).