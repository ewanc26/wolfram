/*
 * test_sync_publish_server.c — server-side end-to-end test for firehose event
 * production, exercising the *real* sync_subscribe client.
 *
 * Starts a local XRPC server, registers a `com.atproto.sync.subscribeRepos`
 * WebSocket route whose handler streams two framed events built with
 * wf_sync_publish_event (a #commit and an #identity), then drives the actual
 * wf_subscribe_start client (libcurl WS transport) against that route. The
 * client's on_event callback decodes each frame back into a wf_subscribe_event
 * (the exact inverse decoder of sync_publish) and the test asserts the decoded
 * fields match what was published. This proves the full
 * sync_publish -> subscribeRepos -> sync_subscribe loop inside one process.
 *
 * Offline, in-process mock server only; no tokens, keys, or secrets are logged
 * or committed. Mirrors the suspend/resume worker-thread pattern from
 * test_xrpc_server_ws.c, but drives the real subscription client rather than a
 * hand-rolled raw socket reader.
 *
 * The wf_subscribe_start client relies on libcurl's WebSocket transport. When
 * the linked libcurl lacks WS support (e.g. the system libcurl on macOS) we
 * cannot drive the real client, so the test reports SKIP honestly instead of
 * faking a pass — the same inverse decoder over a raw WS client is covered by
 * test_sync_publish_e2e. This mirrors the degraded-path handling in
 * test_relay_server.c.
 */

#include "wolfram/xrpc_server.h"
#include "wolfram/sync_publish.h"
#include "wolfram/sync_subscribe.h"
#include "wolfram/websocket.h"
#include "wolfram/repo/cid.h"
#include "wolfram/crypto.h"
#include "test.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Number of distinct events the server publishes and the client must receive
 * before stopping the subscription. */
#define PUBLISHED_COUNT 2

/* ------------------------------------------------------------------ */
/* Event builders (inverse of the sync_subscribe decoder)              */
/* ------------------------------------------------------------------ */

static wf_cid sample_cid(unsigned char seed) {
    unsigned char block[16];
    for (int i = 0; i < 16; i++) block[i] = (unsigned char)(seed + i);
    wf_cid c;
    wf_cid_of_block(block, sizeof(block), &c);
    return c;
}

/* Build the #commit event the server will stream. */
static void build_commit_event(wf_subscribe_event *ev) {
    memset(ev, 0, sizeof(*ev));
    ev->type = WF_SUBSCRIBE_EVENT_COMMIT;
    ev->seq = 7;
    ev->data.commit.seq = 7;
    snprintf(ev->data.commit.did, sizeof(ev->data.commit.did), "did:plc:e2ecommit");
    ev->data.commit.commit_cid = sample_cid(1);
    snprintf(ev->data.commit.rev, sizeof(ev->data.commit.rev), "3kf2e2e");
    snprintf(ev->data.commit.since, sizeof(ev->data.commit.since), "3kf2e2e1");
    snprintf(ev->data.commit.time, sizeof(ev->data.commit.time),
             "2024-05-06T07:08:09.000Z");
    ev->data.commit.has_prev_data = 1;
    ev->data.commit.prev_data = sample_cid(99);

    static const unsigned char blocks[8] = {0xaa, 0xbb, 0xcc, 0xdd,
                                            0xee, 0xff, 0x01, 0x02};
    ev->data.commit.blocks = malloc(sizeof(blocks));
    memcpy(ev->data.commit.blocks, blocks, sizeof(blocks));
    ev->data.commit.blocks_len = sizeof(blocks);

    static const char *paths[] = {"app.bsky.feed.post/abc",
                                  "app.bsky.feed.post/def"};
    ev->data.commit.ops_count = 2;
    ev->data.commit.ops = calloc(2, sizeof(wf_subscribe_repo_op));
    strcpy(ev->data.commit.ops[0].action, "create");
    ev->data.commit.ops[0].path = strdup(paths[0]);
    ev->data.commit.ops[0].has_cid = 1;
    ev->data.commit.ops[0].cid = sample_cid(10);
    ev->data.commit.ops[0].has_prev = 0;
    strcpy(ev->data.commit.ops[1].action, "delete");
    ev->data.commit.ops[1].path = strdup(paths[1]);
    ev->data.commit.ops[1].has_cid = 0;  /* deletion: null cid */
    ev->data.commit.ops[1].has_prev = 1;
    ev->data.commit.ops[1].prev = sample_cid(20);
}

