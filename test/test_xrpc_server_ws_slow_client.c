/*
 * test_xrpc_server_ws_slow_client.c — a subscriber that is slow to drain must
 * still receive every frame the server sent before it closed.
 *
 * This is the deterministic form of a flake that only ever appeared under
 * parallel ctest (wolfram#7): a loaded machine delays the client's reads, and
 * frames that were already written to the socket went missing.
 *
 * The mechanism is TCP, not WebSocket. If a socket is close()d while unread
 * data sits in its receive queue, the kernel sends RST instead of FIN, and a
 * received RST discards whatever is still sitting in the peer's receive
 * buffer — including frames that were delivered perfectly well. A subscriber
 * that had not yet drained loses the tail of the stream.
 *
 * For a firehose that matters beyond a test: the last events before any
 * disconnect are exactly the ones a consumer needs in order to resume from the
 * right cursor.
 *
 * The test forces the losing interleaving rather than waiting for load to
 * produce it: the client completes the handshake, sends an unsolicited frame
 * (so the server has unread data queued, which is what turns close into RST),
 * then sleeps well past the server's send-and-close before reading anything.
 */

#include "wolfram/xrpc_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define NSID "io.example.slow.subscribe"
#define FRAME_COUNT 8
#define PAYLOAD_FMT "slow-frame-%d"

/* ------------------------------------------------------------------ */
/* Raw client helpers                                                  */
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
            return -1;
        }
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

/*
 * Complete the handshake, and hand back any bytes that arrived in the same
 * read as the 101 response.
 *
 * The server pushes its frames the moment the handler runs, so under load they
 * routinely share a segment with the response headers. Discarding whatever
 * follows "\r\n\r\n" would make this test report "0 frames" for a stream that
 * was delivered perfectly — the very failure it exists to detect. `leftover`
 * receives those bytes and the caller counts them alongside the rest.
 */
static int test_handshake(int fd, uint16_t port, unsigned char *leftover,
                          size_t *leftover_len) {
    char req[256];
    char hdr[2048];
    size_t hoff = 0;
    int got_eoh = 0;
    int nr = snprintf(req, sizeof(req),
                      "GET /xrpc/" NSID " HTTP/1.1\r\n"
                      "Host: 127.0.0.1:%u\r\n"
                      "Upgrade: websocket\r\n"
                      "Connection: Upgrade\r\n"
                      "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                      "Sec-WebSocket-Version: 13\r\n"
                      "\r\n",
                      (unsigned)port);
    if (test_write_all(fd, req, (size_t)nr) != 0) return -1;

    while (!got_eoh && hoff < sizeof(hdr) - 1) {
        struct pollfd pfd = {fd, POLLIN, 0};
        if (poll(&pfd, 1, 3000) <= 0) break;
        ssize_t n = read(fd, hdr + hoff, sizeof(hdr) - 1 - hoff);
        if (n <= 0) break;
        hoff += (size_t)n;
        hdr[hoff] = '\0';
        if (strstr(hdr, "\r\n\r\n")) got_eoh = 1;
    }
    hdr[hoff] = '\0';
    if (!got_eoh || strncmp(hdr, "HTTP/1.1 101", 12) != 0) return -1;

    {
        char *eoh = strstr(hdr, "\r\n\r\n");
        size_t body_at = (size_t)(eoh - hdr) + 4;
        *leftover_len = (hoff > body_at) ? hoff - body_at : 0;
        if (*leftover_len) {
            memcpy(leftover, hdr + body_at, *leftover_len);
        }
    }
    return 0;
}

/* Read whatever is available until the peer signals EOF or we stop making
 * progress. Returns bytes read, or -1 if the connection was reset. */
static ssize_t drain_all(int fd, unsigned char *buf, size_t cap) {
    size_t off = 0;
    for (;;) {
        struct pollfd pfd = {fd, POLLIN, 0};
        int pr = poll(&pfd, 1, 2000);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (pr == 0) break; /* quiet: nothing more coming */
        ssize_t n = read(fd, buf + off, cap - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == ECONNRESET) {
                fprintf(stderr, "  (read: ECONNRESET — peer sent RST)\n");
                return -1;
            }
            return -1;
        }
        if (n == 0) break; /* clean EOF (FIN) */
        off += (size_t)n;
        if (off == cap) break;
    }
    return (ssize_t)off;
}

