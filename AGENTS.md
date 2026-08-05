# AGENTS.md

Agentic principles and technical context for the `wolfram` repository.

## Technical philosophy

1. **Transport first**: client protocol modules route HTTP/XRPC and subscription traffic through the XRPC/WebSocket APIs. Keep raw client I/O in `src/transport/`; DNS resolution, optional server listeners, and platform initialization are separate explicit boundaries.
2. **Libraries first, hand-rolling last**: prefer an established, maintained library before considering writing anything from scratch — wrap `libsecp256k1` and an established SHA-256 implementation rather than writing field arithmetic or digest logic, cJSON for JSON, and the platform's TLS stack rather than implementing transport security. This is a strict policy: hand-rolled code is the last resort, used only when no suitable library exists for the target platform, and then isolated behind a single wrapper with a comment recording what was considered and why. Never hand-roll cryptography, hashing, base64url, canonical DAG-CBOR, JWT, or TLS. Verify a candidate library actually exists and links on the target (pkg-config, CMake `find_package`) before designing around it; never assume a library is available.
3. **Stubs are honest**: unimplemented functions return an error and carry a `TODO` explaining what's missing and why — never a silent no-op or a fabricated success. Unimplemented backends/transports (e.g. Wii U/3DS platform stubs) return `WF_ERR_NOT_IMPLEMENTED`; unimplemented protocol functions with missing inputs return `WF_ERR_INVALID_ARG`. When the missing piece becomes available (e.g. a generated lex transport call), replace the stub with a real implementation rather than leaving it.
4. **Ownership is explicit**: every heap-allocated output has a matching `_free` function documented next to it. No hidden allocations, no implicit ownership transfer.
5. **Protocol parity**: cross-reference `bluesky-social/atproto` for wire formats (XRPC envelopes, DID documents, DAG-CBOR, MST) rather than inferring them. Also read the normative specification at <https://atproto.com> — <https://atproto.com/specs/sync> for the firehose, <https://atproto.com/specs/repository> and `data-model` for encoding. It states requirements the reference source does not spell out (deterministic CBOR ordering, `rev` ordering and clock-drift rejection, `prevData` chain verification, what a consuming relay may reject), and those are what other implementations were written against.
6. **C-first, C++ where beneficial**: the SDK and generated clients are C23 at runtime. Development-time code generation and tests use C++ programs (`tools/*.cpp`, built as CMake targets); they must never become a runtime dependency. Prefer C++ where it is beneficial — RAII-based resource management (e.g. cJSON), performance-critical code, and third-party library integrations with no C equivalent — and use it rather than writing error-prone manual-cleanup C. All C++ code must be wrapped with `extern "C"` to maintain C23 compatibility. Always use `extern "C"` for any wrapper so the rest of the SDK can consume it without C++ headers or types. Where a C library equivalent exists, prefer the C one. Default to C for new code; introduce C++ where the complexity, resource management, or performance requirements justify it.
7. **Console/multi-platform support**: support for embedded and cross-compiled targets (Nintendo Wii, Wii U, 3DS, Windows, Linux/AArch64, etc.) is parity across platforms — platform-specific APIs are isolated in `src/platform/`. The Windows target is fully implemented against the Win32 API. Wii has real libogc primitives, mbedTLS HTTPS and WebSocket (RFC 6455 client), and P-256/did:key crypto; secp256k1 is also implemented via mbedTLS. Wii U has real wut primitives and uses the curl transport (see "Platform support"). 3DS has real libctru primitives (LightLock mutex, osGetTime clock, httpc transport) and mbedtls-based P-256/did:key crypto.
8. **No duplication**: if a piece of logic already exists elsewhere in this codebase, call into it rather than reimplementing it a second time — this applies within the SDK itself, not just against external libraries (see point 2). Two independent implementations of the same rule can silently drift apart, and only one of them getting fixed is worse than either alone. This matters most for security-sensitive logic (signature/nonce normalization, replay handling, constant-time comparisons), but applies generally: before writing a new helper, check whether an equivalent already exists nearby and extract/reuse it instead of copying it.

## Code style

