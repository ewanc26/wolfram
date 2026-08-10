#define _GNU_SOURCE

/*
 * xrpc_server_ws.c — RFC 6455 WebSocket subscription endpoints for the
 * XRPC server: handshake/upgrade handling, unmasked server-to-client
 * framing, and the per-connection upgrade worker thread, split out of
 * xrpc_server.c as a self-contained protocol implementation distinct from
 * its HTTP request/response dispatch.
 *
 * Requires libmicrohttpd (built only when WOLFRAM_BUILD_SERVER=ON).
 */

#include "xrpc_server_internal.h"

#include <cJSON.h>
#include <microhttpd.h>
#if defined(WOLFRAM_WIIU)
#include <mbedtls/sha1.h>
#endif

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <sys/socket.h>
#include <openssl/sha.h>

#include "wolfram/log.h"

/* ------------------------------------------------------------------ */
/* WebSocket (RFC 6455) subscription endpoints                          */
/* ------------------------------------------------------------------ */

/** An upgrade that has been queued but not yet handed over. */
typedef struct wf_ws_pending {
    struct MHD_Connection *conn;
    struct wf_ws_upgrade_ctx *uc;
    struct wf_ws_pending *next;
} wf_ws_pending;

/** Closure handed to libmicrohttpd's upgrade handler. */
typedef struct wf_ws_upgrade_ctx {
    wf_xrpc_server *server;
    wf_route *route;
    wf_xrpc_ws_stream *stream;
    wf_xrpc_request req; /* request copy for the user handler */
} wf_ws_upgrade_ctx;

/** RFC 4648 standard base64 with '=' padding (for Sec-WebSocket-Accept). */
static void wf_ws_base64_encode(const unsigned char *in, size_t len,
                                char *out) {
    static const char tab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i, o = 0;
    for (i = 0; i + 3 <= len; i += 3) {
        unsigned int n = ((unsigned int)in[i] << 16) |
                         ((unsigned int)in[i + 1] << 8) |
                         (unsigned int)in[i + 2];
        out[o++] = tab[(n >> 18) & 0x3f];
        out[o++] = tab[(n >> 12) & 0x3f];
        out[o++] = tab[(n >> 6) & 0x3f];
        out[o++] = tab[n & 0x3f];
    }
    if (i < len) {
        unsigned int n = ((unsigned int)in[i] << 16) |
                         ((i + 1 < len) ? ((unsigned int)in[i + 1] << 8) : 0);
        out[o++] = tab[(n >> 18) & 0x3f];
        out[o++] = tab[(n >> 12) & 0x3f];
        out[o++] = (i + 1 < len) ? tab[(n >> 6) & 0x3f] : '=';
        out[o++] = '=';
    }
    out[o] = '\0';
}

/** Write the full buffer to the socket; returns 0 on success, -1 on a hard
 *  failure (EOF, reset, poll error), or -2 when the peer's receive window
 *  stayed full for the whole 2s write timeout — a slow consumer, not a
 *  broken connection, and the two are distinguished so the caller can tell
 *  the client why it is being disconnected (see WF_ERR_TIMEOUT below). */
static int wf_ws_write_all(int sock, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(sock, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = {sock, POLLOUT, 0};
                int pr = poll(&pfd, 1, 2000);
                if (pr == 0) return -2;
                if (pr < 0) return -1;
                continue;
            }
            return -1;
        }
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

/** Read exactly `len` bytes with a per-read timeout; 0 ok, -1
 * error/EOF/timeout. */
