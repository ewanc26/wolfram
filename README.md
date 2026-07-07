# wolfram

A C SDK for the AT Protocol.

The runtime library and all generated client code are pure C11. The optional
Lexicon generator is a development-time Python tool and is never linked into,
embedded in, or required by applications using `libwolfram`.

**Early development.** XRPC transport, identity resolution, crypto (secp256k1 + P-256), repo handling (DAG-CBOR, CAR, MST, signed commits), runtime Lexicon validation, a high-level agent API, OAuth, and syncing (firehose + sync endpoints) are implemented and tested. Not yet at full feature parity with the official SDKs, but the core building blocks and a usable high-level API are in place.

## Overview

`wolfram` provides the low-level building blocks for speaking AT Protocol from C: XRPC over HTTP, DID/handle resolution, repo signing, and MST/CBOR/CAR handling. Layered like the other SDKs in this ecosystem — a small transport core, with identity and repo logic built on top rather than tangled into it.

## Documentation

Per-module usage guides with runnable C snippets live in [`docs/`](docs/):

- [`docs/agent.md`](docs/agent.md) — high-level `wf_agent_*` API (session, posts, feeds, profile, social graph, notifications, blobs, pagination).
- [`docs/sync.md`](docs/sync.md) — repo CAR download, verify/import, firehose subscription, commit verification, and blob/blocks endpoints.
- [`docs/validate.md`](docs/validate.md) — runtime validation with `wf_validate_value` / `wf_validate_record`.
- [`docs/oauth.md`](docs/oauth.md) — OAuth/DPoP discovery, PKCE, PAR, callback completion, and the authenticated XRPC client.

## Name

`wolfram` was chosen for three reasons:

1. **Tungsten.** Wolfram is an older name for tungsten (element symbol **W**).
2. **Low RAM.** It's light on memory usage.
3. **Wolves.** Wolves are cool.

## Layout

| Module                  | Status      | Notes                                          |
| ------------------------ | ----------- | ----------------------------------------------- |
| `wolfram/xrpc.h`          | Implemented | libcurl-backed query/procedure calls, binary blob upload |
| `wolfram/session.h`       | Implemented | PDS login, resume, refresh, get, and logout     |
| `wolfram/identity.h`      | Implemented | did:plc, did:web, portable c-ares/POSIX DNS TXT, well-known fallback |
| `wolfram/repo.h`          | Implemented | DAG-CBOR parse/serialize, CID, CAR, MST, commit, diff verify/apply, operation inversion |
| `wolfram/crypto.h`        | Implemented | secp256k1 + P-256 keygen, sign, verify          |
| `wolfram/oauth.h`         | Partial     | OAuth discovery, PKCE/DPoP, PAR/token calls, callback validation, and persistent state |
| `wolfram/server.h`         | Implemented | Server account management — describeServer, createAccount, app passwords, deleteAccount, password reset |
| `wolfram/jetstream.h`     | Partial     | Filtered Jetstream JSON subscription transport  |
| `wolfram/label.h`         | Implemented | Label subscription (com.atproto.label.subscribeLabels) via WebSocket |
| `wolfram/sync.h`          | Implemented | Firehose subscribeRepos subscription, commit verification, CAR download, plus getBlob/getBlocks/getRecord/listBlobs/getHead/getLatestCommit/getRepoStatus/listRepos |
| `wolfram/validate.h`     | Implemented | Runtime Lexicon schema validation (records and named values), refs/unions/format keywords |
| `wolfram/agent.h`        | Implemented | High-level BskyAgent-style API: session, posts, profile, social graph, feeds, **preferences**, **push registration**, notifications, blobs, server + app-password management |
| `wolfram/richtext.h`     | Implemented | Rich text facets, grapheme detection, mention/link/tag parsing |
| `wolfram/syntax.h`        | Implemented | DID, handle, NSID, TID, AT URI, RFC 3339, BCP 47 validators |
| `wolfram/atproto_lex.h`   | Implemented | Generated lexicon endpoint wrappers (13K header, 74K source) |
| `wolfram/moderation.h`    | Implemented | Moderation decision engine — blur/alert/inform/filter for accounts, profiles, posts, notifications, feed generators, and user lists from labels, blocks, mutes, hidden posts, and muted words |
| `wolfram/store.h`         | Partial/Optional | SQLite-backed session + repo-mirror persistence (OFF by default; build with `WOLFRAM_BUILD_STORE=ON`) |
| `tools/wf_lexgen.py`      | Initial     | Lexicon JSON to typed C data-model declarations |

## Requirements

- A C11 compiler (Clang ships with Xcode Command Line Tools on macOS)
- CMake ≥ 3.20
- libcurl
- OpenSSL (libcrypto) — for SHA-256 hashing
- libsecp256k1 — for secp256k1 signing (optional; crypto stubs gracefully if absent)
- c-ares 1.28+ — for portable DNS TXT resolution (optional; POSIX resolver fallback)
- libzstd — for Jetstream compressed messages (optional)
- SQLite3 — **only** required when building the optional persistence module (`WOLFRAM_BUILD_STORE=ON`); the default build does not link it
- libsodium — **only** required when `WOLFRAM_BUILD_STORE_CRYPTO=ON` (which also requires `WOLFRAM_BUILD_STORE=ON`); it provides the secret-key encryption (`crypto_secretbox_easy`, XSalsa20-Poly1305) and passphrase key derivation (`crypto_pwhash`, Argon2id) used to encrypt persisted sessions at rest. The default build does not link it.