- **Comments are allowed and encouraged** where they aid understanding — especially next to public API declarations (ownership rules, lifetime, thread-safety), non-obvious transport/protocol details, and the `honest stub`/`TODO` notes described in the philosophy. The existing codebase uses comments pervasively; match that. Do not add noise comments that merely restate the code.
- **Atomic conventional commits**: every commit must contain exactly one logical change. Scope by module — `feat(xrpc)`, `feat(repo)`, `fix(identity)`, `fix(lexgen)`, `docs(roadmap)`, etc. Never mix unrelated changes in a single commit (e.g. do not combine a code change with a docs update). Feature work lands on a dedicated `feat/<area>` branch and is merged to `main` with `--no-ff` so the branch history is preserved. If a commit touches multiple concerns, split it into multiple sequential commits.
- **No AI co-authors**: commits must not add a `Co-authored-by:` trailer crediting an AI agent (e.g. `Co-authored-by: Claude ...` or `Co-authored-by: Kilo ...`). AI assistance is welcome, but credit for committed work goes to human authors only. Omit the trailer entirely.
- **Module layering**: transport → identity → repo → agent. New protocol surface follows the existing pattern: generated lex wrappers (`atproto_lex.{c,h}`) for the wire calls, then `*_typed.{c,h}` owning parsers/builders, then `wf_agent_*` convenience wrappers that sync auth and delegate to the generated call.
- **No commented-out code** left in place; delete dead code or move it to a test.
- Follow the surrounding file's indentation and brace style.

## Versioning

- **Tag every version bump**: a commit that changes `VERSION` in
  `CMakeLists.txt` must also create a signed annotated git tag on that commit:
  `git tag -s v<major>.<minor>.<patch> -m "v<major>.<minor>.<patch>"` (use `-s`
  when a signing key is available, otherwise `-a`). Push tags with
  `git push --tags`.
- **Bump in the same commit**: the version change and the tag must refer to the
  same commit — no separate bump commit without a tag.
- **Create a GitHub release for every version bump**: after tagging, create a
  release via `gh release create v<major>.<minor>.<patch> --title "v<major>.<minor>.<patch>" --generate-notes` (the tag is named by that single positional; a second positional would be treated as an upload file). The release must be created in the same commit as the tag — no separate release without a tag.
- **One version definition**: the version lives only in the `VERSION` line of
  `CMakeLists.txt`. The `WOLFRAM_VERSION_STRING` and
  `WOLFRAM_VERSION_{MAJOR,MINOR,PATCH}` macros are derived from
  `PROJECT_VERSION` as PUBLIC compile definitions — there is no `version.h` to
  keep in sync.
- **The version macros are a consumer interface**: MetalBear reads
  `WOLFRAM_VERSION_STRING` at build time and renders it on its landing page
  and in `/operator.json` (`software.wolframVersion`), so the definitions must
  stay PUBLIC and keep their current names — a consumer would not notice a
  private or renamed macro until the version on the page goes stale.
- **Version independently**: Wolfram and MetalBear are sibling projects, not a
  single release unit. Each repo versions itself on its own history; do not
  sync the version string to the sibling's.
- **No version jumps**: bump from the immediately previous released version.
  Never skip a patch, minor, or major number; do not backfill gaps with
  phantom tags or releases.

## Development workflow

