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
 48. Chat moderation event subscription (`chat_typed.h`) — real client-side
     WebSocket subscription for `chat.bsky.moderation.subscribeModEvents`. Decodes
     the atproto framed DAG-CBOR envelope (header map `{op, t}` ++ body map)
     with libcbor, fills a `wf_chat_mod_event` (union tag from the header `t`),
     advances a numeric cursor, and reconnects with capped exponential backoff.
     API: `wf_chat_mod_frame_parse_cbor`, `wf_chat_mod_frame_free`,
     `wf_chat_mod_events_build_url`, `wf_chat_mod_events_start` /
     `wf_chat_mod_events_stop`, and the agent convenience wrapper
     `wf_agent_chat_subscribe_mod_events_typed` (resolves the chat-service
     endpoint from the agent session). Tested offline: decoder unit tests
     (message/error/truncated/garbage) + an in-process `wf_xrpc_server` WS
     round-trip that streams the framed CBOR bytes.
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
    `listActivitySubscriptions`, and `putActivitySubscription`. The v2
    preferences now have a dedicated typed builder/parser:
    `wf_notification_v2_preferences_build` / `_parse` / `_free` over the 13-slot
    `wf_notification_v2_preferences` struct (per `app.bsky.notification.defs#preferences`),
    plus a typed agent wrapper `wf_agent_put_notification_preferences_v2_typed`.
37. Higher-level endpoint examples (`examples/`) — self-contained programs
    exercising generated clients: label query, PLC handle rotation, notification
    v2, and admin account search.
38. High-level `wf_bsky_agent` convenience wrapper (`bsky_agent.h`) — a
    BskyAgent equivalent bundling session + xrpc + identity + agent, with
    one-line helpers for login, post, getProfile, getTimeline, resolveHandle,
    follow, unfollow, like, repost, mute/unmute, getNotifications, searchActors,
    and getThread (all delegating to the existing `wf_agent_*` API). Tested.
39. `app.bsky.actor.status` typed wrappers (`actor_status_typed.h`) — owned
    parsers + a record builder for status records/views following the
    labeler/actor ownership model. The `getActorStatus`/`getStatus`/`putStatus`
    agent wrappers are honest stubs (`WF_ERR_INVALID_ARG` + `TODO`) because the
    local lexicon snapshot lacks generated bindings for those endpoints. Tested.
 40. `tools.ozone.*` typed coverage (`ozone.h`/`ozone.c`) — initial batch of
     typed convenience wrappers for moderation (queryStatuses, getLabelDefs,
     emitEvent, queryEvents, getEvent, getReporterStats, getSubjects,
     getSuggestions), communication templates, and set values. Tested.
 41. `wolfram` CLI social commands — `profile`, `timeline`, `follow`,
     `unfollow`, `like`, `repost`, `search`, `notifications`, `mute`, `unmute`,
     and `thread`, reusing the existing `wf_agent_*` APIs.
 42. Remaining `tools.ozone.*` typed wrappers (`ozone.h`/`ozone.c`) — additional
     moderation sub-endpoints (getAccountTimeline, getRecords, getRepo, getRepos,
     searchRepos, cancelScheduledActions, scheduleAction, listScheduledActions),
     queue (assignModerator, createQueue, deleteQueue, getAssignments, listQueues,
     routeReports, unassignModerator, updateQueue), report (14 endpoints),
     team (add/delete/list/update member), verification (grant/list/revoke),
     signature (findCorrelation, findRelatedAccounts, searchAccounts),
     setting (listOptions, removeOptions, upsertOption), hosting
     (getAccountHistory), server (getConfig), and safelink
     (addRule, queryEvents, queryRules, removeRule, updateRule). Tested.
43. Feed generator + discovery typed wrappers (`feed_gen_typed.h`) — owned
    parsers + agent wrappers for `app.bsky.feed` generator/discovery endpoints
    (getFeedGenerator[s], getActorFeeds, getSuggestedFeeds, getLikes,
    getRepostedBy, getQuotes, getActorLikes, getListFeed, searchPosts[V2]),
    reusing `wf_agent_feed_list`/`wf_agent_actor_list` where they fit. Tested.
