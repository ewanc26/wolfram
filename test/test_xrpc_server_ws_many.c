/*
 * test_xrpc_server_ws_many.c — many concurrent WebSocket subscribers, torn
 * down while they are all still connected.
 *
 * wf_xrpc_server_stop used to snapshot the open streams into a fixed 64-entry
 * array while clearing the whole list unconditionally. Past the 64th stream
 * the workers were unlinked but never joined, and then ran on against a freed
 * server: MHD_upgrade_action() on a stopped daemon and a lock on a destroyed
 * mutex inside freed memory. Sixty-five firehose subscribers is an ordinary
 * load for a PDS, so this was a shutdown crash waiting for a busy day.
 *
 * The test opens comfortably more than that limit, holds them all open, and
 * frees the server underneath them. It fails by crashing or hanging rather
 * than by an assertion, which is the honest shape for a lifetime bug.
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

#define NSID    "io.example.many.subscribe"
#define CLIENTS 96

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
                      "\r\n", (unsigned)port);
    if (test_write_all(fd, req, (size_t)nr) != 0) return -1;

    while (!got_eoh && hoff < sizeof(hdr) - 1) {
        struct pollfd pfd = { fd, POLLIN, 0 };
        if (poll(&pfd, 1, 5000) <= 0) break;
        ssize_t n = read(fd, hdr + hoff, sizeof(hdr) - 1 - hoff);
        if (n <= 0) break;
        hoff += (size_t)n;
        hdr[hoff] = '\0';
        if (strstr(hdr, "\r\n\r\n")) got_eoh = 1;
    }
    hdr[hoff] = '\0';
    if (!got_eoh || strncmp(hdr, "HTTP/1.1 101", 12) != 0) return -1;
    return 0;
}

/*
 * Hold each stream open from a retained producer that outlives the teardown
 * signal.
 *
 * A handler that simply returns is not enough to expose the bug: every worker
 * then exits within microseconds of stop() shutting its socket, so even the
 * ones nobody joined are gone before the server memory is released, and the
 * broken code passes. Keeping a producer retained forces the workers to still
 * be inside their teardown (waiting on worker_refs) when free() runs, which is
 * when an unjoined worker touches the destroyed ws_mutex.
 */
static void *many_producer(void *arg) {
    wf_xrpc_ws_stream *stream = (wf_xrpc_ws_stream *)arg;
    for (int i = 0; i < 40 && !wf_xrpc_server_ws_is_closed(stream); i++) {
        wf_xrpc_server_ws_send(stream, "tick", 4);
        usleep(10 * 1000);
    }
    usleep(150 * 1000);
    wf_xrpc_server_ws_release(stream);
    return NULL;
}

static wf_status many_ws_handler(void *ctx, const wf_xrpc_request *req,
                                 wf_xrpc_ws_stream *stream) {
    pthread_t tid;
    (void)ctx;
    (void)req;
    wf_xrpc_server_ws_send(stream, "hello", 5);
    if (wf_xrpc_server_ws_retain(stream) != WF_OK) {
        return WF_OK;
    }
    if (pthread_create(&tid, NULL, many_producer, stream) != 0) {
        wf_xrpc_server_ws_release(stream);
        return WF_OK;
    }
    pthread_detach(tid);
    return WF_OK;
}

int main(void) {
    wf_xrpc_server *server;
    uint16_t port;
    int fds[CLIENTS];
    int connected = 0;

    /* Enough daemon threads that ~100 concurrent upgrades make progress. */
    server = wf_xrpc_server_start("127.0.0.1", 0, 16);
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
    if (wf_xrpc_server_register_ws(server, NSID, many_ws_handler, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: register WS endpoint\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    for (int i = 0; i < CLIENTS; i++) {
        fds[i] = test_connect(port);
        if (fds[i] < 0) continue;
        if (test_handshake(fds[i], port) != 0) {
            close(fds[i]);
            fds[i] = -1;
            continue;
        }
        connected++;
    }

    /* The bug needs more streams than the old 64-entry snapshot held. Fewer
     * than that would pass against the broken code and prove nothing. */
    if (connected <= 64) {
        fprintf(stderr, "FAIL: only %d of %d subscribers connected; need >64 "
                        "to exercise the teardown path\n", connected, CLIENTS);
        for (int i = 0; i < CLIENTS; i++) if (fds[i] >= 0) close(fds[i]);
        wf_xrpc_server_free(server);
        return 1;
    }

    /* Tear down with every subscriber still attached. */
    wf_xrpc_server_free(server);

    for (int i = 0; i < CLIENTS; i++) {
        if (fds[i] >= 0) close(fds[i]);
    }

    printf("PASS: %d concurrent subscribers torn down cleanly\n", connected);
    return 0;
}