On macOS via Homebrew:

```sh
brew install cmake curl openssl secp256k1 c-ares zstd
# Optional persistence + at-rest encryption:
brew install sqlite3 libsodium
```

(libcurl also ships with macOS itself, but the Homebrew one is newer and CMake finds it more reliably via `pkg-config`.)
Jetstream connections require libcurl 7.86 or newer built with `ws`/`wss`
protocol support. Builds without that optional feature remain usable; WebSocket
connection attempts explicitly return `WF_ERR_INVALID_ARG`.
Compressed Jetstream subscriptions additionally require libzstd and the
[official Jetstream dictionary](https://github.com/bluesky-social/jetstream/blob/main/pkg/models/zstd_dictionary),
supplied through `wf_jetstream_options`. The SDK copies the dictionary; it does
not download protocol assets or silently attempt dictionary-free decoding.
Connected streams can replace collection/DID filters with
`wf_jetstream_update_options`; empty arrays disable the corresponding filter.
Set `require_hello` to request Jetstream's paused-until-update handshake.

## Building

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Generate typed, borrowed-view C declarations from one or more Lexicon files:

```sh
python3 tools/wf_lexgen.py path/to/lexicons/*.json -o generated/lexicons.h
# Add generated JSON codecs and XRPC wrappers:
python3 tools/wf_lexgen.py path/to/lexicons/*.json -o generated/lexicons.h \
  --source-output generated/lexicons.c
```

The generated structs cover records, named object definitions, and XRPC
parameters, inputs, and outputs. Optional fields have explicit `has_*` flags;
input strings, arrays, references, bytes, and encoded JSON values are borrowed
and must outlive the generated struct. Generated output decoders instead return
owning objects with a matching `_output_free` function. They decode nested refs
and arrays, tagged JSON bytes (`$bytes`) via OpenSSL base64, CID links (`$link`),
typed blobs, unions, and unknown values. Generated endpoint calls still delegate
all network I/O to `xrpc.c`. Input encoders resolve referenced objects, arrays,
strings, and tokens recursively, and encode every JSON-compatible Lexicon value
kind, including integer/boolean arrays, tagged bytes and CID links, and blobs.

If Homebrew's libcurl isn't picked up automatically, point CMake at it:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix curl)"
```

A `flake.nix` devShell is also included for machines that do use Nix, but it isn't the assumed path.

### Optional persistence and at-rest encryption

The optional SQLite store is off by default. Enable it, and optionally encrypt
persisted sessions at rest with libsodium, like so:

```sh
# Persistence only (sessions + repo mirror stored in plaintext columns):
cmake -S . -B build -DWOLFRAM_BUILD_STORE=ON
# Persistence with libsodium at-rest encryption of the session blob:
cmake -S . -B build -DWOLFRAM_BUILD_STORE=ON -DWOLFRAM_BUILD_STORE_CRYPTO=ON
```

With `WOLFRAM_BUILD_STORE_CRYPTO=ON`, call `wf_store_set_passphrase` after
`wf_store_open` and before saving/loading a session; the key is derived from
the passphrase via libsodium's `crypto_pwhash` (Argon2id).

## Example

```sh
./build/describe_repo https://eurosky.social did:plc:ofrbh253gwicbkc5nktqepol
```

Calls `com.atproto.repo.describeRepo` and prints the raw JSON response — no parsing yet, just proof that the transport works.

```sh
./build/create_post https://bsky.social you@example.com yourpassword "Hello from wolfram!"
```

Logs in, detects rich text facets (mentions, links, tags), builds a `com.atproto.repo.createRecord` request, and creates a post via the AT Protocol.

More end-to-end examples live in `examples/`: `feed_generator` (create/read an `app.bsky.feed.generator` record), `labeler_service` (create/read an `app.bsky.labeler.service` record with `policies.labelValueDefinitions`), `post_image_embed` (upload an image blob, build a facet + `app.bsky.embed.images` post), and `timeline_moderation` (run the moderation decision engine over the timeline, offline-sample or live).

### Roadmap

1. ✅ Wire in a JSON library ([cJSON](https://github.com/DaveGamble/cJSON)).
2. ✅ DID/handle resolution (`wf_did_resolve`, `wf_handle_resolve`).
3. ✅ DAG-CBOR decode/encode, with full constraint validation and unit tests.
4. ✅ SHA-256 + CID computation (`wf_cid_of_block`, `wf_cid_to_string`).
5. ✅ CAR parsing (`wf_car_parse`).
6. ✅ MST traversal + mutation (`wf_mst_find`, `wf_mst_add`, `wf_mst_delete`, `wf_mst_node_build`, `wf_mst_node_finalize`).
7. ✅ secp256k1 and P-256 signing (`wf_sign`, `wf_verify`, `wf_signing_key_generate`).
8. ✅ Signed commit creation (`wf_commit_create`).
9. ✅ DNS TXT lookup for AT Protocol handle resolution (c-ares when available,
   POSIX resolver fallback; multi-record and chunked TXT handling).
10. ✅ DAG-CBOR schema-driven encoding (structured record creation).
11. ✅ PDS client — session management, credential storage, auth token refresh.
12. ✅ Repository data operations — create/update/delete records, full/diff CAR
    download (`wf_sync_get_repo`), and ownership/signature/content-addressed
    verification/import (`wf_repo_verify`, `wf_repo_import`).
13. ✅ Lexicon integration — typed C schemas, inline and referenced input
    objects, repeated-key array query parameters, complete JSON input encoders,
    generated query/procedure wrappers, and owning output decoders.
14. ✅ Union/jetstream — libcurl WebSocket transport, filtered Jetstream URL
    construction, runtime subscriber options, JSON event envelopes,
    cursor-based reconnect/backoff, and dictionary-based zstd messages.
15. ✅ OAuth — protected-resource, authorization-server, and discoverable
    client metadata validation/discovery, PKCE S256, persistent ES256 DPoP
    keys, JWK thumbprints/proofs, public-client PAR and authorization-code token
    exchange with mandatory nonce retry, state/issuer-bound callback
    validation, subject-bound token refresh, expiring persistent authorization
    state and durable token sessions with private DPoP JWK validation,
    ES256 `private_key_jwt` authentication, authorization-begin orchestration,
    callback-to-session completion, and managed session refresh orchestration.
16. ✅ Syntax validation — DID, handle, at-identifier, NSID, record key, TID,
    AT URI, RFC 3339 datetime, and BCP 47 language tag validators.
17. ✅ Rich text — grapheme-aware byte indexing, facet detection (mentions, links,
    tags), and segment iteration.
18. ✅ Firehose subscription — `com.atproto.sync.subscribeRepos` WebSocket stream
    with CBOR frame parsing, cursor-based reconnect, and backoff.
19. ✅ Firehose verification — commit signature verification, key resolution,
    CAR parsing, and MST/root validation from the subscribeRepos stream.
20. ✅ Blob upload — binary POST to `xrpc/{nsid}` with custom Content-Type,
    authenticated via session or DPoP (`wf_xrpc_upload_blob`,
    `wf_auth_client_upload_blob`).
21. ✅ Label subscription — `com.atproto.label.subscribeLabels` WebSocket stream
    with JSON frame parsing, cursor reconnect, and backoff.
22. ✅ Repo diff tests — comprehensive tests for `wf_repo_diff_apply` and
    `wf_repo_operations_invert` including round-trip verification.
23. ✅ Lexicon validation — runtime object/record validation against lexicon
    schemas, supporting `object`, `record`, `query` (parameters extraction),
    `procedure` (input.schema extraction), `params`, `blob`, `ref`,
     `array`, `string` (including format validation), `integer`, `boolean`,
     `unknown`, and `union` types (`wf_validate_value`, `wf_validate_record`).
24. ✅ Agent API wrappers — `com.atproto.sync.getBlob/getBlocks/getRecord/listBlobs`,
     `com.atproto.repo.listRecords`, social graph (mute/unmute), graph wrappers
     (getBlocks, getMutes, getKnownFollowers, getRelationships, getList, getLists),
     feed wrappers (searchPosts, getActorLikes, getLikes, getRepostedBy),
     and notification wrappers (getUnreadCount). Full input validation tests.
25. ✅ Agent repo sync pipeline — offline mirror seed, verified incremental
     diff apply with CAR parsing, mirror head query, operation inversion,
     and local mirror record lookup (`wf_agent_set_did`, `wf_agent_set_signing_key`,
     `wf_agent_seed_repo`, `wf_agent_apply_repo_diff`, `wf_agent_repo_head`,
     `wf_agent_invert_repo_operations`, `wf_agent_mirror_get_record`). Tested.

### Next planned work

- Full-corpus generated lexicon clients exercised end-to-end against a live PDS in examples.
- More examples — threads, custom feeds, labeler records, image/embed posts via blob upload.
- Documentation: per-function usage examples for the `agent`, `sync`, `validate`, and `oauth` modules.
- Optional: JSON Structure / JSON Schema round-trip for non-Lexicon JSON if a use case appears.

- [cJSON](https://github.com/DaveGamble/cJSON) — vendored via CMake FetchContent.
- [libcbor](https://github.com/PJK/libcbor) — vendored via CMake FetchContent for RFC 8949 parsing and serialization primitives.
- OpenSSL (libcrypto) — for SHA-256 hashing (install via `brew install openssl` on macOS, or your system package manager).
- [libsecp256k1](https://github.com/bitcoin-core/secp256k1) — for secp256k1 signing (install via `brew install secp256k1`). Without it, crypto functions gracefully stub to `WF_ERR_INVALID_ARG`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT
