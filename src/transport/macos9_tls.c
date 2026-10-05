/*
 * macos9_tls.c -- Classic Mac OS 9 TLS socket shim over macTLS.
 *
 * Wraps macTLS's asynchronous Open Transport + BearSSL stream in a small
 * synchronous-looking socket API so xrpc_mac9.c can do HTTP framing without
 * knowing that TLS on this platform is a cooperative state machine rather than
 * a function call.
 *
 * Everything macTLS documents about OSTLS_Pump is respected here:
 *
 *   - Pump never blocks, never sleeps and never waits for an event, so the
 *     loop below is the *only* thing making progress and it must yield;
 *   - OSTLS_Write may accept fewer bytes than offered, so partial writes are
 *     re-offered until the whole buffer is queued;
 *   - OSTLS_Read returning 0 bytes is "nothing available right now", NOT end
 *     of stream. End of stream is state kOSTLSStateClosed with the plaintext
 *     buffer drained;
 *   - OSTLS_Read stays legal in Closing and Closed, because plaintext can
 *     still be buffered there, so a Closed connection must be drained before
 *     it is reported as finished;
 *   - Read/Write in any other state return kOSTLSAsync_WrongState, which means
 *     "pump and try again", not "failed";
 *   - a failed connection is terminal, and Close() on it is a no-op.
 *
 * C89 discipline: no `//` comments, no declarations after statements, no
 * variadic functions, no snprintf, no strdup, no POSIX. See the header for the
 * architectural reasoning.
 */

#include "wolfram/macos9_tls.h"

#include "ostls_async.h"

#include <stdlib.h>
#include <string.h>

/* The tick source. macTLS measures its own timeouts in 1/60 s ticks, so the
 * transfer deadline enforced here uses the same unit and the same clock.
 *
 * Under CodeWarrior the real Mac OS 9 TickCount() is available and that is the
 * production path. The non-__MWERKS__ branch exists so CI (and a developer
 * workstation) can compile and link-check this translation unit: ostls_async.h
 * already provides matching stand-ins for OSErr/UInt16/UInt32 in that case,
 * and a host clock is an adequate stand-in for a monotonic tick when the file
 * is only being type-checked. It is never part of a Mac OS 9 build. */
#if defined(__MWERKS__)
#include <TickCount.h>
static unsigned long macos9_ticks(void) {
    return (unsigned long)TickCount();
}
#else
#include <time.h>
static unsigned long macos9_ticks(void) {
    return (unsigned long)clock();
}
#endif

/* Pump slice size. macTLS caps the work done per OSTLS_Pump() call, so the
 * shim loops rather than expecting one call to finish a transfer. Eight actions
 * is macTLS's own recommended granularity for a host event loop. */
#define WF_MACOS9_PUMP_STEPS 8u

struct wf_macos9_conn {
    OSTLSConnection *tls;
    unsigned long transfer_timeout_ticks;
    char *failure;
};

/* The yield hook is process-wide: a Classic Mac OS 9 application has one event
 * loop, and this transport drives exactly one connection at a time. */
static wf_macos9_yield_fn g_yield_fn;
static void *g_yield_userdata;

void wf_macos9_set_yield_callback(wf_macos9_yield_fn fn, void *userdata) {
    g_yield_fn = fn;
    g_yield_userdata = userdata;
}

static void macos9_yield(void) {
    if (g_yield_fn != NULL) g_yield_fn(g_yield_userdata);
}

static void macos9_fail(wf_macos9_conn *conn, const char *reason) {
    if (conn == NULL || reason == NULL) return;
    if (conn->failure != NULL) return;
    /* Bounded copy into storage owned by the connection: the reason strings
     * here are compile-time constants, so strlen is known-small and this never
     * touches the allocator. */
    conn->failure = (char *)malloc(strlen(reason) + 1);
    if (conn->failure != NULL) strcpy(conn->failure, reason);
}

const char *wf_macos9_last_error(const wf_macos9_conn *conn) {
    return conn != NULL ? conn->failure : NULL;
}

/* Record why macTLS gave up, using its own OSErr when one is available so a
 * future reader is not left with only "handshake failed". */
