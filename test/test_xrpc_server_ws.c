/*
 * test_xrpc_server_ws.c — integration test for WebSocket (RFC 6455)
 * subscription serving on the optional XRPC server module.
 *
 * Starts a local XRPC server, registers a WS endpoint whose handler streams
 * N binary frames from a worker thread (mirroring the SSE suspend/resume
 * pattern, but over a real RFC 6455 upgrade), opens a raw client socket,
 * performs the WS handshake, verifies the Sec-WebSocket-Accept, decodes the
 * server frames, and asserts the stream terminates with a close frame. All
 * over a short timeout so a failure can never hang CI.
 */

#include "wolfram/xrpc_server.h"

#include <openssl/sha.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>

/* ------------------------------------------------------------------ */
/* Minimal base64 (RFC 4648, padded) — mirror of the server encoder    */
/* ------------------------------------------------------------------ */
static void test_base64_encode(const unsigned char *in, size_t len, char *out) {
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

/* ------------------------------------------------------------------ */
/* Raw socket helpers                                                  */
/* ------------------------------------------------------------------ */

/* Connect to 127.0.0.1:port; returns fd or -1. */
static int test_connect(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Write all bytes; returns 0 on success. */
static int test_write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = { fd, POLLOUT, 0 };
                if (poll(&pfd, 1, 2000) <= 0) return -1;
                continue;
            }
            return -1;
        }
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

