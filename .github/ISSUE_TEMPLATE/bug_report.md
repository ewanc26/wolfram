---
name: Bug report
about: Report a defect in wolfram (crash, wrong output, build failure, protocol mismatch)
title: "[bug]: "
labels: ["bug"]
assignees: []
---

## Affected module
Which part of wolfram? e.g. `repo`, `xrpc`/`auth_client`, `agent`, `sync`/`sync_subscribe`, `validate`, `moderation`, `store`, `oauth`, `lexicon` / `tools/wf_lexgen.py`, build system, or docs.

## Describe the bug
A clear and concise description of what the bug is.

## To reproduce
Steps and a minimal code snippet that triggers it:
```c
// ...
```
- Inputs (NSID, record JSON, DID/handle, lexicon path, etc.):
- Expected behaviour:
- Actual behaviour:

## Environment
- wolfram commit / version: (output of `git rev-parse HEAD`)
- OS / compiler / CMake version:
- Build options used (e.g. `WOLFRAM_BUILD_STORE`, `WOLFRAM_BUILD_TEST_HTTPD`, `WOLFRAM_BUILD_STORE_CRYPTO`):
- Linked library versions if relevant (OpenSSL, libsecp256k1, libcbor, cJSON, SQLite3, libsodium):

## Protocol context (if applicable)
- AT Protocol lexicon / NSID involved:
- Reference: does [bluesky-social/atproto](https://github.com/bluesky-social/atproto) or [rsky](https://github.com/blacksky-algorithms/rsky) behave differently? Link the relevant lexicon or source.