- **Desktop builds**: `cmake -S . -B build && cmake --build build`
- **Tests**: `ctest --test-dir build`
- **Lexicon generation**: `cmake --build build --target wf_lexgen_tool && ./build/wf_lexgen_tool $(find lexicons -name "*.json") -o include/wolfram/atproto_lex.h --source-output src/atproto_lex.c --header-rel wolfram/atproto_lex.h` (the tool is C++ — `tools/wf_lexgen.cpp`, replacing the removed `tools/wf_lexgen.py`)
- **Optional modules**: gated by CMake options — `WOLFRAM_BUILD_SERVER` (libmicrohttpd XRPC server), `WOLFRAM_BUILD_STORE` (SQLite persistence), `WOLFRAM_BUILD_STORE_CRYPTO` (libsodium at-rest encryption), `WOLFRAM_BUILD_TEST_HTTPD` (libmicrohttpd mock PDS for offline HTTP integration tests), `WOLFRAM_BUILD_IDN` (libidn2 internationalised-handle resolution), `WOLFRAM_BUILD_CPP` (C++ RAII wrapper `wolfram-cpp`). Platform/example/test flags: `WOLFRAM_BUILD_WII` / `_WIIU` / `_3DS` / `_WINDOWS`, `WOLFRAM_BUILD_EXAMPLES`, `WOLFRAM_BUILD_TESTS`.
- **Platform support for multi-target builds**: cross-compilation targets (Wii, Wii U, 3DS, Windows, linux-aarch64) are supported via `.devdeps/*.cmake` toolchain files. Wii and Wii U use real platform primitives (libogc and wut respectively); 3DS retains a stub platform implementation. Use `-DWOLFRAM_BUILD_*` accordingly. Desktop (x86_64) still uses libcurl, OpenSSL, and pthreads.
- **When picking this back up cold**: read the `## Roadmap` section of `README.md` and `docs/roadmap.md` first — they are kept current and order the remaining work by dependency.
- **Before changing protocol behavior**: inspect `/Volumes/Storage/Developer/Local/atproto` and verify maintained upstream libraries/specifications where integration is preferable to custom code. The `rsky` Rust reference at `/Volumes/Storage/Developer/Git/rsky`, when present, is a useful cross-check but is not required.

## Repository map and generated-code contract

- `include/wolfram/` is the installed C API. Public structs must document ownership, optional fields, lifetime, and the matching free routine; preserve C++ guards and avoid leaking private dependency types unnecessarily.
- `src/transport`, `src/session`, `src/identity`, `src/repo`, `src/crypto`, `src/sync`, and `src/agent` contain the principal client layers. Root-level `src/*_typed.c` and `src/agent/*_typed.c` add owned parsers/builders and agent conveniences around generated calls.
- `src/server`, `src/blob`, and `src/feedgen_server.c` are optional service-building infrastructure, not ports of the upstream PDS/AppView/Ozone backends. `src/store` and the server repo store have separate CMake gates and storage/security assumptions.
- `lexicons/` is a checked-in snapshot of the upstream lexicon tree. `tools/wf_lexgen.cpp` (built as the `wf_lexgen_tool` CMake target) generates `include/wolfram/atproto_lex.h` and `src/atproto_lex.c`; both generated files are checked in. Never hand-edit either generated file. Update the source lexicons, regenerate both outputs, run the C++ `test_lexgen` CTest, and review the generated diff together.
- `test/fixtures/` includes copied upstream interoperability vectors and API-shaped JSON. Keep fixture provenance and byte-level data intact; update a fixture only when the authoritative upstream format or the behavior under test changes.
- `cpp/` and `dotnet/` are bindings with their own generated ownership/interop layers. A C ABI or ownership change is incomplete until affected bindings and smoke tests are rebuilt.

The bundled lexicon filenames currently match the local upstream checkout, but file parity is not semantic proof. For every protocol change, inspect the relevant lexicon and TypeScript implementation/tests under `/Volumes/Storage/Developer/Local/atproto`; check canonical encoding, validation order, limits, error semantics, pagination, union tags, and ownership rather than comparing endpoint names alone.

## Validation matrix