/* Read exactly `len` bytes with an overall timeout (ms). 0 ok, -1 fail. */
static int test_read_exact(int fd, void *buf, size_t len, int timeout_ms) {
    char *p = (char *)buf;
    size_t off = 0;
    int remaining = timeout_ms;
    while (off < len) {
        struct pollfd pfd = { fd, POLLIN, 0 };
        int pr = poll(&pfd, 1, remaining > 0 ? remaining : 1000);
        if (pr < 0) { if (errno == EINTR) continue; return -1; }
        if (pr == 0) return -1;
        ssize_t n = read(fd, p + off, len - off);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* WebSocket streaming handler — pushes 3 binary frames then closes    */
/* ------------------------------------------------------------------ */

struct ws_thread_arg {
    wf_xrpc_ws_stream *stream;
    int                count;
};

struct ws_handler_ctx {
    int cursor_seen;
};

static void *ws_streamer(void *arg) {
    struct ws_thread_arg *a = (struct ws_thread_arg *)arg;
    char payload[32];
    for (int i = 0; i < a->count; i++) {
        snprintf(payload, sizeof(payload), "frame-%d", i);
        if (wf_xrpc_server_ws_send(a->stream, payload, strlen(payload)) != WF_OK) {
            fprintf(stderr, "WARN: ws_send failed on frame %d\n", i);
        }
    }
    wf_xrpc_server_ws_close(a->stream, 1000);
    wf_xrpc_server_ws_release(a->stream);
    free(a);
    return NULL;
}

static wf_status ws_handler(void *ctx, const wf_xrpc_request *req,
                            wf_xrpc_ws_stream *stream) {
    struct ws_thread_arg *a;
    pthread_t tid;
    struct ws_handler_ctx *handler_ctx = (struct ws_handler_ctx *)ctx;
    const cJSON *cursor = req && req->params
                              ? cJSON_GetObjectItemCaseSensitive(req->params,
                                                                 "cursor")
                              : NULL;
    if (handler_ctx && cJSON_IsString(cursor) &&
        strcmp(cursor->valuestring, "42") == 0) {
        handler_ctx->cursor_seen = 1;
    }

    a = (struct ws_thread_arg *)malloc(sizeof(*a));
    if (!a) {
        return WF_ERR_ALLOC;
    }
    a->stream = stream;
    a->count = 3;

    if (wf_xrpc_server_ws_retain(stream) != WF_OK) {
        free(a);
        return WF_ERR_INVALID_ARG;
    }

    /* Spawn a detached worker that streams frames; the handler must return
     * promptly so libmicrohttpd can complete the upgrade handshake. */
    if (pthread_create(&tid, NULL, ws_streamer, a) != 0) {
        wf_xrpc_server_ws_release(stream);
        free(a);
        return WF_ERR_ALLOC;
    }
    pthread_detach(tid);
    return WF_OK;
}

/* ------------------------------------------------------------------ */
/* Test runner                                                         */
/* ------------------------------------------------------------------ */

#define WS_KEY "dGhlIHNhbXBsZSBub25jZQ=="
#define TEST_COUNT 3

static int run_test(void) {
    wf_xrpc_server *server = NULL;
    int failures = 0;
    int fd = -1;
    uint16_t port;
    struct ws_handler_ctx handler_ctx = {0};

    server = wf_xrpc_server_start("127.0.0.1", 0, 1);
    if (!server) {
        fprintf(stderr, "FAIL: wf_xrpc_server_start returned NULL\n");
        return 1;
    }
    port = wf_xrpc_server_port(server);
    if (port == 0) {
        fprintf(stderr, "FAIL: server port is 0\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    if (wf_xrpc_server_register_ws(server, "io.example.subscribe",
                                   ws_handler, &handler_ctx) != WF_OK) {
        fprintf(stderr, "FAIL: register WS endpoint\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    /* --- Open a raw TCP connection and perform the WS handshake --- */
    fd = test_connect(port);
    if (fd < 0) {
        fprintf(stderr, "FAIL: connect to server\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    char req[256];
    int nr = snprintf(req, sizeof(req),
                      "GET /xrpc/io.example.subscribe?cursor=42 HTTP/1.1\r\n"
                      "Host: 127.0.0.1:%u\r\n"
                      "Upgrade: websocket\r\n"
                      "Connection: Upgrade\r\n"
                      "Sec-WebSocket-Key: " WS_KEY "\r\n"
                      "Sec-WebSocket-Version: 13\r\n"
                      "\r\n", (unsigned)port);
    if (test_write_all(fd, req, (size_t)nr) != 0) {
        fprintf(stderr, "FAIL: write handshake request\n");
        close(fd);
        wf_xrpc_server_free(server);
        return 1;
    }

    /* Read the 101 response headers. */
    char hdr[1024];
    size_t hoff = 0;
    int got_eoh = 0;
    while (!got_eoh && hoff < sizeof(hdr) - 1) {
        struct pollfd pfd = { fd, POLLIN, 0 };
        if (poll(&pfd, 1, 3000) <= 0) break;
        ssize_t n = read(fd, hdr + hoff, sizeof(hdr) - 1 - hoff);
        if (n <= 0) break;
        hoff += (size_t)n;
        hdr[hoff] = '\0';
        if (strstr(hdr, "\r\n\r\n")) got_eoh = 1;
    }
    hdr[hoff] = '\0';

    if (!got_eoh) {
        fprintf(stderr, "FAIL: did not receive handshake response\n");
        failures++;
        goto cleanup;
    }
    if (strstr(hdr, " 101 ") == NULL && strstr(hdr, " 101\r\n") == NULL &&
        strncmp(hdr, "HTTP/1.1 101", 12) != 0) {
        fprintf(stderr, "FAIL: expected 101 Switching Protocols, got:\n%.*s\n",
                (int)hoff, hdr);
        failures++;
        goto cleanup;
    }

    /* Extract Sec-WebSocket-Accept and verify it. */
    char *accept = strcasestr(hdr, "sec-websocket-accept:");
    if (!accept) {
        fprintf(stderr, "FAIL: missing Sec-WebSocket-Accept header\n");
        failures++;
        goto cleanup;
    }
    char *val = accept + strlen("sec-websocket-accept:");
    while (*val == ' ' || *val == '\t') val++;
    char got_accept[128];
    size_t k = 0;
    while (*val && *val != '\r' && *val != '\n' && k < sizeof(got_accept) - 1) {
        got_accept[k++] = *val++;
    }
    got_accept[k] = '\0';

    char concat[256];
    unsigned char digest[SHA_DIGEST_LENGTH];
    char expect[40];
    snprintf(concat, sizeof(concat), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
             WS_KEY);
    SHA1((const unsigned char *)concat, strlen(concat), digest);
    test_base64_encode(digest, SHA_DIGEST_LENGTH, expect);

    if (strcmp(got_accept, expect) != 0) {
        fprintf(stderr, "FAIL: Sec-WebSocket-Accept mismatch: got '%s' "
                        "want '%s'\n", got_accept, expect);
        failures++;
        goto cleanup;
    }
    printf("PASS: WS handshake accepted (Sec-WebSocket-Accept verified)\n");

    /* --- Decode the streamed frames --- */
    int frames_seen = 0;
    int close_seen = 0;
    char expect_payload[32];
    for (int i = 0; i < TEST_COUNT; i++) {
        snprintf(expect_payload, sizeof(expect_payload), "frame-%d", i);

        unsigned char h[2];
        if (test_read_exact(fd, h, 2, 3000) != 0) {
            fprintf(stderr, "FAIL: read frame %d header (got %d frames)\n",
                    i, frames_seen);
            failures++;
            goto cleanup;
        }
        uint8_t opcode = (uint8_t)(h[0] & 0x0f);
        uint64_t plen = (uint64_t)(h[1] & 0x7f);
        if (plen == 126) {
            unsigned char e[2];
            if (test_read_exact(fd, e, 2, 1000) != 0) { failures++; goto cleanup; }
            plen = ((uint64_t)e[0] << 8) | (uint64_t)e[1];
        } else if (plen == 127) {
            unsigned char e[8];
            if (test_read_exact(fd, e, 8, 1000) != 0) { failures++; goto cleanup; }
            plen = 0;
            for (int j = 0; j < 8; j++) plen = (plen << 8) | (uint64_t)e[j];
        }
        /* Server→client frames must NOT be masked (RFC 6455 §5.1). */
        if (h[1] & 0x80) {
            fprintf(stderr, "FAIL: server frame %d was masked\n", i);
            failures++;
            goto cleanup;
        }

        if (opcode == 0x8) { /* close */
            close_seen = 1;
            break;
        }
        if (opcode != 0x2) { /* only expect binary frames here */
            fprintf(stderr, "FAIL: unexpected opcode 0x%02x in frame %d\n",
                    opcode, i);
            failures++;
            goto cleanup;
        }

        char payload[256];
        if (plen >= sizeof(payload)) { failures++; goto cleanup; }
        if (test_read_exact(fd, payload, (size_t)plen, 2000) != 0) {
            fprintf(stderr, "FAIL: read frame %d payload\n", i);
            failures++;
            goto cleanup;
        }
        if ((size_t)plen != strlen(expect_payload) ||
            memcmp(payload, expect_payload, (size_t)plen) != 0) {
            fprintf(stderr, "FAIL: frame %d payload mismatch: '%.*s'\n",
                    i, (int)plen, payload);
            failures++;
            goto cleanup;
        }
        frames_seen++;
    }

    if (frames_seen != TEST_COUNT) {
        fprintf(stderr, "FAIL: expected %d binary frames, got %d\n",
                TEST_COUNT, frames_seen);
        failures++;
    } else {
        printf("PASS: received %d binary frames in order\n", frames_seen);
    }

    if (!close_seen) {
        /* Read one more frame expecting the close. */
        unsigned char h[2];
        if (test_read_exact(fd, h, 2, 3000) == 0 && (h[0] & 0x0f) == 0x8) {
            close_seen = 1;
        }
    }
    if (!close_seen) {
        fprintf(stderr, "FAIL: stream did not terminate with a close frame\n");
        failures++;
    } else {
        printf("PASS: stream terminated with a close frame\n");
    }
    if (!handler_ctx.cursor_seen) {
        fprintf(stderr, "FAIL: WS handler did not receive cursor query param\n");
        failures++;
    } else {
        printf("PASS: WS handler received cursor query param\n");
    }

cleanup:
    if (fd >= 0) close(fd);
    wf_xrpc_server_free(server);   /* blocks until clean shutdown */
    if (failures == 0) {
        printf("PASS: XRPC server WebSocket subscription serving\n");
    }
    return failures == 0 ? 0 : 1;
}

int main(void) {
    return run_test();
}