/* Build the #identity event the server will stream. */
static void build_identity_event(wf_subscribe_event *ev) {
    memset(ev, 0, sizeof(*ev));
    ev->type = WF_SUBSCRIBE_EVENT_IDENTITY;
    ev->seq = 11;
    ev->data.identity.seq = 11;
    snprintf(ev->data.identity.did, sizeof(ev->data.identity.did),
             "did:plc:e2eidentity");
    snprintf(ev->data.identity.time, sizeof(ev->data.identity.time),
             "2024-07-08T09:10:11.000Z");
    ev->data.identity.has_handle = 1;
    snprintf(ev->data.identity.handle, sizeof(ev->data.identity.handle),
             "alice.example.com");
}

static void free_commit_event(wf_subscribe_event *ev) {
    for (size_t i = 0; i < ev->data.commit.ops_count; i++)
        free(ev->data.commit.ops[i].path);
    free(ev->data.commit.ops);
    free(ev->data.commit.blocks);
}

/* Field-level equality over the two event shapes this test publishes. */
static int event_equal(const wf_subscribe_event *a, const wf_subscribe_event *b) {
    if (a->type != b->type) return 0;
    if (a->seq != b->seq) return 0;

    if (a->type == WF_SUBSCRIBE_EVENT_COMMIT) {
        const wf_subscribe_commit *x = &a->data.commit, *y = &b->data.commit;
        if (strcmp(x->did, y->did)) return 0;
        if (strcmp(x->rev, y->rev)) return 0;
        if (strcmp(x->since, y->since)) return 0;
        if (strcmp(x->time, y->time)) return 0;
        if (!cid_equal(&x->commit_cid, &y->commit_cid)) return 0;
        if (x->blocks_len != y->blocks_len) return 0;
        if (x->blocks_len && memcmp(x->blocks, y->blocks, x->blocks_len)) return 0;
        if (x->ops_count != y->ops_count) return 0;
        for (size_t i = 0; i < x->ops_count; i++) {
            if (strcmp(x->ops[i].action, y->ops[i].action)) return 0;
            if (strcmp(x->ops[i].path, y->ops[i].path)) return 0;
            if (x->ops[i].has_cid != y->ops[i].has_cid) return 0;
            if (x->ops[i].has_cid && !cid_equal(&x->ops[i].cid, &y->ops[i].cid)) return 0;
            if (x->ops[i].has_prev != y->ops[i].has_prev) return 0;
            if (x->ops[i].has_prev && !cid_equal(&x->ops[i].prev, &y->ops[i].prev)) return 0;
        }
        if (x->has_prev_data != y->has_prev_data) return 0;
        if (x->has_prev_data && !cid_equal(&x->prev_data, &y->prev_data)) return 0;
        return 1;
    }

    if (a->type == WF_SUBSCRIBE_EVENT_IDENTITY) {
        const wf_subscribe_identity *x = &a->data.identity, *y = &b->data.identity;
        if (strcmp(x->did, y->did)) return 0;
        if (strcmp(x->time, y->time)) return 0;
        if (x->has_handle != y->has_handle) return 0;
        if (x->has_handle && strcmp(x->handle, y->handle)) return 0;
        return 1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Shared state between the server-side publisher and the client       */
/* ------------------------------------------------------------------ */

struct test_ctx {
    wf_subscribe_event published[PUBLISHED_COUNT];
    uint16_t port;

    pthread_mutex_t lock;
    int matched[PUBLISHED_COUNT];   /* per published-event match flag */
    int received;                   /* total events delivered to client */
    int unexpected;                 /* events with no matching published seq */
    int done;                       /* client finished consuming */
    wf_subscribe_handle **handle_ptr; /* for the watchdog to stop the client */
};

/* ------------------------------------------------------------------ */
/* Raw-socket WS client (fallback when libcurl lacks WS)              */
/* ------------------------------------------------------------------ */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>

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

/* Read one server→client binary frame into a freshly malloc'd buffer.
 * Returns the opcode (0x2 binary, 0x8 close) or -1 on error. */
static int test_read_ws_frame(int fd, unsigned char **out, size_t *out_len,
                              int timeout_ms) {
    unsigned char h[2];
    if (test_read_exact(fd, h, 2, timeout_ms) != 0) return -1;
    int opcode = (int)(h[0] & 0x0f);
    uint64_t plen = (uint64_t)(h[1] & 0x7f);
    if (plen == 126) {
        unsigned char e[2];
        if (test_read_exact(fd, e, 2, 1000) != 0) return -1;
        plen = ((uint64_t)e[0] << 8) | (uint64_t)e[1];
    } else if (plen == 127) {
        unsigned char e[8];
        if (test_read_exact(fd, e, 8, 1000) != 0) return -1;
        plen = 0;
        for (int j = 0; j < 8; j++) plen = (plen << 8) | (uint64_t)e[j];
    }
    if (h[1] & 0x80) return -1; /* server frames must be unmasked */
    if (plen > 16 * 1024 * 1024) return -1;
    unsigned char *buf = malloc(plen ? plen : 1);
    if (!buf) return -1;
    if (plen && test_read_exact(fd, buf, (size_t)plen, 2000) != 0) {
        free(buf);
        return -1;
    }
    *out = buf;
    *out_len = (size_t)plen;
    return opcode;
}

#define WS_KEY "dGhlIHNhbXBsZSBub25jZQ=="

/* Fallback path: when the linked libcurl lacks WebSocket support we cannot
 * drive the real wf_subscribe_start client, but we can still prove the full
 * publish -> subscribeRepos -> decode loop using a raw WS client and the exact
 * decoder (wf_subscribe_decode_frame) the sync_subscribe client uses. */
static int run_raw_client_e2e(struct test_ctx *ctx) {
    int fd = test_connect(ctx->port);
    if (fd < 0) {
        fprintf(stderr, "FAIL: raw client connect to server\n");
        return 1;
    }

    char req[256];
    int nr = snprintf(req, sizeof(req),
                      "GET /xrpc/com.atproto.sync.subscribeRepos HTTP/1.1\r\n"
                      "Host: 127.0.0.1:%u\r\n"
                      "Upgrade: websocket\r\n"
                      "Connection: Upgrade\r\n"
                      "Sec-WebSocket-Key: " WS_KEY "\r\n"
                      "Sec-WebSocket-Version: 13\r\n"
                      "\r\n", (unsigned)ctx->port);
    if (test_write_all(fd, req, (size_t)nr) != 0) {
        fprintf(stderr, "FAIL: raw client handshake write\n");
        close(fd);
        return 1;
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
        fprintf(stderr, "FAIL: expected 101 Switching Protocols, got:\n%.*s\n",
                (int)hoff, hdr);
        close(fd);
        return 1;
    }

    int failures = 0;
    for (int i = 0; i < PUBLISHED_COUNT; i++) {
        unsigned char *frame = NULL;
        size_t flen = 0;
        int op = test_read_ws_frame(fd, &frame, &flen, 3000);
        if (op != 0x2) {
            fprintf(stderr, "FAIL: expected binary frame %d, got opcode %d\n",
                    i, op);
            failures++;
            free(frame);
            break;
        }
        wf_subscribe_event dec = {0};
        if (wf_subscribe_decode_frame(frame, flen, &dec) != WF_OK) {
            fprintf(stderr, "FAIL: decode frame %d\n", i);
            failures++;
            free(frame);
            break;
        }
        if (!event_equal(&ctx->published[i], &dec)) {
            fprintf(stderr, "FAIL: decoded frame %d does not match published\n", i);
            failures++;
        } else {
            printf("PASS: published #%s (seq %lld) round-trips through server\n",
                   dec.type == WF_SUBSCRIBE_EVENT_COMMIT ? "commit" : "identity",
                   (long long)dec.seq);
        }
        wf_subscribe_event_free(&dec);
        free(frame);
    }
    close(fd);
    return failures;
}

static int find_published_index(struct test_ctx *ctx, int64_t seq) {
    for (int i = 0; i < PUBLISHED_COUNT; i++)
        if (ctx->published[i].seq == seq) return i;
    return -1;
}

/* ------------------------------------------------------------------ */
/* WebSocket streaming handler — publishes framed events then closes  */
/* ------------------------------------------------------------------ */

struct ws_thread_arg {
    wf_xrpc_ws_stream *stream;
    struct test_ctx *ctx;
};

static void *ws_publisher(void *arg) {
    struct ws_thread_arg *a = (struct ws_thread_arg *)arg;

    for (int i = 0; i < PUBLISHED_COUNT; i++) {
        unsigned char *buf = NULL;
        size_t len = 0;
        if (wf_sync_publish_event(&a->ctx->published[i], &buf, &len) == WF_OK &&
            buf) {
            if (wf_xrpc_server_ws_send(a->stream, buf, len) != WF_OK)
                fprintf(stderr, "WARN: ws_send failed on event %d\n", i);
            free(buf);
        } else {
            fprintf(stderr, "WARN: wf_sync_publish_event failed on event %d\n", i);
        }
    }

    wf_xrpc_server_ws_close(a->stream, 1000);
    wf_xrpc_server_ws_release(a->stream);
    free(a);
    return NULL;
}

static wf_status ws_handler(void *ctx, const wf_xrpc_request *req,
                            wf_xrpc_ws_stream *stream) {
    (void)req;
    struct test_ctx *tctx = (struct test_ctx *)ctx;
    struct ws_thread_arg *a = malloc(sizeof(*a));
    if (!a) return WF_ERR_ALLOC;
    a->stream = stream;
    a->ctx = tctx;

    /* The WS handler runs on the server's upgrade worker thread; retain the
     * stream and hand it to a dedicated publisher thread so we return promptly
     * and let libmicrohttpd finish the handshake. */
    if (wf_xrpc_server_ws_retain(stream) != WF_OK) {
        free(a);
        return WF_ERR_INVALID_ARG;
    }
    pthread_t tid;
    if (pthread_create(&tid, NULL, ws_publisher, a) != 0) {
        wf_xrpc_server_ws_release(stream);
        free(a);
        return WF_ERR_ALLOC;
    }
    pthread_detach(tid);
    return WF_OK;
}

/* ------------------------------------------------------------------ */
/* sync_subscribe client                                               */
/* ------------------------------------------------------------------ */

static void on_event(const wf_subscribe_event *event, void *userdata) {
    struct test_ctx *ctx = (struct test_ctx *)userdata;
    pthread_mutex_lock(&ctx->lock);
    ctx->received++;
    int idx = find_published_index(ctx, event->seq);
    if (idx < 0) {
        ctx->unexpected++;
        fprintf(stderr, "WARN: client received event with unexpected seq %lld\n",
                (long long)event->seq);
    } else if (!ctx->matched[idx] && event_equal(&ctx->published[idx], event)) {
        ctx->matched[idx] = 1;
        printf("PASS: client decoded published #%s (seq %lld) and it matched\n",
               event->type == WF_SUBSCRIBE_EVENT_COMMIT ? "commit" : "identity",
               (long long)event->seq);
    } else if (!ctx->matched[idx]) {
        fprintf(stderr, "FAIL: decoded event seq %lld did not match published\n",
                (long long)event->seq);
    }

    if (ctx->received >= PUBLISHED_COUNT && ctx->handle_ptr && *ctx->handle_ptr)
        wf_subscribe_stop(*ctx->handle_ptr);
    pthread_mutex_unlock(&ctx->lock);
}

static void on_error(wf_status status, const char *msg, void *userdata) {
    (void)status;
    (void)msg;
    (void)userdata;
    /* Benign for this offline test; the watchdog/stop logic drives completion. */
}

static void *client_thread(void *arg) {
    struct test_ctx *ctx = (struct test_ctx *)arg;

    wf_subscribe_handle *handle = NULL;
    ctx->handle_ptr = &handle;

    char service[64];
    snprintf(service, sizeof(service), "ws://127.0.0.1:%u", (unsigned)ctx->port);

    wf_subscribe_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.service = service;
    opts.has_cursor = 0;
    opts.on_event = on_event;
    opts.on_error = on_error;
    opts.userdata = ctx;
    opts.max_retry_seconds = 1;
    opts.reconnect_delay_ms = 100;

    wf_subscribe_start(&opts, &handle);
    pthread_mutex_lock(&ctx->lock);
    ctx->done = 1;
    pthread_mutex_unlock(&ctx->lock);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Test runner                                                         */
/* ------------------------------------------------------------------ */

static int run_test(void) {
    struct test_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.port = 0;
    ctx.handle_ptr = NULL;
    ctx.done = 0;
    pthread_mutex_init(&ctx.lock, NULL);

    int failures = 0;

    build_commit_event(&ctx.published[0]);
    build_identity_event(&ctx.published[1]);

    wf_xrpc_server *server = wf_xrpc_server_start("127.0.0.1", 0, 1);
    if (!server) {
        fprintf(stderr, "FAIL: wf_xrpc_server_start returned NULL\n");
        failures = 1;
        goto cleanup_events;
    }
    ctx.port = wf_xrpc_server_port(server);
    if (ctx.port == 0) {
        fprintf(stderr, "FAIL: server port is 0\n");
        wf_xrpc_server_free(server);
        failures = 1;
        goto cleanup_events;
    }

    if (wf_xrpc_server_register_ws(server, "com.atproto.sync.subscribeRepos",
                                   ws_handler, &ctx) != WF_OK) {
        fprintf(stderr, "FAIL: register WS endpoint\n");
        wf_xrpc_server_free(server);
        failures = 1;
        goto cleanup_events;
    }

    if (!wf_websocket_supported()) {
        /* The linked libcurl lacks WebSocket (ws) protocol support, so the
         * real wf_subscribe_start client cannot connect. Drive the same
         * publish -> subscribeRepos -> decode loop with a raw WS client and the
         * exact decoder (wf_subscribe_decode_frame) the sync_subscribe client
         * uses, so the inverse encoder/decoder pair is still proven over a real
         * server WS route. Mirrors the degraded-path handling in
         * test_relay_server.c; test_sync_publish_e2e covers this same decoder. */
        printf("NOTE: linked libcurl lacks WebSocket (ws) support; driving the "
               "loop with a raw WS client + wf_subscribe_decode_frame (the "
               "sync_subscribe client's decoder) instead of wf_subscribe_start.\n");
        failures = run_raw_client_e2e(&ctx);
        wf_xrpc_server_free(server);
        goto cleanup_events;
    }

    /* Drive the real sync_subscribe client from a worker thread. */
    pthread_t cthr;
    if (pthread_create(&cthr, NULL, client_thread, &ctx) != 0) {
        fprintf(stderr, "FAIL: pthread_create client\n");
        wf_xrpc_server_free(server);
        failures = 1;
        goto cleanup_events;
    }

    /* Watchdog: bound the test so a delivery failure can never hang CI. If the
     * client has not finished within 10s, stop it; join regardless. */
    int done = 0;
    for (int i = 0; i < 100; i++) {
        struct timespec ts = {0, 100000000L}; /* 100ms */
        nanosleep(&ts, NULL);
        pthread_mutex_lock(&ctx.lock);
        done = ctx.done;
        pthread_mutex_unlock(&ctx.lock);
        if (done) break;
    }
    if (!done && ctx.handle_ptr && *ctx.handle_ptr)
        wf_subscribe_stop(*ctx.handle_ptr);
    pthread_join(cthr, NULL);

    pthread_mutex_lock(&ctx.lock);
    for (int i = 0; i < PUBLISHED_COUNT; i++) {
        if (!ctx.matched[i]) {
            fprintf(stderr, "FAIL: published event seq %lld was not matched\n",
                    (long long)ctx.published[i].seq);
            failures++;
        }
    }
    if (ctx.unexpected) {
        fprintf(stderr, "FAIL: client received %d unexpected event(s)\n",
                ctx.unexpected);
        failures++;
    }
    pthread_mutex_unlock(&ctx.lock);

    if (failures == 0)
        printf("PASS: sync_publish -> subscribeRepos -> sync_subscribe e2e\n");

    wf_xrpc_server_free(server);

cleanup_events:
    free_commit_event(&ctx.published[0]);
    pthread_mutex_destroy(&ctx.lock);
    if (failures == 0) printf("PASS: sync_publish_server\n");
    return failures == 0 ? 0 : 1;
}

int main(void) {
    return run_test();
}