44. Graph social typed wrappers (`graph_social_typed.h`) — owned parsers +
    agent wrappers for mutes/blocks list views (getListMutes, getListBlocks),
    starter packs (getStarterPack[s], getActorStarterPacks), suggested follows,
    and mute/unmute (actor + actor-list) procedures. Tested.
45. Core notification typed wrappers (`notification_typed.h`) — a richer
    notification view union (all reason types) plus wrappers for
    listNotifications, getUnreadCount, updateSeen, register/unregisterPush,
    distinct from the v2 preferences module. Tested.
46. Actor preferences typed wrappers (`actor_prefs_typed.h`) — an owned
    preferences union (all 16 known `$type` preference objects, unknown types
    preserved verbatim) with parser + builder, plus getSuggestions and a
    declaration parser/builder. Tested.

47. XRPC server module (`xrpc_server.h`) — optional `libmicrohttpd`-backed
    server with route registration (query/procedure), auth middleware, query
    parameter parsing, POST body accumulation, CORS headers, integrated
    token-bucket rate limiter (`wf_rate_limiter`), and readback of the bound
    port. Built when `WOLFRAM_BUILD_SERVER=ON`. Tested with an offline
    round-trip test against `wf_xrpc_client`, rate limiter unit tests, and
     a server rate-limit integration test.

