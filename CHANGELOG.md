# Changelog

Everything that changes for someone using Wolfram goes here, newest first, in the [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) shape. Versions follow [semver](https://semver.org/); while the major version is 0, a breaking change bumps the minor. Every pull request that changes code adds a line under Unreleased (the flow check fails it otherwise), and `tools/release.sh` moves Unreleased into a dated section when I cut a release.

## [Unreleased]

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

[Unreleased]: https://github.com/ewanc26/wolfram/compare/v0.27.0...HEAD
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