/* Count unmasked server->client data frames carrying our payloads. */
static int count_payload_frames(const unsigned char *buf, size_t len) {
    size_t i = 0;
    int found = 0;
    while (i + 2 <= len) {
        uint8_t opcode = buf[i] & 0x0f;
        uint64_t plen = buf[i + 1] & 0x7f;
        size_t hdr = 2;
        if (plen == 126) {
            if (i + 4 > len) break;
            plen = ((uint64_t)buf[i + 2] << 8) | buf[i + 3];
            hdr = 4;
        } else if (plen == 127) {
            break; /* not produced by this test */
        }
        if (buf[i + 1] & 0x80) break; /* server frames must not be masked */
        if (i + hdr + plen > len) break;
        if (opcode == 0x2 && plen >= 11 &&
            memcmp(buf + i + hdr, "slow-frame-", 11) == 0) {
            found++;
        }
        i += hdr + (size_t)plen;
    }
    return found;
}

/* ------------------------------------------------------------------ */
/* Server handler: push every frame, then close immediately            */
/* ------------------------------------------------------------------ */

static wf_status slow_ws_handler(void *ctx, const wf_xrpc_request *req,
                                 wf_xrpc_ws_stream *stream) {
    char payload[32];
    (void)ctx;
    (void)req;
    for (int i = 0; i < FRAME_COUNT; i++) {
        int n = snprintf(payload, sizeof(payload), PAYLOAD_FMT, i);
        if (wf_xrpc_server_ws_send(stream, payload, (size_t)n) != WF_OK) {
            fprintf(stderr, "WARN: ws_send failed on frame %d\n", i);
        }
    }
    /* Close straight away: the whole point is that teardown must not destroy
     * frames the client has not collected yet. */
    wf_xrpc_server_ws_close(stream, 1000);
    return WF_OK;
}

/* ------------------------------------------------------------------ */

static int run_test(void) {
    wf_xrpc_server *server;
    uint16_t port;
    int fd;
    unsigned char buf[8192];
    size_t pre = 0; /* bytes the handshake read past the headers */
    ssize_t got;
    int frames;

    server = wf_xrpc_server_start("127.0.0.1", 0, 4);
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
    if (wf_xrpc_server_register_ws(server, NSID, slow_ws_handler, NULL) !=
        WF_OK) {
        fprintf(stderr, "FAIL: register WS endpoint\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    fd = test_connect(port);
    if (fd < 0) {
        fprintf(stderr, "FAIL: connect to server\n");
        wf_xrpc_server_free(server);
        return 1;
    }
    if (test_handshake(fd, port, buf, &pre) != 0) {
        fprintf(stderr, "FAIL: WS handshake\n");
        close(fd);
        wf_xrpc_server_free(server);
        return 1;
    }

    /* Leave unread data in the server's receive queue. A close() with unread
     * data queued is what makes the kernel send RST rather than FIN, so this
     * is the condition the fix has to survive — a masked 1-byte ping. */
    {
        unsigned char ping[7] = {0x89, 0x81, 0x01, 0x02, 0x03, 0x04, 0x41};
        if (test_write_all(fd, ping, sizeof(ping)) != 0) {
            fprintf(stderr, "FAIL: could not send client ping\n");
            close(fd);
            wf_xrpc_server_free(server);
            return 1;
        }
    }

    /* Be slow. The server has already sent all its frames and closed by now;
     * under parallel ctest this delay is what a loaded scheduler imposed. */
    usleep(600 * 1000);

    got = drain_all(fd, buf + pre, sizeof(buf) - pre);
    if (got >= 0) {
        got += (ssize_t)pre; /* the handshake's over-read counts too */
    }
    if (got < 0) {
        fprintf(stderr,
                "FAIL: connection was reset before the client could read; "
                "frames written by the server were discarded\n");
        close(fd);
        wf_xrpc_server_free(server);
        return 1;
    }

    frames = count_payload_frames(buf, (size_t)got);
    close(fd);
    wf_xrpc_server_free(server);

    if (frames != FRAME_COUNT) {
        fprintf(stderr,
                "FAIL: slow client received %d of %d frames (%zd bytes)\n",
                frames, FRAME_COUNT, got);
        return 1;
    }

    printf("PASS: slow client received all %d frames despite immediate "
           "server close\n",
           FRAME_COUNT);
    return 0;
}

int main(void) {
    return run_test();
}
