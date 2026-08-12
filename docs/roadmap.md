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
 22. Firehose event production — `sync_publish` builds the framed CBOR event
     messages a relay/PDS streams over WebSocket: a header map `{op, t}`
     immediately followed by the body map, covering `commit`/`sync`/`identity`/
     `account`/`info` and `op:-1` error frames. Exact inverse of the
     `sync_subscribe` decoder; round-trip tested by `test_sync_publish`.
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
    (`wf_agent_identity_rotate_handle`) built on `plc.h`. The convenience now
    wires the full rotation flow: it builds the rotation operation locally as a
    validation gate, then (given the out-of-band `requestPlcOperationSignature`
    token) signs it server-side via `signPlcOperation` and submits it via
    `submitPlcOperation`. With a missing token it triggers the email and returns
    an honest `WF_ERR_INVALID_ARG`.
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
    labeler/actor ownership model. `getActorStatus`/`getStatus`/`putStatus`
    are not real endpoints in the reference (confirmed against
    bluesky-social/atproto and bluesky-social/social-app: no such RPCs
    exist anywhere, only the `app.bsky.actor.status` record type), so the
    agent wrappers read the status embedded in a getProfile(s) response and
    write it via a plain `com.atproto.repo.putRecord`, matching how
    social-app's liveNow feature actually does it. `wf_actor_status_parse_view`
    also had a bug fixed alongside this: createdAt/durationMinutes live inside
    statusView's opaque `record` field, not at the top level, so real
    responses always left both unset. Tested.
 40. `tools.ozone.*` typed coverage (`ozone.h`/`ozone.c`) — initial batch of
     typed convenience wrappers for moderation (queryStatuses, getLabelDefs,
     emitEvent, queryEvents, getEvent, getReporterStats, getSubjects),
     communication templates, and set values. Tested. `getSuggestions` is
     also wrapped but unconfirmed against the reference -- no lexicon,
     server route, or test for `tools.ozone.moderation.getSuggestions`
     exists anywhere in bluesky-social/atproto as of this writing; see the
     comment on `wf_ozone_get_suggestions`.
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

 58. Blob persistence + serving — migrated to MetalBear as
     `metalbear_blob_store*` (see the MetalBear repository for the
     `metalbear_blob_store.h` / `metalbear_blob_store.c` implementation and
     `metalbear_blob_store_server.c` XRPC route handlers). The original
     `wf_blob_store*` source files remain in this repository for historical
     reference but are no longer built or part of the SDK.

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
     (`tools/gen_owners.cpp` → `generated_owners.hpp`, 372 handles from 51 headers).
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
      `exp`,   and jti uniqueness.

  58. Injectable DID-key resolver for commit/label verification (`verify.h` /
      `verify.c`). `wf_verify_record_commit` already verified a signed commit
      CAR against a caller-supplied `did:key:z...` signing key; the
      resolve-and-verify path (`wf_agent_verify_record`) previously stubbed the
      live key-fetch behind `#ifndef WF_TEST_LIVE` and returned
      `WF_ERR_INVALID_ARG`. It now takes an injectable resolver
      (`wf_verify_key_resolver`, installed via `wf_verify_set_key_resolver`) so
      verification can fetch the signing key for a DID/key-id WITHOUT a hard
      network dependency. New `wf_verify_record_commit_resolved(did, car, len,
      &valid)` uses the resolver to obtain the key, then delegates to the
      existing `wf_repo_verify` core. The firehose commit verifier
      (`wf_sync_verify_commit`, the commit/label stream path) now consults the
      same resolver via `wf_verify_resolve_signing_key`, falling back to live
      `wf_did_resolve` when no resolver is set. A built-in
      `wf_verify_resolve_via_did` resolver (backed by `wf_did_resolve`) is
      provided for production use. Honest default preserved: with no resolver
      installed, the resolve-and-verify entry points return `WF_ERR_INVALID_ARG`
      rather than fabricating a key — signatures are never accepted by default.
      Tested offline by `test_verify_resolver` (happy path, wrong-key rejection,
      unavailable-key error, tampered-commit rejection, no-resolver honest
      error, and agent-wrapper resolver wiring with no network reached).

  59. Cross-compilation / platform abstraction (`platform.h`, `src/platform/`,
      `.devdeps/*.cmake`) — a thin platform layer (init/shutdown, mutex,
      monotonic time) with POSIX (Linux/macOS) and Win32 backends fully
      implemented, plus toolchain files and client-only build wiring for
      Nintendo Wii/Wii U/3DS (devkitPro), Windows (MinGW-w64), and Linux/AArch64.
      The Wii platform primitives use libogc (`net_init`, LWP mutexes, and the
      timebase clock); Wii U platform primitives use wut (`socket_lib_init`,
      `OSMutex`, `OSGetTime`, divide-first tick conversion). Wii HTTPS
      (mbedTLS, verified certificate chain, fail-closed entropy), P-256/
      did:key crypto, and secp256k1 (did:key) crypto via mbedTLS are real.
      3DS platform primitives (LightLock mutex, osGetTime clock, httpc
      transport) and mbedtls-based P-256/did:key crypto are real.

  60. Wii WebSocket client (`src/transport/websocket_wii.c`) — a real RFC 6455
      implementation replacing the prior honest stub, built on `wii_tls`
      (mbedTLS handshake + verified certificate chain over lwIP). Covers the
      HTTP/1.1 Upgrade handshake with Sec-WebSocket-Accept verification
      (`mbedtls_sha1` + RFC 4648 base64, mirroring the check the SDK's own
      XRPC server performs as a WS server), masked client framing per RFC
      6455 §5.2/§5.3, fragmented-message reassembly, and transparent
      ping/pong. wss:// only, matching every AT Protocol subscription
      endpoint. The mask key and Sec-WebSocket-Key are drawn from
      `wii_tls_random` — the same DRBG that seeds TLS and P-256 signing —
      never `srand()`/timing. `wii_tls_pending()` (new in `wii_tls.{c,h}`)
      gives `wf_websocket_receive` a real non-blocking readiness check: it
      consults mbedTLS's internal record buffer before falling back to a
      zero-timeout `select()`, so already-decrypted-but-unread bytes are not
      missed. Verified via a devkitPPC cross-build and `powerpc-eabi-nm`: the
      prior stub was missing `wf_websocket_send_ping` entirely, an undefined
      symbol that would only have surfaced at link time in a real firehose/
      Jetstream consumer. As with the rest of the Wii backend, a cross-build
      proves compile/link compatibility, not HTTP/TLS correctness on
      hardware — this has not run on real console hardware.

  61. Typed output-decoder coverage for `ref`-shaped endpoint outputs
      (`tools/wf_lexgen.cpp`) — `wf_lexgen` previously only cataloged a
      query/procedure's output as decodable when its `output.schema` was an
      inline `object`; 29 of 314 callable endpoints instead declare
      `output.schema` as a `ref` to an object def defined elsewhere (e.g.
      `tools.ozone.moderation.getRecord` → `tools.ozone.moderation.defs#recordViewDetail`,
      `com.atproto.identity.resolveIdentity` → `com.atproto.identity.defs#identityInfo`,
      `app.bsky.actor.getProfile` → `app.bsky.actor.defs#profileViewDetailed`),
      and those silently got no `_output_decode_json`/`_output_free` pair —
      callers had to hand-parse raw cJSON. New `output_object_type()` mirrors
      the existing `json_input_alias_type()` pattern for ref-typed inputs:
      for a ref resolving to an object def already in the object catalog,
      `<base>_output` becomes a typedef alias onto that type (no duplicated
      struct), and the generated decode/free functions delegate to the
      referenced type's own `wf_lex_decode_*`/`wf_lex_clear_*`. Raises typed
      output coverage from 231/314 to **260/314** callable endpoints.
      `test_lexgen`'s new `output_decoders_cover_object_shaped_endpoints`
      parses the full bundled lexicon corpus, resolves every callable
      endpoint's output schema the same way the generator does, and asserts
      the generated header declares a decoder/free pair for each of the 260
      — regenerating without this fix would fail that test. The remaining
      54 endpoints have no JSON output at all: 48 reply with an empty body
      (most `procedure`s with no `output` schema at all), and 6 use a
      non-JSON encoding for a raw byte/CAR/JSONL stream
      (`com.atproto.sync.getRepo`/`getRecord`/`getBlocks`/`getCheckout`,
      `com.atproto.sync.getBlob`, `chat.bsky.actor.exportAccountData`) that
      already has a dedicated raw-bytes wrapper elsewhere (`sync_typed.h`,
      `blob.h`) — there is no JSON object for a decoder to produce, so this
      is a structural floor, not a remaining gap.

  62. Typed agent wrappers for the last generated-only callable endpoints —
      `app.bsky.graph.searchStarterPacksV2` (`wf_agent_search_starter_packs_v2_typed`
      / `wf_graph_search_starter_packs_v2_result` in `graph_social_typed.h`,
      reusing the existing per-item starter-pack-view parser and adding the
      optional `hitsTotal` field the v1 endpoint doesn't have),
      `tools.ozone.report.closeReports` (added to the existing X-macro
      endpoint table in `ozone_typed.h`/`.c` — `X(report, closeReports,
      close_reports, P)` — rather than a one-off wrapper), and
      `tools.ozone.moderation.getRecord` upgraded from a raw-`wf_response`
      wrapper to an owning decoder in `ozone_admin_typed.h`/`.c` now that its
      ref-typed output has one (item 61). `app.bsky.video.getJobStatus` /
      `getUploadLimits` (`src/agent/agent.c`) were rewired from a hand-built
      `wf_xrpc_query_params` call plus a `WF_LEX_*_NSID` constant to the
      generated `wf_lex_app_bsky_video_get_*_main_call`, closing the same
      class of drift risk the ref-output decoder gap did; a new
      `test_video_typed_httpd.c` (built under `WOLFRAM_BUILD_TEST_HTTPD`)
      proves the swap against a real local mock PDS rather than by
      inspection alone. `tools.ozone.set.deleteSet` and `.querySets` — named
      in the originating issue as missing — turned out to already be wired
      through the same X-macro table; verified rather than duplicated.
       `tools.ozone.moderation.getRepo` has the identical ref-output shape as
       `getRecord` and was upgraded from the stale QR-vs-Q wrapper to an owning
       decoder alongside it. `internal.bsky.actor.
      getProfiles` stays unwrapped by design (internal namespace, excluded
      from the reference codegen too).

  63. Authenticated WebSocket connections (`websocket.h`/`.c`,
      `websocket_wii.c`) — `wf_websocket_connect_with_headers(url, headers,
      header_count, out)` sends extra HTTP header lines with the WS upgrade
      request on both real backends (desktop libcurl via `CURLOPT_HTTPHEADER`;
      Wii's hand-built RFC 6455 client splices them into the request text).
      `wf_websocket_connect` is now defined in terms of it (NULL/0 headers),
      so every existing caller is unaffected. Used to fix
      `chat.bsky.moderation.subscribeModEvents`
      (`chat_typed.h`/`.c`): this is a documented "private endpoint" requiring
      moderator authentication, but the WS connection previously had no way
      to carry a credential at all — `wf_chat_mod_events_options.access_token`
      is now sent as `Authorization: Bearer <token>` on every (re)connect,
      with `wf_agent_chat_subscribe_mod_events_typed` supplying the agent's
      current session token via `wf_agent_get_session_data` (a session with
      none connects unauthenticated, matching the prior behavior). This was
      investigated for issue #19, which also claimed a missing `filter`
      parameter and said the hand-rolled path should use "the generated
      `_main_call`" — neither holds up: the bundled `subscribeModEvents`
      lexicon (verified byte-identical to the upstream checkout) has no
      `filter` parameter, only `cursor`, and `wf_lexgen` never generates a
      `_main_call` for `subscription`-kind defs at all (only `query`/
      `procedure`), matching `subscribeRepos` and `subscribeLabels`, which
      hand-build their WS URLs the same way `subscribeModEvents` already did.
      The requested offline frame-decode round-trip test also already existed
      (`test_chat_modevents_sub.c`). Fixing the real gap (authentication)
      surfaced a separate, unrelated pre-existing bug: `WOLFRAM_BUILD_SERVER`
      was never defined as a compile macro for that test's target (the CMake
      option only controlled which targets got built), so its Layer-2 e2e
      round-trip had silently never compiled in, even with the option ON —
      fixed alongside extending that e2e test to assert the server observes
      the `Authorization` header for real. Verified with a devkitPPC
      cross-build of both the Wii and Wii U targets (`powerpc-eabi-nm` shows
      `wf_websocket_connect_with_headers` defined in both archives).

  64. Two P-256 crypto primitives (`crypto.h`/`.c`) added for MetalBear's
      WebAuthn/passkey login support: `wf_crypto_p256_verify_allow_malleable`
      (same as `wf_crypto_p256_verify` but accepts either low-S or high-S
      signature form — needed because a WebAuthn authenticator's assertion
      signature, unlike this SDK's own DPoP/service-JWT signing, is not
      guaranteed low-S normalized) and `wf_crypto_ecdsa_der_to_raw` (converts
      a DER-encoded ECDSA-Sig-Value, the form a browser's WebAuthn
      implementation produces, into the raw 64-byte r||s form the verify
      functions expect). The COSE_Key decoder and ceremony/ credential
      storage the prior scoping note below called out as net-new both landed
      in MetalBear (`src/oauth/webauthn.c`, a purpose-built CBOR reader
      rather than `wf_cbor_parse` — see that file's header comment for why
      DAG-CBOR's canonical-ordering enforcement is the wrong fit for a
      browser-produced `attestationObject`), not this SDK; these two
      primitives were the actual SDK-level gap.
  65. Removed five speculative `tools.ozone.moderation.*` wrappers
      (`getLabelDefinitions`/`wf_ozone_get_label_defs`,
      `getSuggestions`/`wf_ozone_get_suggestions` and its typed wrapper
      `wf_ozone_moderation_get_suggestions`,
      `getLabelDefinitions`'s typed wrapper
      `wf_ozone_moderation_get_label_definitions`, `getTag`/`wf_ozone_get_tag`,
      `queryTags`/`wf_ozone_query_tags`, and `scanVerdicts`/
      `wf_ozone_scan_verdicts`), plus their CLI subcommands
      (`ozone moderation get-suggestions`/`get-label-definitions`) and tests.
      Item 40 flagged `getSuggestions` as unconfirmed against the reference;
      revisiting turned up that none of these five NSIDs exist anywhere in
      bluesky-social/atproto's lexicons, the bluesky-social/ozone frontend,
      or a GitHub code search of either — not just missing from the local
      snapshot, but not real, published, or served by any known deployment.
      Shipping API surface for endpoints that don't exist just misleads
      wolfram's own users into writing code against something that will
      404/MethodNotImplemented on every real server. `wf_ozone_get_subjects`
      (`tools.ozone.moderation.getSubjects`, a real lexicon) is unaffected.
  66. Found and fixed a real Wii/Wii U build break while cross-compiling to
      verify item 65 didn't disturb anything nearby: `wf_sign`'s signature
      line and part of `wf_b58_decode`'s for-loop body in `crypto_wii.c` had
      been spliced together since dcc913a (the commit that added secp256k1
      support), leaving `wf_b58_decode`'s error branch replaced by an
      orphaned copy of `wf_sign`'s SECP256K1 block, and `wf_sign` itself
      missing its return type/name and SECP256K1 branch entirely. This
      never surfaced in CI's usual desktop build (crypto_wii.c isn't
      compiled there), and nothing had cross-built Wii U since that commit
      -- confirmed by cross-compiling with devkitPPC, which failed without
      the fix and links clean with it. Also implemented `RAND_bytes`
      (`openssl_compat.c`) for real via the same seeded mbedTLS DRBG
      `wii_tls_random` already uses for TLS/P-256 signing, rather than
      leaving it permanently failing; nothing currently calls it (every
      caller in the tree is excluded from embedded builds), but the next
      thing that starts routing through `openssl/rand.h` on Wii/Wii U
      shouldn't hit a dead end. 3DS keeps failing honestly (no seeded DRBG
      wired up yet; separate, already-tracked gap). Also cleaned up three
      stale TODO comments (`actor_status_typed.h`, `temp_typed.h`/`.c`,
      `plc.c`) that described states already resolved or that were never
      actually gaps.

