# Getting started

## Requirements

- A C11 compiler (Clang ships with Xcode Command Line Tools on macOS)
- CMake ≥ 3.20
- libcurl
- OpenSSL (libcrypto) — for SHA-256 hashing
- libsecp256k1 — for secp256k1 signing (optional; crypto stubs gracefully if absent)
- c-ares 1.28+ — for portable DNS TXT resolution (optional; POSIX resolver fallback)
- libzstd — for Jetstream compressed messages (optional)
- SQLite3 — **only** required when building the optional persistence module
  (`WOLFRAM_BUILD_STORE=ON`); the default build does not link it
- libsodium — **only** required when `WOLFRAM_BUILD_STORE_CRYPTO=ON` (which also
  requires `WOLFRAM_BUILD_STORE=ON`); it provides secret-key encryption
  (`crypto_secretbox_easy`, XSalsa20-Poly1305) and passphrase key derivation
  (`crypto_pwhash`, Argon2id) for persisted sessions at rest
- libmicrohttpd (GNU MHD) — **only** required when `WOLFRAM_BUILD_TEST_HTTPD=ON`
  for the offline HTTP integration test (`mock_pds`)
- libidn2 (GNU, MIT) — **only** required when `WOLFRAM_BUILD_IDN=ON`. It provides
  IDNA2008 / punycode (ACE, `xn--…`) conversion for internationalized handles

On macOS via Homebrew:

```sh
brew install cmake curl openssl secp256k1 c-ares zstd
# Optional persistence + at-rest encryption:
brew install sqlite3 libsodium
# Optional offline HTTP integration test (WOLFRAM_BUILD_TEST_HTTPD=ON):
brew install libmicrohttpd
# Optional internationalized (IDN) handle resolution (WOLFRAM_BUILD_IDN=ON):
brew install libidn2
```

(libcurl also ships with macOS itself, but the Homebrew one is newer and CMake
finds it more reliably via `pkg-config`.) Jetstream connections require libcurl
7.86+ built with `ws`/`wss` support; without it, WebSocket attempts return
`WF_ERR_INVALID_ARG`. Compressed Jetstream subscriptions additionally require
libzstd and the [official Jetstream dictionary](https://github.com/bluesky-social/jetstream/blob/main/pkg/models/zstd_dictionary),
supplied through `wf_jetstream_options`.

## Building

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The generated Lexicon wrappers (`include/wolfram/atproto_lex.h` and
`src/atproto_lex.c`) are **prebuilt and checked into the repository**, so a fresh
clone compiles without running `wf_lexgen.py`. You only need it to regenerate
wrappers from a different/newest Lexicon set or a custom lexicon.

If Homebrew's libcurl isn't picked up automatically:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix curl)"
```

## Optional persistence and at-rest encryption

```sh
# Persistence only (sessions + repo mirror stored in plaintext columns):
cmake -S . -B build -DWOLFRAM_BUILD_STORE=ON
# Persistence with libsodium at-rest encryption of the session blob:
cmake -S . -B build -DWOLFRAM_BUILD_STORE=ON -DWOLFRAM_BUILD_STORE_CRYPTO=ON
```

With `WOLFRAM_BUILD_STORE_CRYPTO=ON`, call `wf_store_set_passphrase` after
`wf_store_open` and before saving/loading a session; the key is derived via
libsodium's `crypto_pwhash` (Argon2id).

## Generating Lexicon wrappers

```sh
python3 tools/wf_lexgen.py path/to/lexicons/*.json -o generated/lexicons.h
# Add generated JSON codecs and XRPC wrappers:
python3 tools/wf_lexgen.py path/to/lexicons/*.json -o generated/lexicons.h \
  --source-output generated/lexicons.c
```

Generated structs cover records, named object definitions, and XRPC parameters,
inputs, and outputs. Optional fields have explicit `has_*` flags; inputs are
borrowed and must outlive the struct, while output decoders return owning
objects with a `_output_free`. Input encoders resolve referenced objects,
arrays, strings, and tokens recursively.

## Examples

```sh
./build/describe_repo https://eurosky.social did:plc:ofrbh253gwicbkc5nktqepol
./build/create_post https://bsky.social you@example.com yourpassword "Hello from wolfram!"
./build/upload_image https://bsky.social you@example.com yourpassword ./cat.jpg "a cat"
./build/get_record https://bsky.social did:plc:example app.bsky.feed.post 3jui7kd54zh2y
./build/subscribe_labels https://mod.bsky.app
./build/create_account https://bsky.social alice.bsky.social alice@example.com hunter2
./build/create_app_password https://bsky.social alice@example.com hunter2 "my-app"
```

End-to-end examples also live in `examples/`: `feed_generator`,
`labeler_service`, `ozone_moderation`, `post_image_embed`, and
`timeline_moderation`.
