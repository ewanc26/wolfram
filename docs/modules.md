# Modules

| Module                  | Status      | Notes                                          |
| ------------------------ | ----------- | ----------------------------------------------- |
| `wolfram/xrpc.h`          | Implemented | libcurl-backed query/procedure calls, binary blob upload |
| `wolfram/session.h`       | Implemented | PDS login, resume, refresh, get, and logout     |
| `wolfram/identity.h`      | Implemented | did:plc, did:web, portable c-ares/POSIX DNS TXT, well-known fallback |
| `wolfram/repo.h`          | Implemented | DAG-CBOR parse/serialize, CID, CAR, MST, commit, diff verify/apply, operation inversion |
| `wolfram/crypto.h`        | Implemented | secp256k1 + P-256 keygen, sign, verify          |
| `wolfram/oauth.h`         | Implemented | OAuth discovery, PKCE/DPoP, PAR/token calls, callback validation, and persistent state |
| `wolfram/server.h`         | Implemented | Server account management — describeServer, createAccount, app passwords, deleteAccount, password reset |
| `wolfram/jetstream.h`     | Implemented | Filtered Jetstream JSON subscription transport, cursor reconnect/backoff, optional zstd, and typed commit/identity/account/sync event payload parsing (owned `wf_jetstream_event_typed`) |
| `wolfram/json.h`         | Implemented | Generic (non-Lexicon) JSON: canonical round-trip and a JSON-Schema validator subset (type/required/properties/items, enum/const, format, numeric bounds, string length/pattern, array constraints, additionalProperties, anyOf/oneOf/not) |
| `wolfram/label.h`        | Implemented | Label subscription (com.atproto.label.subscribeLabels) via WebSocket |
| `wolfram/sync.h`          | Implemented | Firehose subscribeRepos subscription, commit verification, CAR download, plus getBlob/getBlocks/getRecord/listBlobs/getHead/getLatestCommit/getRepoStatus/listRepos |
| `wolfram/validate.h`     | Implemented | Runtime Lexicon schema validation (records and named values), refs/unions/format keywords |
| `wolfram/agent.h`        | Implemented | High-level BskyAgent-style API: session, posts, profile, social graph, feeds, **preferences**, **push registration**, notifications, blobs, **video upload** (`wf_agent_upload_video`/`wf_agent_get_video_job_status`/`wf_agent_get_video_upload_limits`), server + app-password management |
| `wolfram/blob.h`         | Implemented | Binary blob upload — image/blob POST (`wf_xrpc_upload_blob`) and dedicated **video** upload (`wf_agent_upload_video`, `wf_uploaded_blob_free`) |
| `wolfram/chat_typed.h`   | Implemented | Chat (DM) — `chat.bsky.convo`/`group`/`actor`/`moderation` write+query wrappers with chat-service endpoint resolution |
| `wolfram/ozone.h`        | Implemented | Ozone moderation-service / labeler helper — verify and emit labels, build service auth headers. Full typed wrapper coverage across all tools.ozone.* namespaces: moderation, queue, report, team, verification, signature, setting, hosting, server, safelink, communication, and set value wrappers |
| `wolfram/auth_client.h`  | Implemented | Authenticated XRPC client — DPoP-binding OAuth-authenticated query/procedure/blob-upload (`wf_auth_client_*`) with session refresh and DPoP nonce retry |
| `wolfram/plc.h`          | Implemented | DID PLC operation build/sign/submit helpers (`wf_plc_*`): create/rotate/tombstone, signing-key and handle operations with ES256 signature + verification |
| `wolfram/xrpc_server.h`  | Implemented | Optional XRPC server (libmicrohttpd) — route registration, auth middleware, CORS, POST body accumulation, GET query-param parsing, token-bucket rate limiter (`wf_rate_limiter`) |
| `wolfram/richtext.h`     | Implemented | Rich text facets, grapheme detection, mention/link/tag parsing |
| `wolfram/syntax.h`        | Implemented | DID, handle, NSID, TID, AT URI, RFC 3339, BCP 47 validators |
| `wolfram/atproto_lex.h`   | Implemented | Generated lexicon endpoint wrappers (~15K-line header, ~82K-line source; all 312 query/procedure endpoints) |
| `wolfram/moderation.h`    | Implemented | Moderation decision engine — blur/alert/inform/filter for accounts, profiles, posts, notifications, feed generators, and user lists from labels, blocks, mutes, hidden posts, and muted words |
| `wolfram/store.h`         | Partial/Optional | SQLite-backed session + repo-mirror persistence + persisted-label storage for the moderation engine (OFF by default; build with `WOLFRAM_BUILD_STORE=ON`; optional `WOLFRAM_BUILD_STORE_CRYPTO=ON` adds libsodium at-rest encryption) |
| `wolfram/threadgate_postgate.h` | Implemented | Threadgate / postgate record helpers — `wf_agent_create_threadgate`/`wf_agent_create_postgate` and `wf_agent_delete_record_by_uri` |
| `wolfram/embed_typed.h`   | Implemented | Generated owning parser for embed records (`app.bsky.embed.*`) |
| `wolfram/feed_typed.h`    | Implemented | Generated owning parser for timeline/feed records (`app.bsky.feed.*`) |
| `wolfram/feedgen_typed.h` | Implemented | Generated owning parser for feed-generator records (`app.bsky.feed.generator`) |
| `wolfram/graph_typed.h`   | Implemented | Generated owning parser for graph records (`app.bsky.graph.*`) |
| `wolfram/list_typed.h`    | Implemented | Generated owning parser for list records (`app.bsky.graph.list*`) |
| `wolfram/thread_typed.h`  | Implemented | Generated owning parser for thread records (`app.bsky.feed.defs#thread*`) |
| `wolfram/labeler_typed.h` | Implemented | Owned typed parsers + agent wrappers for labeler service coverage (`com.atproto.label.queryLabels`, `app.bsky.labeler.getServices`, `com.atproto.temp.fetchLabels`, service record + policies + label value defs) |
| `wolfram/lexicon_typed.h` | Implemented | Owned parser + agent wrapper for `com.atproto.lexicon.resolveLexicon` (lexicon document fetch by NSID/version) |
| `wolfram/identity_typed.h` | Implemented | Owned typed parsers + agent wrappers for `com.atproto.identity` (resolveHandle, resolveDid, updateHandle, getRecommendedDidCredentials, signPlcOperation, submitPlcOperation, resolveIdentity, refreshIdentity) plus a PLC handle-rotation convenience (`wf_agent_identity_rotate_handle`) |
| `wolfram/notification_v2_typed.h` | Implemented | Owned typed parsers + agent wrappers for `app.bsky.notification` v2 preferences + activity subscriptions (`putPreferencesV2`, `listActivitySubscriptions`, `putActivitySubscription`) |
| `wolfram/actor_status_typed.h` | Implemented | Owned typed wrappers for `app.bsky.actor.status` (stored record + `#statusView`; query/procedure ops are honest stubs — the lexicon defines only a `record`) |
| `wolfram/video_typed.h`   | Implemented | Owned typed parsers + builders + agent wrappers for `app.bsky.video` (getJobStatus, getUploadLimits, uploadVideo) |
| `wolfram/unspecced_typed.h` | Implemented | Owned typed parsers for `app.bsky.unspecced` query endpoints (trends, suggested users, thread v2, etc.) |
| `wolfram/temp_typed.h`    | Implemented | Owned typed parsers + agent wrappers for `com.atproto.temp` account/signup helper flows |
| `wolfram/admin_typed.h`   | Implemented | Owned typed parsers + agent wrappers for `com.atproto.admin` PDS/service administration |
| `wolfram/contact_typed.h` | Implemented | Owned typed parsers + agent wrappers for `app.bsky.contact` (import / phone verification / match discovery) |
| `wolfram/draft_typed.h`   | Implemented | Owned typed parser + agent wrappers for `app.bsky.draft` (post drafts) |
| `wolfram/ageassurance_typed.h` | Implemented | Owned typed parsers + agent wrappers for `app.bsky.ageassurance` (age verification state) |
| `wolfram/bookmark_typed.h` | Implemented | Owned typed parser + agent wrappers for `app.bsky.bookmark` (bookmarks) |
| `wolfram/oauth/verify.h`  | Implemented | OAuth resource-server token verification (`wf_oauth_verify_bearer`/`_dpop`/`_request`) with DPoP replay cache and trusted-key set |
| `wolfram/sync_publish.h`  | Implemented | Firehose event production — builds framed `{header}{body}` CBOR messages (`wf_sync_publish_event`/`_error`), the inverse of `sync_subscribe` |
| `wolfram/feedgen_server.h` | Optional | libmicrohttpd feed-generator skeleton server serving `getFeedSkeleton`/`getFeedGenerator` (`WOLFRAM_BUILD_SERVER`) |
| `wolfram/relay_server.h`  | Optional | libmicrohttpd generic upstream→downstream WebSocket subscription relay (`WOLFRAM_BUILD_SERVER`) |
| `wolfram/blob_store.h`    | Optional | Self-contained blob persistence + serving (in-memory or file-backed); XRPC integration registers `uploadBlob`/`getBlob` (`WOLFRAM_BUILD_SERVER`; core store always built) |
| `wolfram/platform.h`      | Implemented | Platform abstraction (init/shutdown, mutex, monotonic time). POSIX + Win32 implemented; Wii/Wii U/3DS ship honest `WF_ERR_NOT_IMPLEMENTED` stubs |
| `examples/` | Implemented | Higher-level endpoint examples using generated clients (label query, PLC handle rotation, notification v2, admin search) |
| `tools/wf_lexgen.py`      | Implemented | Lexicon JSON → typed C declarations, recursive input encoders, endpoint wrappers, and owning output decoders (development-time only; generated output is checked in) |
