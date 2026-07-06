# AGENTS.md

Agentic principles and technical context for the `wolfram` repository.

## Technical philosophy

1. **Transport first**: only transport modules (`xrpc.c` and `websocket.c`) do real network I/O. Protocol modules consume those APIs.
2. **No hand-rolled crypto or hashing**: wrap `libsecp256k1` and an established SHA-256 implementation rather than writing field arithmetic or digest logic from scratch.
3. **Stubs are honest**: unimplemented functions return `WF_ERR_INVALID_ARG` and carry a `TODO` explaining what's missing and why — never a silent no-op or a fabricated success.
4. **Ownership is explicit**: every heap-allocated output has a matching `_free` function documented next to it. No hidden allocations, no implicit ownership transfer.
5. **Protocol parity**: cross-reference `bluesky-social/atproto` for wire formats (XRPC envelopes, DID documents, DAG-CBOR, MST) rather than inferring them.
6. **Pure C runtime**: the SDK and generated clients are C11. Python is permitted only for optional development-time code generation and tests; it must never become a runtime dependency.

## Development workflow

- Build: `cmake -S . -B build && cmake --build build`
- Test: `ctest --test-dir build`
- Lexicon tests: `python3 test/test_lexgen.py`
- Commit scoping: `feat(xrpc)`, `feat(repo)`, `fix(identity)`, etc.
- When picking this back up cold, read the `## Roadmap` section of README.md first — it's kept current and orders the remaining work by dependency, not by importance.
- Before changing protocol behavior, inspect `/Volumes/Storage/Developer/Local/atproto` and then verify maintained upstream libraries/specifications where integration is preferable to custom code.

## Current state

- `xrpc`: libcurl query/procedure calls, encoded scalar/repeated parameters, generic HTTP GET, bearer authentication, and binary response bodies. Tested.
- `session`: PDS login, resume, refresh, get, and logout with explicit owned credential state. Tested.
- `identity`: did:plc and did:web resolution. Handle DNS TXT resolution uses c-ares when available, POSIX `libresolv` otherwise, then HTTPS well-known fallback. Multi-record/chunk ambiguity rules follow the handle specification. Tested.
- `crypto`: secp256k1 via libsecp256k1 and P-256 via OpenSSL. `did:key`/multikey verification handles the protocol multicodec encodings. Tested.
- `repo`: libcbor-backed canonical DAG-CBOR, CIDs, CAR parse/write, deterministic MST add/delete/merge, signed v3 commits, record create/get/update/delete, and full CAR ownership/signature/content verification/import. Tested.
- `record`: schema-driven JSON-to-DAG-CBOR structured record encoding. Tested.
- `sync`: `com.atproto.sync.getRepo` full/diff CAR download and parsing. Tested.
- `jetstream`: libcurl WebSockets, filters, runtime `options_update`, cursor reconnect/backoff, and optional dictionary-based zstd decoding. Tested.
- `oauth`: strict atproto metadata discovery, PKCE S256, ES256 DPoP, PAR/token/refresh calls with nonce retry, callback validation, public and `private_key_jwt` client authentication, serializable authorization/token state, authorization-begin orchestration through PAR, callback-to-session completion, and authenticating XRPC wrapper (`wf_auth_client`) with DPoP binding, session-refresh, and DPoP nonce retry. Tested.
- `syntax`: AT Protocol syntax validation — DID, handle, at-identifier, NSID, record key, TID, AT URI, RFC 3339 datetime, and BCP 47 language tag validators. Tests cover all interop-test-file patterns from the atproto reference. Tested.
- `lexicon`: `tools/wf_lexgen.py` generates pure-C declarations, recursive input encoders (including referenced definitions), endpoint wrappers, and owning output decoders. Full-corpus headers compile. Tested.
- `richtext`: UTF-8 grapheme length, facet detection (mentions, links, tags, cashtags), segment iteration, text insert/delete with facet index adjustment, domain/TLD validation. Tested.
- `sync_subscribe`: WebSocket firehose subscription to `com.atproto.sync.subscribeRepos`, CBOR frame parsing (commit/sync/identity/account/info events), cursor tracking, exponential-backoff reconnection. Tested with integration test that self-terminates via callback, including `*out = handle` fix so handle is valid during callbacks.
- `sync_verify`: Firehose commit verification — parses the CAR from a commit event, resolves the repo DID to obtain the signing key, and verifies the commit signature, block integrity, and MST structure. Tested.
- `test_sync_subscribe`: Integration test connecting to `wss://bsky.network`, receiving up to 5 events, verifying WebSocket connection and CBOR event parsing. Graceful skip when network unavailable.

## Next planned work

- Higher-level endpoint examples using generated clients.
- Repository sync toward verified incremental diff application and operation inversion.
- Blob upload support (`com.atproto.repo.uploadBlob`) — binary POST in XRPC transport.
- Label subscription (`com.atproto.label.subscribeLabels`) — WebSocket label stream.
- Additional sync endpoints — getBlob, getBlocks, getRecord, listBlobs.
- Server account operations — createAccount, createAppPassword, etc.
- Lexicon validation — runtime JSON/CBOR validation against Lexicon schema.
- High-level client API — a "BskyAgent" equivalent wrapping session + XRPC + identity.
