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
