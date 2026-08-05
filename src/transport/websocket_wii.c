/**
 * websocket_wii.c — Real WebSocket (RFC 6455) client transport for the
 * Nintendo Wii.
 *
 * Built on wii_tls (mbedTLS handshake + verified certificate chain over
 * lwIP), so this file only speaks the WebSocket layer on top: the HTTP/1.1
 * Upgrade handshake (RFC 6455 §4.1/§4.2.2), masked client framing (§5.2, the
 * mask key is drawn from the same DRBG that seeds TLS and P-256 — never
 * srand()/timing), fragmented-message reassembly, and transparent ping/pong.
 *
 * wss:// only. The Wii has no non-TLS transport anywhere in this SDK, and
 * every AT Protocol subscription endpoint (firehose, Jetstream, label
 * subscription) is wss anyway, so a plaintext ws:// path would be an
 * honest-stub surface nobody could exercise — WF_ERR_INVALID_ARG documents
 * that up front instead of silently degrading security.
 *
 * Client → server frames MUST be masked per the RFC; server → client frames
 * MUST NOT be. A masked frame received here is a protocol violation and
 * fails the read rather than being decoded leniently.
 */

#include "wolfram/websocket.h"
#include "wii_tls.h"

#include <mbedtls/sha1.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define WF_WEBSOCKET_MAX_MESSAGE (16u * 1024u * 1024u)

struct wf_websocket {
    wii_tls_conn *conn;

    /* Raw bytes read off the socket but not yet consumed by the frame
     * parser. Needed because the 101 response and the first WebSocket
     * frame(s) can arrive in the same TCP segment/TLS record — anything
     * past the header block belongs to the frame stream, not the HTTP
     * response, and must not be dropped. */
    unsigned char *raw_buf;
    size_t raw_len;   /* total bytes held */
    size_t raw_pos;   /* index of the first unconsumed byte */
    size_t raw_cap;

    /* Reassembly buffer for a fragmented message (opcode 0x0 continuations
     * following an initial text/binary frame without FIN). */
    unsigned char *pending;
    size_t pending_len;
    size_t pending_cap;
    wf_websocket_message_type pending_type;
    int have_pending_type;

    int closed;  /* a close frame was sent or received; no further I/O */
};

/* ── RFC 4648 standard base64 (Sec-WebSocket-Key/-Accept; not base64url) ── */

static void wii_ws_base64_encode(const unsigned char *in, size_t len, char *out) {
    static const char tab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i, o = 0;
    for (i = 0; i + 3 <= len; i += 3) {
        unsigned int n = ((unsigned int)in[i] << 16) |
                         ((unsigned int)in[i + 1] << 8) | (unsigned int)in[i + 2];
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

/* ── URL parsing (ws(s)://host[:port]/path) ─────────────────────────── */

static wf_status wii_ws_parse_url(const char *url, char *host, size_t host_cap,
                                  uint16_t *port, char *path, size_t path_cap) {
    if (strncmp(url, "wss://", 6) != 0) return WF_ERR_INVALID_ARG;
    const char *p = url + 6;
    *port = 443;

    const char *host_start = p;
    const char *host_end = p;
    while (*host_end && *host_end != ':' && *host_end != '/') host_end++;
    size_t hlen = (size_t)(host_end - host_start);
    if (hlen == 0 || hlen >= host_cap) return WF_ERR_INVALID_ARG;
    memcpy(host, host_start, hlen);
    host[hlen] = '\0';

    p = host_end;
    if (*p == ':') {
        p++;
        long parsed = 0;
        while (isdigit((unsigned char)*p)) parsed = parsed * 10 + (*p++ - '0');
        if (parsed > 0 && parsed <= 65535) *port = (uint16_t)parsed;
    }

    const char *path_start = (*p == '/') ? p : "/";
    size_t plen = strlen(path_start);
    if (plen + 1 > path_cap) return WF_ERR_INVALID_ARG;
    memcpy(path, path_start, plen + 1);
    return WF_OK;
}

/* ── Raw byte buffering over the TLS connection ─────────────────────── */

/* Ensure at least `need` unconsumed bytes are buffered, reading from the
 * socket as necessary. Blocks (bounded by the connection's own I/O
 * timeouts) only when genuinely more data is required to complete a frame
 * already known to be in flight — never called speculatively without a
 * prior wii_tls_pending() check at message boundaries. */
static wf_status wii_ws_fill(wf_websocket *ws, size_t need) {
    for (;;) {
        size_t have = ws->raw_len - ws->raw_pos;
        if (have >= need) return WF_OK;

        if (ws->raw_pos > 0) {
            memmove(ws->raw_buf, ws->raw_buf + ws->raw_pos, have);
            ws->raw_len = have;
            ws->raw_pos = 0;
        }

        size_t want = need - ws->raw_len;
        size_t chunk = want > 4096 ? want : 4096;
        if (ws->raw_len + chunk > ws->raw_cap) {
            size_t cap = ws->raw_cap ? ws->raw_cap : 4096;
            while (cap < ws->raw_len + chunk) cap *= 2;
            unsigned char *grown = realloc(ws->raw_buf, cap);
            if (!grown) return WF_ERR_ALLOC;
            ws->raw_buf = grown;
            ws->raw_cap = cap;
        }

        long n = wii_tls_recv(ws->conn, ws->raw_buf + ws->raw_len, chunk);
        if (n < 0) return WF_ERR_NETWORK;
        if (n == 0) return WF_ERR_NETWORK; /* peer closed mid-frame */
        ws->raw_len += (size_t)n;
    }
}

static void wii_ws_consume(wf_websocket *ws, size_t n, unsigned char *dst) {
    if (dst && n) memcpy(dst, ws->raw_buf + ws->raw_pos, n);
    ws->raw_pos += n;
}

/* ── Message reassembly buffer ──────────────────────────────────────── */

static wf_status wii_ws_append(wf_websocket *ws, const unsigned char *data,
                               size_t len) {
    if (len > WF_WEBSOCKET_MAX_MESSAGE - ws->pending_len) return WF_ERR_PARSE;
    size_t needed = ws->pending_len + len + 1;
    if (needed > ws->pending_cap) {
        size_t cap = ws->pending_cap ? ws->pending_cap : 4096;
        while (cap < needed) cap *= 2;
        unsigned char *grown = realloc(ws->pending, cap);
        if (!grown) return WF_ERR_ALLOC;
        ws->pending = grown;
        ws->pending_cap = cap;
    }
    if (len) memcpy(ws->pending + ws->pending_len, data, len);
    ws->pending_len += len;
    ws->pending[ws->pending_len] = '\0';
    return WF_OK;
}

static void wii_ws_discard_pending(wf_websocket *ws) {
    free(ws->pending);
    ws->pending = NULL;
    ws->pending_len = 0;
    ws->pending_cap = 0;
    ws->pending_type = 0;
    ws->have_pending_type = 0;
}

/* ── Frame send (client → server frames are always masked, RFC 6455 §5.3) ── */

static wf_status wii_ws_send_frame(wf_websocket *ws, unsigned char opcode,
                                   const unsigned char *payload, size_t len) {
    if (!ws || ws->closed) return WF_ERR_NETWORK;
    if (len > WF_WEBSOCKET_MAX_MESSAGE) return WF_ERR_INVALID_ARG;

    unsigned char header[14];
    size_t hlen = 0;
    header[hlen++] = (unsigned char)(0x80 | (opcode & 0x0F)); /* FIN, no fragmentation on send */
    if (len <= 125) {
        header[hlen++] = (unsigned char)(0x80 | len);
    } else if (len <= 0xFFFF) {
        header[hlen++] = 0x80 | 126;
        header[hlen++] = (unsigned char)((len >> 8) & 0xFF);
        header[hlen++] = (unsigned char)(len & 0xFF);
    } else {
        header[hlen++] = 0x80 | 127;
        for (int i = 7; i >= 0; i--)
            header[hlen++] = (unsigned char)((len >> (8 * i)) & 0xFF);
    }

    unsigned char mask[4];
    if (wii_tls_random(NULL, mask, sizeof(mask)) != 0) return WF_ERR_CRYPTO;
    memcpy(header + hlen, mask, sizeof(mask));
    hlen += sizeof(mask);

    unsigned char *masked = NULL;
    if (len > 0) {
        masked = malloc(len);
        if (!masked) return WF_ERR_ALLOC;
        for (size_t i = 0; i < len; i++) masked[i] = payload[i] ^ mask[i % 4];
    }

    wf_status status = WF_OK;
    long sent = wii_tls_send(ws->conn, header, hlen);
    if (sent < 0 || (size_t)sent != hlen) status = WF_ERR_NETWORK;
    if (status == WF_OK && len > 0) {
        sent = wii_tls_send(ws->conn, masked, len);
        if (sent < 0 || (size_t)sent != len) status = WF_ERR_NETWORK;
    }
    free(masked);
    return status;
}

/* ── Public API ──────────────────────────────────────────────────────── */

int wf_websocket_supported(void) {
    return 1; /* wss:// is genuinely implemented on this backend */
}

wf_status wf_websocket_connect(const char *url, wf_websocket **out) {
    if (!url || !out) return WF_ERR_INVALID_ARG;
    *out = NULL;

    char host[256];
    uint16_t port = 443;
    char path[2048];
    if (wii_ws_parse_url(url, host, sizeof(host), &port, path, sizeof(path)) != WF_OK)
        return WF_ERR_INVALID_ARG;

    wii_tls_conn *conn = wii_tls_connect(host, port);
    if (!conn) return WF_ERR_NETWORK;

    /* Sec-WebSocket-Key: 16 bytes from the same DRBG used for TLS/P-256, per
     * RFC 6455 §4.1 — timing- or address-derived "randomness" is exactly
     * what this project's entropy policy forbids elsewhere, and there is no
     * reason to relax it for a value an on-path observer could otherwise
     * predict. */
    unsigned char key_raw[16];
    if (wii_tls_random(NULL, key_raw, sizeof(key_raw)) != 0) {
        wii_tls_close(conn);
        return WF_ERR_CRYPTO;
    }
    char key_b64[32];
    wii_ws_base64_encode(key_raw, sizeof(key_raw), key_b64);

    char req[2560];
    int n = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "User-Agent: wolfram/%s\r\n"
        "\r\n",
        path, host, key_b64, WOLFRAM_VERSION_STRING);
    if (n < 0 || (size_t)n >= sizeof(req)) {
        wii_tls_close(conn);
        return WF_ERR_ALLOC;
    }
    long sent = wii_tls_send(conn, req, (size_t)n);
    if (sent < 0 || (size_t)sent != (size_t)n) {
        wii_tls_close(conn);
        return WF_ERR_NETWORK;
    }

    wf_websocket *ws = calloc(1, sizeof(*ws));
    if (!ws) {
        wii_tls_close(conn);
        return WF_ERR_ALLOC;
    }
    ws->conn = conn;

    /* Read until the full header block ("\r\n\r\n"). Anything after that
     * point in the same buffer is the start of the frame stream and is left
     * in raw_buf for the first wf_websocket_receive() to parse. */
    size_t header_end = 0;
    int header_done = 0;
    for (;;) {
        for (size_t i = ws->raw_pos; i + 3 < ws->raw_len; i++) {
            if (ws->raw_buf[i] == '\r' && ws->raw_buf[i + 1] == '\n' &&
                ws->raw_buf[i + 2] == '\r' && ws->raw_buf[i + 3] == '\n') {
                header_end = i + 4;
                header_done = 1;
                break;
            }
        }
        if (header_done) break;
        if (ws->raw_len > 8192) { /* handshake headers this large are not legitimate */
            wf_websocket_free(ws);
            return WF_ERR_PARSE;
        }
        if (wii_ws_fill(ws, ws->raw_len - ws->raw_pos + 1) != WF_OK) {
            wf_websocket_free(ws);
            return WF_ERR_NETWORK;
        }
    }

    /* NUL-terminate the header window in place for strstr/strncasecmp
     * lookups (the trailing "\r\n" before header_end is left intact so
     * per-line scanning below still finds the last real header). */
    char *headers = (char *)ws->raw_buf;
    char saved = headers[header_end - 2];
    headers[header_end - 2] = '\0';

    int status_ok = (ws->raw_len >= 12 && strncmp(headers, "HTTP/", 5) == 0);
    if (status_ok) {
        const char *code = headers + 5;
        while (*code && *code != ' ') code++;
        status_ok = (*code == ' ' && strtol(code + 1, NULL, 10) == 101);
    }

    char accept_hdr[256] = {0};
    int have_accept = 0;
    {
        const char *p = headers;
        const char *name = "Sec-WebSocket-Accept:";
        size_t nlen = strlen(name);
        while (*p) {
            if (strncasecmp(p, name, nlen) == 0) {
                const char *v = p + nlen;
                while (*v == ' ' || *v == '\t') v++;
                size_t i = 0;
                while (v[i] && v[i] != '\r' && v[i] != '\n' &&
                       i + 1 < sizeof(accept_hdr))
                    accept_hdr[i] = v[i], i++;
                accept_hdr[i] = '\0';
                have_accept = 1;
                break;
            }
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
        }
    }
    headers[header_end - 2] = saved;

    if (!status_ok || !have_accept) {
        wf_websocket_free(ws);
        return WF_ERR_NETWORK;
    }

    /* Verify Sec-WebSocket-Accept per RFC 6455 §4.2.2: base64(SHA-1(key +
     * GUID)) must match exactly, or the peer isn't a conformant WebSocket
     * endpoint (or something on the path tampered with the handshake). This
     * is the client-side half of the same check the SDK's own XRPC server
     * performs when acting as the WS endpoint. */
    char concat[128];
    snprintf(concat, sizeof(concat),
             "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key_b64);
    unsigned char digest[20];
    mbedtls_sha1((const unsigned char *)concat, strlen(concat), digest);
    char expect_accept[32];
    wii_ws_base64_encode(digest, sizeof(digest), expect_accept);
    if (strcmp(accept_hdr, expect_accept) != 0) {
        wf_websocket_free(ws);
        return WF_ERR_NETWORK;
    }

    /* Consume the header block; whatever remains is frame data already in
     * hand for the first receive(). */
    ws->raw_pos = header_end;

    *out = ws;
    return WF_OK;
}

wf_status wf_websocket_receive(wf_websocket *socket, wf_websocket_message *out) {
    if (!socket || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (socket->closed) return WF_ERR_NETWORK;

    for (;;) {
        if (socket->raw_len == socket->raw_pos) {
            int ready = wii_tls_pending(socket->conn);
            if (ready < 0) {
                socket->closed = 1;
                wii_ws_discard_pending(socket);
                return WF_ERR_NETWORK;
            }
            if (ready == 0) return WF_ERR_WOULD_BLOCK;
        }

        if (wii_ws_fill(socket, socket->raw_len - socket->raw_pos + 2) != WF_OK) {
            socket->closed = 1;
            wii_ws_discard_pending(socket);
            return WF_ERR_NETWORK;
        }
        unsigned char hdr[2];
        wii_ws_consume(socket, 2, hdr);

        int fin = (hdr[0] & 0x80) != 0;
        int opcode = hdr[0] & 0x0F;
        int masked = (hdr[1] & 0x80) != 0;
        uint64_t plen = hdr[1] & 0x7F;

        /* RFC 6455 §5.1: a client MUST close the connection on receiving a
         * masked frame from the server. */
        if (masked) {
            socket->closed = 1;
            wii_ws_discard_pending(socket);
            return WF_ERR_PARSE;
        }

        if (plen == 126) {
            unsigned char ext[2];
            if (wii_ws_fill(socket, socket->raw_len - socket->raw_pos + 2) != WF_OK)
                { socket->closed = 1; wii_ws_discard_pending(socket); return WF_ERR_NETWORK; }
            wii_ws_consume(socket, 2, ext);
            plen = ((uint64_t)ext[0] << 8) | ext[1];
        } else if (plen == 127) {
            unsigned char ext[8];
            if (wii_ws_fill(socket, socket->raw_len - socket->raw_pos + 8) != WF_OK)
                { socket->closed = 1; wii_ws_discard_pending(socket); return WF_ERR_NETWORK; }
            wii_ws_consume(socket, 8, ext);
            plen = 0;
            for (int i = 0; i < 8; i++) plen = (plen << 8) | ext[i];
        }

        if (plen > WF_WEBSOCKET_MAX_MESSAGE) {
            socket->closed = 1;
            wii_ws_discard_pending(socket);
            return WF_ERR_PARSE;
        }

        unsigned char *payload = NULL;
        if (plen > 0) {
            payload = malloc((size_t)plen);
            if (!payload) return WF_ERR_ALLOC;
            if (wii_ws_fill(socket, socket->raw_len - socket->raw_pos + (size_t)plen) != WF_OK) {
                free(payload);
                socket->closed = 1;
                wii_ws_discard_pending(socket);
                return WF_ERR_NETWORK;
            }
            wii_ws_consume(socket, (size_t)plen, payload);
        }

        switch (opcode) {
        case 0x0: /* continuation */
        case 0x1: /* text */
        case 0x2: /* binary */ {
            if (opcode != 0x0) {
                if (socket->have_pending_type) {
                    free(payload);
                    wii_ws_discard_pending(socket);
                    socket->closed = 1;
                    return WF_ERR_PARSE; /* new message while one is still open */
                }
                socket->pending_type = (opcode == 0x1) ? WF_WEBSOCKET_TEXT
                                                        : WF_WEBSOCKET_BINARY;
                socket->have_pending_type = 1;
            } else if (!socket->have_pending_type) {
                free(payload);
                socket->closed = 1;
                return WF_ERR_PARSE; /* continuation with nothing to continue */
            }

            wf_status status = plen ? wii_ws_append(socket, payload, (size_t)plen) : WF_OK;
            free(payload);
            if (status != WF_OK) {
                wii_ws_discard_pending(socket);
                socket->closed = 1;
                return status;
            }

            if (fin) {
                out->data = socket->pending;
                out->len = socket->pending_len;
                out->type = socket->pending_type;
                socket->pending = NULL;
                socket->pending_len = 0;
                socket->pending_cap = 0;
                socket->pending_type = 0;
                socket->have_pending_type = 0;
                return WF_OK;
            }
            continue;
        }
        case 0x8: /* close */
            free(payload);
            socket->closed = 1;
            wii_ws_discard_pending(socket);
            (void)wii_ws_send_frame(socket, 0x8, NULL, 0); /* best-effort echo close */
            return WF_ERR_NETWORK;
        case 0x9: /* ping: answer with pong carrying the same payload */
            (void)wii_ws_send_frame(socket, 0xA, payload, (size_t)plen);
            free(payload);
            continue;
        case 0xA: /* pong: no action needed */
            free(payload);
            continue;
        default:
            free(payload);
            socket->closed = 1;
            wii_ws_discard_pending(socket);
            return WF_ERR_PARSE;
        }
    }
}

wf_status wf_websocket_send_text(wf_websocket *socket, const char *text,
                                 size_t text_len) {
    if (!socket || (!text && text_len) || text_len > WF_WEBSOCKET_MAX_MESSAGE)
        return WF_ERR_INVALID_ARG;
    return wii_ws_send_frame(socket, 0x1, (const unsigned char *)text, text_len);
}

wf_status wf_websocket_send_ping(wf_websocket *socket) {
    if (!socket) return WF_ERR_INVALID_ARG;
    return wii_ws_send_frame(socket, 0x9, NULL, 0);
}

void wf_websocket_message_free(wf_websocket_message *message) {
    if (!message) return;
    free(message->data);
    memset(message, 0, sizeof(*message));
}

void wf_websocket_free(wf_websocket *socket) {
    if (!socket) return;
    if (!socket->closed) (void)wii_ws_send_frame(socket, 0x8, NULL, 0);
    wii_tls_close(socket->conn);
    free(socket->raw_buf);
    free(socket->pending);
    free(socket);
}
