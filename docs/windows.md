# Windows

Wolfram cross-compiles for Windows with MinGW-w64. What comes out is the same
subset of the library the consoles get, as a static archive, and it has no
network transport yet. This page says exactly what that means, so nobody has
to find out from a link error.

## Building

From Linux or macOS, with nothing installed but CMake, git and MinGW-w64
(`apt install mingw-w64`, `brew install mingw-w64`):

```sh
cmake -S . -B build-windows \
  -DCMAKE_TOOLCHAIN_FILE=.devdeps/windows.cmake \
  -DWOLFRAM_BUILD_WINDOWS=ON
cmake --build build-windows
```

That produces `build-windows/libwolfram.a` and
`build-windows/wolfram_windows_smoke.exe`. CMake fetches cJSON, libcbor,
base64url and mbedTLS itself; there is no curl, OpenSSL or libsecp256k1 to
find, and nothing is read from the build machine's own libraries.

The build is static only. `-DBUILD_SHARED_LIBS=ON` is refused at configure
time, because the subset leaves references to modules that are not built
(OAuth, the server) and only a static link with `--gc-sections` can drop them.

A CMake consumer links the `wolfram` target and adds `-Wl,--gc-sections` to
its own link; the target carries everything else. A consumer linking by hand
needs:

- `-DWOLFRAM_WINDOWS -D_POSIX_THREAD_SAFE_FUNCTIONS=200112L` when compiling
  against the headers;
- `-Wl,--gc-sections` on the link;
- `libwolfram.a`, then `_deps/mbedtls-build/library/libmbedcrypto.a` from the
  build directory, then `-lws2_32 -lbcrypt -lpthread`.

The binaries need Windows 8 or later, for `GetSystemTimePreciseAsFileTime`.

## What is in the build

Windows sits on the embedded axis of `CMakeLists.txt`
(`WOLFRAM_BUILD_EMBEDDED`), next to the Wii, Wii U and 3DS, and builds the
same sources they do, with three of its own and the consoles' OpenSSL shim:

| Source | What it supplies |
|--------|------------------|
| `src/platform/windows_platform.c` | `wolfram/platform.h`: Winsock start-up, critical-section mutexes, a UNIX-epoch microsecond clock (`GetSystemTimePreciseAsFileTime`). |
| `src/crypto/windows_random.c` | The random source behind key generation and signing, from `BCryptGenRandom`. |
| `src/transport/windows_tls.c` | The connection backend of the socket transport. It opens no connections; see below. |
| `src/platform/openssl_compat.c` | The few OpenSSL entry points the library still calls (SHA-256, base64), shared with the consoles. |

Crypto is `src/crypto/crypto_wii.c` on mbedTLS, pinned by commit in
`CMakeLists.txt` and built from source as `mbedcrypto` only. P-256 and
secp256k1 key generation, signing, verification and did:key all work, and
signatures are low-S, so they verify under the OpenSSL backend and on the
network.

So this works on Windows today: records, CIDs, TIDs, CBOR, CAR and MST
handling, the typed parsers and builders, lexicon validation, signing and
verification, and the XRPC client up to the point where it needs a connection.

## What is not

- **No network transport.** `wf_xrpc_*` calls return `WF_ERR_NETWORK` and
  `wf_websocket_connect` fails the same way, before anything is sent. The
  client code above the connection is the real thing, so a consumer can drive
  it through `wf_xrpc_set_handler()` with its own HTTP stack, or with canned
  replies in tests.
- **No OAuth client, XRPC server, CLI, stores or examples.** They are excluded
  for every embedded target. Their headers still install; calling into them
  fails at link time.
- **No `pattern` in `wf_json_validate`.** There is no POSIX `<regex.h>` in
  MinGW-w64. A string checked against a schema that uses `pattern` fails
  validation with an error that says so, rather than passing unchecked.
- **No test suite.** The unit tests are off for every embedded target. One
  program is built instead: `test/windows_smoke.c`.

## The smoke program

A static archive hides undefined symbols until something links against it, so
the Windows build always links `wolfram_windows_smoke.exe`. It calls into each
Windows-specific source above and checks a known SHA-256 vector, a CID computed
two ways, a sign and verify round trip on both curves, that a TID's timestamp
is the current UNIX time, that a request with no backend fails and a request
through the handler seam succeeds, and that `pattern` fails closed.

CI builds it on every pull request (`cross-build windows`) and does not run it.
It needs no network, so it runs anywhere Windows binaries do:

```sh
wine build-windows/wolfram_windows_smoke.exe
```

It prints a pass and fail count and exits non-zero on any failure.

## What comes next

[#248](https://github.com/ewanc26/wolfram/issues/248) chose this subset first
and left the full build (a Winsock transport, the OAuth client, the test suite)
as the follow-up. The place to start is `src/transport/windows_tls.c`: it implements the seven connection functions of
`src/transport/wii_tls.h`, and `xrpc_wii.c` and `websocket_wii.c` already speak
HTTP and WebSocket over them. Giving those functions a Winsock socket and a TLS
session turns the transport on without touching anything above it.
