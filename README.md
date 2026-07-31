# wolfram

A primarily C SDK for the AT Protocol — a client-side, wire-level implementation of the
protocol, not a port of the upstream `atproto` service backends.

**Version:** 0.2.0

The runtime library and all generated client code are pure C11. The optional
Lexicon generator is a development-time Python tool and is never linked into,
embedded in, or required by applications using `libwolfram`.

**Scope.** Wolfram is a faithful C port of the AT Protocol's *protocol/SDK
layer* — the client and wire-format packages of the upstream TypeScript
repository (`@atproto/api`, `xrpc`, `identity`, `repo`, `crypto`, `syntax`,
`oauth`, `lex`, `lexicon`, `did`, `ws-client`). It does **not** port the
upstream *server-side service backends* (`pds`, `bsky` AppView, `ozone`,
`bsync`), which are application servers (databases, business logic, hosting)
rather than protocol SDK code. The optional `WOLFRAM_BUILD_SERVER` component is
a generic XRPC server *framework* (routing, auth, SSE, WebSocket, relay, blob
store) you can build a service on top of — it is not itself a PDS, AppView, or
Ozone implementation.

**Protocol parity:** The bundled Lexicon snapshot matches the 394 files in the
upstream atproto repository. Generated C11 and OAuth-authenticated clients cover
all 312 query/procedure endpoints, and dedicated streaming clients cover all
three subscription endpoints. CI enforces this complete wire-level coverage.

**Status:** Broad, multi-layer *client* coverage is implemented and tested —
transport (XRPC/WebSocket), identity (DID/handle + `com.atproto.identity`
typed wrappers), repo (DAG-CBOR/CAR/MST), agent (com.atproto.* + chat/ozone/
moderation), OAuth (DPoP/PAR), sync (firehose + Jetstream), moderation, DID PLC
ops, rich text, syntax/validate/json, labeler service coverage,
`app.bsky.video` typed wrappers (job status / upload limits / upload),
notification v2 + activity subscriptions, optional SQLite store persistence,
app.bsky.graph write wrappers (`wf_agent_graph_*`: mute/unmute thread +
actor-list, block/list/listitem/starterpack/listblock create/update/delete),
and higher-level endpoint examples. Wire-level coverage of the protocol is
complete; the upstream *service backends* (PDS, AppView, Ozone, bsync) are out
of scope, as described under **Scope** above.

Beyond the client surface above, the SDK also ships streaming/infra modules —
`jetstream` (filtered Jetstream subscription with cursor reconnect/backoff),
`sync_publish` (firehose event production, the inverse of `sync_subscribe`),
and `relay_server` / `feedgen_server` (libmicrohttpd helpers) — plus dedicated
`*_typed` parser/wrapper families across every lexicon namespace (including
honest `actor_status_typed` stubs where the lexicon defines only a `record`).
OAuth additionally covers resource-server token verification. See
[`AGENTS.md`](AGENTS.md) (Current state) for the full per-module status.
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

That builds `./build/wolfram`, the CLI, which is the quickest way to check the
SDK end to end:

```sh
./build/wolfram post https://bsky.social you@example.com yourpassword "Hello from wolfram!"
./build/wolfram help post
```

See [docs/cli.md](docs/cli.md) for the full command reference. The `examples/`
programs referenced elsewhere in the docs are a separate set of small,
single-purpose binaries built only with `-DWOLFRAM_BUILD_EXAMPLES=ON`.

`wolfram` is organized into small, layered modules — transport → identity →
repo → agent. See [docs/modules.md](docs/modules.md) for the full status table.

## Cross-compilation Support

Cross-compilation targets for Nintendo platforms and other architectures are supported:

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

The Wii U is the one console target that can also build the XRPC **server**,
using the bundled libmicrohttpd shim (`src/server/mhd_shim.c`) in place of a
library that has no console port. devkitPro packages no SQLite, so point the
build at the [amalgamation](https://sqlite.org/download.html):

```sh
cmake -S . -B build-wiiu \
  -DCMAKE_TOOLCHAIN_FILE=.devdeps/wiiu.cmake \
  -DWOLFRAM_BUILD_WIIU=ON \
  -DWOLFRAM_BUILD_SERVER=ON \
  -DWOLFRAM_SQLITE_AMALGAMATION=/path/to/sqlite-amalgamation
cmake --build build-wiiu
```

**The Wii U build is theoretically compatible: it compiles and links, and it
has never been run on hardware.** Everything it depends on cross-compiles —
secp256k1, SQLite, mbedTLS (including server-side TLS), curl — and the shim
is exercised by the full test suite natively, where `-DWOLFRAM_MHD_SHIM=ON`
substitutes it for the real library on any platform. That is evidence the code
is portable and honours MHD's contract. It is not evidence that it boots, and
a clean cross-compile says nothing about a runtime that has never executed a
single instruction of it. Treat it as a starting point for someone with a
console, not as a supported target.

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

### Linux (ARM64)

A cross-compilation target for Linux on AArch64 is supported (e.g. from
x86_64 macOS/Linux to ARM64 Linux).

```sh
cmake -S . -B build-aarch64 \
  -DCMAKE_TOOLCHAIN_FILE=.devdeps/linux-aarch64.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-aarch64
```

The toolchain file is at `.devdeps/linux-aarch64.cmake`. Toolchains for
additional architectures (`arm32.cmake`, `amd64.cmake`) are also provided
under `.devdeps/`.

The Wii platform implements libogc networking, LWP mutexes, monotonic timing,
mbedTLS HTTPS with CA validation, and the P-256/did:key crypto needed by the
client. It requires a unique externally provisioned entropy seed through
`wf_wii_set_entropy_seed`. Wii WebSocket and secp256k1 support remain honest
stubs. The 3DS platform, transport, and crypto backends remain stubbed. The
Windows target is fully implemented against the Win32 API.

The Wii and 3DS builds are client-only: server modules, OAuth flows, and the
desktop dependencies (libcurl, OpenSSL, pthreads) are excluded. The Wii U can
build the server as well — see above — but only as far as compiling, which is
not the same as working.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT
