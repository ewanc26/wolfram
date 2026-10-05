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
- Treat generated files, credentials, deployment configuration, and release metadata as sensitive.

<!-- flow:begin -->
## Unified flow (canonical: ewanc26/wolfram, docs/flow.md)

This block is byte-identical in every repo of the stack and is drift-checked by CI. Do not edit a copy; change it by PR to wolfram, then copy it out.

- Branch from main as `<type>/<slug>`. Types: feat fix docs ci chore refactor test perf build ui release (titles and commits also allow revert). Slug: lowercase `a-z 0-9 . _ -`.
- Commit subjects and PR titles are Conventional Commits: `type(scope): summary`. Keep commits focused. Never push an empty commit.
- Agent commits end with the `Co-Authored-By:` and `Claude-Session:` trailers the session supplies. PR descriptions use `.github/PULL_REQUEST_TEMPLATE.md` (What this changes, Verification, Docs) and end with the session link.
- Nothing goes straight to main. Branch, open a PR, wait for green CI, merge the PR (squash). Required checks: `CI gate` and `flow / conventions`.
- Never merge red. Never force-push. Never skip, disable or delete a test to get green: read the job log, reproduce, fix the root cause, wait, repeat. A red main is fixed before anything else.
- Update AGENTS.md, README and docs/ in the same PR as the change. AGENTS.md is imperative and exact; README and docs are user-facing prose.
- State exactly what was verified and where (host, emulator, hardware). Never claim hardware you did not use.
- Releases go through the repo's own release script only, and only after every consumer in the stack has been verified against the change.
- Anything only the owner can supply (credentials, hardware results, money, irreversible actions): file an issue labelled `needs-owner` and move on.
- No secrets in the repo or its CI. No Vercel. No registry publishing.
<!-- flow:end -->

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
