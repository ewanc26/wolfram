/*
 * test_relay_server.c — integration test for the generic upstream→downstream
 * WebSocket subscription relay (relay_server) on the optional XRPC server.
 *
 * The relay's upstream side uses the libcurl WebSocket transport
 * (wf_websocket_connect). When the linked libcurl supports WS we run the full
 * path: an in-process "upstream" server streams 3 ordered binary frames, the
 * relay forwards them to a wf_websocket client byte-for-byte, then closes.
 *
 * When the linked libcurl lacks WS support (e.g. the system libcurl on macOS)
 * we still verify the relay's wiring offline: a relay registered against an
 * unreachable upstream must spawn its forward thread, detect the upstream
 * connect failure, and close the downstream connection cleanly (a raw-socket
 * client receives the 101 handshake followed by a close frame). This exercises
 * registration, the handler, the detached forward thread, and teardown.
 *
 * All socket operations use short timeouts so a failure can never hang CI.
 */

#include "wolfram/relay_server.h"
#include "wolfram/websocket.h"
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

/* Perform a raw WS handshake against `path` on `port`; on success leave `fd`
 * positioned at the start of the framed stream and return 0, else -1. */
static int test_ws_handshake(int fd, uint16_t port, const char *path) {
    char req[256];
    int nr = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.1\r\n"
                      "Host: 127.0.0.1:%u\r\n"
                      "Upgrade: websocket\r\n"
                      "Connection: Upgrade\r\n"
                      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                      "Sec-WebSocket-Version: 13\r\n"
                      "\r\n", path, (unsigned)port);
    if (test_write_all(fd, req, (size_t)nr) != 0) {
        return -1;
    }

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
    if (!got_eoh || strncmp(hdr, "HTTP/1.1 101", 12) != 0) {
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Upstream server: WS route that streams 3 frames then closes         */
/* ------------------------------------------------------------------ */

struct upstream_arg {
    wf_xrpc_ws_stream *stream;
    int                count;
};

static void *upstream_streamer(void *arg) {
    struct upstream_arg *a = (struct upstream_arg *)arg;
    char payload[32];
    for (int i = 0; i < a->count; i++) {
        snprintf(payload, sizeof(payload), "frame-%d", i);
        if (wf_xrpc_server_ws_send(a->stream, payload, strlen(payload)) != WF_OK) {
            fprintf(stderr, "WARN: upstream ws_send failed on frame %d\n", i);
        }
    }
    wf_xrpc_server_ws_close(a->stream, 1000);
    wf_xrpc_server_ws_release(a->stream);
    free(a);
    return NULL;
}

static wf_status upstream_ws_handler(void *ctx, const wf_xrpc_request *req,
                                     wf_xrpc_ws_stream *stream) {
    struct upstream_arg *a;
    pthread_t tid;
    (void)ctx;
    (void)req;

    a = (struct upstream_arg *)malloc(sizeof(*a));
    if (!a) {
        return WF_ERR_ALLOC;
    }
    a->stream = stream;
    a->count = 3;

    if (wf_xrpc_server_ws_retain(stream) != WF_OK) {
        free(a);
        return WF_ERR_INVALID_ARG;
    }

    if (pthread_create(&tid, NULL, upstream_streamer, a) != 0) {
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
#define UPSTREAM_NSID "io.example.upstream.subscribe"
#define RELAY_NSID    "com.atproto.sync.subscribeRepos"

static int run_test(void) {
    wf_xrpc_server   *upstream = NULL;
    wf_xrpc_server   *relay_srv = NULL;
    wf_relay_server  *relay = NULL;
    wf_websocket     *client = NULL;
    int               failures = 0;
    uint16_t          relay_port;
    char              upstream_url[128];

    /* Always bring up the relay server so we exercise registration + teardown. */
    relay_srv = wf_xrpc_server_start("127.0.0.1", 0, 1);
    if (!relay_srv) {
        fprintf(stderr, "FAIL: relay wf_xrpc_server_start returned NULL\n");
        return 1;
    }
    relay_port = wf_xrpc_server_port(relay_srv);
    if (relay_port == 0) {
        fprintf(stderr, "FAIL: relay port is 0\n");
        wf_xrpc_server_free(relay_srv);
        return 1;
    }

    if (wf_websocket_supported()) {
        /* ---- Full path: relay forwards the upstream subscription ---- */
        uint16_t up_port;

        upstream = wf_xrpc_server_start("127.0.0.1", 0, 1);
        if (!upstream) {
            fprintf(stderr, "FAIL: upstream wf_xrpc_server_start returned NULL\n");
            wf_xrpc_server_free(relay_srv);
            return 1;
        }
        up_port = wf_xrpc_server_port(upstream);
        if (up_port == 0) {
            fprintf(stderr, "FAIL: upstream port is 0\n");
            wf_xrpc_server_free(upstream);
            wf_xrpc_server_free(relay_srv);
            return 1;
        }
        if (wf_xrpc_server_register_ws(upstream, UPSTREAM_NSID,
                                       upstream_ws_handler, NULL) != WF_OK) {
            fprintf(stderr, "FAIL: register upstream WS endpoint\n");
            wf_xrpc_server_free(upstream);
            wf_xrpc_server_free(relay_srv);
            return 1;
        }
        snprintf(upstream_url, sizeof(upstream_url),
                 "ws://127.0.0.1:%u/xrpc/%s", (unsigned)up_port, UPSTREAM_NSID);
    } else {
        /* ---- Degraded offline path: relay cannot reach any upstream ---- */
        snprintf(upstream_url, sizeof(upstream_url),
                 "ws://127.0.0.1:9/xrpc/%s", UPSTREAM_NSID);
    }

    {
        wf_relay_config cfg = WF_RELAY_CONFIG_INIT;
        cfg.nsid = strdup(RELAY_NSID);
        cfg.upstream_url = strdup(upstream_url);
        cfg.reconnect_delay_ms = 0;
        relay = wf_xrpc_server_register_relay(relay_srv, &cfg);
        /* Free our copy; the relay keeps its own deep copy. */
        wf_relay_config_free(&cfg);
    }
    if (!relay) {
        fprintf(stderr, "FAIL: wf_xrpc_server_register_relay returned NULL\n");
        wf_xrpc_server_free(upstream);
        wf_xrpc_server_free(relay_srv);
        return 1;
    }

    if (wf_websocket_supported()) {
        /* Connect a raw wf_websocket client to the relay and verify it
         * receives the same ordered frames byte-for-byte, then a clean close. */
        char url[128];
        snprintf(url, sizeof(url), "ws://127.0.0.1:%u/xrpc/%s",
                 (unsigned)relay_port, RELAY_NSID);
        if (wf_websocket_connect(url, &client) != WF_OK || !client) {
            fprintf(stderr, "FAIL: client wf_websocket_connect to relay\n");
            failures++;
            goto cleanup;
        }

        for (int i = 0; i < TEST_COUNT; i++) {
            wf_websocket_message msg;
            char expect_payload[32];
            snprintf(expect_payload, sizeof(expect_payload), "frame-%d", i);

            if (wf_websocket_receive(client, &msg) != WF_OK) {
                fprintf(stderr, "FAIL: client did not receive frame %d "
                                "(got %d of %d)\n", i, i, TEST_COUNT);
                failures++;
                goto cleanup;
            }
            if (msg.type != WF_WEBSOCKET_BINARY) {
                fprintf(stderr, "FAIL: frame %d was not binary (type %d)\n",
                        i, (int)msg.type);
                wf_websocket_message_free(&msg);
                failures++;
                goto cleanup;
            }
            if (msg.len != strlen(expect_payload) ||
                memcmp(msg.data, expect_payload, msg.len) != 0) {
                fprintf(stderr, "FAIL: frame %d payload mismatch: got %.*s "
                                "want %s\n", i, (int)msg.len,
                                (const char *)msg.data, expect_payload);
                wf_websocket_message_free(&msg);
                failures++;
                goto cleanup;
            }
            wf_websocket_message_free(&msg);
        }
        printf("PASS: client received %d ordered binary frames "
               "(byte-for-byte)\n", TEST_COUNT);

        {
            wf_websocket_message msg;
            wf_status rc = wf_websocket_receive(client, &msg);
            wf_websocket_message_free(&msg);
            if (rc == WF_OK) {
                fprintf(stderr, "FAIL: expected clean close after %d frames\n",
                        TEST_COUNT);
                failures++;
                goto cleanup;
            }
            printf("PASS: stream terminated with a clean upstream close\n");
        }
    } else {
        /* Degraded path: a raw client should get 101 then a clean close frame
         * (the relay detects the unreachable upstream and tears down). */
        int fd = test_connect(relay_port);
        char path[128];
        if (fd < 0) {
            fprintf(stderr, "FAIL: connect raw client to relay\n");
            failures++;
            goto cleanup;
        }
        snprintf(path, sizeof(path), "/xrpc/%s", RELAY_NSID);
        if (test_ws_handshake(fd, relay_port, path) != 0) {
            fprintf(stderr, "FAIL: relay did not complete WS handshake\n");
            close(fd);
            failures++;
            goto cleanup;
        }
        printf("PASS: relay completed WS handshake for downstream client\n");

        unsigned char h[2];
        if (test_read_exact(fd, h, 2, 3000) != 0) {
            fprintf(stderr, "FAIL: relay did not send a close frame after "
                            "upstream connect failure\n");
            close(fd);
            failures++;
            goto cleanup;
        }
        if ((h[0] & 0x0f) != 0x8) {
            fprintf(stderr, "FAIL: expected close frame (0x8), got 0x%02x\n",
                    h[0] & 0x0f);
            close(fd);
            failures++;
            goto cleanup;
        }
        printf("PASS: relay closed downstream cleanly on upstream failure\n");
        close(fd);
    }

cleanup:
    if (client) wf_websocket_free(client);
    wf_xrpc_server_free(relay_srv);   /* blocks until clean shutdown */
    wf_xrpc_server_free(upstream);    /* blocks until clean shutdown */
    /* Free the relay handle only after the servers have torn down their
     * WebSocket worker threads, per the module's ownership contract. */
    wf_relay_server_free(relay);

    if (failures == 0) {
        printf("PASS: WebSocket subscription relay serving\n");
    }
    return failures == 0 ? 0 : 1;
}

int main(void) {
    return run_test();
}
