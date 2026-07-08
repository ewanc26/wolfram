# Design & rationale

## Overview

`wolfram` provides the low-level building blocks for speaking AT Protocol from
C: XRPC over HTTP, DID/handle resolution, repo signing, and MST/CBOR/CAR
handling. It is layered like the other SDKs in this ecosystem — a small
transport core, with identity and repo logic built on top rather than tangled
into it.

## Name

`wolfram` was chosen for three reasons:

1. **Tungsten.** Wolfram is an older name for tungsten, a chemical element
   with symbol **W** (from the German *Wolfram*) and atomic number **74**. Its
   important ores include scheelite and wolframite, the latter lending the
   element its alternative name.
2. **Low RAM.** It's light on memory usage, and the codebase is structured to
   stay that way:
   - **No runtime, no allocator, no GC.** The C11 core builds and runs without
     a built-in allocator, event loop, or garbage collector — a long-lived
     firehose/Jetstream consumer has a flat, predictable heap profile rather
     than a sawtooth one.
   - **A small default dependency tree.** A stock build only hard-requires
     `libcurl` and OpenSSL's `libcrypto`. `libsecp256k1`, `libzstd`, and
     `c-ares` are optional and degrade gracefully. SQLite3, libsodium, libidn2,
     and libmicrohttpd are gated behind `WOLFRAM_BUILD_*` flags.
   - **Explicit, documented ownership.** Per `AGENTS.md`, every heap-allocated
     output has a matching `_free` documented at the call site. No hidden
     allocation, no implicit ownership hand-off.
   - **Borrowed views over copies.** Generated Lexicon code borrows input
     strings/arrays/references into caller-owned buffers, while only owning
     decoders return heap objects with a `_output_free`.
3. **Wolves.** Wolves are cool.

## Why C, not Rust?

The dominant AT Protocol SDKs are written in TypeScript
(`bluesky-social/atproto`) and Rust (`rsky`, `indigo`). `wolfram` is a C11
alternative, and that choice is deliberate rather than incidental:

- **Tiny, predictable footprint.** No runtime or allocator built in; no GC
  pause, no `tokio` reactor to size.
- **Trivial to embed.** `libwolfram` links into a host app like zlib or SQLite
  — no Cargo workspace, no `std` redistribution, no second toolchain to bridge.
- **Honest, explicit ownership.** Documented ownership contracts in the header,
  readable by binding authors in any language.
- **Selective dependency surface.** Heavy dependencies are opt-in behind
  `WOLFRAM_BUILD_*` flags; a Rust app tends to pull a much larger default tree.
- **Stable ABI, stable API.** C's stable calling convention makes FFI
  straightforward and future-proof. Generated Lexicon clients are plain C
  structures and functions.
- **Licensing flexibility.** MIT, with no dual-licensing friction for embedded
  or proprietary use.

This is not a claim that C is universally better — the TypeScript and Rust
ecosystems have far richer tooling. `wolfram` targets the case where you want a
small, embeddable, dependency-light native core and are willing to do a little
more of the wiring yourself.
