# wolfram documentation

Usage documentation for the `wolfram` C11 SDK for the AT Protocol.

This directory contains per-module guides with runnable C snippets. Each
snippet is accurate to the public function signatures in `include/wolfram/`
and follows the project conventions: the `wf_` prefix, `wf_status` return
codes, and explicit ownership (every heap-allocated output has a matching
`_free` that you must call).

| Guide | Module | What it covers |
|-------|--------|----------------|
| [getting-started.md](getting-started.md) | build | Prerequisites, building, optional modules, Lexicon generation, examples. |
| [agent.md](agent.md) | `agent.h` | High-level `wf_agent_*` API: login, posts/replies, feeds, profile, social graph, notifications, blobs, preferences, push, pagination. |
| [agent_usage.md](agent_usage.md) | `agent.h` | Deeper agent internals and end-to-end usage patterns. |
| [typed_api.md](typed_api.md) | `*_typed.h` | Owning typed parsers/builders for feed, thread, graph, list, embed, notification, and more. |
| [identity.md](identity.md) | `identity.h`, `plc.h` | DID/handle resolution, `com.atproto.identity` ops, and PLC operations. |
| [sync.md](sync.md) | `sync.h`, `sync_subscribe.h`, `sync_verify.h` | Repo CAR download (`wf_sync_get_repo`), CAR verify/import, firehose `subscribeRepos`, commit verification, blob/blocks endpoints. |
| [moderation.md](moderation.md) | `moderation.h` | `wf_mod_*` decision engine (blur/alert/inform/filter). |
| [video.md](video.md) | `video_typed.h` | Video blob upload, job-status polling, and post embedding. |
| [validate.md](validate.md) | `validate.h` | Runtime validation with `wf_validate_value` / `wf_validate_record` against a lexicon registry. |
| [oauth.md](oauth.md) | `oauth.h` + `oauth/` | Protected-resource / authorization-server metadata discovery, PKCE S256, ES256 DPoP, PAR begin, callback completion, and the `wf_auth_client`. |
| [notification.md](notification.md) | `notification.h` | Notification listing and preferences. |
| [unspecced.md](unspecced.md) | `unspecced_typed.h` | `app.bsky.unspecced` typed parsers (trends, suggested users, thread v2, etc.). |
| [cli.md](cli.md) | `cli` | The `wolfram` command-line client reference. |
| [bindings-cpp-csharp.md](bindings-cpp-csharp.md) | `cpp/`, `dotnet/` | C++ RAII (`wolfram-cpp`) and C# (`Wolfram.Interop`) binding guidance. |
| [modules.md](modules.md) | all | Full module/status table. |
| [design.md](design.md) | — | Design, rationale, and the name. |
| [roadmap.md](roadmap.md) | — | What's built and what's next. |

## Getting started

Build with CMake (requires CMake ≥ 3.20, libcurl, and OpenSSL; libsecp256k1,
c-ares, and zstd are optional and degrade gracefully):

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
