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

## Next planned work

- Exercise the gated live example test (`test_examples_live`) in CI with real
  credentials (it SKIPs cleanly when `BSKY_HANDLE`/`BSKY_PASSWORD` are unset).
- Continue evaluating upstream C libraries for server-side infrastructure
  (event loop, config parsing).
- Consider WebSocket subscription endpoints served from the XRPC server (e.g.
  `com.atproto.sync.subscribeRepos` / `com.atproto.label.subscribeLabels`
  relays), building on the SSE streaming transport.
- Broaden generated typed-wrapper coverage for any remaining lexicon endpoints
  not yet wrapped at the agent level.

## Dependencies

- [cJSON](https://github.com/DaveGamble/cJSON) — vendored via CMake FetchContent.
- [libcbor](https://github.com/PJK/libcbor) — vendored via CMake FetchContent for RFC 8949 parsing and serialization primitives.
- OpenSSL (libcrypto) — for SHA-256 hashing.
- [libsecp256k1](https://github.com/bitcoin-core/secp256k1) — for secp256k1 signing. Without it, crypto functions gracefully stub to `WF_ERR_INVALID_ARG`.
