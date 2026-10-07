# Changelog

Everything that changes for someone using Wolfram goes here, newest first, in the [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) shape. Versions follow [semver](https://semver.org/); while the major version is 0, a breaking change bumps the minor. Every pull request that changes code adds a line under Unreleased (the flow check fails it otherwise), and `tools/release.sh` moves Unreleased into a dated section when I cut a release.

## [Unreleased]

### Added

- `wf_post_display` now carries a video embed's poster frame, alt text and aspect ratio (`video_thumb`, `video_alt`, `video_width`, `video_height`), so a client that cannot play video can still draw the poster. The playlist is not kept. ([#184](https://github.com/ewanc26/wolfram/pull/184))

## [0.36.2] - 2026-10-07

### Changed

- I've split `profile_tab.c` so the tab names, filters and cycle (`wf_profile_tab_name`, `_filter`, `_next`) can be linked without the agent; `wf_agent_get_profile_tab_typed` is in `profile_tab_fetch.c`. No API change. ([#182](https://github.com/ewanc26/wolfram/pull/182))

## [0.36.0] - 2026-10-07

### Added

- I've added `wolfram/profile_tab.h`: the Posts, Replies, Media and Likes tabs of a profile (`wf_profile_tab_name`, `_filter`, `_next`) and `wf_agent_get_profile_tab_typed`, which fetches one page of a tab. Cobalt carried this alone; Indigo had only the Posts tab. ([#179](https://github.com/ewanc26/wolfram/pull/179))

## [0.35.0] - 2026-10-07

### Added

- I've added `wolfram/saved_feeds.h`: `wf_agent_get_saved_feeds` returns the account's saved custom feeds with display names, read tolerantly from the raw preferences (V2, then V1), and `wf_saved_feeds_parse` is the pure JSON half. Cobalt and Indigo each carried the same ~80 lines. ([#177](https://github.com/ewanc26/wolfram/pull/177))

### Changed

- I've replaced the 36 private copies of `strdup` and 38 of `set_string` scattered through `src/` with one `wf_str_dup` and `wf_str_set` in `wolfram/util.h`, and deleted the copies. Behaviour is the same; `wf_str_dup` is NULL-safe and `wf_str_set` leaves the destination alone on an allocation failure, as every copy did. ([#176](https://github.com/ewanc26/wolfram/pull/176))

### Removed

- I've deleted about 1,800 lines of dead code from `src/agent/agent.c` and `post.c`: six blocks under `#if 0` that said they had moved to `post.c`, `feed.c`, `graph.c` and `notification.c`, and the helpers only they used. Nothing a caller can see changes. ([#175](https://github.com/ewanc26/wolfram/pull/175))

## [0.34.0] - 2026-10-07

### Added

- I've added `wf_agent_set_reply_gate` and `wf_reply_gate` (`wolfram/threadgate_postgate.h`): the "everyone, followed and mentioned, or nobody" choice as one call, so a client does not build the threadgate rules itself. ([#173](https://github.com/ewanc26/wolfram/pull/173))

## [0.33.0] - 2026-10-07

### Added

- I've added `wolfram/attach.h`: `wf_attach_mime`, `wf_attach_scan_images` and `wf_agent_upload_image_file`, so a client can list the JPEG and PNG files in a folder (within `WF_ATTACH_MAX_BYTES`) and upload one as an image embed without carrying its own copy. ([#168](https://github.com/ewanc26/wolfram/pull/169))

## [0.32.0] - 2026-10-07

### Added

- I've added `wf_agent_post_thread` (`wolfram/agent.h`): post several texts as a thread, the first as a top-level post and each later one as a reply to the one before it with the first as root, so a client does not have to carry the root/parent bookkeeping. It stops at the first failure and says how many posts went through. ([#166](https://github.com/ewanc26/wolfram/pull/166))

### Fixed

- I've fixed a use-after-free in `wf_xrpc_server_stop`: a WebSocket worker that had already left the stream list could still be about to call `MHD_upgrade_action` when the daemon was stopped and its upgrade handle freed. That was the intermittent `relay_server` segfault on macOS. `stop()` now waits for such workers. ([#164](https://github.com/ewanc26/wolfram/pull/164))

## [0.31.0] - 2026-10-07

### Added

- I've added `wf_muted_list` (`wolfram/muted_words.h`): a fixed-size, allocation-free copy of the account's muted words with `wf_muted_list_from_prefs`, `wf_muted_list_add` and `wf_muted_list_match`, so Cobalt and Indigo stop each carrying the same struct, add function and load from the preferences. ([#161](https://github.com/ewanc26/wolfram/pull/161))

### Changed

- `wf_muted_words_match` no longer has its own matching rules: it adapts the account's mutedWord to `wf_mod_match_mute_words`, the official client's matcher, so Cobalt, Indigo and the moderation code apply the same rules. The visible difference is that punctuation at the ends of a word is ignored (`cat` now matches `cat.` and `(cat)`, still not `cat's`) and a single character matches as a substring. It still takes `now`, so the console's clock decides expiry. ([#159](https://github.com/ewanc26/wolfram/pull/159))
- `wf_failure_classify` reads a `WF_ERR_HTTP` with no HTTP status and no error name as a server failure rather than `other`, so a client that only has the `wf_status` (Indigo's sign-in and feeds) still says the service had a problem. ([#160](https://github.com/ewanc26/wolfram/pull/160))

## [0.30.0] - 2026-10-07

### Added

- I've added `wolfram/drag.h`: `wf_drag_begin`, `wf_drag_move` and `wf_drag_end` turn a finger dragged along a list into whole rows to scroll, with the slop that separates a tap from a drag and the carry between frames, so Indigo and Cobalt scroll their feeds by touch with one implementation. Strict C89. ([#157](https://github.com/ewanc26/wolfram/pull/157))

## [0.29.0] - 2026-10-07

### Added

- I've added `wolfram/cdn.h`: `wf_bsky_cdn_url()` rewrites a `cdn.bsky.app` image URL to another preset (`avatar_thumbnail` is 128 px) and to `@jpeg` or `@png`. A URL with no format comes back as WebP, which `wolfram/image.h` cannot decode, and the full-size presets are far larger than a console can draw; Indigo's avatars failed on both counts. Strict C89, with vectors. ([#152](https://github.com/ewanc26/wolfram/pull/152))

### Fixed

- Decoding a JPEG on a thread made with libctru's `threadCreate` no longer crashes the 3DS build. stb_image's thread-local failure reason corrupted memory there, so Indigo's avatar loader died a moment after its first successful decode. `wf_image_decode_rgba` now builds stb without thread-local storage. Found by running Indigo in Azahar. ([#154](https://github.com/ewanc26/wolfram/pull/154))

## [0.28.0] - 2026-10-06

### Added

- I've added `wolfram/failure.h`: `wf_failure_classify(status, http_status, xrpc_error)` gives one failure kind (bad credentials, network, timeout, TLS, rate limit, server, bad response, not ready, other) and `wf_failure_tag()` a stable tag for it, so Cobalt, Indigo and the rest stop deriving it separately. The mapping is in `docs/failure.md` and pinned by `test/vectors/failure.json`; message text stays in each client. It builds as C89, so the Mac OS 9 target compiles it. ([#145](https://github.com/ewanc26/wolfram/pull/145), [#114](https://github.com/ewanc26/wolfram/issues/114))
- I've added `wf_update_verify_signature()` and `wolfram/ed25519.h`: Ed25519 verification (with its own SHA-512, no OpenSSL or mbedTLS behind it, so the consoles can use it) for a detached `update.json.sig` over the manifest, so an update can be checked against a public key compiled into the client and a replaced release refused. Checked against RFC 8032 vectors and 1500 random signatures against pyca/cryptography. Signing stays in each client's release workflow. ([#148](https://github.com/ewanc26/wolfram/pull/148), [#106](https://github.com/ewanc26/wolfram/issues/106))
- The libcurl transport now reports a failed TLS handshake or certificate check as `WF_ERR_TLS` instead of lumping it in with `WF_ERR_NETWORK`, so a client can tell "the secure connection failed" from "I can't reach it". The console and Mac OS 9 transports still say `WF_ERR_NETWORK` until I can check them on hardware. The C++ and C# status mirrors also gain `WF_ERR_AUTH`, which they were missing. ([#142](https://github.com/ewanc26/wolfram/pull/142), [#141](https://github.com/ewanc26/wolfram/issues/141))

### Fixed

- `tools/repo_sync.py labels apply` now deletes an old label that was meant to be renamed when the new name already exists, but only if no issue still carries it; otherwise it says which issue to relabel. Platinum hit this with `accessibility`. ([#139](https://github.com/ewanc26/wolfram/pull/139))

### Added

- I've added `wolfram/muted_words.h`: one implementation of muted-word matching for the account's preferences, including `actorTarget` (`exclude-following`) and `expiresAt`, which Cobalt's and Indigo's copies never read. ([#137](https://github.com/ewanc26/wolfram/pull/137), [#112](https://github.com/ewanc26/wolfram/issues/112))
- I've added `wolfram/qr.h`: a QR code encoder that returns the module matrix (byte mode, versions 1 to 10, error correction levels L to H), so a client without a browser can show a link as a code to scan. Checked by decoding every version and level with an independent decoder. ([#138](https://github.com/ewanc26/wolfram/pull/138), [#132](https://github.com/ewanc26/wolfram/issues/132))
- I've added `wolfram/time.h`: parse a lexicon `datetime` to Unix seconds (numeric offsets converted, not refused), format one back, and give a short relative age, all without `timegm` or the locale, so Cobalt and Indigo can delete their copies. ([#133](https://github.com/ewanc26/wolfram/pull/133), [#116](https://github.com/ewanc26/wolfram/issues/116))
- I've added `wf_crypto_base64url_decode_strict()`: URL alphabet only, no padding, no length of 1 mod 4, and trailing bits must be zero, so each byte string has exactly one encoding. MetalBear can drop the copy in its OAuth code. ([#134](https://github.com/ewanc26/wolfram/pull/134), [#115](https://github.com/ewanc26/wolfram/issues/115))

### Changed

- A merged release PR now publishes itself: the `release` workflow runs `tools/release.sh publish` once CI is green on the release commit, so a release no longer depends on anyone being able to create one from their own machine. ([#131](https://github.com/ewanc26/wolfram/pull/131)). The changelog drift check gives that workflow 30 minutes to tag a merged release, then fails, so a release that never got tagged turns checks red.

## [0.27.0] - 2026-10-06

### Added

- I've added a client for the sign-in pairing contract the hosted OAuth node speaks: `wolfram/oauth_pairing.h` parses the begin and poll replies and runs the whole begin/poll loop, stopping on a 404 instead of polling a dead code for nine minutes. The contract and its test vectors are in `docs/oauth-pairing.md` and `test/vectors/oauth_pairing.json`. ([#117](https://github.com/ewanc26/wolfram/pull/117), [#101](https://github.com/ewanc26/wolfram/issues/101))
- I've added `wolfram/update.h` for client self-update: release-manifest parsing with a caller policy, semver comparison that refuses malformed versions, and SHA-256 download verification, the core of it in C89 so the Mac OS 9 target can use it. ([#118](https://github.com/ewanc26/wolfram/pull/118), [#106](https://github.com/ewanc26/wolfram/issues/106))
- I've added `wf_websocket_support_detail()`, which says how `wf_websocket_supported()` reached its answer. ([#120](https://github.com/ewanc26/wolfram/pull/120))
- The shared flow for all five repos now lives here: a reusable workflow (conventions, drift, house style, labels and metadata), the PR template, `docs/flow.md`, `docs/house-style.md`, `.github/labels.yml` and `.github/repo-metadata.yml`. ([#102](https://github.com/ewanc26/wolfram/pull/102), [#110](https://github.com/ewanc26/wolfram/pull/110), [#119](https://github.com/ewanc26/wolfram/pull/119))
- I now keep this changelog, and `flow / changelog` checks it on every PR in all five repos: code changes need an Unreleased entry, and every tag needs a section. ([#121](https://github.com/ewanc26/wolfram/pull/121))
- CI now builds and tests on macOS, against both Apple's libcurl and Homebrew's. ([#108](https://github.com/ewanc26/wolfram/pull/108))

### Changed

- `tools/release.sh publish` now lets GitHub create the tag when it creates the release, instead of pushing the tag itself, so it works where `git push` of a tag is refused. ([#126](https://github.com/ewanc26/wolfram/pull/126))
- Issues are filed through nine atomic forms (one per kind, plus parity gap, duplication and needs-owner) instead of the old Markdown templates. Blank issues are off and the contact link is a private security advisory. The forms are shared by all five repos and drift-checked. ([#127](https://github.com/ewanc26/wolfram/pull/127))
- `tools/release.sh` no longer pushes to `main`. A release goes through a `release/vX.Y.Z` pull request, is tagged only on a commit whose `CI gate` is green, and takes its notes from this file. It also refuses to run without `--consumers-verified`. ([#102](https://github.com/ewanc26/wolfram/pull/102), [#121](https://github.com/ewanc26/wolfram/pull/121))

### Fixed

- `wf_agent_set_bearer()` refused every valid DID and handle: it compared the syntax validators, which return nonzero for valid input, against `WF_OK`. Sign-in through the OAuth node could never finish. Cobalt's end-to-end test of the pairing client found it. ([#124](https://github.com/ewanc26/wolfram/issues/124))
- On macOS, Apple's libcurl exports the WebSocket API without supporting it. Wolfram assumed support and the WebSocket tests failed; it now asks libcurl with a probe connect. ([#108](https://github.com/ewanc26/wolfram/pull/108))
- `chat_modevents_sub` hung with any libcurl that completes the loopback upgrade, because the test never published its subscription handle and so could never stop it. (`aabb55e`, https://github.com/ewanc26/wolfram/commit/aabb55e)
- The pairing client and the update module are now linked into console builds; the pairing client first shipped inside the non-console block and failed to link on the 3DS. ([#117](https://github.com/ewanc26/wolfram/pull/117))

## [0.26.0] - 2026-10-05

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.26.0).

## [0.25.0] - 2026-10-04

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.25.0).

## [0.24.0] - 2026-10-01

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.24.0).

## [0.23.1] - 2026-09-24

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.23.1).

## [0.23.0] - 2026-09-24

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.23.0).

## [0.22.0] - 2026-09-23

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.22.0).

## [0.21.3] - 2026-09-23

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.21.3).

## [0.21.2] - 2026-08-26

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.21.2).

## [0.21.1] - 2026-08-12

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.21.1).

## [0.21.0] - 2026-08-12

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.21.0).

## [0.20.1] - 2026-08-12

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.20.1).

## [0.20.0] - 2026-08-12

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.20.0).

## [0.19.0] - 2026-08-12

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.19.0).

## [0.18.4] - 2026-08-12

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.18.4).

## [0.18.3] - 2026-08-11

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.18.3).

## [0.18.2] - 2026-08-11

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.18.2).

## [0.18.1] - 2026-08-11

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.18.1).

## [0.18.0] - 2026-08-11

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.18.0).

## [0.17.0] - 2026-08-11

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.17.0).

## [0.16.0] - 2026-08-11

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.16.0).

## [0.15.0] - 2026-08-11

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.15.0).

## [0.14.3] - 2026-08-11

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.14.3).

## [0.14.2] - 2026-08-11

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.14.2).

## [0.14.1] - 2026-08-11

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.14.1).

## [0.14.0] - 2026-08-11

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.14.0).

## [0.13.1] - 2026-08-10

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.13.1).

## [0.13.0] - 2026-08-10

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.13.0).

## [0.12.2] - 2026-08-10

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.12.2).

## [0.12.1] - 2026-08-10

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.12.1).

## [0.12.0] - 2026-08-10

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.12.0).

## [0.11.0] - 2026-08-09

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.11.0).