## Next planned work

- The "full" CI job now passes `BSKY_HANDLE`/`BSKY_PASSWORD`/`BSKY_SERVICE`
  through to `test_examples_live` from repo secrets (`.github/workflows/ci.yml`),
  pointed at MetalBear's own dev PDS (`ewan.bear1.croft.click` on
  bear1.croft.click) rather than production Bluesky -- once the three
  secrets are provisioned, every push to main becomes a live smoke test of
  that deployment, not just an offline build. Still SKIPs cleanly (and CI
  still passes) on a fork or before the secrets are set.
- Continue evaluating upstream C libraries for server-side infrastructure
  (event loop, config parsing).
- Generated typed-wrapper coverage is complete: every query/procedure NSID in
  the full lexicon corpus (314 endpoints across `com.atproto`, `app.bsky`,
  `chat.bsky`, `tools.ozone`, `internal.bsky`) has codegen in
  `atproto_lex.h`/`atproto_lex.c`, and the generated `wf_lex_..._main_call`
  symbol or `WF_LEX_..._NSID` constant for every one of them is referenced by
  application code somewhere outside that codegen file itself — verified by
  deriving each endpoint's expected symbol name from its NSID and grepping
  the whole tree, not by sampling. This is raw-call coverage (every endpoint
  is reachable); typed *output-decoder* coverage is the separate, narrower
  concern tracked by item 61 above (260/314 — the rest have no JSON body to
  decode).