- The default desktop configure requires libcurl and OpenSSL, fetches pinned cJSON/libcbor sources, and builds examples and tests. A clean configure therefore may require network access even when tests themselves are offline.
- `cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure` is the baseline desktop check. Use a fresh build directory after changing options, public layouts, generated code, or platform selection.
- CTest includes the C++ `test_lexgen` test (a port of the removed `test_lexgen.py`, covering the generated codecs, wrappers, and bundled-endpoint coverage); a C++ compiler and the cJSON/OpenSSL dev libraries are therefore development/test requirements even though they are not runtime dependencies. `validate_corpus` can pass by printing `SKIP` unless `WF_ATPROTO_LEXICONS` points at an upstream corpus, and `examples_live` passes with a `SKIP` unless live credentials are supplied. Do not report those surfaces as exercised from a green default CTest run alone.
- Exercise optional modules explicitly: `WOLFRAM_BUILD_SERVER` also requires libmicrohttpd and SQLite; `WOLFRAM_BUILD_STORE` requires SQLite; store encryption additionally requires libsodium; `WOLFRAM_BUILD_TEST_HTTPD`, `WOLFRAM_BUILD_IDN`, and `WOLFRAM_BUILD_CPP` each add distinct coverage. Build only the matrix relevant to the change, but state what remained disabled or skipped.
- Embedded configurations force tests, examples, OAuth, and server modules off. A console cross-build proves compile/link compatibility, not HTTP/TLS correctness on hardware. Wii **and Wii U** P-256 work requires an installation-unique 64-byte seed supplied before use and a rotate/persist/commit cycle; never add a shared fallback seed. Wii secp256k1 and 3DS P-256 are implemented via mbedTLS. 3DS platform/transport are implemented via libctru.
- Cross-builds hide link errors: an undefined symbol in a static archive only fails when something links it. After changing platform source selection, check the archive (`powerpc-eabi-nm build-wiiu/libwolfram.a | grep ' U '`) rather than trusting a successful `libwolfram.a`. Routing the Wii U through the socket transport built cleanly for months while leaving five undefined `wii_tls_*` symbols in the archive.
- Run focused executables or `ctest -R <name>` while iterating. For ABI changes, rebuild all consumers rather than trusting an incremental relink. For parser/encoder work, add malformed, limit, allocation/cleanup, and round-trip cases as appropriate.

## Security and correctness boundaries

- Treat service URLs, DIDs, handles, NSIDs, AT URIs, record keys, cursors, JSON/CBOR/CAR, HTTP headers, redirects, and server request bodies as untrusted. Preserve size/recursion limits, exact audience/issuer/subject checks, algorithm restrictions, low-S signing rules where required, replay/nonce handling, and refresh retry bounds.
- Never log or commit access/refresh tokens, app passwords, OAuth state/verifiers, DPoP or signing private keys, store encryption keys, live response bodies, or Wii entropy. Tests should mint ephemeral keys and use fixtures or the in-process mock server.
- `WF_OK` means the promised output is initialized and owned as documented. On failure, leave outputs safely freeable and release every partial allocation. Do not collapse protocol-specific failures into success or infer missing union members.
- The presence of a wrapper does not establish complete behavior. Several typed conveniences are intentionally honest stubs because no matching lexicon endpoint exists, and some repository-store paths still map protocol-specific errors to broad status codes. Search the implementation and tests before claiming coverage.
- Platform implementations are not interchangeable: desktop crypto/HTTP uses OpenSSL/libcurl and optional secp256k1, while Wii uses mbedTLS plus compatibility code and excludes substantial desktop surface. Test the backend whose behavior changed.

## Current state

The SDK is broad and multi-layered, with extensive offline coverage. “Implemented” below means a concrete code path exists; it does not imply every optional build, live service, or console backend ran in the current validation. Known honest stubs and partial modules remain and must stay visible. Highlights:

- `xrpc`: libcurl query/procedure calls, encoded scalar/repeated parameters, generic HTTP GET, bearer authentication, binary blob upload (incl. video), DPoP-bound OAuth client (`auth_client`). Tested.
- `session` / `server`: PDS login, resume, refresh, logout, and full `com.atproto.server` account lifecycle (createAccount, app passwords, deactivate, email/account-delete requests, session refresh). The `server_typed` agent wrappers implement the parameterless `com.atproto.server` procedures (`requestAccountDelete`, `requestEmailUpdate`, `requestEmailConfirmation`, `refreshSession`). Tested.
- `identity` / `identity_typed` / `plc`: did:plc, did:web, handle DNS TXT (c-ares/POSIX `libresolv`/well-known fallback), `com.atproto.identity` wrappers, and DID PLC operation build/sign/submit helpers. `wf_agent_identity_rotate_handle` now wires the full handle-rotation flow: it builds the rotation operation locally (validation gate) and, given the out-of-band `requestPlcOperationSignature` token, signs it server-side via `signPlcOperation` and submits it via `submitPlcOperation`. Tested.
- `crypto`: secp256k1 (libsecp256k1) + P-256 (OpenSSL), `did:key`/multikey verification. Tested.
- `repo` / `record`: DAG-CBOR, CIDs, CAR, MST, signed v3 commits, record CRUD, diff verify/apply, operation inversion, schema-driven record encoding. Tested.
- `sync` / `sync_typed` / `sync_subscribe` / `sync_verify`: CAR download, `com.atproto.sync.*` typed wrappers, firehose `subscribeRepos` WebSocket subscription with commit verification. Tested.
- `sync_publish`: firehose event production. Frames must be **canonical
  DAG-CBOR**, and three separate defects of that kind each made the PDS
  unfederatable while every local test passed:

  | defect | our decoder | a strict decoder |
  |---|---|---|
  | CID link missing the `0x00` multibase prefix | skips leading zeros | rejects |
  | map keys not in canonical order | order-independent | rejects |
  | integer encoded wider than necessary | accepts any width | rejects |

  The last one was the worst: every integer was built at 64-bit width, so the
  frame header's `op: 1` took eight bytes where one is canonical. A consumer
  failed on the *header* and dropped the connection before reading anything,
  which looks from the outside exactly like a relay that will not talk to you.
  Integers go through `int_item`, which picks the narrowest form; map keys are
  sorted centrally in `serialize_two` (RFC 8949 §4.2.1: shorter keys first,
  then bytewise) so builders stay free to add fields in whatever order reads
  best.

  **Assert on encoded bytes, never on a round-trip** — our own decoder is
  tolerant of precisely what the encoder gets wrong. When something will not
  federate, capture a live frame from `bsky.network` and compare it field by
  field and byte by byte; that is what found all three. This cannot be caught by a round-trip — our
  decoder accepts any order, so insertion-ordered frames decode perfectly and
  are rejected by a strict reader. Assert on encoded bytes, and when in doubt
  compare against a live `#commit` from `bsky.network`. Firehose event production — builds the framed `{header}{body}` CBOR messages a relay/PDS streams over WebSocket (`wf_sync_publish_event` / `wf_sync_publish_error`), the exact inverse of the `sync_subscribe` decoder, covering `commit`/`sync`/`identity`/`account`/`info` and `op:-1` error frames. Round-trip tested by `test_sync_publish`. Tested.
