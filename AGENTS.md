# AGENTS.md

Agentic principles and technical context for the `wolfram` repository.

## Technical philosophy

1. **Transport first**: only transport modules (`xrpc.c` and `websocket.c`) do real network I/O. Protocol modules consume those APIs.
2. **No hand-rolled crypto or hashing**: wrap `libsecp256k1` and an established SHA-256 implementation rather than writing field arithmetic or digest logic from scratch.
3. **Stubs are honest**: unimplemented functions return `WF_ERR_INVALID_ARG` and carry a `TODO` explaining what's missing and why — never a silent no-op or a fabricated success. When the missing piece becomes available (e.g. a generated lex transport call), replace the stub with a real implementation rather than leaving it.
4. **Ownership is explicit**: every heap-allocated output has a matching `_free` function documented next to it. No hidden allocations, no implicit ownership transfer.
5. **Protocol parity**: cross-reference `bluesky-social/atproto` for wire formats (XRPC envelopes, DID documents, DAG-CBOR, MST) rather than inferring them.
6. **Pure C runtime**: the SDK and generated clients are C11. Python is permitted only for optional development-time code generation and tests; it must never become a runtime dependency.

## Code style

- **Comments are allowed and encouraged** where they aid understanding — especially next to public API declarations (ownership rules, lifetime, thread-safety), non-obvious transport/protocol details, and the `honest stub`/`TODO` notes described in the philosophy. The existing codebase uses comments pervasively; match that. Do not add noise comments that merely restate the code.
- **Conventional commits**: scope by module — `feat(xrpc)`, `feat(repo)`, `fix(identity)`, `fix(lexgen)`, `docs(roadmap)`, etc. Feature work lands on a dedicated `feat/<area>` branch and is merged to `main` with `--no-ff` so the branch history is preserved.
- **Module layering**: transport → identity → repo → agent. New protocol surface follows the existing pattern: generated lex wrappers (`atproto_lex.{c,h}`) for the wire calls, then `*_typed.{c,h}` owning parsers/builders, then `wf_agent_*` convenience wrappers that sync auth and delegate to the generated call.
- **No commented-out code** left in place; delete dead code or move it to a test.
- Follow the surrounding file's indentation and brace style.

## Development workflow

- Build: `cmake -S . -B build && cmake --build build`
- Test: `ctest --test-dir build`
- Lexicon tests: `python3 test/test_lexgen.py`
- Regenerate lexicons after editing `lexicons/**/*.json` or `tools/wf_lexgen.py`:
  `python3 tools/wf_lexgen.py $(find lexicons -name "*.json") -o include/wolfram/atproto_lex.h --source-output src/atproto_lex.c --header-rel wolfram/atproto_lex.h`
