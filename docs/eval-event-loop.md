# Evaluation: Event Loop Library for Wolfram XRPC Server

**Issue:** [#31](https://github.com/ewanc26/wolfram/issues/31)
**Date:** 2026-08-26

## Current State

The XRPC server (`src/server/xrpc_server.c`) uses libmicrohttpd (MHD) with
`MHD_USE_INTERNAL_POLLING_THREAD` and a configurable thread pool sized to
`CPU count * 2` (default 8). On Wii U, a bundled `mhd_shim.c` (976 lines)
reimplements the MHD API surface with a thread-per-connection model.

### Architecture

| Component | Model | File |
|-----------|-------|------|
| HTTP requests | MHD thread pool (desktop) or 1-thread-per-conn (shim) | `xrpc_server.c`, `mhd_shim.c` |
| SSE streams | Suspend/resume via content-reader callback | `xrpc_server.c:499-526` |
| WebSocket | 1 pthread per upgrade, self-detaches or joined at teardown | `xrpc_server_ws.c:288-444` |
| Relay | 1 detached pthread per downstream, reconnect loop | `relay_server.c:105-136` |
| Route lookup | Linear scan under `routes_mutex` | `xrpc_server.c:947-987` |

### Strengths

1. **Clean locking discipline** — four mutexes (`routes_mutex`, `sse_mutex`,
   `ws_mutex`, `rate_limit_mutex`) with documented `_locked` variant pattern.
2. **No open TODOs** — the server module is free of FIXMEs or performance
   comments.
3. **SSE suspend/resume works correctly** — condition-variable-based, CPU-efficient.
4. **WebSocket teardown is safe** — reference-counted workers, graceful half-close,
   `ws_stopping` latch prevents races.
5. **Cross-platform** — desktop (real MHD), Wii U (shim), no platform-specific
   event loop APIs.

### Limitations

| Limitation | Impact | Severity |
|-----------|--------|----------|
| Thread-per-connection (shim) | Each connection costs a pthread (~8KB stack) | Low for PDS workloads |
| No keep-alive (shim) | Every response closes the TCP connection | Low (reverse proxy handles this) |
| No HTTP pipelining (shim) | Sequential request processing per connection | Negligible for AT Protocol |
| Linear route lookup | O(n) scan under mutex per request | Low (route table is small, <100 entries) |
| Hardcoded listen backlog (16) | SYN drops under burst | Low (reverse proxy absorbs bursts) |
| Sequential WS teardown | Blocking join of all workers at stop | Low (only at shutdown) |
| 2-second WS write timeout | Slow consumers get disconnected | By design (prevents head-of-line blocking) |

## Candidate Event Loop Libraries

| Library | Stars | License | Language | Dependencies | Platform Support |
|---------|-------|---------|----------|-------------|-----------------|
| **libuv** | 25k+ | MIT | C | None | Linux (epoll), macOS (kqueue), Windows (IOCP), embedded stubs |
| **libev** | 1k+ | BSD | C | None | Linux (epoll), macOS (kqueue), no Windows |
| **libevent** | 10k+ | BSD | C | Optional OpenSSL | Linux, macOS, Windows |
| **io_uring** | N/A | N/A | C (kernel API) | liburing | Linux 5.1+ only |
| **kqueue/epoll** | N/A | N/A | C (syscall) | None | Platform-specific |

### Assessment Against Wolfram Constraints

**Constraint 1: Must work on Wii U (embedded, no epoll/kqueue).**
- libuv: Has embedding support but requires platform-specific backends. Wii U
  would need a custom backend (no existing one).
- libev: Same — needs a custom backend for Wii U.
- libevent: Same.
- Raw kqueue/epoll: Platform-specific, not portable to Wii U.
- **MHD (current):** Already works on Wii U via the bundled shim. No event
  loop library has a Wii U backend.

**Constraint 2: Must support SSE (long-lived suspend/resume connections).**
- libuv: `uv_poll_t` can watch fds, but SSE suspend/resume requires manual
  implementation. No built-in HTTP layer.
- libev: Same — no HTTP layer.
- libevent: Has `evhttp` but it's deprecated and less capable than MHD for
  SSE.
- **MHD (current):** `MHD_suspend_connection`/`MHD_resume_connection` is
  already implemented and working.

**Constraint 3: Must support WebSocket upgrades.**
- libuv: No built-in WebSocket. Would need a separate library (e.g.
  libwebsockets).
- libev: Same.
- libevent: Same.
- **MHD (current):** `MHD_ALLOW_UPGRADE` + `MHD_create_response_for_upgrade`
  is already implemented.

**Constraint 4: Thread safety for shared state.**
- All candidate event loop libraries are single-threaded by design (one loop,
  one thread). The current multi-threaded model with mutex-guarded shared state
  would need significant restructuring.

## Recommendation: **No change.**

The current MHD-based architecture is the right choice for this project because:

1. **No event loop library has a Wii U backend.** The bundled `mhd_shim.c`
   already solves the embedded-platform problem with a clean, minimal
   implementation. Switching to libuv/libev would require writing a custom
   platform backend anyway — more code than the shim, not less.

2. **MHD provides the HTTP/SSE/WebSocket abstraction the server needs.** Event
   loop libraries are lower-level — they give you fd readiness notifications,
   not HTTP request parsing, SSE chunked responses, or WebSocket upgrades.
   Replacing MHD with libuv would mean reimplementing all of that on top of
   raw connections.

3. **The thread-per-connection model is appropriate for the workload.** AT
   Protocol PDS servers handle a moderate number of concurrent connections
   (the AGENTS.md notes 65 firehose subscribers as ordinary). The overhead of
   one pthread per connection (~8KB stack) is negligible compared to the
   correctness benefits of isolated request processing.

4. **The limitations are by design, not bugs.** No keep-alive (reverse proxy
   handles it), sequential teardown (only at shutdown), linear route lookup
   (table is small) — these are documented tradeoffs that match the actual
   deployment model.

5. **The locking discipline is already correct.** Migrating to an event loop
   would require rewriting the mutex model, SSE suspend/resume, WebSocket
   upgrade lifecycle, and relay forwarding — all of which are currently
   well-tested and bug-free.

### What WOULD Justify a Migration

- **10,000+ concurrent connections** — the thread model breaks down at scale
  where event loops excel. Not a realistic PDS workload.
- **Dropping Wii U support** — removing the embedded constraint opens up
  libuv with its mature platform backends.
- **Need for HTTP/2 or HTTP/3** — MHD doesn't support these. But AT Protocol
  doesn't require them.

### Suggested Improvements (Without Migration)

If performance tuning is needed, these are lower-risk than an event loop swap:

1. **Hash-based route lookup** — replace linear scan with a hash table under
   `routes_mutex`. Easy change, meaningful at >50 routes.
2. **Configurable listen backlog** — expose `backlog` as a server parameter
   instead of hardcoding 16.
3. **Connection keep-alive for the shim** — implement HTTP/1.1 keep-alive in
   `mhd_shim.c` to avoid per-request TCP handshake cost.
4. **Batch WS teardown** — join workers in parallel during shutdown instead of
   sequentially.
