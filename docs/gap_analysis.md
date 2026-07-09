# Gap Analysis: Wolfram (C) vs. rsky (Rust)

This document outlines the functional gaps identified by comparing the `wolfram` C SDK with the `rsky` Rust reference implementation.

## Summary of Comparison

| Component | `rsky` (Rust) | `wolfram` (C) | Status | Notes |
|-----------|----------------|----------------|--------|--------|
| **Client SDK** | Full | Full | $\checkmark$ | Parity in session, identity, repo, and agent APIs. |
| **PDS Server** | Full (`rsky-pds`) | Client Only | Gap | `wolfram` provides client wrappers but no server implementation. |
| **Admin CLI** | Full (`rsky-pdsadmin`) | Library Only | Gap | `wolfram` has admin typed wrappers but no standalone admin tool. |
| **Firehose Relay** | Full (`rsky-relay`) | Client Only | Gap | `wolfram` can subscribe but cannot act as a relay server. |
| **Indexer/Pipeline** | Full (`rsky-wintermute`) | Partial | Gap | `wolfram` lacks the background indexing/ingestion pipeline. |
| **Feed Generation** | Server (`rsky-feedgen`) | Helper | Partial | `wolfram` has `wf_feedgen_build_skeleton` but no full service. |
| **Video Pipeline** | Server (`rsky-video`) | Client Only | Gap | `wolfram` handles uploads/status but not the encoding backend. |
| **Labeler Service** | Server (`rsky-labeler`) | Client Only | Gap | `wolfram` has labeler wrappers but not the labeling service. |
| **UI/Frontend** | Full (`rsky-satnav`) | N/A | Gap | Out of scope for C SDK. |

## Detailed Gaps

### 1. Server-Side Implementations
The most significant gap is that `wolfram` is strictly a client-side SDK. `rsky` provides full server implementations for:
- **PDS (Personal Data Server):** Handling account lifecycles, repository storage, and authentication.
- **Relay:** Distributing firehose events.
- **Feed Generator:** A full service to serve feed skeletons.
- **Video Service:** Processing and transcoding video uploads.

### 2. Background Processing (Wintermute)
`rsky-wintermute` implements a specialized indexer for notifications and activity. While `wolfram` can verify firehose commits and subscribe to repos, it lacks the database-backed ingestion and indexing logic required for high-performance notification querying.

### 3. Tooling
While `wolfram` has a CLI for exercising the SDK, it lacks a dedicated PDS administrative tool equivalent to `rsky-pdsadmin`.

## Recommended Next Steps
- Implement a basic PDS server stub to test server-side logic.
- Expand the `wolfram` CLI to include administrative commands using `admin_typed.h`.
- Explore implementing a lightweight version of the firehose indexing pipeline for offline mirror querying.