static void macos9_record_mactls_error(wf_macos9_conn *conn) {
    OSErr code;

    if (conn == NULL || conn->tls == NULL) return;
    code = OSTLS_GetLastError(conn->tls);
    if (code == kOSTLSAsync_ConnectTimeout)
        macos9_fail(conn, "connect timed out");
    else if (code == kOSTLSAsync_HandshakeTimeout)
        macos9_fail(conn, "TLS handshake timed out");
    else if (code == kOSTLSAsync_PeerClosed)
        macos9_fail(conn, "peer closed during TLS handshake");
    else if (code == kOSTLSAsync_BearSSLError)
        macos9_fail(conn, "TLS handshake rejected (certificate or protocol)");
    else if (code == kOSTLSAsync_EntropyFail)
        macos9_fail(conn, "no entropy available for the TLS handshake");
    else if (code != kOSTLSAsync_OK)
        macos9_fail(conn, "Open Transport connection failed");
}

/* Pump one slice. Returns kOSTLSAsync_OK when the caller may keep going, or a
 * negative wf_status when it must give up. */
static long macos9_pump(wf_macos9_conn *conn) {
    OSErr err;
    OSTLSEvent event;

    event = kOSTLSEventNone;
    err = OSTLS_Pump(conn->tls, WF_MACOS9_PUMP_STEPS, &event);
    if (err != kOSTLSAsync_OK) {
        macos9_fail(conn, "TLS transport error while pumping");
        return -WF_ERR_NETWORK;
    }
    if (OSTLS_GetState(conn->tls) == kOSTLSStateFailed) {
        macos9_record_mactls_error(conn);
        return -WF_ERR_NETWORK;
    }
    return WF_OK;
}

/* Has `deadline_ticks` (an absolute tick) passed? Expressed as a subtraction
 * so it stays correct across the 32-bit TickCount wrap. */
static int macos9_deadline_passed(unsigned long now, unsigned long deadline) {
    return (long)(now - deadline) >= 0;
}

wf_macos9_conn *wf_macos9_connect(const char *host, unsigned short port,
                                  const char *server_name,
                                  const wf_macos9_options *options) {
    OSTLSConfig config;
    OSTLSConnection *tls;
    wf_macos9_conn *conn;
    OSErr err;
    unsigned long deadline;

    if (host == NULL || host[0] == '\0') return NULL;

    /* An empty server_name is the same as no server_name: fall back to the host
     * actually being dialled, which is also the right SNI value. */
    if (server_name == NULL || server_name[0] == '\0') server_name = host;

    memset(&config, 0, sizeof(config));
    config.host = host;
    config.port = (UInt16)port;
    config.server_name = server_name;
    if (options != NULL) {
        config.connect_timeout_ticks = (UInt32)options->connect_timeout_ticks;
        config.handshake_timeout_ticks =
            (UInt32)options->handshake_timeout_ticks;
    }

    /* macTLS copies host/server_name at OSTLS_New time. */
    tls = NULL;
    err = OSTLS_New(&tls, &config);
    if (err != kOSTLSAsync_OK || tls == NULL) return NULL;

    err = OSTLS_Start(tls);
    if (err != kOSTLSAsync_OK) {
        OSTLS_Dispose(tls);
        return NULL;
    }

    conn = (wf_macos9_conn *)calloc(1, sizeof(*conn));
    if (conn == NULL) {
        OSTLS_Close(tls);
        OSTLS_Dispose(tls);
        return NULL;
    }
    conn->tls = tls;
    if (options != NULL)
        conn->transfer_timeout_ticks = options->transfer_timeout_ticks;

    /* Drive the connection until it is open, failed, or closed. macTLS owns the
     * connect and handshake deadlines (30 s each by default), so an unbounded
     * loop here cannot hang on a dead peer; the loop only has to notice the
     * terminal states promptly. */
    deadline = conn->transfer_timeout_ticks != 0
                   ? macos9_ticks() + conn->transfer_timeout_ticks
                   : 0;

    for (;;) {
        OSTLSState state;
        long pumped;

        pumped = macos9_pump(conn);
        if (pumped != WF_OK) {
            wf_macos9_close(conn);
            return NULL;
        }

        state = OSTLS_GetState(tls);
        if (state == kOSTLSStateOpen) return conn;
        if (state == kOSTLSStateClosed) {
            macos9_fail(conn, "peer closed before the TLS handshake finished");
            wf_macos9_close(conn);
            return NULL;
        }
        if (deadline != 0 && macos9_deadline_passed(macos9_ticks(), deadline)) {
            macos9_fail(conn, "connect timed out");
            wf_macos9_close(conn);
            return NULL;
        }

        macos9_yield();
    }
}

