# wolfram

A C SDK for the AT Protocol.

**Early development.** XRPC transport and identity resolution are implemented; crypto and repo handling are stubbed with `TODO`s. Not usable for production yet.

## Overview

`wolfram` provides the low-level building blocks for speaking AT Protocol from C: XRPC over HTTP, DID/handle resolution, repo signing, and MST/CBOR/CAR handling. Layered like the other SDKs in this ecosystem — a small transport core, with identity and repo logic built on top rather than tangled into it.

## Layout

| Module                  | Status      | Notes                                          |
| ------------------------ | ----------- | ----------------------------------------------- |
| `wolfram/xrpc.h`          | Implemented | libcurl-backed query/procedure calls            |
| `wolfram/identity.h`      | Implemented | did:plc, did:web resolution; handle → DID       |
| `wolfram/repo.h`          | Implemented | DAG-CBOR decode + CID computation + CAR parse   |
| `wolfram/crypto.h`        | Stubbed     | Awaiting a secp256k1/P-256 backend              |

## Requirements

- A C11 compiler (Clang ships with Xcode Command Line Tools on macOS)
- CMake ≥ 3.20
- libcurl
- OpenSSL (libcrypto) — for SHA-256 hashing

On macOS via Homebrew:

```sh
brew install cmake curl openssl
```

(libcurl also ships with macOS itself, but the Homebrew one is newer and CMake finds it more reliably via `pkg-config`.)

## Building

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

If Homebrew's libcurl isn't picked up automatically, point CMake at it:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix curl)"
```

A `flake.nix` devShell is also included for machines that do use Nix, but it isn't the assumed path.

## Example

```sh
./build/describe_repo https://eurosky.social did:plc:ofrbh253gwicbkc5nktqepol
```

Calls `com.atproto.repo.describeRepo` and prints the raw JSON response — no parsing yet, just proof that the transport works.

## Roadmap

Roughly in the order it makes sense to tackle them:

1. ✅ Wire in a JSON library ([cJSON](https://github.com/DaveGamble/cJSON)).
2. ✅ DID/handle resolution (`wf_did_resolve`, `wf_handle_resolve`).
3. ✅ DAG-CBOR decode (read-only), with full constraint validation and unit tests.
4. ✅ SHA-256 + CID computation (`wf_cid_of_block`, `wf_cid_to_string`).
5. ✅ CAR parsing (`wf_car_parse`).
6. MST traversal, then mutation.
7. secp256k1 signing via `libsecp256k1`.

### New dependencies

- [cJSON](https://github.com/DaveGamble/cJSON) — vendored via CMake FetchContent.
- OpenSSL (libcrypto) — for SHA-256 hashing (install via `brew install openssl` on macOS, or your system package manager).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT
