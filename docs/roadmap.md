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
26. DID PLC operation helpers (`wf_plc_*`, `plc.h`) — build/sign/submit
    `plc` operations (create, rotate signing/handle/rotation keys, tombstone)
    with ES256 signatures and verification.
27. Moderation decision engine (`moderation.h`) — blur/alert/inform/filter
    decisions for accounts, profiles, posts, notifications, feed generators,
    and user lists from labels, blocks, mutes, hidden posts, and muted words.
    Offline; ingests API-shaped JSON (`wf_mod_prefs_from_json`,
    `wf_mod_label_defs_from_labeler`, `wf_mod_labels_from_json`).
28. Generic JSON module (`json.h`) — canonical round-trip (`wf_json_canonicalize`)
    and a JSON-Schema validator subset (type/required/properties/items,
    enum/const, format, numeric bounds, string length/pattern, array
    constraints, additionalProperties, anyOf/oneOf/not).
29. SQLite store persistence (`store.h`) — session + repo-mirror persistence and
    persisted-label storage for the moderation engine; OFF by default, build
    with `WOLFRAM_BUILD_STORE=ON`. With `WOLFRAM_BUILD_STORE_CRYPTO=ON` (libsodium)
    the session credentials are encrypted at rest (XSalsa20-Poly1305, Argon2id).
30. Video upload (`blob.h` / `agent.h`) — dedicated video endpoint upload
    (`wf_agent_upload_video`), job-status polling
    (`wf_agent_get_video_job_status`), and upload limits
    (`wf_agent_get_video_upload_limits`).
31. Chat typed wrappers (`chat_typed.h`) — `chat.bsky.convo`/`group`/`actor`/
    `moderation` write+query wrappers with chat-service endpoint resolution.
    The full chat write surface is now implemented.
32. Ozone moderation-service / labeler helper (`ozone.h`) — verify and emit
    labels, build service auth headers for the Ozone moderation service.
33. Generated typed wrappers — owning parsers for embed/feed/feed-generator/
    graph/list/thread records (`embed_typed.h`, `feed_typed.h`,
    `feedgen_typed.h`, `graph_typed.h`, `list_typed.h`, `thread_typed.h`) plus
    threadgate/postgate record helpers (`threadgate_postgate.h`).
34. Authenticated XRPC client (`auth_client.h`) — DPoP-binding OAuth-authenticated
    XRPC query/procedure/blob-upload wrapper (`wf_auth_client_*`) with session
    refresh and DPoP nonce retry.
34. Labeler service record coverage (`labeler_typed.h`) — owned typed parsers +
    agent wrappers for `com.atproto.label.queryLabels`, `app.bsky.labeler.getServices`
    (including embedded service records, policies, and label value defs), and
    `com.atproto.temp.fetchLabels`.
35. Identity namespace wrappers (`identity_typed.h`) — owned typed parsers + agent
    wrappers for `com.atproto.identity` (resolveHandle, resolveDid, updateHandle,
    getRecommendedDidCredentials, signPlcOperation, submitPlcOperation,
    resolveIdentity, refreshIdentity) and a PLC handle-rotation convenience
    (`wf_agent_identity_rotate_handle`) built on `plc.h`.
36. Notification v2 + activity subscriptions (`notification_v2_typed.h`) — owned
    typed parsers + agent wrappers for `app.bsky.notification.putPreferencesV2`,
    `listActivitySubscriptions`, and `putActivitySubscription`.
37. Higher-level endpoint examples (`examples/`) — self-contained programs
    exercising generated clients: label query, PLC handle rotation, notification
    v2, and admin account search.

## Next planned work

- Exercise the gated live example test (`test_examples_live`) in CI with real
  credentials (it SKIPs cleanly when `BSKY_HANDLE`/`BSKY_PASSWORD` are unset).
- Continue cross-referencing `bluesky-social/atproto` and `rsky` for protocol
  parity. Remaining parity items include broader `tools.ozone.*` and
  `app.bsky.*` record coverage and tooling.

## Dependencies

- [cJSON](https://github.com/DaveGamble/cJSON) — vendored via CMake FetchContent.
- [libcbor](https://github.com/PJK/libcbor) — vendored via CMake FetchContent for RFC 8949 parsing and serialization primitives.
- OpenSSL (libcrypto) — for SHA-256 hashing.
- [libsecp256k1](https://github.com/bitcoin-core/secp256k1) — for secp256k1 signing. Without it, crypto functions gracefully stub to `WF_ERR_INVALID_ARG`.