- `agent` / `bsky_agent`: high-level BskyAgent bundling session + xrpc + identity + agent; posts, profile, social graph, feeds, preferences, push registration, notifications, blobs, video upload, and `app.bsky.graph` write wrappers (`graph_write.{c,h}`: mute/unmute thread + actor-list, block/list/listitem/starterpack/listblock create/update/delete) tested against an offline mock PDS. Tested.
- `chat` / `chat_typed`: `chat.bsky.*` DM/group/actor/moderation write+query wrappers with chat-service endpoint resolution. Tested.
- `ozone` / `ozone_typed`: full `tools.ozone.*` typed coverage (moderation, queue, report, team, verification, signature, setting, hosting, server, safelink, communication, set value). Tested.
- `moderation`: offline decision engine (blur/alert/inform/filter) from labels, blocks, mutes, muted words, hidden posts. Tested.
- `label` / `labeler_typed` / `label_subscribe_typed` / `unspecced` / `unspecced_typed`: label subscription (low-level `wf_label_subscribe_start` over `com.atproto.label.subscribeLabels`, plus `wf_label_typed` owning parsers and the agent-level `wf_agent_subscribe_labels_typed` consumption wrapper that resolves the labeler service, syncs auth, and dispatches each decoded `#labels` event as an owned `wf_mod_label`), labeler service coverage, and full `app.bsky.unspecced` (trends, suggested users, thread v2, etc.). Tested.
- `oauth`: discovery, PKCE S256, ES256 DPoP, PAR, callback validation, `private_key_jwt`, serializable sessions, `wf_auth_client` with DPoP nonce retry, **and OAuth resource-server token verification** (`oauth/verify.h`: `wf_oauth_verify_bearer` / `wf_oauth_verify_dpop` / `wf_oauth_verify_request` over the `wf_crypto_*` P-256/JWK/SHA-256/base64url primitives, with a `wf_oauth_dpop_replay_cache` and `wf_oauth_trusted_keys`). Tested.
- `jetstream`: filtered Jetstream WebSocket subscription with cursor reconnect/backoff and optional zstd. Tested.
- `validate` / `json` / `syntax` / `richtext`: runtime lexicon validation, generic JSON canonicalize/validate, syntax validators, rich-text facets. `syntax` and `json` are C++ (migrated from C) with RAII for cJSON objects; the public C ABI is preserved via `extern "C"`.
- `store`: optional SQLite session + repo-mirror + persisted-label storage (`WOLFRAM_BUILD_STORE`; `WOLFRAM_BUILD_STORE_CRYPTO` adds libsodium at-rest encryption).
- `xrpc_server`: optional `libmicrohttpd`-backed XRPC server (`WOLFRAM_BUILD_SERVER`). Route registration, **Server-Sent Events (SSE) streaming** for subscription-style endpoints, and **WebSocket (RFC 6455) subscription endpoints** with per-route token-bucket rate limiting, auth middleware, CORS. Tested offline.
  Closing a WS stream half-closes and drains the socket before handing it back
  to libmicrohttpd. This is not politeness: `close()` on a socket with unread
  inbound data makes the kernel send RST, and a received RST discards the
  peer's receive buffer — destroying frames that were delivered correctly but
  not yet read. A subscriber that is merely slow (a loaded machine is enough)
  loses the tail of the stream, which for a firehose is exactly the events a
  consumer needs in order to resume from the right cursor. Covered by
  `test_xrpc_server_ws_slow_client`, which fails deterministically without it.
  Raw-socket test clients must also replay whatever the handshake read past
  `\r\n\r\n`: one `read()` can return the 101 response and the first frames
  together, and discarding the remainder makes a test wait for bytes it is
  already holding.

  **Membership of `server->ws_streams` means "a joinable worker exists for
  this stream."** Every WebSocket lifetime bug found here came from that
  invariant not holding, and each was fatal rather than cosmetic:

  - The worker frees its upgrade context as its first act and can run to
    completion in microseconds, so nothing may dereference that context after
    `pthread_create` succeeds. The worker parks on `thread_ready` until the
    spawning thread has stored its id, which is also why the id and the list
    insertion happen under the same lock — `wf_xrpc_server_stop` reads that
    field under `ws_mutex` and would otherwise join a zero id.
  - Teardown drains the list one entry at a time. A bounded snapshot that
    still cleared the whole list left every worker past the limit unlinked and
    unjoined, running on into a freed server; 65 firehose subscribers is an
    ordinary load, not an exotic one.
  - `ws_stopping` latches under `ws_mutex` so no upgrade can join the list
    behind the drain loop.
  - A worker that finishes on its own detaches itself, decided under
    `ws_mutex` in the same critical section as the unlink (`reaped`). Without
    it every completed connection leaked a thread descriptor and its stack for
    the life of the process — unbounded on a host with churning subscribers.
  - Unlink before closing the socket. The kernel reuses the fd immediately, so
    a still-listed stream let `stop()` `shutdown()` an unrelated live
    connection.

  `test_xrpc_server_ws_many` covers the teardown path with 96 concurrent
  subscribers. It only detects the use-after-free under a sanitizer — a plain
  build can exit 0 while corrupting memory — so it sets `abort_on_error`, and
  a handler that merely returns is not enough to expose it: the workers must
  still be running when the server is freed.
