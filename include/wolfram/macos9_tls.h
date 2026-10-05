/**
 * macos9_tls.h -- Wolfram's Classic Mac OS 9 TLS socket shim over macTLS.
 *
 * This header is the only part of the Mac OS 9 transport an application has
 * to reach for directly. Most Classic Mac OS 9 clients (Platinum among them)
 * should use the ordinary Wolfram XRPC/HTTP API in <wolfram/xrpc.h> and let
 * this shim stay out of sight; it is public because a client has to install a
 * yield callback so a request can be pumped without freezing its UI.
 *
 * Layering:
 *
 *     application event loop
 *            ^  wf_macos9_set_yield_callback()  (yield between pump slices)
 *            |
 *        Wolfram  (HTTP/XRPC framing lives here)
 *            |
 *      this shim (macOS 9 glue; no HTTP knowledge at all)
 *            |
 *          macTLS (Open Transport + BearSSL; TLS, certs, entropy)
 *            |
 *         Open Transport TCP
 *
 * The split is deliberate. macTLS owns the Open Transport endpoint, DNS, the
 * TLS handshake, certificate validation and entropy; Wolfram owns HTTP/1.1 and
 * XRPC framing. Nothing here parses HTTP, and nothing here re-implements TLS.
 *
 * Constraints this file must keep honouring:
 *
 *   - strict C89 / CodeWarrior-era C. No `//` comments, no declarations after
 *     statements, no `snprintf`, no `strdup`, no POSIX or C99 libc;
 *   - no pthreads, no blocking BSD sockets, no libcurl, no OpenSSL. Every wait
 *     is expressed as a macTLS pump slice plus a yield back to the caller;
 *   - macTLS is an external dependency supplied through WOLFRAM_MACTLS_ROOT.
 */

#ifndef WOLFRAM_MACOS9_TLS_H
#define WOLFRAM_MACOS9_TLS_H

#include <stddef.h>

#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque TLS connection handle. */
typedef struct wf_macos9_conn wf_macos9_conn;

/**
 * Yield hook.
 *
 * Called by the transport between macTLS pump slices so a synchronous Wolfram
 * call stays responsive to the host application's event loop. A Classic Mac OS
 * 9 application normally points this at something that calls WaitNextEvent()
 * and handles a tick, a mouse click, a key press or an idle callback.
 *
 * Contract:
 *
 *   - it must return promptly. Time spent inside it is time the transport is
 *     not making network progress;
 *   - it must be re-entrant in the sense that it may call back into
 *     application state, but it must NOT issue a nested Wolfram request on the
 *     same connection, or the two drivers will fight over one macTLS stream;
 *   - it may be invoked an unbounded number of times during a single request,
 *     so it should do real work rather than spin.
 */
typedef void (*wf_macos9_yield_fn)(void *userdata);

/**
 * Install (or, with NULL, remove) the yield hook used by every Wolfram Mac OS
 * 9 connection. The hook is process-wide rather than per-connection because a
 * Classic Mac OS 9 application has one event loop, and because the transport
 * holds exactly one connection at a time by design.
 */
void wf_macos9_set_yield_callback(wf_macos9_yield_fn fn, void *userdata);

/**
 * Per-connection knobs. All tick counts are in 1/60th of a second, matching
 * macTLS's own tick unit, so they are directly comparable with macTLS's
 * defaults. Zero means "use the default".
 */
typedef struct wf_macos9_options {
    /** TCP connect deadline handed to macTLS. 0 = macTLS default (30 s). */
    unsigned long connect_timeout_ticks;

    /** TLS handshake deadline handed to macTLS. 0 = macTLS default (30 s). */
    unsigned long handshake_timeout_ticks;

    /**
     * Deadline for the application's own transfers (send/recv), enforced by
     * this shim rather than macTLS. 0 = no deadline. Classic Mac OS 9's
     * TickCount is only 32-bit and wraps every ~2.3 years at 60 Hz, so a very
     * large value is not meaningful; anything up to a few minutes is fine.
     */
    unsigned long transfer_timeout_ticks;

    /**
     * TLS server name, used for SNI and for certificate hostname matching.
     * NULL (or "") means "use `host`". Only set this when the certificate name
     * deliberately differs from the name being dialled.
     */
    const char *server_name;
} wf_macos9_options;

/**
 * Open a verified TLS connection to `host` on `port`.
 *
 * Drives macTLS from OSTLS_New() through OSTLS_Start() to an open state,
 * pumping and yielding as it goes, and returns only once the handshake has
 * completed. Returns NULL on failure.
 *
 * A failed connect leaves no handle behind, so there is no error string to
 * inspect: on Classic Mac OS 9 "could not reach the server" is a single
 * user-visible condition. wf_macos9_last_error() describes failures that happen
 * on a connection that did open, which is where a caller can still act.
 *
 * `server_name` may be NULL to use `host` for SNI and certificate matching.
 * `options` may be NULL for macTLS defaults and no transfer deadline.
 */
wf_macos9_conn *wf_macos9_connect(const char *host, unsigned short port,
                                  const char *server_name,
                                  const wf_macos9_options *options);

/**
 * Write exactly `len` bytes.
 *
 * Handles macTLS's partial acceptance internally: it re-offers the remainder
 * on subsequent pump slices until every byte has been queued, so a caller can
 * treat a successful return as "the request is fully handed to macTLS".
 *
 * Returns `len` on success, or a negative wf_status (WF_ERR_INVALID_ARG,
 * WF_ERR_NETWORK or WF_ERR_TIMEOUT) on failure.
 */
long wf_macos9_send(wf_macos9_conn *conn, const void *buf, size_t len);

/**
 * Read up to `cap` bytes.
 *
 * Pumps until at least one byte is available. A return of 0 means a clean end
 * of stream: the peer closed and every byte macTLS had already decrypted has
 * been handed over. macTLS's "0 bytes available right now" is deliberately not
 * surfaced as EOF, because that is the normal state between pump slices.
 *
 * Returns the byte count (>= 0) or a negative wf_status on failure.
 */
long wf_macos9_recv(wf_macos9_conn *conn, void *buf, size_t cap);

/**
 * Close and release a connection: OSTLS_Close() (TLS close_notify, orderly OT
 * disconnect) then OSTLS_Dispose(). Safe to call with NULL, and safe to call
 * on a connection whose handshake already failed.
 */
void wf_macos9_close(wf_macos9_conn *conn);

/**
 * Human-readable description of the most recent failure on this connection, or
 * NULL if the connection has not failed. Points at storage owned by the
 * connection, so it stays valid until the next call on the same connection or
 * until the connection is closed. Never contains key material.
 */
const char *wf_macos9_last_error(const wf_macos9_conn *conn);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_MACOS9_TLS_H */