## [0.10.1] - 2026-08-09

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.10.1).

## [0.10.0] - 2026-08-08

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.10.0).

## [0.9.0] - 2026-08-08

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.9.0).

## [0.8.1] - 2026-08-08

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.8.1).

## [0.8.0] - 2026-08-07

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.8.0).

## [0.7.0] - 2026-08-06

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.7.0).

## [0.6.12] - 2026-08-06

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.6.12).

## [0.6.11] - 2026-08-06

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.6.11).

## [0.6.10] - 2026-08-06

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.6.10).

## [0.6.9] - 2026-08-06

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.6.9).

## [0.6.8] - 2026-08-05

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.6.8).

## [0.6.7] - 2026-08-05

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.6.7).

## [0.6.6] - 2026-08-05

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.6.6).

## [0.6.5] - 2026-08-05

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.6.5).

## [0.6.4] - 2026-08-01

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.6.4).

## [0.6.3] - 2026-08-01

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.6.3).

## [0.6.2] - 2026-08-01

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.6.2).

## [0.6.1] - 2026-08-01

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.6.1).

## [0.6.0] - 2026-08-01

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.6.0).

## [0.5.0] - 2026-08-01

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.5.0).

## [0.4.0] - 2026-08-01

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.4.0).

## [0.3.0] - 2026-08-01

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.3.0).