- `feedgen_server`: optional `libmicrohttpd`-backed feed-generator skeleton server helper (`WOLFRAM_BUILD_SERVER`) serving `app.bsky.feed.getFeedSkeleton` and `getFeedGenerator`. Tested.
- `relay_server`: optional `libmicrohttpd`-backed generic upstream→downstream WebSocket subscription relay (`WOLFRAM_BUILD_SERVER`), built on the server's WS endpoints and the libcurl WebSocket client transport. `wf_xrpc_server_register_relay` registers a WS route (e.g. `com.atproto.sync.subscribeRepos`) that, on a downstream connect, opens an upstream `ws(s)://` connection and forwards each received message byte-for-byte until either side closes, then closes downstream. Protocol-agnostic (raw frames, no parsing) so it serves `subscribeRepos`, `subscribeLabels`, or any binary subscription. Config deep-copied and freed by `wf_relay_config_free`; the `wf_relay_server` handle owns the copy and is freed by `wf_relay_server_free` after `wf_xrpc_server_free`. Tested offline (`test_relay_server`).
- `blob_store`: **Migrated to MetalBear.** The PDS blob persistence/serving code (`wf_blob_store*`) has been moved to the MetalBear repository as `metalbear_blob_store*` (header `metalbear_blob_store.h`, core store `metalbear_blob_store.c`, XRPC route handlers `metalbear_blob_store_server.c`). The original Wolfram source files remain for historical reference but are no longer compiled or part of the SDK.
- `video_typed`: owning parsers + agent wrappers for `app.bsky.video` (job status, upload limits, upload). Tested.
- `actor_prefs_typed` / `actor_status_typed` / `notification_typed` / `notification_v2_typed` / `labeler_typed` / `embed_typed` / `feed_typed` / `feedgen_typed` / `graph_typed` / `list_typed` / `thread_typed` / `bookmark_typed` / `contact_typed` / `draft_typed` / `ageassurance_typed` / `temp_typed` / `admin_typed`: owned typed parsers/builders and agent wrappers across the remaining lexicon namespaces. `actor_status_typed` keeps honest stubs for `getActorStatus`/`getStatus`/`putStatus` because the `app.bsky.actor.status` lexicon defines `main` as a `record` (no query/procedure defs). Tested.
- `lexicon` (`tools/wf_lexgen.cpp`, built as the `wf_lexgen_tool` CMake target): generates C declarations, recursive input encoders, endpoint wrappers, and owning output decoders. The generator always emits the definition for query/procedure endpoints that have neither an `input` schema nor `parameters`. Tested.
- `cli`: `wolfram` command-line client (login/post/get/threads/notifications/labels/moderation/profile/timeline/follow/like/repost/search/mute/thread, plus `oauth-login`/`oauth-callback`, `block`/`unblock`, `notifications update-seen`, `repo put-record`/`delete-record`/`list-records`/`describe`, `feed get`/`author`, `moderation report`; global `--json` flag for raw JSON output). Built by default.

## Next planned work

- Exercise the gated live example test (`test_examples_live`) in CI with real credentials.
- Continue evaluating upstream C libraries for server-side infrastructure (event loop, config parsing).
- Broaden generated typed-wrapper coverage for any remaining lexicon endpoints not yet wrapped at the agent level.
- Optionally add a server-side end-to-end test that emits a built `sync_publish` frame over a `subscribeRepos`-style WebSocket route and decodes it back with the `sync_subscribe` client, and extend `sync_publish` to the `subscribeLabels` `#labels` event.

## Platform support

Cross-compilation targets for Nintendo consoles and Windows:

**Wii**: `.devdeps/wii.cmake`; client-only build, excludes OAuth, server modules, and desktop dependencies.

**Wii U**: `.devdeps/wiiu.cmake`; client-only build. Requires `dkp-pacman -S wiiu-dev wiiu-pkg-config wiiu-curl wiiu-mbedtls`. The toolchain file delegates to devkitPro's own `WiiU.cmake`.

**3DS**: `.devdeps/3ds.cmake`; client-only build.

**Windows**: `.devdeps/windows.cmake`; MinGW-w64 cross-compilation.

**Linux ARM64**: `.devdeps/linux-aarch64.cmake`; AArch64 cross-compilation.

Wii uses libogc for network initialisation, LWP mutexes, and monotonic timing,
plus mbedTLS for verified HTTPS, a real RFC 6455 WebSocket client, and
P-256/did:key crypto. secp256k1 is also implemented via mbedTLS.
3DS uses libctru for platform primitives (LightLock mutex, osGetTime clock,
httpc transport) and mbedtls for P-256/did:key crypto. The Windows target is
fully implemented against the Win32 API (`windows_platform.c`).

**Wii U is no longer a stub target.** `wiiu_platform.c` is implemented against
wut: `socket_lib_init()` for the socket library, `OSMutex` for locking, and
`OSGetTime` for the clock. Note the clock conversion is done divide-first —
wut's `OSTicksToMicroseconds()` multiplies by 1000000 before dividing, which
overflows uint64 for ticks-since-2000 values and would return a wrapped,
non-monotonic result.

