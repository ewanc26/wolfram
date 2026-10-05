<p align="center">
  <img src="docs/logo.svg" alt="Wolfram" width="420">
</p>

<p align="center">
  <a href="https://github.com/ewanc26/wolfram/actions/workflows/ci.yml"><img src="https://github.com/ewanc26/wolfram/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="https://github.com/ewanc26/wolfram/releases/latest"><img src="https://img.shields.io/github/v/release/ewanc26/wolfram?sort=semver" alt="Latest release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/github/license/ewanc26/wolfram" alt="AGPL-3.0"></a>
  <a href="https://github.com/sponsors/ewanc26"><img src="https://img.shields.io/github/sponsors/ewanc26?logo=githubsponsors&logoColor=white&label=sponsors" alt="Sponsor"></a>
</p>

# wolfram

A C/C++ SDK for the AT Protocol — a client-side, wire-level implementation of the
protocol, not a port of the upstream `atproto` service backends.

![version](https://img.shields.io/github/v/release/ewanc26/wolfram?label=version)

> **Not affiliated with Wolfram Alpha.** Despite the name, this project is an
> independent AT Protocol SDK and has no connection to, or endorsement from,
> Wolfram Alpha, Wolfram Research, Mathematica, or Stephen Wolfram.

The runtime library and all generated client code are pure C23. The optional
Lexicon generator is a development-time C++ tool (`tools/wf_lexgen.cpp`, built
as the `wf_lexgen_tool` CMake target) and is never linked into, embedded in, or
required by applications using `libwolfram`.

C is the default language; C++ is used for complex or sensitive components where C is insufficient — RAII-based resource management (e.g. cJSON in `syntax` and `json`), performance-critical code, and third-party library integrations. All C++ code exposes a C ABI via `extern "C"` so the SDK never requires a C++ toolchain at runtime. See the policy in [AGENTS.md](AGENTS.md).

## Core Features

- XRPC client with bearer authentication, DPoP-bound OAuth, and binary blob upload
- Current AT Protocol lexicon snapshot: 402 documents and 319 query/procedure definitions; JSON calls are generated and binary procedures use the dedicated upload/import APIs
- Streaming subscriptions: `subscribeRepos` (firehose), `subscribeLabels`, and `subscribeRepos` with cursor reconnect/backoff
- Identity: DID resolution (`did:plc`, `did:web`), handle DNS TXT resolution, PLC operations
- Repo: DAG-CBOR encoding/decoding, CAR import/export, MST, signed v3 commits, record CRUD, diff verify/apply
- Agent: high-level `wf_agent_*` wrappers for `com.atproto.*`, `app.bsky.*`, `chat.bsky.*`, `tools.ozone.*`, and `app.bsky.graph` write operations
- OAuth: discovery, PKCE S256, ES256 DPoP, PAR, callback validation, token refresh/revoke, and resource-server token verification
- Moderation: offline decision engine (blur/alert/inform/filter) from labels, blocks, mutes, muted words, hidden posts
- Validation: runtime lexicon validation, JSON canonicalize/validate, rich-text facets
- Optional SQLite persistence (`WOLFRAM_BUILD_STORE`) with at-rest encryption via libsodium (`WOLFRAM_BUILD_STORE_CRYPTO`)
- Optional libmicrohttpd-backed XRPC server (`WOLFRAM_BUILD_SERVER`) with
  buffered JSON procedures, bounded-memory binary procedures, SSE streaming,
  WebSocket subscriptions, and relay forwarding

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

That builds `./build/wolf`, the CLI, which is the quickest way to check the
SDK end to end:

```sh
./build/wolf post https://bsky.social you@example.com yourpassword "Hello from wolfram!"
./build/wolf help post
```

See [docs/cli.md](docs/cli.md) for the full command reference. The `examples/`
programs referenced elsewhere in the docs are a separate set of small,
single-purpose binaries built only with `-DWOLFRAM_BUILD_EXAMPLES=ON`.

`wolfram` is organized into small, layered modules — transport → identity →
repo → agent. See [docs/modules.md](docs/modules.md) for the full status table.

## Build and test

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

The default desktop configure requires libcurl and OpenSSL, fetches pinned
cJSON/libcbor sources, and builds examples and tests. A clean configure may
require network access even when tests themselves are offline.

Two tests, `relay_server` and `sync_publish_server`, fail on macOS and pass in
CI. They are the only two that stand up a loopback HTTP server and drive a
real WebSocket handshake and stream against it; on macOS the client side fails
(`FAIL: client wf_websocket_connect to relay`, and `FAIL: published event
seq N was not matched` in the sync case). Confirmed against unmodified `main`
on 2026-10-03, so red there is not by itself evidence that a change broke
something — check on Linux, or exclude them locally with
`ctest -E 'relay_server|sync_publish_server'`.

## Releases

Wolfram is a source library, so a release is a version bump, an annotated tag
and a GitHub release with no attached artifacts. Cut one with:

```sh
tools/release.sh --consumers-verified minor        # 0.25.0 -> 0.26.0
tools/release.sh --consumers-verified 0.26.0       # or name the version outright
tools/release.sh --dry-run minor                   # run the checks, change nothing
tools/release.sh --full --consumers-verified minor # also cover the full-features configuration
```

The version lives only in `project()` in `CMakeLists.txt`; everything else is
derived from it. There is no CHANGELOG, so the release notes are generated
from the commit subjects since the previous tag.

The script refuses to run unless it is on `main` with a clean tree and local
`main` identical to `origin/main` — a tag on a commit nobody else can fetch is
not a release. It also refuses without `--consumers-verified`: a release is cut
only once metalbear, cobalt, indigo and platinum have been built against the
change, and the script cannot check that for you, so the flag is your word.
Nothing is pushed to `main` directly. The script runs the default build and
test configuration that CI's `default` job covers, commits the bump on
`release/vX.Y.Z`, opens a pull request, merges it once its checks are green,
and tags the merge commit. If CI fails, nothing is merged or tagged. The rest
of the working flow is in [docs/flow.md](docs/flow.md).

`--full` additionally builds and tests the server, store, store-crypto and C++
wrapper configuration. On macOS that run excludes `relay_server` and
`sync_publish_server`, the two documented macOS failures above, and says so;
every other test still has to pass.

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

#### Crypto on the consoles

No console ships OpenSSL, so all three console targets compile
`src/crypto/crypto_wii.c` (mbedTLS, via devkitPro's 3DS/Wii portlibs) instead
of `src/crypto/crypto.c`, and `src/platform/openssl_compat.c` supplies the
handful of OpenSSL entry points that survive (`SHA256`, base64). P-256 signing,
verification and did:key derivation all work, and the console and desktop
backends interoperate in both directions: a signature made by one verifies
under the other. This is emulator-verified on 3DS, and the DER
`ECDSA-Sig-Value` parser agrees with the OpenSSL backend on every input tested,
including the malformed ones — `wf_crypto_ecdsa_der_to_raw` holds the same DER
minimality rule OpenSSL does so the two cannot drift.

The console crypto surface is otherwise complete, and this is worth checking
when adding to `crypto.h`, because a function missing from `crypto_wii.c` does
not fail the console build — it produces a link error in the *application*
instead, long after the library looks healthy:

```sh
# every function declared in a public header must be defined in the archive
nm build-3ds/libwolfram.a | grep -E '^[0-9a-f]+ T ' | awk '{print $3}' | sort -u > /tmp/have.txt
grep -ohE '\bwf_[a-z0-9_]+\(' include/wolfram/*.h | tr -d '(' | sort -u > /tmp/want.txt
comm -23 /tmp/want.txt /tmp/have.txt
```

Expect the XRPC server, blob store, filesystem store, OAuth client and the other
consoles' entropy hooks in that list; they are excluded on purpose
(`CMakeLists.txt`, `WOLFRAM_BUILD_EMBEDDED`). Anything in `crypto.h` should not
be.

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
mbedTLS HTTPS with CA validation, P-256/did:key crypto, and secp256k1
(did:key) crypto via mbedTLS. It requires a unique externally provisioned
entropy seed through `wf_wii_set_entropy_seed`. Wii WebSocket support remains
an honest stub. The 3DS platform now has real libctru primitives (LightLock
mutex, osGetTime clock, httpc transport) and mbedtls-based P-256/did:key
crypto. The Windows target is fully implemented against the Win32 API.

The Wii and 3DS builds are client-only: server modules, OAuth flows, and the
desktop dependencies (libcurl, OpenSSL, pthreads) are excluded. The Wii U can
build the server as well — see above — but only as far as compiling, which is
not the same as working.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## Support

If you find this project useful, consider supporting its development:

[![Ko-fi](https://img.shields.io/badge/Ko--fi-F16061?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/ewancroft)
[![GitHub Sponsors](https://img.shields.io/badge/GitHub%20Sponsors-30363D?style=for-the-badge&logo=github&logoColor=white)](https://github.com/sponsors/ewanc26)

## License

[GNU AGPL-3.0](LICENSE). Running a modified Wolfram as part of a public network
service obliges you to offer its users the corresponding source.

## Star History

<a href="https://www.star-history.com/?repos=ewanc26%2Fwolfram&type=date&legend=bottom-right">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=ewanc26/wolfram&type=date&theme=dark&legend=bottom-right" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=ewanc26/wolfram&type=date&legend=bottom-right" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=ewanc26/wolfram&type=date&legend=bottom-right" />
 </picture>
</a>

## Classic Mac OS 9 transport

Wolfram includes a standalone Classic Mac OS 9 XRPC transport for consumers such as Platinum. It uses Open Transport for TCP and the external [macTLS](https://github.com/mplsllc/macTLS) async stream for TLS 1.3 with TLS 1.2 fallback. The transport does not use libcurl, OpenSSL, pthreads, or modern POSIX networking APIs.

Build the transport with:

```sh
cmake -S . -B build-macos9-transport \
  -DWOLFRAM_BUILD_MACOS9_TRANSPORT=ON \
  -DWOLFRAM_MACTLS_ROOT=/path/to/macTLS
cmake --build build-macos9-transport --target wolfram-macos9-transport
ctest --test-dir build-macos9-transport -R macos9_transport
```

The transport exposes the same low-level XRPC request semantics as every other Wolfram backend, including bearer authentication, one automatic refresh-and-retry on an expired token, DPoP nonce capture, bounded response buffering, and Content-Length/chunked HTTP responses. Every response path is bounded by `wf_xrpc_client_set_max_response_bytes()`, so a hostile or broken server cannot stream an unbounded body into memory. The Mac OS 9 adapter yields through an application callback so a cooperative event loop can continue servicing the UI.

Most applications should use the ordinary `<wolfram/xrpc.h>` API and leave the transport alone. `<wolfram/macos9_tls.h>` is public only because a client has to install a yield callback so a request can be pumped without freezing its UI.

The transport is written to strict C89 for CodeWarrior, and CMake enforces that rather than only documenting it: the target compiles as `-std=c90` with `-Wdeclaration-after-statement`, `-Wstrict-prototypes` and `-Wvla`, and CI recompiles both sources with `-std=c89 -pedantic-errors`. It also has no cJSON dependency, because cJSON is C99 and would not compile under those settings; the XRPC error envelope is decoded by a small bounded scanner instead.

Two things this does **not** give you, both by design:

- It is not a full `libwolfram` Mac OS 9 port. That needs a dedicated Classic Mac OS 9 crypto backend for OAuth/DPoP signing and verification, which is not in scope. Clients that authenticate through an external bridge (Platinum does) need no local signing and can use this transport as-is.
- CI compiles and tests it on Linux against macTLS's public header, which carries plain-C fallback typedefs. That verifies the C89 dialect and the transport's own logic, but it is not a native CodeWarrior build or a real Mac OS 9 runtime test.
