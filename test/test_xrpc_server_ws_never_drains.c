/*
 * test_xrpc_server_ws_never_drains.c — a consumer that never reads at all
 * makes wf_xrpc_server_ws_send return WF_ERR_TIMEOUT, not WF_ERR_NETWORK.
 *
 * The reference PDS's firehose distinguishes a slow consumer (its outbound
 * buffer overflowed because the subscriber can't keep up) from an ordinary
 * disconnect, and sends a `ConsumerTooSlow` error frame before closing
 * (packages/pds/src/sequencer/outbox.ts). wolfram's WS write path is
 * synchronous rather than buffered, so the analogous condition is a write
 * that stays blocked on a full send/receive window for the whole write
 * timeout (see wf_ws_write_all in src/server/xrpc_server.c) — that path
 * returns WF_ERR_TIMEOUT specifically so a caller like MetalBear's
 * subscribeRepos can label the disconnect the same way.
 *
 * To make a real send actually block deterministically (rather than
 * depending on how large the OS's default socket buffers happen to be on
 * whatever machine runs this), the client shrinks its own SO_RCVBUF to the
 * kernel's practical minimum before ever reading, then never reads at all.
 * The server pushes frames until wf_xrpc_server_ws_send stops returning
 * WF_OK. This test only passes once that happens within the write timeout
 * and the returned status is WF_ERR_TIMEOUT — WF_ERR_NETWORK or any other
 * status is exactly the bug this exists to catch.
 *
 * wf_xrpc_server_stop() (called by wf_xrpc_server_free() below) marks every
 * still-open ws_stream `closed` and shutdown()s its socket *before* joining
 * the handler thread — by design, so a slow consumer can never hang server
 * shutdown for the full write timeout. wf_xrpc_server_ws_send() checks that
 * flag under the same mutex the handler's send loop uses and returns
 * WF_ERR_INVALID_ARG immediately once it is set, without attempting a write
 * at all. That races the handler thread's very first send: on a machine
 * where the newly spawned handler thread is slow to get scheduled, main can
 * reach wf_xrpc_server_free() and mark the stream closed before the handler
 * has sent anything, so its first (and only) attempt sees `closed` and
 * returns WF_ERR_INVALID_ARG after 1 attempt instead of ~16 real sends and a
 * genuine WF_ERR_TIMEOUT. This does not reproduce reliably on a lightly
 * loaded machine, where the handler thread routinely wins that race, but
 * does on a more contended one (this is exactly what surfaced it in CI).
 * The fix belongs in the test, not in wf_xrpc_server_stop()'s shutdown
 * ordering: wait for the handler to reach its own conclusion — real write
 * timeout or MAX_ATTEMPTS exhausted — before ever calling
 * wf_xrpc_server_free(), so the proactive-close sweep has nothing left to
 * race against. */

#include "wolfram/xrpc.h"
#include "wolfram/xrpc_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define NSID "io.example.never.drains"
#define CHUNK_LEN 65536  /* one WS frame payload per send attempt */
#define MAX_ATTEMPTS 512 /* generous: up to 32MB before giving up */

static int test_connect_never_reading(uint16_t port) {
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
    /* Ask for the smallest receive window the kernel will give us. Linux and
     * macOS both clamp this upward to a platform minimum, but that minimum is
     * still small enough that a handful of 4KB frames fills it without ever
     * being read. */
    int tiny = 1;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &tiny, sizeof(tiny));
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

