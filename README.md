# wolfram

A C SDK for the AT Protocol.

**Early development.** XRPC transport, identity resolution, crypto (secp256k1 + P-256), and repo handling (DAG-CBOR, CAR, MST, signed commits) are implemented and tested. Not at feature parity with the official SDKs yet, but the core building blocks are in place.

## Overview

`wolfram` provides the low-level building blocks for speaking AT Protocol from C: XRPC over HTTP, DID/handle resolution, repo signing, and MST/CBOR/CAR handling. Layered like the other SDKs in this ecosystem — a small transport core, with identity and repo logic built on top rather than tangled into it.

## Layout

| Module                  | Status      | Notes                                          |
| ------------------------ | ----------- | ----------------------------------------------- |
| `wolfram/xrpc.h`          | Implemented | libcurl-backed query/procedure calls            |
| `wolfram/identity.h`      | Implemented | did:plc, did:web, DNS TXT, well-known fallback  |
| `wolfram/repo.h`          | Implemented | DAG-CBOR parse/serialize, CID, CAR, MST, commit |
| `wolfram/crypto.h`        | Implemented | secp256k1 + P-256 keygen, sign, verify          |

## Requirements

- A C11 compiler (Clang ships with Xcode Command Line Tools on macOS)
- CMake ≥ 3.20
- libcurl
- OpenSSL (libcrypto) — for SHA-256 hashing
- libsecp256k1 — for secp256k1 signing (optional; crypto stubs gracefully if absent)

On macOS via Homebrew:

```sh
brew install cmake curl openssl secp256k1
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

### Roadmap

1. ✅ Wire in a JSON library ([cJSON](https://github.com/DaveGamble/cJSON)).
2. ✅ DID/handle resolution (`wf_did_resolve`, `wf_handle_resolve`).
3. ✅ DAG-CBOR decode/encode, with full constraint validation and unit tests.
4. ✅ SHA-256 + CID computation (`wf_cid_of_block`, `wf_cid_to_string`).
5. ✅ CAR parsing (`wf_car_parse`).
6. ✅ MST traversal + mutation (`wf_mst_find`, `wf_mst_add`, `wf_mst_delete`, `wf_mst_node_build`, `wf_mst_node_finalize`).
7. ✅ secp256k1 and P-256 signing (`wf_sign`, `wf_verify`, `wf_signing_key_generate`).
8. ✅ Signed commit creation (`wf_commit_create`).
9. 🟡 DNS TXT lookup for AT Protocol handle resolution.
10. ⬜ DAG-CBOR schema-driven encoding (structured record creation).
11. ⬜ PDS client — session management, credential storage, auth token refresh.
12. ⬜ Repository data operations — create/update/delete records, sync repo state.
13. ⬜ Lexicon integration — code generation from Lexicon schemas for typed AT Protocol calls.
14. ⬜ Union/jetstream — WebSocket-based firehose subscription for live AT Protocol events.

- [cJSON](https://github.com/DaveGamble/cJSON) — vendored via CMake FetchContent.
- OpenSSL (libcrypto) — for SHA-256 hashing (install via `brew install openssl` on macOS, or your system package manager).
- [libsecp256k1](https://github.com/bitcoin-core/secp256k1) — for secp256k1 signing (install via `brew install secp256k1`). Without it, crypto functions gracefully stub to `WF_ERR_INVALID_ARG`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT
