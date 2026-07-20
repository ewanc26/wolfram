/*
 * test_sync_publish_e2e.c — server-side end-to-end test for firehose event
 * production.
 *
 * Starts a local XRPC server, registers a `com.atproto.sync.subscribeRepos`
 * WebSocket route whose handler streams two framed events built with
 * wf_sync_publish_event (a `com.atproto.label.subscribeLabels` #labels batch
 * and a #commit), opens a raw client socket, performs the RFC 6455 handshake,
 * reads the two binary frames, decodes each with wf_subscribe_decode_frame, and
 * asserts the decoded events match what was published. Mirrors the
 * suspend/resume worker-thread pattern from test_xrpc_server_ws.c, but over the
 * real sync_publish ↔ sync_subscribe round-trip. Offline, in-process mock
 * server only; no tokens, keys, or secrets are logged or committed.
 */

#include "wolfram/xrpc_server.h"
#include "wolfram/sync_publish.h"
#include "wolfram/sync_subscribe.h"
#include "wolfram/repo/cid.h"
#include "wolfram/crypto.h"
#include "test.h"

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
    /* Server→client frames must NOT be masked (RFC 6455 §5.1). */
    if (h[1] & 0x80) return -1;
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

/* ------------------------------------------------------------------ */
/* Event builders                                                      */
/* ------------------------------------------------------------------ */

static wf_cid sample_cid(unsigned char seed) {
    unsigned char block[16];
    for (int i = 0; i < 16; i++) block[i] = (unsigned char)(seed + i);
    wf_cid c;
    wf_cid_of_block(block, sizeof(block), &c);
    return c;
}

/* Build a #labels event (the new subscribeLabels path under test). */
static wf_subscribe_event build_labels_event(void) {
    wf_subscribe_event ev = {0};
    ev.type = WF_SUBSCRIBE_EVENT_LABELS;
    ev.seq = 1001;
    ev.data.labels.seq = 1001;
    ev.data.labels.labels_count = 2;
    ev.data.labels.labels = calloc(2, sizeof(wf_label));

    wf_label *a = &ev.data.labels.labels[0];
    a->ver = 1; a->has_ver = 1;
    a->src = strdup("did:plc:labeler");
    a->uri = strdup("at://did:plc:alice/app.bsky.feed.post/abc");
    a->cid = strdup("bafyreictrgtcg7wph56xjgu3ke7c2tjjgbj5dssviw6staozglsfg5nlu");
    a->has_cid = 1;
    a->val = strdup("!no-unauthenticated");
    a->neg = 1; a->has_neg = 1;
    a->cts = strdup("2024-01-02T03:04:05.000Z");
    a->exp = strdup("2025-01-02T03:04:05.000Z");
    a->has_exp = 1;
    unsigned char sig_bytes[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    (void)wf_crypto_base64url_encode(sig_bytes, sizeof(sig_bytes), &a->sig);
    a->has_sig = 1;

    wf_label *b = &ev.data.labels.labels[1];
    b->src = strdup("did:plc:labeler");
    b->uri = strdup("at://did:plc:bob/app.bsky.graph.follow/xyz");
    b->val = strdup("spam");
    b->cts = strdup("2024-03-04T05:06:07.000Z");

    return ev;
}

/* Build a #commit event (an existing production path, for breadth). */
static wf_subscribe_event build_commit_event(void) {
    wf_subscribe_event ev = {0};
    ev.type = WF_SUBSCRIBE_EVENT_COMMIT;
    ev.seq = 7;
    ev.data.commit.seq = 7;
    snprintf(ev.data.commit.did, sizeof(ev.data.commit.did), "did:plc:e2ecommit");
    ev.data.commit.commit_cid = sample_cid(1);
    snprintf(ev.data.commit.rev, sizeof(ev.data.commit.rev), "3kf2e2e");
    snprintf(ev.data.commit.since, sizeof(ev.data.commit.since), "3kf2e2e1");
    snprintf(ev.data.commit.time, sizeof(ev.data.commit.time),
             "2024-05-06T07:08:09.000Z");

    static const unsigned char blocks[8] = {0xaa, 0xbb, 0xcc, 0xdd,
                                            0xee, 0xff, 0x01, 0x02};
    ev.data.commit.blocks = malloc(sizeof(blocks));
    memcpy(ev.data.commit.blocks, blocks, sizeof(blocks));
    ev.data.commit.blocks_len = sizeof(blocks);

    static const char *paths[] = {"app.bsky.feed.post/abc",
                                  "app.bsky.feed.post/def"};
    ev.data.commit.ops_count = 2;
    ev.data.commit.ops = calloc(2, sizeof(wf_subscribe_repo_op));
    strcpy(ev.data.commit.ops[0].action, "create");
    ev.data.commit.ops[0].path = strdup(paths[0]);
    ev.data.commit.ops[0].has_cid = 1;
    ev.data.commit.ops[0].cid = sample_cid(10);
    ev.data.commit.ops[0].has_prev = 0;
    strcpy(ev.data.commit.ops[1].action, "delete");
    ev.data.commit.ops[1].path = strdup(paths[1]);
    ev.data.commit.ops[1].has_cid = 0;
    ev.data.commit.ops[1].has_prev = 1;
    ev.data.commit.ops[1].prev = sample_cid(20);

    return ev;
}

static void free_labels_event(wf_subscribe_event *ev) {
    for (size_t i = 0; i < ev->data.labels.labels_count; i++) {
        wf_label *lab = &ev->data.labels.labels[i];
        free(lab->src); free(lab->uri); free(lab->cid); free(lab->val);
        free(lab->cts); free(lab->exp); free(lab->sig);
    }
    free(ev->data.labels.labels);
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

    if (a->type == WF_SUBSCRIBE_EVENT_LABELS) {
        const wf_subscribe_labels *x = &a->data.labels, *y = &b->data.labels;
        if (x->labels_count != y->labels_count) return 0;
        for (size_t i = 0; i < x->labels_count; i++) {
            const wf_label *lx = &x->labels[i], *ly = &y->labels[i];
            if (lx->has_ver != ly->has_ver) return 0;
            if (lx->has_ver && lx->ver != ly->ver) return 0;
            if (strcmp(lx->src, ly->src)) return 0;
            if (strcmp(lx->uri, ly->uri)) return 0;
            if (lx->has_cid != ly->has_cid) return 0;
            if (lx->has_cid && strcmp(lx->cid, ly->cid)) return 0;
            if (strcmp(lx->val, ly->val)) return 0;
            if (lx->has_neg != ly->has_neg) return 0;
            if (lx->has_neg && !!lx->neg != !!ly->neg) return 0;
            if (strcmp(lx->cts, ly->cts)) return 0;
            if (lx->has_exp != ly->has_exp) return 0;
            if (lx->has_exp && strcmp(lx->exp, ly->exp)) return 0;
            if (lx->has_sig != ly->has_sig) return 0;
            if (lx->has_sig && strcmp(lx->sig, ly->sig)) return 0;
        }
        return 1;
    }

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
            if (x->ops[i].has_cid &&
                !cid_equal(&x->ops[i].cid, &y->ops[i].cid)) return 0;
            if (x->ops[i].has_prev != y->ops[i].has_prev) return 0;
            if (x->ops[i].has_prev &&
                !cid_equal(&x->ops[i].prev, &y->ops[i].prev)) return 0;
        }
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* WebSocket streaming handler — publishes two framed events then closes */
/* ------------------------------------------------------------------ */

struct ws_thread_arg {
    wf_xrpc_ws_stream *stream;
    wf_subscribe_event *labels;
    wf_subscribe_event *commit;
};

static void *ws_publisher(void *arg) {
    struct ws_thread_arg *a = (struct ws_thread_arg *)arg;

    /* Frame 0: #labels batch built by wf_sync_publish_event. */
    unsigned char *buf0 = NULL; size_t len0 = 0;
    if (wf_sync_publish_event(a->labels, &buf0, &len0) == WF_OK && buf0) {
        if (wf_xrpc_server_ws_send(a->stream, buf0, len0) != WF_OK)
            fprintf(stderr, "WARN: ws_send labels frame failed\n");
        free(buf0);
    } else {
        fprintf(stderr, "WARN: wf_sync_publish_event (labels) failed\n");
    }

    /* Frame 1: #commit built by wf_sync_publish_event. */
    unsigned char *buf1 = NULL; size_t len1 = 0;
    if (wf_sync_publish_event(a->commit, &buf1, &len1) == WF_OK && buf1) {
        if (wf_xrpc_server_ws_send(a->stream, buf1, len1) != WF_OK)
            fprintf(stderr, "WARN: ws_send commit frame failed\n");
        free(buf1);
    } else {
        fprintf(stderr, "WARN: wf_sync_publish_event (commit) failed\n");
    }

    wf_xrpc_server_ws_close(a->stream, 1000);
    wf_xrpc_server_ws_release(a->stream);
    free(a);
    return NULL;
}

static wf_status ws_handler(void *ctx, const wf_xrpc_request *req,
                            wf_xrpc_ws_stream *stream) {
    (void)req;
    struct ws_thread_arg *a = malloc(sizeof(*a));
    if (!a) return WF_ERR_ALLOC;
    a->stream = stream;
    a->labels = ((struct ws_thread_arg *)ctx)->labels;
    a->commit = ((struct ws_thread_arg *)ctx)->commit;

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
/* Test runner                                                         */
/* ------------------------------------------------------------------ */

#define WS_KEY "dGhlIHNhbXBsZSBub25jZQ=="

static int run_test(void) {
    wf_xrpc_server *server = NULL;
    int failures = 0;
    int fd = -1;
    uint16_t port;

    wf_subscribe_event labels = build_labels_event();
    wf_subscribe_event commit = build_commit_event();

    struct ws_thread_arg handler_ctx = { NULL, &labels, &commit };

    server = wf_xrpc_server_start("127.0.0.1", 0, 1);
    if (!server) {
        fprintf(stderr, "FAIL: wf_xrpc_server_start returned NULL\n");
        free_labels_event(&labels); free_commit_event(&commit);
        return 1;
    }
    port = wf_xrpc_server_port(server);
    if (port == 0) {
        fprintf(stderr, "FAIL: server port is 0\n");
        wf_xrpc_server_free(server);
        free_labels_event(&labels); free_commit_event(&commit);
        return 1;
    }

    if (wf_xrpc_server_register_ws(server, "com.atproto.sync.subscribeRepos",
                                   ws_handler, &handler_ctx) != WF_OK) {
        fprintf(stderr, "FAIL: register WS endpoint\n");
        wf_xrpc_server_free(server);
        free_labels_event(&labels); free_commit_event(&commit);
        return 1;
    }

    /* --- Open a raw TCP connection and perform the WS handshake --- */
    fd = test_connect(port);
    if (fd < 0) {
        fprintf(stderr, "FAIL: connect to server\n");
        wf_xrpc_server_free(server);
        free_labels_event(&labels); free_commit_event(&commit);
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
                      "\r\n", (unsigned)port);
    if (test_write_all(fd, req, (size_t)nr) != 0) {
        fprintf(stderr, "FAIL: write handshake request\n");
        close(fd);
        wf_xrpc_server_free(server);
        free_labels_event(&labels); free_commit_event(&commit);
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
        failures++;
        goto cleanup;
    }
    printf("PASS: WS handshake accepted\n");

    /* Verify the Sec-WebSocket-Accept value per RFC 6455 §1.3. */
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
    while (*val && *val != '\r' && *val != '\n' && k < sizeof(got_accept) - 1)
        got_accept[k++] = *val++;
    got_accept[k] = '\0';

    char concat[256];
    unsigned char digest[SHA_DIGEST_LENGTH];
    char expect[40];
    snprintf(concat, sizeof(concat), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
             WS_KEY);
    SHA1((const unsigned char *)concat, strlen(concat), digest);
    test_base64_encode(digest, SHA_DIGEST_LENGTH, expect);
    if (strcmp(got_accept, expect) != 0) {
        fprintf(stderr, "FAIL: Sec-WebSocket-Accept mismatch: got '%s' want '%s'\n",
                got_accept, expect);
        failures++;
        goto cleanup;
    }
    printf("PASS: Sec-WebSocket-Accept verified\n");

    /* Frame 0: #labels. */
    unsigned char *f0 = NULL; size_t f0len = 0;
    int op0 = test_read_ws_frame(fd, &f0, &f0len, 3000);
    if (op0 != 0x2) {
        fprintf(stderr, "FAIL: expected binary frame 0 (labels), got opcode %d\n",
                op0);
        failures++;
        goto cleanup;
    }
    wf_subscribe_event dec_labels = {0};
    if (wf_subscribe_decode_frame(f0, f0len, &dec_labels) != WF_OK) {
        fprintf(stderr, "FAIL: decode labels frame\n");
        failures++;
        goto cleanup;
    }
    if (!event_equal(&labels, &dec_labels)) {
        fprintf(stderr, "FAIL: decoded #labels event does not match published\n");
        failures++;
    } else {
        printf("PASS: published #labels frame round-trips through server\n");
    }
    wf_subscribe_event_free(&dec_labels);
    free(f0);

    /* Frame 1: #commit. */
    unsigned char *f1 = NULL; size_t f1len = 0;
    int op1 = test_read_ws_frame(fd, &f1, &f1len, 3000);
    if (op1 != 0x2) {
        fprintf(stderr, "FAIL: expected binary frame 1 (commit), got opcode %d\n",
                op1);
        failures++;
        goto cleanup;
    }
    wf_subscribe_event dec_commit = {0};
    if (wf_subscribe_decode_frame(f1, f1len, &dec_commit) != WF_OK) {
        fprintf(stderr, "FAIL: decode commit frame\n");
        failures++;
        goto cleanup;
    }
    if (!event_equal(&commit, &dec_commit)) {
        fprintf(stderr, "FAIL: decoded #commit event does not match published\n");
        failures++;
    } else {
        printf("PASS: published #commit frame round-trips through server\n");
    }
    wf_subscribe_event_free(&dec_commit);
    free(f1);

    /* Stream should terminate with a close frame. */
    unsigned char *cf = NULL; size_t cf_len = 0;
    int opc = test_read_ws_frame(fd, &cf, &cf_len, 3000);
    if (opc == 0x8) {
        printf("PASS: stream terminated with a close frame\n");
    } else {
        fprintf(stderr, "FAIL: expected close frame, got opcode %d\n", opc);
        failures++;
    }
    free(cf);

cleanup:
    if (fd >= 0) close(fd);
    wf_xrpc_server_free(server);
    free_labels_event(&labels);
    free_commit_event(&commit);
    if (failures == 0)
        printf("PASS: sync_publish → subscribeRepos server-side e2e\n");
    return failures == 0 ? 0 : 1;
}

int main(void) {
    return run_test();
}