48. Feed-generator skeleton server helper (`feedgen_server.h`) — high-level
    helper built on the XRPC server that serves `app.bsky.feed.getFeedSkeleton`
    (delegated to a caller-supplied callback returning skeleton post AT-URIs)
    and `app.bsky.feed.getFeedGenerator` (synthesised from a config struct of
    display name, description, DID, and optional avatar/CID). Deep-copied
    config with `wf_feedgen_server_config_free`, `wf_feedgen_server_new`/`start`/
    `stop`/`free`, and an offline round-trip test against `wf_xrpc_client`.
    Built when `WOLFRAM_BUILD_SERVER=ON`. Tested.

 49. Server-Sent Events (SSE) streaming for the XRPC server — real streaming via
    libmicrohttpd `MHD_suspend_connection` / `MHD_resume_connection`. An SSE
    route receives a `wf_xrpc_sse_stream` in its handler; frames are pushed with
    `wf_xrpc_server_sse_send` (formats `data: <payload>\n\n`, optional
    `event:` line) or `wf_xrpc_server_sse_send_raw`, and the connection is
    closed with `wf_xrpc_server_sse_close`. Single-shot SSE (send then close) is
    supported as a fallback. Suspended connections are resumed and drained on
    `wf_xrpc_server_stop` / `wf_xrpc_server_free` so teardown never hangs.
    Tested by `test_xrpc_server_sse` (streaming + single-shot, clean teardown).

 50. Graph write wrappers (`graph_write.c` / `graph_write.h`) — agent-level
    write helpers for the app.bsky.graph lexicon: `wf_agent_graph_*`
    for mute/unmute thread and actor-list procedures, and record-backed
    writes (block/unblock, list create/update/delete, listitem
    create/delete, starterpack create/update/delete, listblock
    create/delete). Create operations return an owned `wf_agent_post_result`
    ({uri, cid}, freed with `wf_agent_post_result_free`); deletes and
    procedures return `wf_status`. Inputs are validated (no silent no-ops).
    Tested end-to-end against an offline mock PDS (asserting returned
    uri/cid and the exact request payloads). Tested.

 51. `app.bsky.video` typed wrappers (`video_typed.h`) — owning parsers for the
    `getJobStatus` and `uploadVideo` jobStatus envelopes, the `getUploadLimits`
    output, and the shared `app.bsky.video.defs#jobStatus` blob-bearing object
    (unknown fields preserved in owned `extra`); builders for the defs and upload
    limits shapes; and agent wrappers (`wf_agent_video_get_job_status`,
    `wf_agent_video_get_upload_limits`, `wf_agent_video_upload`) that call the
     existing raw video helpers and return owned structs. Tested.

 52. C++ RAII wrapper (`wolfram-cpp`, `cpp/wolfram-cpp/`) — header-only consumer
     layer over `libwolfram`: `unique_handle<T, Free>` mirroring the `wf_T` +
     `wf_T_free` ownership contract, a `cstring` RAII owner for heap `char*`, and
     `wf_status` → `std::error_code` mapping with a throwing `require`. Owned
     handle typedefs are generated from the `wf_*_free` set
     (`tools/gen_owners.py` → `generated_owners.hpp`, 372 handles from 51 headers).
     Built with `WOLFRAM_BUILD_CPP=ON`; covered by an offline `wolfram-cpp-smoke`
     test. As part of adding it, the C public headers were made C++-clean: the
     `wf_status` / `wf_error` status enums were unified (previously duplicated
     with divergent values), `_result.h` gained `extern "C"` guards, and a stray
     `type` token before `static inline` was removed.

 53. C# interop wrapper (`Wolfram.Interop`, `dotnet/Wolfram.Interop/`) — pure
      pass-through P/Invoke layer over `libwolfram` (no reimplementation of
      crypto/transport/serialization/server logic). Raw tier uses source-generated
      `LibraryImport` (NativeAOT/trim-safe) with explicit UTF-8 marshalling and a
      combined `DllImportResolver` for `libwolfram` (+ platform libc to free owned
      strings); managed tier adds `XrpcClientHandle : SafeHandle`, a `Status` mirror
      of `wf_status`, and `WolframException` raised on non-`Ok`. Covered by an
      offline xUnit smoke test (`Wolfram.Interop.Tests`).

  54. WebSocket (RFC 6455) subscription endpoints for the XRPC server — real
       server→client push over a libmicrohttpd upgrade (`MHD_create_response_for_upgrade`
       + `MHD_ALLOW_UPGRADE`, libmicrohttpd 1.0.5). A WS route receives a
       `wf_xrpc_ws_stream` in its handler (invoked from the connection's upgrade
       worker thread after the 101 handshake, with `Sec-WebSocket-Accept` computed
       via SHA-1 + base64 over the client key and the RFC GUID). Frames are pushed
       with `wf_xrpc_server_ws_send` (binary, opcode 0x2, UNMASKED) and the stream is
       ended with `wf_xrpc_server_ws_close` (close frame, opcode 0x8). The upgrade
       worker also reads client control frames — answering ping (0x9) with pong (0xA)
       and honouring client close (0x8) — while inbound data frames are drained and
       ignored. Suspended/upgraded connections are resumed, shut down, and joined on
       `wf_xrpc_server_stop` / `wf_xrpc_server_free` so teardown never hangs. Built
       when `WOLFRAM_BUILD_SERVER=ON`. Tested by `test_xrpc_server_ws` (raw-client
       handshake + accept verification, ordered binary frames, close termination,
       clean teardown).

  55. Typed-wrapper coverage broadened across remaining lexicon endpoints
      (developed on parallel branches, all merged to `main`). `embed_typed.h`:
      owning parser + agent wrapper for `app.bsky.embed.getEmbedExternalView`.
      `feed_gen_typed.h`: `wf_feedgen_send_interactions_typed` for
      `app.bsky.feed.sendInteractions`. `graph_social_typed.h`: owning parsers +
      agent wrappers for `app.bsky.graph.getListsWithMembership` and
      `app.bsky.graph.getStarterPacksWithMembership` (membership envelopes reuse
      `wf_graph_list_view` / `wf_graph_starter_pack_view` + an optional
      `wf_graph_list_item_view`); `searchStarterPacks` is reused from
      `unspecced_typed.h` rather than redefined. `repo_typed.h`: owning parser for
      create/put record results + agent wrappers for `com.atproto.repo`
      createRecord/putRecord/deleteRecord/uploadBlob/importRepo. `server_typed.h`:
      agent wrappers for the remaining `com.atproto.server` endpoints
      (describeServer, createAccount, createAppPassword, listAppPasswords,
      revokeAppPassword, deleteSession, activate/deactivateAccount, confirmEmail,
      resetPassword, updateEmail, createInviteCode(s)) layered on the owned
      `server.h` result structs. `sync_typed.h`: `wf_agent_get_blob_typed` for
      `com.atproto.sync.getBlob`. `temp_typed.h`: real `revokeAccountCredentials`
      wrapper carrying the required `account` input (replaces the prior stub). New
      `lexicon_typed.h` module: owning parser + `wf_agent_resolve_lexicon_typed`
      for `com.atproto.lexicon.resolveLexicon`. `chat_typed.h`: owning parser +
      agent wrappers for `chat.bsky.notification.getPreferences` / `putPreferences`.
       All tested offline (parser round-trips + argument validation).

  56. Generic upstream→downstream WebSocket **subscription relay** for the XRPC
      server (`relay_server.h`) — a protocol-agnostic raw-frame relay built on
      the XRPC server's WS endpoints and the libcurl WebSocket client transport.
      `wf_xrpc_server_register_relay(server, cfg)` registers a WS route (e.g.
      `com.atproto.sync.subscribeRepos`) that, on a downstream connect, opens an
      upstream `ws(s)://` connection and forwards each received message
      byte-for-byte to the client until either side closes or errors, then closes
      the downstream stream. Config (`wf_relay_config`) deep-copied on
      registration and freed by `wf_relay_config_free`; the returned
      `wf_relay_server` handle owns that copy and is freed by
      `wf_relay_server_free` (after `wf_xrpc_server_free`). The forward loop runs
      on a worker thread spawned by the server's upgrade handler, matching the
      server's streaming contract; optional reconnect delay (bounded by the
      downstream client staying connected). Built when `WOLFRAM_BUILD_SERVER=ON`.
      Tested by `test_relay_server` (offline: forwards an in-process upstream's
      ordered binary frames to a `wf_websocket` client, then a clean close; when
      the linked libcurl lacks WS support the test still verifies registration,
      the forward thread, and clean downstream teardown against an unreachable
       upstream).

  57. OAuth **resource-server** token verification (`oauth/verify.h`) — validates
      incoming `Authorization: Bearer <access-token>` and `DPoP: <proof-jwt>`
      headers the way an XRPC server (or any atproto resource server) must. Reuses
      the existing P-256 / ES256, SHA-256, base64url and JWK primitives
      (`wf_crypto_p256_verify`, `wf_crypto_p256_jwk_coords`, `wf_crypto_sha256`,
      `wf_crypto_base64url_decode`/`_encode`, added to `crypto.h`/`crypto.c`) and
      never hand-rolls crypto. Public API: `wf_oauth_trusted_keys` (container of
      one or more trusted JWKs, with `_new`/`_add_jwk`/`_free`), `wf_oauth_verified_token`
      (a verified principal with owned `sub`/`iss`/`aud`/`scope`/`dpop_jkt` and a
      `dpop_bound` flag, freed by `wf_oauth_verified_token_free`),
      `wf_oauth_dpop_replay_cache` (in-memory `jti` cache with `_new`/`_is_seen`/
      `_mark_seen`/`_free` and optional per-entry TTL eviction), and the verify
      entry points `wf_oauth_verify_bearer`, `wf_oauth_verify_dpop`, and the
      request-level convenience `wf_oauth_verify_request` (parses `Bearer` +
      `DPoP` headers, verifies both, checks the DPoP `ath` against
      `SHA-256(access_token)`, and enforces `cnf.jkt` confirmation matching).
      DPoP checks follow RFC 9449 + atproto: signature over the proof with its own
      JWK, `typ "dpop+jwt"`, `alg ES256`, `htm`/`htu`/`jti`/`iat`/`exp`
      validation, and `jti` replay rejection. An XRPC `wf_xrpc_auth_cb` can call
      `wf_oauth_verify_request` with a `wf_xrpc_server`-supplied method/nsid and a
      shared trusted-keys set + replay cache, returning 401 on any non-`WF_OK`.
      Tested offline by `test_oauth_verify` (happy path + tampered signature,
      expired token, wrong `htm`/`htu`, `ath` mismatch, reused `jti`, missing
      proof `jwk`, non-ES256 `alg`, and Bearer-only / DPoP-only paths).

  57. Service-auth (service JWT) **token issuance** (`server.h`) — the PDS-side
      complement of `com.atproto.server.getServiceAuth`. Where the SDK could
      previously only *request* a service token from a PDS
      (`wf_agent_get_service_auth_typed`) and extract the response's `token`
      field (`wf_server_parse_auth_token`), a self-hosted PDS can now *mint* one
      locally by signing a compact JWT with its own repo signing key.
      `wf_server_create_service_auth(req, key, &token)` builds header
      `{typ:"JWT", alg}` + payload `{iat, iss, aud, exp, [lxm], jti, [nuance]}`
      and signs `base64url(header)"."base64url(payload)` with `wf_sign`
      (P-256 → ES256, secp256k1 → ES256K; 64-byte compact low-S signature),
      defaulting `exp` to `iat+60` and generating a fresh 16-byte hex `jti` per
      call. `wf_server_verify_service_auth(token, didkey, now, &claims)` decodes
      and validates the token: it checks the signature with `wf_verify` against
      the issuer's `did:key` and rejects expired tokens, returning owned claims
      (`wf_service_auth_claims_free`). The signing key is modelled as the
      existing `wf_signing_key` (same representation the crypto layer uses) plus
      the DID strings in `wf_service_auth_request`; no JOSE signing is
      hand-rolled — crypto is delegated to `wf_sign`/`wf_verify`. Wire format
      mirrors `@atproto/xrpc-server` `createServiceJwt`. Tested offline
      (`test_service_auth`): field-level round-trip of iss/aud/exp/iat/jti/lxm/
      nuance, wrong-key / tampered-payload / expired-token rejection, defaulted
      `exp`, and jti uniqueness.

