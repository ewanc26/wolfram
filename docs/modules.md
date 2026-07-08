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
| `wolfram/ozone.h`        | Implemented | Ozone moderation-service / labeler helper (verify/emit labels, auth headers) |
| `wolfram/auth_client.h`  | Implemented | Authenticated XRPC client — DPoP-binding OAuth-authenticated query/procedure/blob-upload (`wf_auth_client_*`) with session refresh and DPoP nonce retry |
| `wolfram/plc.h`          | Implemented | DID PLC operation build/sign/submit helpers (`wf_plc_*`): create/rotate/tombstone, signing-key and handle operations with ES256 signature + verification |
| `wolfram/richtext.h`     | Implemented | Rich text facets, grapheme detection, mention/link/tag parsing |
| `wolfram/syntax.h`        | Implemented | DID, handle, NSID, TID, AT URI, RFC 3339, BCP 47 validators |
| `wolfram/atproto_lex.h`   | Implemented | Generated lexicon endpoint wrappers (13K header, 74K source) |
| `wolfram/moderation.h`    | Implemented | Moderation decision engine — blur/alert/inform/filter for accounts, profiles, posts, notifications, feed generators, and user lists from labels, blocks, mutes, hidden posts, and muted words |
| `wolfram/store.h`         | Partial/Optional | SQLite-backed session + repo-mirror persistence + persisted-label storage for the moderation engine (OFF by default; build with `WOLFRAM_BUILD_STORE=ON`; optional `WOLFRAM_BUILD_STORE_CRYPTO=ON` adds libsodium at-rest encryption) |
| `wolfram/threadgate_postgate.h` | Implemented | Threadgate / postgate record helpers — `wf_agent_create_threadgate`/`wf_agent_create_postgate` and `wf_agent_delete_record_by_uri` |
| `wolfram/embed_typed.h`   | Implemented | Generated owning parser for embed records (`app.bsky.embed.*`) |
| `wolfram/feed_typed.h`    | Implemented | Generated owning parser for timeline/feed records (`app.bsky.feed.*`) |
| `wolfram/feedgen_typed.h` | Implemented | Generated owning parser for feed-generator records (`app.bsky.feed.generator`) |
| `wolfram/graph_typed.h`   | Implemented | Generated owning parser for graph records (`app.bsky.graph.*`) |
| `wolfram/list_typed.h`    | Implemented | Generated owning parser for list records (`app.bsky.graph.list*`) |
| `wolfram/thread_typed.h`  | Implemented | Generated owning parser for thread records (`app.bsky.feed.defs#thread*`) |
| `tools/wf_lexgen.py`      | Initial     | Lexicon JSON to typed C data-model declarations |
