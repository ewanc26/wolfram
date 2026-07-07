# wolfram documentation

Usage documentation for the `wolfram` C11 SDK for the AT Protocol.

This directory contains per-module guides with runnable C snippets. Each
snippet is accurate to the public function signatures in `include/wolfram/`
and follows the project conventions: the `wf_` prefix, `wf_status` return
codes, and explicit ownership (every heap-allocated output has a matching
`_free` that you must call).

| Guide | Module | What it covers |
|-------|--------|----------------|
| [agent.md](agent.md) | `agent.h` | High-level `wf_agent_*` API: login, posts/replies, feeds, profile, social graph, notifications, blobs, preferences, push, pagination. |
| [sync.md](sync.md) | `sync.h`, `sync_subscribe.h`, `sync_verify.h` | Repo CAR download (`wf_sync_get_repo`), CAR verify/import, firehose `subscribeRepos`, commit verification, blob/blocks endpoints. |
| [validate.md](validate.md) | `validate.h` | Runtime validation with `wf_validate_value` / `wf_validate_record` against a lexicon registry. |
| [oauth.md](oauth.md) | `oauth.h` + `oauth/` | Protected-resource / authorization-server metadata discovery, PKCE S256, ES256 DPoP, PAR begin, callback completion, and the `wf_auth_client`. |

## Getting started

Build with CMake (requires CMake ≥ 3.20, libcurl, OpenSSL, libsecp256k1,
and optionally c-ares and zstd):

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

On macOS, install dependencies with Homebrew first:

```sh
brew install cmake curl openssl secp256k1 c-ares zstd
```

If CMake cannot find Homebrew's libcurl, point it at the prefix:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix curl)"
```

Include the single umbrella header in your program:

```c
#include "wolfram/wolfram.h"   /* pulls in every module */
```

Link the `wolfram` target. The core SDK is pure C11; Python is only used for
the optional `tools/wf_lexgen.py` code generator and is never a runtime
dependency. See the top-level [`README.md`](../README.md) for the full module
status table and prerequisites.