## Next planned work

- Exercise the gated live example test (`test_examples_live`) in CI with real
  credentials (it SKIPs cleanly when `BSKY_HANDLE`/`BSKY_PASSWORD` are unset).
- Continue evaluating upstream C libraries for server-side infrastructure
  (event loop, config parsing).
- Broaden generated typed-wrapper coverage for any remaining lexicon endpoints
  not yet wrapped at the agent level.
- Service-auth *issuance* is now available (`wf_server_create_service_auth` /
  `wf_server_verify_service_auth`, item 57). A natural follow-up is a
  server-side `verifyJwt`-style auth middleware for the XRPC server that
  resolves the issuer's signing key from its DID document (via `identity.h`)
  and enforces `aud`/`lxm` binding on inbound service tokens.
- `app.bsky.notification.putPreferencesV2` is now fully typed
  (`wf_notification_v2_preferences_build`/`_parse`/`_free` +
  `wf_agent_put_notification_preferences_v2_typed`); the v1 `putPreferences`
  `priorities` field is still not transmittable (generated lex input only
  carries `priority`) — see the TODO in `src/notification_prefs_typed.c`.

## Dependencies

- [cJSON](https://github.com/DaveGamble/cJSON) — vendored via CMake FetchContent.
- [libcbor](https://github.com/PJK/libcbor) — vendored via CMake FetchContent for RFC 8949 parsing and serialization primitives.
- OpenSSL (libcrypto) — for SHA-256 hashing.
- [libsecp256k1](https://github.com/bitcoin-core/secp256k1) — for secp256k1 signing. Without it, crypto functions gracefully stub to `WF_ERR_INVALID_ARG`.