## [0.2.6] - 2026-07-31

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.2.6).

## [0.2.5] - 2026-07-31

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.2.5).

## [0.2.4] - 2026-07-27

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.2.4).

## [0.2.3] - 2026-07-27

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.2.3).

## [0.2.2] - 2026-07-27

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.2.2).

## [0.2.1] - 2026-07-27

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.2.1).

## [0.2.0] - 2026-07-25

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.2.0).

## [0.1.4] - 2026-07-23

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.1.4).

## [0.1.3] - 2026-07-23

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.1.3).

## [0.1.2] - 2026-07-23

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.1.2).

## [0.1.1] - 2026-07-21

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.1.1).

## [0.1.0] - 2026-07-05

Released before this changelog existed. The notes, generated from the commit subjects of the time, are on the [release page](https://github.com/ewanc26/wolfram/releases/tag/v0.1.0).

[Unreleased]: https://github.com/ewanc26/wolfram/compare/v0.36.2...HEAD
[0.36.2]: https://github.com/ewanc26/wolfram/releases/tag/v0.36.2
[0.36.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.36.0
[0.35.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.35.0
[0.34.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.34.0
[0.33.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.33.0
[0.32.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.32.0
[0.31.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.31.0
[0.30.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.30.0
[0.29.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.29.0
[0.28.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.28.0
[0.27.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.27.0
[0.26.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.26.0
[0.25.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.25.0
[0.24.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.24.0
[0.23.1]: https://github.com/ewanc26/wolfram/releases/tag/v0.23.1
[0.23.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.23.0
[0.22.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.22.0
[0.21.3]: https://github.com/ewanc26/wolfram/releases/tag/v0.21.3
[0.21.2]: https://github.com/ewanc26/wolfram/releases/tag/v0.21.2
[0.21.1]: https://github.com/ewanc26/wolfram/releases/tag/v0.21.1
[0.21.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.21.0
[0.20.1]: https://github.com/ewanc26/wolfram/releases/tag/v0.20.1
[0.20.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.20.0
[0.19.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.19.0
[0.18.4]: https://github.com/ewanc26/wolfram/releases/tag/v0.18.4
[0.18.3]: https://github.com/ewanc26/wolfram/releases/tag/v0.18.3
[0.18.2]: https://github.com/ewanc26/wolfram/releases/tag/v0.18.2
[0.18.1]: https://github.com/ewanc26/wolfram/releases/tag/v0.18.1
[0.18.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.18.0
[0.17.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.17.0
[0.16.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.16.0
[0.15.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.15.0
[0.14.3]: https://github.com/ewanc26/wolfram/releases/tag/v0.14.3
[0.14.2]: https://github.com/ewanc26/wolfram/releases/tag/v0.14.2
[0.14.1]: https://github.com/ewanc26/wolfram/releases/tag/v0.14.1
[0.14.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.14.0
[0.13.1]: https://github.com/ewanc26/wolfram/releases/tag/v0.13.1
[0.13.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.13.0
[0.12.2]: https://github.com/ewanc26/wolfram/releases/tag/v0.12.2
[0.12.1]: https://github.com/ewanc26/wolfram/releases/tag/v0.12.1
[0.12.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.12.0
[0.11.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.11.0
[0.10.1]: https://github.com/ewanc26/wolfram/releases/tag/v0.10.1
[0.10.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.10.0
[0.9.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.9.0
[0.8.1]: https://github.com/ewanc26/wolfram/releases/tag/v0.8.1
[0.8.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.8.0
[0.7.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.7.0
[0.6.12]: https://github.com/ewanc26/wolfram/releases/tag/v0.6.12
[0.6.11]: https://github.com/ewanc26/wolfram/releases/tag/v0.6.11
[0.6.10]: https://github.com/ewanc26/wolfram/releases/tag/v0.6.10
[0.6.9]: https://github.com/ewanc26/wolfram/releases/tag/v0.6.9
[0.6.8]: https://github.com/ewanc26/wolfram/releases/tag/v0.6.8
[0.6.7]: https://github.com/ewanc26/wolfram/releases/tag/v0.6.7
[0.6.6]: https://github.com/ewanc26/wolfram/releases/tag/v0.6.6
[0.6.5]: https://github.com/ewanc26/wolfram/releases/tag/v0.6.5
[0.6.4]: https://github.com/ewanc26/wolfram/releases/tag/v0.6.4
[0.6.3]: https://github.com/ewanc26/wolfram/releases/tag/v0.6.3
[0.6.2]: https://github.com/ewanc26/wolfram/releases/tag/v0.6.2
[0.6.1]: https://github.com/ewanc26/wolfram/releases/tag/v0.6.1
[0.6.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.6.0
[0.5.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.5.0
[0.4.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.4.0
[0.3.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.3.0
[0.2.6]: https://github.com/ewanc26/wolfram/releases/tag/v0.2.6
[0.2.5]: https://github.com/ewanc26/wolfram/releases/tag/v0.2.5
[0.2.4]: https://github.com/ewanc26/wolfram/releases/tag/v0.2.4
[0.2.3]: https://github.com/ewanc26/wolfram/releases/tag/v0.2.3
[0.2.2]: https://github.com/ewanc26/wolfram/releases/tag/v0.2.2
[0.2.1]: https://github.com/ewanc26/wolfram/releases/tag/v0.2.1
[0.2.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.2.0
[0.1.4]: https://github.com/ewanc26/wolfram/releases/tag/v0.1.4
[0.1.3]: https://github.com/ewanc26/wolfram/releases/tag/v0.1.3
[0.1.2]: https://github.com/ewanc26/wolfram/releases/tag/v0.1.2
[0.1.1]: https://github.com/ewanc26/wolfram/releases/tag/v0.1.1
[0.1.0]: https://github.com/ewanc26/wolfram/releases/tag/v0.1.0