static int test_handshake(int fd, uint16_t port) {
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

    /* Read only the handshake response itself; once "\r\n\r\n" arrives, stop
     * reading for the rest of the test — that is the whole point. A partial
     * over-read of the server's first frames here would just refill the
     * window we are trying to keep empty of drains, not full of them. */
    while (!got_eoh && hoff < sizeof(hdr) - 1) {
        ssize_t n = read(fd, hdr + hoff, 1);
        if (n <= 0) break;
        hoff += (size_t)n;
        hdr[hoff] = '\0';
        if (hoff >= 4 && strstr(hdr, "\r\n\r\n")) got_eoh = 1;
    }
    if (!got_eoh || strncmp(hdr, "HTTP/1.1 101", 12) != 0) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Server handler: push chunks until the send stops succeeding          */
/* ------------------------------------------------------------------ */

typedef struct {
    _Atomic wf_status observed;
    atomic_int attempts;
    atomic_int done; /* set once the loop below has reached a conclusion */
} handler_result;

static handler_result g_result;

static wf_status never_drains_handler(void *ctx, const wf_xrpc_request *req,
                                      wf_xrpc_ws_stream *stream) {
    static unsigned char chunk[CHUNK_LEN];
    (void)ctx;
    (void)req;
    memset(chunk, 'x', sizeof(chunk));
    atomic_store(&g_result.observed, WF_OK);
    atomic_store(&g_result.attempts, 0);
    for (int i = 0; i < MAX_ATTEMPTS; i++) {
        wf_status rc = wf_xrpc_server_ws_send(stream, chunk, sizeof(chunk));
        atomic_fetch_add(&g_result.attempts, 1);
        if (rc != WF_OK) {
            atomic_store(&g_result.observed, rc);
            break;
        }
    }
    atomic_store(&g_result.done, 1);
    return WF_OK;
}

/* ------------------------------------------------------------------ */

static int run_test(void) {
    wf_xrpc_server *server;
    uint16_t port;
    int fd;

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
    if (wf_xrpc_server_register_ws(server, NSID, never_drains_handler, NULL) !=
        WF_OK) {
        fprintf(stderr, "FAIL: register WS endpoint\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    fd = test_connect_never_reading(port);
    if (fd < 0) {
        fprintf(stderr, "FAIL: connect to server\n");
        wf_xrpc_server_free(server);
        return 1;
    }
    if (test_handshake(fd, port) != 0) {
        fprintf(stderr, "FAIL: WS handshake\n");
        close(fd);
        wf_xrpc_server_free(server);
        return 1;
    }

    /* Never read again. Wait for the handler's send loop to reach its own
     * conclusion (real write timeout, or attempts exhausted) BEFORE calling
     * wf_xrpc_server_free() — see the top-of-file comment. Calling it
     * immediately races wf_xrpc_server_stop()'s proactive close-and-shutdown
     * sweep against the handler thread even starting its first send. Bounded
     * at 10s (the real per-attempt write timeout is ~2s) so a genuine
     * regression still fails loudly instead of hanging the suite. */
    {
        struct timespec start, now;
        clock_gettime(CLOCK_MONOTONIC, &start);
        while (!atomic_load(&g_result.done)) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = (double)(now.tv_sec - start.tv_sec) +
                             (double)(now.tv_nsec - start.tv_nsec) / 1e9;
            if (elapsed > 10.0) {
                fprintf(stderr,
                        "FAIL: handler send loop did not conclude within 10s "
                        "(%d attempts so far)\n",
                        atomic_load(&g_result.attempts));
                close(fd);
                wf_xrpc_server_free(server);
                return 1;
            }
            sched_yield();
        }
    }

    wf_xrpc_server_free(server);
    close(fd);

    wf_status observed = atomic_load(&g_result.observed);
    int attempts = atomic_load(&g_result.attempts);

    if (observed == WF_OK) {
        fprintf(stderr,
                "FAIL: send loop never blocked after %d attempts (%d bytes) — "
                "this platform's socket buffers are larger than this test "
                "assumed\n",
                attempts, attempts * CHUNK_LEN);
        return 1;
    }
    if (observed != WF_ERR_TIMEOUT) {
        fprintf(stderr,
                "FAIL: expected WF_ERR_TIMEOUT once the peer never drains, "
                "got status %d after %d attempts\n",
                (int)observed, attempts);
        return 1;
    }

    printf("PASS: send to a peer that never reads returned WF_ERR_TIMEOUT "
           "after %d attempts (%d bytes)\n",
           attempts, attempts * CHUNK_LEN);
    return 0;
}

int main(void) {
    return run_test();
}