static int wf_ws_read_exact(int sock, void *buf, size_t len, int timeout_ms) {
    char *p = (char *)buf;
    size_t off = 0;
    while (off < len) {
        struct pollfd pfd = {sock, POLLIN, 0};
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (pr == 0) return -1;
        ssize_t n = read(sock, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

/** Write a server→client frame (UNMASKED) of the given opcode.
 *  Caller MUST hold s->mutex (serialises all writes to the socket). */
static wf_status wf_ws_write_frame_locked(wf_xrpc_ws_stream *s, uint8_t opcode,
                                          const void *data, size_t len) {
    unsigned char hdr[10];
    size_t hlen = 0;
    hdr[0] = (unsigned char)(0x80 | (opcode & 0x0f));
    if (len < 126) {
        hdr[1] = (unsigned char)len;
        hlen = 2;
    } else if (len <= 0xFFFF) {
        hdr[1] = 126;
        hdr[2] = (unsigned char)((len >> 8) & 0xff);
        hdr[3] = (unsigned char)(len & 0xff);
        hlen = 4;
    } else {
        hdr[1] = 127;
        for (int i = 0; i < 8; i++) {
            hdr[2 + i] = (unsigned char)((len >> (8 * (7 - i))) & 0xff);
        }
        hlen = 10;
    }
    int hr = wf_ws_write_all(s->sock, hdr, hlen);
    int dr = (hr == 0 && len > 0) ? wf_ws_write_all(s->sock, data, len) : 0;
    if (hr == -2 || dr == -2) return WF_ERR_TIMEOUT;
    if (hr != 0 || dr != 0) return WF_ERR_NETWORK;
    return WF_OK;
}

/**
 * Half-close the socket and drain whatever the peer still has queued, so the
 * subsequent close() cannot turn into an RST.
 *
 * shutdown(SHUT_WR) puts a FIN after everything we have already written, so
 * the peer sees an ordinary end-of-stream once it has read our frames. We then
 * read (and discard) until the peer's own FIN arrives, because it is *unread
 * inbound* data that makes close() send RST — a client that pinged us, or sent
 * its own close frame, is enough. The wait is bounded: a peer that never
 * finishes costs us WS_LINGER_MS, not a stuck worker thread.
 */
#define WS_LINGER_MS 500

static void wf_ws_shutdown_gracefully(int sock) {
    unsigned char scratch[1024];
    int remaining = WS_LINGER_MS;

    if (sock < 0) {
        return;
    }
    shutdown(sock, SHUT_WR);

    while (remaining > 0) {
        struct pollfd pfd = {sock, POLLIN, 0};
        int step = remaining < 50 ? remaining : 50;
        int pr = poll(&pfd, 1, step);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (pr == 0) {
            remaining -= step;
            continue;
        }
        ssize_t n = read(sock, scratch, sizeof(scratch));
        if (n <= 0) {
            /* 0 = peer FIN (the good case); <0 = already reset or errored. */
            return;
        }
        /* Discarded deliberately: the stream is over, and the only purpose of
         * reading is to leave the receive queue empty. */
    }
}

/* Release an upgrade that never happened: the context, the request copy it
 * carries, and the half-built stream. Mirrors the cleanup the upgrade handler
 * performs when it refuses. */
void wf_ws_discard_upgrade(wf_ws_upgrade_ctx *uc) {
    if (!uc) return;
    wf_xrpc_ws_stream *stream = uc->stream;
    free((void *)uc->req.nsid);
    free((void *)uc->req.auth_header);
    free((void *)uc->req.dpop_header);
    cJSON_Delete(uc->req.params);
    free(uc->req.authed_subject);
    if (stream) {
        free(stream->nsid);
        pthread_cond_destroy(&stream->worker_cond);
        pthread_mutex_destroy(&stream->mutex);
        free(stream);
    }
    free(uc);
}

/* Record an upgrade as queued-but-not-handed-over. */
static void wf_ws_pending_add(wf_xrpc_server *server,
                              struct MHD_Connection *conn,
                              wf_ws_upgrade_ctx *uc) {
    wf_ws_pending *node = calloc(1, sizeof(*node));
    if (!node) return; /* Losing the record only costs us the safety net. */
    node->conn = conn;
    node->uc = uc;
    pthread_mutex_lock(&server->ws_mutex);
    node->next = server->ws_pending;
    server->ws_pending = node;
    pthread_mutex_unlock(&server->ws_mutex);
}

/*
 * Remove a pending record and return the context it held, or NULL when there
 * is no match. Matches on `uc` when given, otherwise on `conn`; with both NULL
 * it takes whatever is at the head, which is how teardown drains the list.
 * The caller decides what the removal means — the handler claims the upgrade,
 * connection close and teardown discard it.
 */
wf_ws_upgrade_ctx *wf_ws_pending_take(wf_xrpc_server *server,
                                      struct MHD_Connection *conn,
                                      wf_ws_upgrade_ctx *uc) {
    wf_ws_upgrade_ctx *found = NULL;
    pthread_mutex_lock(&server->ws_mutex);
    wf_ws_pending **pp = &server->ws_pending;
    while (*pp) {
        if ((uc && (*pp)->uc == uc) || (!uc && conn && (*pp)->conn == conn) ||
            (!uc && !conn)) {
            wf_ws_pending *node = *pp;
            *pp = node->next;
            found = node->uc;
            free(node);
            break;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&server->ws_mutex);
    return found;
}

/*
 * MHD connection lifecycle hook.
 *
 * The only reason this exists: MHD never tells us when an upgrade response is
 * destroyed without upgrading. A client that vanishes between the queued 101
 * and the handover leaves its context and stream allocated with nothing left
 * holding a pointer to them. Connection close is the one event that covers
 * that case, and by then a successful upgrade has already claimed its record.
 */
void wf_ws_notify_connection(void *cls, struct MHD_Connection *conn,
                             void **socket_context,
                             enum MHD_ConnectionNotificationCode code) {
    (void)socket_context;
    if (code != MHD_CONNECTION_NOTIFY_CLOSED) return;
    wf_xrpc_server *server = cls;
    if (!server) return;
    wf_ws_discard_upgrade(wf_ws_pending_take(server, conn, NULL));
}

/** Upgrade worker: run the user handler then drain client control frames. */
static void *wf_ws_serve_thread(void *arg) {
    wf_ws_upgrade_ctx *uc = (wf_ws_upgrade_ctx *)arg;
    wf_xrpc_ws_stream *s = uc->stream;
    wf_route *route = uc->route;
    wf_xrpc_request req = uc->req;
    free(uc);

    /* Wait until the spawning thread has stored our thread id.
     *
     * Everything below can finish and free `s` — a handler that pushes a few
     * frames and closes takes microseconds — so running ahead of that store
     * left wf_ws_upgrade_handler writing through freed memory, crashing the
     * whole process on the MHD polling thread. It is rare (about 1 run in 300
     * under parallel load) and fatal, so the ordering is enforced rather than
     * hoped for. */
    pthread_mutex_lock(&s->mutex);
    while (!s->thread_ready) {
        pthread_cond_wait(&s->worker_cond, &s->mutex);
    }
    pthread_mutex_unlock(&s->mutex);

    /* 1) Invoke the user handler (it pushes frames / may close). */
    route->handler.ws(route->ctx, &req, s);

    /* 2) Control-frame loop: read client frames, answer ping, honour close. */
    for (;;) {
        bool closed;
        pthread_mutex_lock(&s->mutex);
        closed = s->closed;
        pthread_mutex_unlock(&s->mutex);
        if (closed) {
            break;
        }

        struct pollfd pfd = {s->sock, POLLIN, 0};
        int pr = poll(&pfd, 1, 250);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) {
            continue; /* timed out: re-check closed flag */
        }

        unsigned char h[2];
        if (wf_ws_read_exact(s->sock, h, 2, 1000) != 0) break;
        uint8_t opcode = (uint8_t)(h[0] & 0x0f);
        bool masked = (h[1] & 0x80) != 0;
        uint64_t plen = (uint64_t)(h[1] & 0x7f);
        if (plen == 126) {
            unsigned char e[2];
            if (wf_ws_read_exact(s->sock, e, 2, 1000) != 0) break;
            plen = ((uint64_t)e[0] << 8) | (uint64_t)e[1];
        } else if (plen == 127) {
            unsigned char e[8];
            if (wf_ws_read_exact(s->sock, e, 8, 1000) != 0) break;
            plen = 0;
            for (int i = 0; i < 8; i++) plen = (plen << 8) | (uint64_t)e[i];
        }
        unsigned char mask[4] = {0, 0, 0, 0};
        if (masked) {
            if (wf_ws_read_exact(s->sock, mask, 4, 1000) != 0) break;
        }
        unsigned char *payload = NULL;
        if (plen > 0) {
            if (plen > 16 * 1024 * 1024) break; /* safety guard */
            payload = (unsigned char *)malloc((size_t)plen ? (size_t)plen : 1);
            if (!payload) break;
            if (wf_ws_read_exact(s->sock, payload, (size_t)plen, 2000) != 0) {
                free(payload);
                break;
            }
            if (masked) {
                for (uint64_t i = 0; i < plen; i++) payload[i] ^= mask[i & 3];
            }
        }

        if (opcode == 0x9) { /* ping → pong */
            pthread_mutex_lock(&s->mutex);
            wf_ws_write_frame_locked(s, 0xA, payload, (size_t)plen);
            pthread_mutex_unlock(&s->mutex);
        } else if (opcode == 0x8) { /* close */
            pthread_mutex_lock(&s->mutex);
            if (!s->closed) {
                s->closed = true;
                wf_ws_write_frame_locked(s, 0x8, payload, (size_t)plen);
            }
            pthread_mutex_unlock(&s->mutex);
            free(payload);
            break;
        }
        free(payload);
    }

    /* Prevent retained producer workers from racing the stream teardown. */
    pthread_mutex_lock(&s->mutex);
    s->closed = true;
    while (s->worker_refs > 0) {
        pthread_cond_wait(&s->worker_cond, &s->mutex);
    }
    pthread_mutex_unlock(&s->mutex);

    /* 3) Tear down the upgraded socket via libmicrohttpd — but only after the
     * peer has had a chance to collect what we already sent. Closing a socket
     * that still has unread data in its receive queue makes the kernel send
     * RST instead of FIN, and a received RST discards the peer's receive
     * buffer: frames we delivered perfectly well are destroyed before a slow
     * subscriber can read them. For a firehose the lost frames are the last
     * ones before the disconnect, which are exactly the ones a consumer needs
     * to resume from the right cursor. */
    /*
     * Leave the server's list before closing the socket, not after.
     *
     * MHD_upgrade_action(CLOSE) closes the fd, and the kernel is free to hand
     * that same number straight back to the next accepted connection. While
     * this stream was still listed, wf_xrpc_server_stop would read the stale
     * number and shutdown() somebody else's live connection.
     *
     * The same critical section decides who joins this thread: if stop() has
     * already claimed it (`reaped`) it holds our id and will join, so we must
     * not detach. Otherwise nothing will ever join us and we detach here —
     * without that, every finished WebSocket connection would leak a thread
     * descriptor and its stack for the life of the process.
     */
    bool reaped = false;
    if (s->server) {
        pthread_mutex_lock(&s->server->ws_mutex);
        wf_xrpc_ws_stream **pp = &s->server->ws_streams;
        while (*pp) {
            if (*pp == s) {
                *pp = s->next;
                break;
            }
            pp = &(*pp)->next;
        }
        reaped = s->reaped;
        pthread_mutex_unlock(&s->server->ws_mutex);
    }

    wf_ws_shutdown_gracefully(s->sock);
    MHD_upgrade_action(s->urh, MHD_UPGRADE_ACTION_CLOSE);

    if (!reaped) {
        pthread_detach(pthread_self());
    }

    free((void *)req.nsid);
    free((void *)req.auth_header);
    free((void *)req.dpop_header);
    cJSON_Delete(req.params);
    free(req.authed_subject);
    free(s->nsid);
    pthread_cond_destroy(&s->worker_cond);
    pthread_mutex_destroy(&s->mutex);
    free(s);
    return NULL;
}

/** libmicrohttpd upgrade callback: hand off the raw socket to a worker. */
static void wf_ws_upgrade_handler(void *cls, struct MHD_Connection *connection,
                                  void *req_cls, const char *extra_in,
                                  size_t extra_in_size, MHD_socket sock,
                                  struct MHD_UpgradeResponseHandle *urh) {
    (void)connection;
    (void)req_cls;
    (void)extra_in;
    (void)extra_in_size;
    wf_ws_upgrade_ctx *uc = (wf_ws_upgrade_ctx *)cls;
    wf_xrpc_server *server = uc->server;
    /* Hold the stream directly: the worker frees `uc` as its first act, so
     * `uc` must not be dereferenced once pthread_create has succeeded. */
    wf_xrpc_ws_stream *stream = uc->stream;
    /* The handover happened, so connection close must not also free this. */
    wf_ws_pending_take(server, NULL, uc);
    pthread_t tid;

    /*
     * MHD hands us MHD_INVALID_SOCKET when it could not complete the upgrade
     * itself — it cannot allocate the internal socketpair under load, or the
     * connection died first. Spawning a worker on that means poll()/read() on
     * fd -1 for the life of the stream: EBADF at best, and on a platform where
     * the cast lands on a live descriptor, reads and writes against somebody
     * else's connection. Refuse before anything is spawned or published.
     */
    if (sock == MHD_INVALID_SOCKET) {
        MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
        free((void *)uc->req.nsid);
        free((void *)uc->req.auth_header);
        free((void *)uc->req.dpop_header);
        cJSON_Delete(uc->req.params);
        free(uc->req.authed_subject);
        free(stream->nsid);
        pthread_cond_destroy(&stream->worker_cond);
        pthread_mutex_destroy(&stream->mutex);
        free(stream);
        free(uc);
        return;
    }

    stream->sock = (int)sock;
    stream->urh = urh;

    /*
     * Spawn the worker and publish the stream under one hold of ws_mutex.
     *
     * The list must never contain a stream whose `thread` has not been stored:
     * wf_xrpc_server_stop reads that field under this same lock to decide what
     * to join, and would otherwise join a zero id. The worker is parked on
     * thread_ready throughout, so touching the stream here is safe even though
     * the thread is already running.
     */
    pthread_mutex_lock(&server->ws_mutex);

    if (server->ws_stopping) {
        /* Teardown has started; this connection would never be joined. */
        pthread_mutex_unlock(&server->ws_mutex);
        MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
        free((void *)uc->req.nsid);
        free((void *)uc->req.auth_header);
        free((void *)uc->req.dpop_header);
        cJSON_Delete(uc->req.params);
        free(uc->req.authed_subject);
        free(stream->nsid);
        pthread_cond_destroy(&stream->worker_cond);
        pthread_mutex_destroy(&stream->mutex);
        free(stream);
        free(uc);
        return;
    }

    if (pthread_create(&tid, NULL, wf_ws_serve_thread, uc) != 0) {
        /* Could not spawn a worker: close the upgrade immediately. */
        pthread_mutex_unlock(&server->ws_mutex);
        MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
        free((void *)uc->req.nsid);
        free((void *)uc->req.auth_header);
        free((void *)uc->req.dpop_header);
        cJSON_Delete(uc->req.params);
        free(uc->req.authed_subject);
        free(stream->nsid);
        pthread_cond_destroy(&stream->worker_cond);
        pthread_mutex_destroy(&stream->mutex);
        free(stream);
        free(uc);
        return;
    }

    /* Record the id and publish the stream, both under ws_mutex, so stop()
     * never observes a listed stream with an unwritten thread. */
    stream->thread = tid;
    stream->next = server->ws_streams;
    server->ws_streams = stream;
    pthread_mutex_unlock(&server->ws_mutex);

    /* Release the worker. Releasing stream->mutex publishes the store above,
     * which the worker acquires when it observes thread_ready. */
    pthread_mutex_lock(&stream->mutex);
    stream->thread_ready = true;
    pthread_cond_broadcast(&stream->worker_cond);
    pthread_mutex_unlock(&stream->mutex);
}

/**
 * Perform the RFC 6455 handshake and queue the 101 upgrade response.
 * Returns MHD_YES if the upgrade was queued (caller returns MHD_YES), or
 * MHD_NO if the request was not a valid WebSocket upgrade (caller should
 * send a 400 error).
 */
enum MHD_Result wf_server_ws_handshake(wf_xrpc_server *server, wf_route *route,
                                       struct MHD_Connection *conn,
                                       const wf_xrpc_request *request) {
    const char *nsid = request ? request->nsid : NULL;
    const char *upgrade =
        MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Upgrade");
    const char *connection =
        MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Connection");
    const char *key =
        MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Sec-WebSocket-Key");
    const char *version = MHD_lookup_connection_value(conn, MHD_HEADER_KIND,
                                                      "Sec-WebSocket-Version");
    wf_xrpc_ws_stream *stream;
    wf_ws_upgrade_ctx *uc;
    struct MHD_Response *resp;
#if defined(WOLFRAM_WIIU)
    /* No OpenSSL on the console; mbedTLS supplies the SHA-1 that RFC 6455
     * requires for Sec-WebSocket-Accept. */
    unsigned char digest[20];
#else
    unsigned char digest[SHA_DIGEST_LENGTH];
#endif
    char accept[40];
    char concat[256];

    /* Validate the handshake request per RFC 6455 §4.2.1. */
    if (!upgrade || strcasecmp(upgrade, "websocket") != 0) return MHD_NO;
    if (!connection || strcasestr(connection, "upgrade") == NULL) return MHD_NO;
    if (!version || strcmp(version, "13") != 0) return MHD_NO;
    if (!key || key[0] == '\0') return MHD_NO;

    snprintf(concat, sizeof(concat), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
             key);
#if defined(WOLFRAM_WIIU)
    mbedtls_sha1((const unsigned char *)concat, strlen(concat), digest);
    wf_ws_base64_encode(digest, 20, accept);
#else
    SHA1((const unsigned char *)concat, strlen(concat), digest);
    wf_ws_base64_encode(digest, SHA_DIGEST_LENGTH, accept);
#endif

    stream = (wf_xrpc_ws_stream *)calloc(1, sizeof(*stream));
    if (!stream) return MHD_NO;
    stream->server = server;
    stream->nsid = nsid ? strdup(nsid) : NULL;
    if (nsid && !stream->nsid) {
        free(stream);
        return MHD_NO;
    }
    if (pthread_mutex_init(&stream->mutex, NULL) != 0) {
        free(stream->nsid);
        free(stream);
        return MHD_NO;
    }
    if (pthread_cond_init(&stream->worker_cond, NULL) != 0) {
        pthread_mutex_destroy(&stream->mutex);
        free(stream->nsid);
        free(stream);
        return MHD_NO;
    }

    uc = (wf_ws_upgrade_ctx *)calloc(1, sizeof(*uc));
    if (!uc) {
        pthread_cond_destroy(&stream->worker_cond);
        pthread_mutex_destroy(&stream->mutex);
        free(stream->nsid);
        free(stream);
        return MHD_NO;
    }
    uc->server = server;
    uc->route = route;
    uc->stream = stream;
    uc->req.nsid = nsid ? strdup(nsid) : NULL;
    uc->req.method = "GET";
    uc->req.auth_header =
        request && request->auth_header ? strdup(request->auth_header) : NULL;
    uc->req.dpop_header =
        request && request->dpop_header ? strdup(request->dpop_header) : NULL;
    uc->req.params = request && request->params
                         ? cJSON_Duplicate(request->params, true)
                         : NULL;
    uc->req.handler_ctx = route->ctx;
    uc->req.authed_subject = request && request->authed_subject
                                 ? strdup(request->authed_subject)
                                 : NULL;
    uc->req.authed_principal_kind =
        request ? request->authed_principal_kind : WF_XRPC_PRINCIPAL_NONE;
    if ((nsid && !uc->req.nsid) ||
        (request && request->auth_header && !uc->req.auth_header) ||
        (request && request->dpop_header && !uc->req.dpop_header) ||
        (request && request->params && !uc->req.params) ||
        (request && request->authed_subject && !uc->req.authed_subject)) {
        free((void *)uc->req.nsid);
        free((void *)uc->req.auth_header);
        free((void *)uc->req.dpop_header);
        cJSON_Delete(uc->req.params);
        free(uc->req.authed_subject);
        free(uc);
        pthread_cond_destroy(&stream->worker_cond);
        pthread_mutex_destroy(&stream->mutex);
        free(stream->nsid);
        free(stream);
        return MHD_NO;
    }

    resp = MHD_create_response_for_upgrade(wf_ws_upgrade_handler, uc);
    if (!resp) {
        free((void *)uc->req.nsid);
        free((void *)uc->req.auth_header);
        free((void *)uc->req.dpop_header);
        cJSON_Delete(uc->req.params);
        free(uc->req.authed_subject);
        free(uc);
        pthread_cond_destroy(&stream->worker_cond);
        pthread_mutex_destroy(&stream->mutex);
        free(stream->nsid);
        free(stream);
        return MHD_NO;
    }
    MHD_add_response_header(resp, "Upgrade", "websocket");
    MHD_add_response_header(resp, "Connection", "Upgrade");
    MHD_add_response_header(resp, "Sec-WebSocket-Accept", accept);
    /* Track it until the handover: see wf_ws_notify_connection. */
    wf_ws_pending_add(server, conn, uc);
    MHD_queue_response(conn, MHD_HTTP_SWITCHING_PROTOCOLS, resp);
    MHD_destroy_response(resp);
    return MHD_YES;
}

wf_status wf_xrpc_server_ws_send(wf_xrpc_ws_stream *stream, const void *data,
                                 size_t len) {
    wf_xrpc_ws_stream *s = stream;
    if (!s) {
        return WF_ERR_INVALID_ARG;
    }
    if (!data && len > 0) {
        return WF_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s->mutex);
    wf_status rc =
        s->closed ? WF_ERR_INVALID_ARG
                  : wf_ws_write_frame_locked(s, 0x2, data ? data : "", len);
    pthread_mutex_unlock(&s->mutex);
    return rc;
}

wf_status wf_xrpc_server_ws_ping(wf_xrpc_ws_stream *stream) {
    wf_xrpc_ws_stream *s = stream;
    if (!s) return WF_ERR_INVALID_ARG;
    pthread_mutex_lock(&s->mutex);
    /* Opcode 0x9, empty payload. A conforming peer replies with a pong, but
     * the value here is the outbound byte itself: it resets the idle timer on
     * every proxy between us and the subscriber. */
    wf_status rc = s->closed ? WF_ERR_INVALID_ARG
                             : wf_ws_write_frame_locked(s, 0x9, "", 0);
    pthread_mutex_unlock(&s->mutex);
    return rc;
}

wf_status wf_xrpc_server_ws_retain(wf_xrpc_ws_stream *stream) {
    if (!stream) return WF_ERR_INVALID_ARG;
    pthread_mutex_lock(&stream->mutex);
    if (stream->closed) {
        pthread_mutex_unlock(&stream->mutex);
        return WF_ERR_INVALID_ARG;
    }
    stream->worker_refs++;
    pthread_mutex_unlock(&stream->mutex);
    return WF_OK;
}

void wf_xrpc_server_ws_release(wf_xrpc_ws_stream *stream) {
    if (!stream) return;
    pthread_mutex_lock(&stream->mutex);
    if (stream->worker_refs > 0) stream->worker_refs--;
    if (stream->worker_refs == 0) pthread_cond_broadcast(&stream->worker_cond);
    pthread_mutex_unlock(&stream->mutex);
}

int wf_xrpc_server_ws_is_closed(wf_xrpc_ws_stream *stream) {
    int closed;
    if (!stream) return 1;
    pthread_mutex_lock(&stream->mutex);
    closed = stream->closed ? 1 : 0;
    pthread_mutex_unlock(&stream->mutex);
    return closed;
}

wf_status wf_xrpc_server_ws_close(wf_xrpc_ws_stream *stream, uint16_t code) {
    wf_xrpc_ws_stream *s = stream;
    unsigned char body[2];
    if (!s) return WF_ERR_INVALID_ARG;
    body[0] = (unsigned char)((code >> 8) & 0xff);
    body[1] = (unsigned char)(code & 0xff);
    pthread_mutex_lock(&s->mutex);
    if (s->closed) {
        pthread_mutex_unlock(&s->mutex);
        return WF_ERR_INVALID_ARG;
    }
    s->closed = true;
    wf_ws_write_frame_locked(s, 0x8, body, 2);
    pthread_mutex_unlock(&s->mutex);
    return WF_OK;
}

wf_status wf_xrpc_server_register_ws(wf_xrpc_server *server, const char *nsid,
                                     wf_xrpc_ws_handler handler, void *ctx) {
    wf_route *r;
    if (!server || !nsid || !handler) {
        return WF_ERR_INVALID_ARG;
    }
    r = (wf_route *)calloc(1, sizeof(*r));
    if (!r) {
        return WF_ERR_ALLOC;
    }
    r->nsid = strdup(nsid);
    if (!r->nsid) {
        free(r);
        return WF_ERR_ALLOC;
    }
    r->kind = WF_ROUTE_QUERY;
    r->handler.ws = handler;
    r->ctx = ctx;
    r->is_ws = true;
    pthread_mutex_lock(&server->routes_mutex);
    r->next = server->routes;
    server->routes = r;
    pthread_mutex_unlock(&server->routes_mutex);
    return WF_OK;
}

/* ------------------------------------------------------------------ */