- Service-auth *verification* middleware for the XRPC server is landed
  (`wf_xrpc_server_set_auth_middleware`, `xrpc_server_auth.h`): it resolves the
  issuer's signing key from its DID document, enforces `aud`/`lxm` binding,
  retries once on signature failure for rotated keys, and falls back to
  DPoP-bound OAuth user tokens. Issuer fragments are handled per upstream
  `verifyServiceJwt`: `iss#atproto_labeler` selects the `#atproto_label`
  verification method and any other issuer selects `#atproto`
  (`wf_did_resolve_verification_key`). Per-route principal policies
  (`wf_xrpc_server_auth_config_require_principal`) are landed too: a SERVICE
  rule guards routes that must never accept OAuth user credentials, a USER
  rule guards routes that must never accept service tokens, and an ANY rule
  overrides a broader rule (longest-prefix wins; any matching rule also
  protects its prefix).
- The blob store (item 58) is wired into the full PDS write path in
  MetalBear: `metalbear_blob_store_associate`/`_dissociate`/
  `_is_referenced` track which record URIs reference each blob, and
  createRecord/putRecord/deleteRecord/applyWrites keep that bookkeeping
  current, deleting a blob outright the moment no record references it
  (mirroring the reference PDS's `record_blob` + `deleteDereferencedBlobs`).
  See the MetalBear repository's `blob_store.h`/`repo_store.c` and its
  AGENTS.md "Repo writes" section for the same-CID-across-replacement
  invariant. Blob mimetype/size limits (`accept`/`maxSize` on a lexicon's
  `type: blob` field) were already enforced by `wf_validate_record`
  (`src/validate/validate.c`) — the same place any other schema constraint
  is checked, and the correct place per the reference PDS, which enforces
  these "when the reference is created" rather than at upload time; that
  had SDK-level tests (`test_validate.c`) but no end-to-end proof that
  MetalBear's write handlers actually reject an out-of-bounds blob against
  the real `app.bsky.embed.images` lexicon — `test_repo_store.c`'s
  `run_blob_constraint_validation` now covers that. Remaining follow-up:
  optional at-rest encryption of stored bytes (analogous to
  `WOLFRAM_BUILD_STORE_CRYPTO`).
- `app.bsky.notification.putPreferencesV2` and `getPreferences` share the
  fully typed 13-slot `defs#preferences` representation. The legacy v1
  `putPreferences` endpoint carries only its required `priority` boolean;
  `wf_agent_put_notification_priority` transmits that exact schema.
- Wii HTTPS and WebSocket, and Wii U platform/transport, are real and
  cross-build-verified (though unverified on physical hardware — see items 59
  and 60). Wii secp256k1 is now implemented via mbedTLS (which supports
  secp256k1 natively). 3DS platform primitives (LightLock mutex, osGetTime
  clock, httpc transport) and mbedtls-based P-256/did:key crypto are real.
  Remaining gaps: 3DS transport (httpc-based HTTPS) and 3DS crypto
  (mbedtls-based P-256 and secp256k1) need cross-build verification since
  devkitARM is not available in this environment. See the `TODO` markers in
  `src/platform/3ds_platform.c` and `src/crypto/crypto_3ds.c`.
- Sync v1.1 (ordered `com.atproto.sync.getRepo` CAR block ordering, and
  partial repo export by collection) surveyed on request and deliberately
  not started: neither has a stable upstream target yet. The reference
  PDS's own ordered-CAR-export work exists but "is not performant enough to
  merge" per the [relay-updates blog
  post](https://atproto.com/blog/relay-updates-sync-v1-1), and that same
  post says the exact ordering "needs to be described more formally to
  ensure interoperation" — there is no wire format to cross-reference yet,
  which point 5 of this file's Technical philosophy requires. Partial
  sync-by-collection is listed in the [sync
  spec](https://atproto.com/specs/sync) as a "likely" future direction,
  with implementation "not yet started" upstream. Revisit once
  bluesky-social/pds actually merges ordered export and the spec text
  stabilizes, rather than building against a moving target now.

## Dependencies

- [cJSON](https://github.com/DaveGamble/cJSON) — vendored via CMake FetchContent.
- [libcbor](https://github.com/PJK/libcbor) — vendored via CMake FetchContent for RFC 8949 parsing and serialization primitives.
- OpenSSL (libcrypto) — for SHA-256 hashing.
- [libsecp256k1](https://github.com/bitcoin-core/secp256k1) — for secp256k1 signing on desktop. Console targets use mbedTLS's built-in secp256k1 support instead.
