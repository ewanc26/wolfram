# Roadmap

## What's been built

A historical record of the surface that has shipped (each implemented and
tested). For what's still ahead, see [Next planned work](#next-planned-work).

1. Wire in a JSON library ([cJSON](https://github.com/DaveGamble/cJSON)).
2. DID/handle resolution (`wf_did_resolve`, `wf_handle_resolve`).
3. DAG-CBOR decode/encode, with full constraint validation and unit tests.
4. SHA-256 + CID computation (`wf_cid_of_block`, `wf_cid_to_string`).
5. CAR parsing (`wf_car_parse`).
6. MST traversal + mutation (`wf_mst_find`, `wf_mst_add`, `wf_mst_delete`, `wf_mst_node_build`, `wf_mst_node_finalize`).
7. secp256k1 and P-256 signing (`wf_sign`, `wf_verify`, `wf_signing_key_generate`).
8. Signed commit creation (`wf_commit_create`).
9. DNS TXT lookup for AT Protocol handle resolution (c-ares when available,
   POSIX resolver fallback; multi-record and chunked TXT handling).
10. DAG-CBOR schema-driven encoding (structured record creation).
11. PDS client — session management, credential storage, auth token refresh.
12. Repository data operations — create/update/delete records, full/diff CAR
    download (`wf_sync_get_repo`), and ownership/signature/content-addressed
    verification/import (`wf_repo_verify`, `wf_repo_import`).
13. Lexicon integration — typed C schemas, inline and referenced input
    objects, repeated-key array query parameters, complete JSON input encoders,
    generated query/procedure wrappers, and owning output decoders.
14. Union/jetstream — libcurl WebSocket transport, filtered Jetstream URL
    construction, runtime subscriber options, JSON event envelopes,
    cursor-based reconnect/backoff, and dictionary-based zstd messages.
15. OAuth — protected-resource, authorization-server, and discoverable client
    metadata discovery, PKCE S256, persistent ES256 DPoP keys, JWK
    thumbprints/proofs, public-client PAR and authorization-code token exchange
    with mandatory nonce retry, state/issuer-bound callback validation,
    subject-bound token refresh, durable token sessions, ES256
    `private_key_jwt` authentication, authorization-begin orchestration,
    callback-to-session completion, and managed session refresh.
16. Syntax validation — DID, handle, at-identifier, NSID, record key, TID,
    AT URI, RFC 3339 datetime, and BCP 47 language tag validators.
17. Rich text — grapheme-aware byte indexing, facet detection (mentions,
    links, tags), and segment iteration.
18. Firehose subscription — `com.atproto.sync.subscribeRepos` WebSocket stream
    with CBOR frame parsing, cursor-based reconnect, and backoff.
19. Firehose verification — commit signature verification, key resolution,
    CAR parsing, and MST/root validation.
20. Blob upload — binary POST to `xrpc/{nsid}` with custom Content-Type,
    authenticated via session or DPoP (`wf_xrpc_upload_blob`,
    `wf_auth_client_upload_blob`).
21. Label subscription — `com.atproto.label.subscribeLabels` WebSocket stream
    with JSON frame parsing, cursor reconnect, and backoff.
22. Repo diff tests — comprehensive tests for `wf_repo_diff_apply` and
    `wf_repo_operations_invert`.
23. Lexicon validation — runtime object/record validation against lexicon
    schemas (`object`, `record`, `query`, `procedure`, `params`, `blob`, `ref`,
    `array`, `string`, `integer`, `boolean`, `unknown`, `union`).
24. Agent API wrappers — sync getBlob/getBlocks/getRecord/listBlobs,
    repo.listRecords, social graph (mute/unmute), graph wrappers (getBlocks,
    getMutes, getKnownFollowers, getRelationships, getList, getLists), feed
    wrappers, and notification wrappers. Full input-validation tests.
25. Agent repo sync pipeline — offline mirror seed, verified incremental diff
    apply, mirror head query, operation inversion, local mirror record lookup.

## Next planned work

- Exercise the gated live example test (`test_examples_live`) in CI with real
  credentials (it SKIPs cleanly when `BSKY_HANDLE`/`BSKY_PASSWORD` are unset).
- Broaden the `wolfram` CLI further (label subscription streaming) and wire
  `help <command>`.
- Generic JSON module (`wolfram/json.h`): add a sorted canonical form if a use
  case appears.
- Continue cross-referencing `bluesky-social/atproto` and `rsky` for protocol
  parity. The full `chat.bsky.convo`/`group`/`actor`/`moderation` write surface
  is now implemented; remaining parity items include labeler service records.

## Dependencies

- [cJSON](https://github.com/DaveGamble/cJSON) — vendored via CMake FetchContent.
- [libcbor](https://github.com/PJK/libcbor) — vendored via CMake FetchContent for RFC 8949 parsing and serialization primitives.
- OpenSSL (libcrypto) — for SHA-256 hashing.
- [libsecp256k1](https://github.com/bitcoin-core/secp256k1) — for secp256k1 signing. Without it, crypto functions gracefully stub to `WF_ERR_INVALID_ARG`.
