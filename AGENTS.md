# AGENTS.md

Guidance for AI coding agents working in **wolfram**.

## Project overview

Primarily C AT Protocol SDK: client-side, wire-level implementation (XRPC, OAuth/DPoP, identity, repos/MST/CAR, firehose/Jetstream, moderation, CLI, generated clients). Not a port of the PDS/AppView/Ozone backends.

- Language: C
- Default branch: main

## Working rules

- Inspect the README, manifests, CI workflows, and nearby code before editing.
- Preserve existing architecture, naming, formatting, and error-handling conventions.
- Use project scripts for validation; never claim checks you did not run.
- Keep changes scoped and update tests or documentation when behavior changes.
- Use feature branches and pull requests.
- Treat generated files, credentials, deployment configuration, and release metadata as sensitive.

## Recent history

Sync AGENTS.md from zincfox; Sync AGENTS.md from zincfox; Sync CONTRIBUTING.md from zincfox; Sync CONTRIBUTING.md from zincfox; test(xrpc-server): cover AppView-forwarding request headers

## Classic Mac OS 9

- The Mac OS 9 transport uses Open Transport through the external macTLS async stream API.
- Keep the transport cooperative: never add pthreads, blocking socket calls, libcurl, OpenSSL, or modern POSIX APIs to the Mac OS 9 path.
- macTLS owns TLS, certificate validation, entropy, and the Open Transport endpoint; Wolfram owns HTTP/XRPC framing and response parsing.
- Use the application-provided `wf_macos9_set_yield_callback()` hook when a synchronous Wolfram operation must yield to the Mac event loop.
- Do not silently introduce a local OAuth/DPoP dependency into the Mac OS 9 transport. Platinum authenticates through its bridge.
- The transport is strict C89 for CodeWarrior, and CMake enforces it (`C_STANDARD 90`, `C_EXTENSIONS OFF`, `-Wdeclaration-after-statement -Wstrict-prototypes -Wvla`). Keep it that way: no `//` comments, no mid-block declarations, no `snprintf`, `strdup`, `%zu`, VLAs, or C99/POSIX libc.
- Do not reintroduce a cJSON dependency into this target. cJSON is C99 and cannot compile under those settings; the bounded JSON scanner in `xrpc_mac9.c` replaces it.
- Transport-level limits are policies, not conveniences: `https_only` and the response-size cap are checked before any I/O and before the `wf_xrpc_set_handler()` test seam, so neither can be switched off by a handler.
- The public macTLS surface lives in `include/wolfram/macos9_tls.h`; keep `<wolfram/xrpc.h>` free of Mac-specific types.
- Test the transport offline via `wf_xrpc_set_handler()` and the stubs in `test/macos9_mactls_stub.c`. The suite asserts no macTLS entry point is ever called, so a test cannot pass by reaching the network.

## Client self-update

- Manifest parsing, version comparison and SHA-256 verification live in `include/wolfram/update.h` and are specified in `docs/update.md`. Clients call them; do not re-implement in a consumer.
- `src/update/update_core.c` is strict C89 with no `stdint.h` and is part of the Mac OS 9 target; CI compiles it with `-std=c89 -pedantic-errors`. Keep it that way. `manifest.c` needs cJSON and stays out of that target.
- Change the manifest or version rules by changing `test/vectors/update/*.json` and `docs/update.md` in the same PR. Consumers, including Platinum's TypeScript port, run those vectors.
- Never describe a SHA-256 from the same release as authentication. `signature` is reserved and unverified; do not add a public key to the repo, the owner supplies it.