long wf_macos9_send(wf_macos9_conn *conn, const void *buf, size_t len) {
    const unsigned char *cursor;
    size_t sent;
    unsigned long deadline;

    if (conn == NULL || conn->tls == NULL || (buf == NULL && len != 0))
        return -(long)WF_ERR_INVALID_ARG;
    if (len == 0) return 0;

    cursor = (const unsigned char *)buf;
    sent = 0;
    deadline = conn->transfer_timeout_ticks != 0
                   ? macos9_ticks() + conn->transfer_timeout_ticks
                   : 0;

    while (sent < len) {
        UInt32 accepted;
        OSErr err;
        OSTLSState state;
        long pumped;

        /* macTLS only accepts bytes in the Open state and may take fewer than
         * offered. WrongState means "not ready yet", which is a pump-and-retry,
         * not a failure -- so it must not be folded into the error test. */
        accepted = 0;
        err = OSTLS_Write(conn->tls, cursor + sent, (UInt32)(len - sent),
                          &accepted);
        if (err != kOSTLSAsync_OK && err != kOSTLSAsync_WrongState) {
            macos9_record_mactls_error(conn);
            return -(long)WF_ERR_NETWORK;
        }
        sent += (size_t)accepted;
        if (sent == len) return (long)sent;

        state = OSTLS_GetState(conn->tls);
        if (state == kOSTLSStateClosed) {
            macos9_fail(conn, "peer closed while the request was being sent");
            return -(long)WF_ERR_NETWORK;
        }

        pumped = macos9_pump(conn);
        if (pumped != WF_OK) return pumped;

        if (deadline != 0 && macos9_deadline_passed(macos9_ticks(), deadline)) {
            macos9_fail(conn, "sending the request timed out");
            return -(long)WF_ERR_TIMEOUT;
        }

        macos9_yield();
    }

    return (long)sent;
}

long wf_macos9_recv(wf_macos9_conn *conn, void *buf, size_t cap) {
    unsigned long deadline;

    if (conn == NULL || conn->tls == NULL || buf == NULL || cap == 0)
        return -(long)WF_ERR_INVALID_ARG;

    deadline = conn->transfer_timeout_ticks != 0
                   ? macos9_ticks() + conn->transfer_timeout_ticks
                   : 0;

    for (;;) {
        UInt32 got;
        OSErr err;
        OSTLSState state;
        long pumped;

        /* OSTLS_Read is legal in Open, Closing and Closed: the last two can
         * still hold decrypted plaintext. Anything else is a transient
         * WrongState, so pump and try again. */
        got = 0;
        err = OSTLS_Read(conn->tls, buf, (UInt32)cap, &got);
        if (err != kOSTLSAsync_OK && err != kOSTLSAsync_WrongState) {
            macos9_record_mactls_error(conn);
            return -(long)WF_ERR_NETWORK;
        }
        if (got != 0) return (long)got;

        /* No plaintext available. Only a Closed connection with an empty
         * plaintext buffer is a genuine end of stream; Closed with buffered
         * bytes falls through to another Read above. */
        state = OSTLS_GetState(conn->tls);
        if (state == kOSTLSStateClosed) return 0;
        if (state == kOSTLSStateFailed) {
            macos9_record_mactls_error(conn);
            return -(long)WF_ERR_NETWORK;
        }

        pumped = macos9_pump(conn);
        if (pumped != WF_OK) return pumped;

        if (deadline != 0 && macos9_deadline_passed(macos9_ticks(), deadline)) {
            macos9_fail(conn, "waiting for the server timed out");
            return -(long)WF_ERR_TIMEOUT;
        }

        macos9_yield();
    }
}

void wf_macos9_close(wf_macos9_conn *conn) {
    if (conn == NULL) return;
    if (conn->tls != NULL) {
        /* Close is a documented no-op on an already Closed/Failed connection,
         * and Dispose tears down a live endpoint when Close never ran, so this
         * pair is safe on every exit path including handshake failure. */
        OSTLS_Close(conn->tls);
        OSTLS_Dispose(conn->tls);
        conn->tls = NULL;
    }
    free(conn->failure);
    free(conn);
}