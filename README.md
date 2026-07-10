# wolfram

A C SDK for the AT Protocol.

The runtime library and all generated client code are pure C11. The optional
Lexicon generator is a development-time Python tool and is never linked into,
embedded in, or required by applications using `libwolfram`.

**Status:** Broad, multi-layer coverage is implemented and tested — transport
(XRPC/WebSocket), identity (DID/handle + `com.atproto.identity` typed wrappers),
repo (DAG-CBOR/CAR/MST), agent (com.atproto.* + chat/ozone/moderation), OAuth
 (DPoP/PAR), sync (firehose + Jetstream), moderation, DID PLC ops, rich text,
syntax/validate/json, labeler service coverage, `app.bsky.video` typed wrappers
(job status / upload limits / upload), notification v2 + activity
subscriptions, optional SQLite store persistence, app.bsky.graph write
wrappers (`wf_agent_graph_*`: mute/unmute thread + actor-list,
block/list/listitem/starterpack/listblock create/update/delete), and
higher-level endpoint examples — though it is not yet at full feature
parity with the official SDKs.
The optional `libmicrohttpd`-backed XRPC server (`WOLFRAM_BUILD_SERVER=ON`)
supports route registration, auth middleware, a token-bucket rate limiter,
Server-Sent Events (SSE) streaming, and WebSocket (RFC 6455) subscription
endpoints for subscription-style feeds. A feed-generator skeleton server helper
(`feedgen_server.h`) and a generic upstream→downstream WebSocket subscription
relay (`relay_server.h`, forwarding raw frames from an upstream `ws(s)://`
subscription such as `com.atproto.sync.subscribeRepos` or
`com.atproto.label.subscribeLabels`) are provided.

## Documentation

Per-module usage guides (runnable C snippets):

- [`docs/agent.md`](docs/agent.md) — high-level `wf_agent_*` API
- [`docs/sync.md`](docs/sync.md) — repo CAR, firehose, commit verification
- [`docs/validate.md`](docs/validate.md) — `wf_validate_value` / `wf_validate_record`
- [`docs/moderation.md`](docs/moderation.md) — `wf_mod_*` decision engine
- [`docs/oauth.md`](docs/oauth.md) — OAuth/DPoP, PKCE, PAR, callback flow

Topic guides:

- [Design & rationale](docs/design.md) — overview, the name, and why C not Rust
- [Getting started](docs/getting-started.md) — install, build, persistence, Lexicon generation, examples
- [Modules](docs/modules.md) — full module/status table
- [Roadmap](docs/roadmap.md) — what's built and what's next
- [CLI reference](docs/cli.md) — the `wolfram` command-line client

## Quick start

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

```sh
./build/create_post https://bsky.social you@example.com yourpassword "Hello from wolfram!"
```

`wolfram` is organized into small, layered modules — transport → identity →
repo → agent. See [docs/modules.md](docs/modules.md) for the full status table.

## Nintendo Console Support

Cross-compilation targets for Nintendo platforms are supported:

### Wii

A cross-compilation target for the Nintendo Wii (devkitPPC/libogc) is
supported. The Wii build is **client-only** — server modules, OAuth flows,
and desktop dependencies (libcurl, OpenSSL, pthreads) are excluded.

```sh
cmake -S . -B build-wii \
  -DCMAKE_TOOLCHAIN_FILE=.devdeps/wii.cmake \
  -DWOLFRAM_BUILD_WII=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-wii
```

Requires [devkitPro](https://devkitpro.org/) with devkitPPC and libogc
installed. The toolchain file is at `.devdeps/wii.cmake`.

### Wii U

A cross-compilation target for the Nintendo Wii U (devkitPPC/wut) is
supported.

```sh
cmake -S . -B build-wiiu \
  -DCMAKE_TOOLCHAIN_FILE=.devdeps/wiiu.cmake \
  -DWOLFRAM_BUILD_WIIU=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-wiiu
```

Requires [devkitPro](https://devkitpro.org/) with devkitPPC and the wut SDK
installed. The toolchain file is at `.devdeps/wiiu.cmake`.

### 3DS

A cross-compilation target for the Nintendo 3DS (devkitARM/libctru) is
supported.

```sh
cmake -S . -B build-3ds \
  -DCMAKE_TOOLCHAIN_FILE=.devdeps/3ds.cmake \
  -DWOLFRAM_BUILD_3DS=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-3ds
```

Requires [devkitPro](https://devkitpro.org/) with devkitARM and libctru
installed. The toolchain file is at `.devdeps/3ds.cmake`.

### Windows

A cross-compilation target for Windows (MinGW-w64) is supported.

```sh
cmake -S . -B build-windows \
  -DCMAKE_TOOLCHAIN_FILE=.devdeps/windows.cmake \
  -DWOLFRAM_BUILD_WINDOWS=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-windows
```

Requires MinGW-w64. The toolchain file is at `.devdeps/windows.cmake`.

All four targets use stub implementations for transport and crypto that
return `WF_ERR_NOT_IMPLEMENTED`. Before integrating with a console application,
replace the stubs with platform-specific backends — see the `TODO` comments
in the individual platform stub files (`*_platform.c`).

For consoles (Wii, Wii U, 3DS), the builds are client-only — server modules,
OAuth flows, and desktop dependencies (libcurl, OpenSSL, pthreads) are excluded.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT
