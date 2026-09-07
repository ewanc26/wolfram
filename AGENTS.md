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