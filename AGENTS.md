# AGENTS.md

Agentic principles and technical context for the `wolfram` repository.

## Technical philosophy

1. **Transport first**: `xrpc.c` is the only module that should do real network I/O. Everything else consumes it.
2. **No hand-rolled crypto or hashing**: wrap `libsecp256k1` and an established SHA-256 implementation rather than writing field arithmetic or digest logic from scratch.
3. **Stubs are honest**: unimplemented functions return `WF_ERR_INVALID_ARG` and carry a `TODO` explaining what's missing and why — never a silent no-op or a fabricated success.
4. **Ownership is explicit**: every heap-allocated output has a matching `_free` function documented next to it. No hidden allocations, no implicit ownership transfer.
5. **Protocol parity**: cross-reference `bluesky-social/atproto` for wire formats (XRPC envelopes, DID documents, DAG-CBOR, MST) rather than inferring them.

## Development workflow

- Build: `cmake -S . -B build && cmake --build build`
- Test: `ctest --test-dir build`
- Commit scoping: `feat(xrpc)`, `feat(repo)`, `fix(identity)`, etc.
- When picking this back up cold, read the `## Roadmap` section of README.md first — it's kept current and orders the remaining work by dependency, not by importance.

## Current state

- `xrpc`: fully working query/procedure calls over libcurl. Generic `wf_http_get` for arbitrary URLs.
- `identity`: DID method detection + resolution for did:plc (via plc.directory) and did:web (via .well-known/did.json). Handle resolution via DNS TXT `_atproto.<handle>` (POSIX `res_query`, fallback to HTTPS well-known). cJSON for JSON parsing.
- `crypto`: secp256k1 key generation, signing, and verification via libsecp256k1. P-256 keygen, sign, and verify via OpenSSL EC API (`EC_KEY_new_by_curve_name`, `ECDSA_do_sign`, `ECDSA_do_verify`).
- `repo`: DAG-CBOR decoder and encoder implemented and tested (canonical validation, tag 42 CID links, deterministic map key sorting on CBOR bytes). CID computation (`wf_cid_of_block`, `wf_cid_to_string`) — SHA-256 via OpenSSL, base32 (RFC4648 lowercase, no padding). CAR parsing (`wf_car_parse`) with block lookup (`wf_car_find_block`). MST: node parse/find, tree add/delete, node build/finalize. Commit: parse and signed creation (`wf_commit_create` with secp256k1 or P-256 signing).