Two things differ from the Wii and are easy to get wrong:

- **Transport is curl, not the socket stack.** "Embedded" is not one thing.
  `xrpc_wii.c`/`websocket_wii.c` depend on `wii_tls.c`, which is written
  against libogc's `net_*` API and only builds for the Wii. The Wii U has a
  real libcurl portlib, so it uses the same curl transport as desktop. This is
  the `WOLFRAM_USE_SOCKET_TRANSPORT` axis in CMakeLists.txt, deliberately
  separate from `WOLFRAM_BUILD_EMBEDDED`. Crypto stays on the embedded axis —
  no console has OpenSSL, so all of them use the mbedTLS-backed `crypto_wii.c`.
- **wut owns nn::ac.** `__init_wut_socket` already calls `ACInitialize()` and
  `ACConnectAsync()`, and `__fini_wut_socket` calls `ACClose()`/`ACFinalize()`.
  `wf_platform_init()` therefore touches only the socket library; deciding
  whether the link is up stays with the application.

**Wii U entropy fails closed, for the same reason the Wii's does.** devkitPro's
Wii U mbedTLS defines `mbedtls_hardware_poll`, so seeding a DRBG from it
compiles and runs — but it is `srand(OSGetSystemTick()); rand()`, a
timer-seeded libc PRNG. Since `wii_tls_random()` feeds P-256 key generation and
ECDSA signing, accepting it would make private keys recoverable by search.
`src/crypto/wiiu_random.c` supplies that symbol (the Wii gets it from
`wii_tls.c`, which the Wii U does not build) and refuses to produce output
until the application provisions 64 real bytes via `wf_wiiu_set_entropy_seed()`
— see `include/wolfram/wiiu.h`, which mirrors the Wii API including the
rotate/persist/commit cycle. Never add a shared fallback seed.

**That same weak poll also seeds libcurl's TLS on Wii U, and closing it needs
the application's help.** `wf_wiiu_set_entropy_seed()` fixes Wolfram's own
P-256 work, but libcurl holds a *separate* mbedTLS entropy context for the
transport — so client randoms and ephemeral ECDHE keys were still being drawn
from `srand(OSGetSystemTick())` on every request. Three details make that
unfixable from inside Wolfram alone:

- devkitPro's patch puts `mbedtls_hardware_poll` in `library/entropy.c`, the
  same translation unit that registers it. A competing definition collides, and
  `ld --wrap` does not redirect an intra-unit reference, so it cannot be
  overridden at link time.
- The portlib is built with `MBEDTLS_NO_PLATFORM_ENTROPY`, so that poll is the
  only source in the pool. Nothing sits behind it.
- No PowerPC-reachable hardware RNG is documented for this console; IOSU
  gatekeeps the crypto hardware.

`wf_xrpc_client_set_tls_rng()` is the way in. It installs
`CURLOPT_SSL_CTX_FUNCTION`, and the callback calls `mbedtls_ssl_conf_rng()` on
the `mbedtls_ssl_config *` curl hands over. curl invokes that callback *after*
its own `mbedtls_ssl_conf_rng()` and before `mbedtls_ssl_setup()`, so ours wins
for the whole handshake — verified against curl 8.7.1, which is what
`wiiu-curl` ships.

The hook compiles only where libcurl is genuinely mbedTLS-backed
(`WOLFRAM_CURL_MBEDTLS`, implied by `WOLFRAM_WIIU`) *and* re-checks
`curl_version_info()` at runtime, because handing the callback's `void *` to a
different backend's context type would be straight type confusion. Everywhere
else the setter returns `WF_ERR_UNSUPPORTED` rather than accepting an RNG it
will never call.

The desktop (x86_64) build includes the full suite of dependencies: libcurl, OpenSSL, pthreads, libmicrohttpd (if `WOLFRAM_BUILD_SERVER`), SQLite (if `WOLFRAM_BUILD_STORE`), libsodium (if `WOLFRAM_BUILD_STORE_CRYPTO`).