- Optional modules are gated by CMake options: `WOLFRAM_BUILD_SERVER` (libmicrohttpd XRPC server + SSE), `WOLFRAM_BUILD_STORE` (SQLite persistence), `WOLFRAM_BUILD_STORE_CRYPTO` (libsodium at-rest encryption of session credentials).
- Companion-language bindings (C++ `wolfram-cpp`, C# `Wolfram.Interop`) are separate consumer layers that link `libwolfram`; the C11 core stays the single source of truth. Conventions: `docs/bindings-cpp-csharp.md`.
- When picking this back up cold, read the `## Roadmap` section of README.md and `docs/roadmap.md` first — they are kept current and order the remaining work by dependency.
- Before changing protocol behavior, inspect `/Volumes/Storage/Developer/Local/atproto` and then verify maintained upstream libraries/specifications where integration is preferable to custom code. The `rsky` Rust reference at `/Volumes/Storage/Developer/Git/rsky`, when present, is a useful cross-check but is not required.

## Current state

The SDK is broad and multi-layered; almost all of it is implemented and tested. Highlights:

- `xrpc`: libcurl query/procedure calls, encoded scalar/repeated parameters, generic HTTP GET, bearer authentication, binary blob upload (incl. video), DPoP-bound OAuth client (`auth_client`). Tested.
- `session` / `server`: PDS login, resume, refresh, logout, and full `com.atproto.server` account lifecycle (createAccount, app passwords, deactivate, email/account-delete requests, session refresh). The `server_typed` agent wrappers previously stubbed out the parameterless `com.atproto.server` procedures (`requestAccountDelete`, `requestEmailUpdate`, `requestEmailConfirmation`, `refreshSession`); those are now implemented (see lexgen note below). Tested.
- `identity` / `identity_typed` / `plc`: did:plc, did:web, handle DNS TXT (c-ares/POSIX `libresolv`/well-known fallback), `com.atproto.identity` wrappers, and DID PLC operation build/sign/submit helpers. Tested.
- `crypto`: secp256k1 (libsecp256k1) + P-256 (OpenSSL), `did:key`/multikey verification. Tested.
- `repo` / `record`: DAG-CBOR, CIDs, CAR, MST, signed v3 commits, record CRUD, diff verify/apply, operation inversion, schema-driven record encoding. Tested.
- `sync` / `sync_typed` / `sync_subscribe` / `sync_verify`: CAR download, `com.atproto.sync.*` typed wrappers, firehose `subscribeRepos` WebSocket subscription with commit verification. Tested.
- `agent` / `bsky_agent`: high-level BskyAgent bundling session + xrpc + identity + agent; posts, profile, social graph, feeds, preferences, push registration, notifications, blobs, video upload, and `app.bsky.graph` write wrappers (`graph_write.{c,h}`: mute/unmute thread + actor-list, block/list/listitem/starterpack/listblock create/update/delete) tested against an offline mock PDS. Tested.
- `chat` / `chat_typed`: `chat.bsky.*` DM/group/actor/moderation write+query wrappers with chat-service endpoint resolution. Tested.
- `ozone` / `ozone_typed`: full `tools.ozone.*` typed coverage (moderation, queue, report, team, verification, signature, setting, hosting, server, safelink, communication, set value). Tested.
- `moderation`: offline decision engine (blur/alert/inform/filter) from labels, blocks, mutes, muted words, hidden posts. Tested.
- `label` / `labeler_typed` / `unspecced` / `unspecced_typed`: label subscription, labeler service coverage, and full `app.bsky.unspecced` (trends, suggested users, thread v2, etc.). Tested.
- `oauth`: discovery, PKCE S256, ES256 DPoP, PAR, callback validation, `private_key_jwt`, serializable sessions, `wf_auth_client` with DPoP nonce retry. Tested.
- `jetstream`: filtered Jetstream WebSocket subscription with cursor reconnect/backoff and optional zstd. Tested.
- `validate` / `json` / `syntax` / `richtext`: runtime lexicon validation, generic JSON canonicalize/validate, syntax validators, rich-text facets. Tested.
- `store`: optional SQLite session + repo-mirror + persisted-label storage (`WOLFRAM_BUILD_STORE`; `WOLFRAM_BUILD_STORE_CRYPTO` adds libsodium at-rest encryption).
- `xrpc_server`: optional `libmicrohttpd`-backed XRPC server (`WOLFRAM_BUILD_SERVER`). Route registration (query/procedure), **Server-Sent Events (SSE) streaming** for subscription-style endpoints (register an SSE route, push frames from a handler via `wf_xrpc_server_sse_send` / `wf_xrpc_server_sse_send_raw`, close with `wf_xrpc_server_sse_close`; implemented with `MHD_suspend_connection`/`MHD_resume_connection`), per-route token-bucket rate limiting (`wf_rate_limiter`), auth middleware, CORS. Tested offline (round-trip, rate limiter, SSE streaming).
- `feedgen_server`: optional `libmicrohttpd`-backed feed-generator skeleton server helper (`WOLFRAM_BUILD_SERVER`) serving `app.bsky.feed.getFeedSkeleton` and `getFeedGenerator`. Tested.
- `video_typed`: owning parsers + agent wrappers for `app.bsky.video` (job status, upload limits, upload). Tested.
- `actor_prefs_typed` / `actor_status_typed` / `notification_typed` / `notification_v2_typed` / `labeler_typed` / `embed_typed` / `feed_typed` / `feedgen_typed` / `graph_typed` / `list_typed` / `thread_typed` / `bookmark_typed` / `contact_typed` / `draft_typed` / `ageassurance_typed` / `temp_typed` / `admin_typed`: owned typed parsers/builders and agent wrappers across the remaining lexicon namespaces. `actor_status_typed` keeps honest stubs for `getActorStatus`/`getStatus`/`putStatus` because the `app.bsky.actor.status` lexicon defines `main` as a `record` (no query/procedure defs), so no generated transport call exists.
- `lexicon` (`tools/wf_lexgen.py`): generates C declarations, recursive input encoders, endpoint wrappers, and owning output decoders. **Note (fixed):** the generator previously dropped the `_call`/`_call_auth` *definition* for query/procedure endpoints that have neither an `input` schema nor `parameters` (e.g. several parameterless `com.atproto.server` procedures), even though the header declared them. The generator now always emits the definition; regenerate `atproto_lex.{c,h}` after any lexgen change. Tested (`python3 test/test_lexgen.py`).
- `cli`: `wolfram` command-line client (login/post/get/threads/notifications/labels/moderation/profile/timeline/follow/like/repost/search/mute/thread). Built by default.

## Next planned work

- Exercise the gated live example test (`test_examples_live`) in CI with real credentials (it SKIPs cleanly when `BSKY_HANDLE`/`BSKY_PASSWORD` are unset).
- Continue evaluating upstream C libraries for server-side infrastructure (event loop, config parsing).
- Consider WebSocket subscription endpoints served from the XRPC server (`com.atproto.sync.subscribeRepos` / `com.atproto.label.subscribeLabels` relays), building on the SSE streaming transport.
- Broaden generated typed-wrapper coverage for any remaining lexicon endpoints not yet wrapped at the agent level.
