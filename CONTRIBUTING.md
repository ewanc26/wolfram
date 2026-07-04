# Contributing to wolfram

Thanks for your interest. This is an early scaffold, so there's more scope for structural feedback than usual — but a few ground rules still apply.

## Core philosophy

1. **Protocol parity**: cross-reference [bluesky-social/atproto](https://github.com/bluesky-social/atproto) when implementing anything protocol-level, rather than guessing at wire formats.
2. **No hand-rolled crypto**: signing/verification wraps an established library (`libsecp256k1`, OpenSSL/LibreSSL). This is non-negotiable.
3. **Modular decoupling**: `xrpc` knows nothing about lexicons; `identity`, `crypto`, and `repo` stay independent of each other except where the protocol genuinely requires it.
4. **Test what's implemented**: stubs don't need tests. Anything with a real body does, at minimum via `test/`.

## How to contribute

1. Fork and clone.
2. Build with CMake (see README) and confirm `ctest` passes before you start.
3. Keep PRs scoped to one module or concern.
4. Commit style: `type(scope): description` — e.g. `feat(repo): decode DAG-CBOR maps`.

## Development environment

- CMake + a C11 compiler, or `nix develop` for a ready-made shell.
- `cmake --build build && ctest --test-dir build` before opening a PR.
