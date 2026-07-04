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

## Current state (accurate as of scaffold)

- `xrpc`: fully working query/procedure calls over libcurl.
- `identity`: DID method detection only; resolution unimplemented.
- `crypto`: pure stubs, no backend chosen yet.
- `repo`: pure stubs; CBOR decode is the correct next step, not MST.
