/*
 * test_xrpc_server_sse.c — integration test for SSE streaming on the XRPC
 * server module.
 *
 * Starts a local XRPC server, registers SSE endpoints whose handlers stream
 * frames from a worker thread (the canonical libmicrohttpd suspend/resume
 * pattern), connects with the XRPC client, and verifies the events arrive in
 * order and the connection is eventually closed and cleaned up without hangs.
 */

#include "wolfram/xrpc.h"
#include "wolfram/xrpc_server.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Streaming SSE handler — pushes 3 events from a worker thread        */
/* ------------------------------------------------------------------ */

struct sse_thread_arg {
    wf_xrpc_sse_stream *stream;
    int                 count;       /* number of events to emit */
    int                 delay_ms;    /* delay between events */
};

static void *sse_streamer(void *arg) {
    struct sse_thread_arg *a = (struct sse_thread_arg *)arg;
    char name[16];
    char payload[32];

    for (int i = 0; i < a->count; i++) {
        if (a->delay_ms > 0) {
            usleep((useconds_t)a->delay_ms * 1000);
        }
        snprintf(name, sizeof(name), "evt%d", i);
        snprintf(payload, sizeof(payload), "message-%d", i);
        if (wf_xrpc_server_sse_send(a->stream, name, payload) != WF_OK) {
            fprintf(stderr, "WARN: sse_send failed on event %d\n", i);
        }
    }
    wf_xrpc_server_sse_close(a->stream);
    free(a);
    return NULL;
}

static wf_status streaming_sse_handler(void *ctx, const wf_xrpc_request *req,
                                       wf_xrpc_sse_stream *stream) {
    struct sse_thread_arg *a;
    pthread_t tid;
    (void)ctx;
    (void)req;

    a = (struct sse_thread_arg *)malloc(sizeof(*a));
    if (!a) {
        return WF_ERR_ALLOC;
    }
    a->stream = stream;
    a->count = 3;
    a->delay_ms = 20;

    /* Spawn a detached worker that streams frames; the handler must return
     * promptly so libmicrohttpd can park the connection. */
    if (pthread_create(&tid, NULL, sse_streamer, a) != 0) {
        free(a);
        return WF_ERR_ALLOC;
    }
    pthread_detach(tid);
    return WF_OK;
}

/* ------------------------------------------------------------------ */
/* Single-shot SSE handler — sends one frame then closes               */
/* ------------------------------------------------------------------ */

static wf_status single_shot_sse_handler(void *ctx, const wf_xrpc_request *req,
                                         wf_xrpc_sse_stream *stream) {
    struct sse_thread_arg *a;
    pthread_t tid;
    (void)ctx;
    (void)req;

    a = (struct sse_thread_arg *)malloc(sizeof(*a));
    if (!a) {
        return WF_ERR_ALLOC;
    }
    a->stream = stream;
    a->count = 1;
    a->delay_ms = 0;

    if (pthread_create(&tid, NULL, sse_streamer, a) != 0) {
        free(a);
        return WF_ERR_ALLOC;
    }
    pthread_detach(tid);
    return WF_OK;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Test runner                                                         */
/* ------------------------------------------------------------------ */

static int run_test(void) {
    wf_xrpc_server *server = NULL;
    wf_xrpc_client *client = NULL;
    wf_response res = {0};
    int failures = 0;
    char base_url[64];

    server = wf_xrpc_server_start("127.0.0.1", 0, 1);
    if (!server) {
        fprintf(stderr, "FAIL: wf_xrpc_server_start returned NULL\n");
        return 1;
    }

    uint16_t port = wf_xrpc_server_port(server);
    if (port == 0) {
        fprintf(stderr, "FAIL: server port is 0\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    if (wf_xrpc_server_register_sse(server, "io.example.stream",
                                    streaming_sse_handler, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: register streaming SSE\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    if (wf_xrpc_server_register_sse(server, "io.example.once",
                                    single_shot_sse_handler, NULL) != WF_OK) {
        fprintf(stderr, "FAIL: register single-shot SSE\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%u", (unsigned)port);
    client = wf_xrpc_client_new(base_url);
    if (!client) {
        fprintf(stderr, "FAIL: wf_xrpc_client_new\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    /* Test 1: streaming SSE — 3 events in order */
    {
        wf_status s = wf_xrpc_query(client, "io.example.stream", NULL, &res);
        if (s != WF_OK) {
            fprintf(stderr, "FAIL: streaming query (status=%d http=%ld)\n",
                    (int)s, res.status);
            failures++;
        } else if (res.status != 200) {
            fprintf(stderr, "FAIL: streaming status=%ld\n", res.status);
            failures++;
        } else {
            const char *body = res.body;
            size_t len = res.body_len;
            size_t p1, p2, p3;

            p1 = (body && strstr(body, "data: message-0")) ?
                 (size_t)(strstr(body, "data: message-0") - body) : (size_t)-1;
            p2 = (body && strstr(body, "data: message-1")) ?
                 (size_t)(strstr(body, "data: message-1") - body) : (size_t)-1;
            p3 = (body && strstr(body, "data: message-2")) ?
                 (size_t)(strstr(body, "data: message-2") - body) : (size_t)-1;

            if (p1 == (size_t)-1 || p2 == (size_t)-1 || p3 == (size_t)-1) {
                fprintf(stderr, "FAIL: streaming missing event(s): '%.*s'\n",
                        (int)len, body ? body : "");
                failures++;
            } else if (!(p1 < p2 && p2 < p3)) {
                fprintf(stderr, "FAIL: streaming events out of order: "
                        "p1=%zu p2=%zu p3=%zu\n", p1, p2, p3);
                failures++;
            } else if (!strstr(body, "event: evt0") ||
                       !strstr(body, "event: evt1") ||
                       !strstr(body, "event: evt2")) {
                fprintf(stderr, "FAIL: streaming event names missing: '%.*s'\n",
                        (int)len, body ? body : "");
                failures++;
            } else {
                printf("PASS: SSE streamed 3 events in order\n");
            }
        }
        wf_response_free(&res);
    }

    /* Test 2: single-shot SSE — one event then clean close */
    {
        wf_status s = wf_xrpc_query(client, "io.example.once", NULL, &res);
        if (s != WF_OK) {
            fprintf(stderr, "FAIL: single-shot query (status=%d http=%ld)\n",
                    (int)s, res.status);
            failures++;
        } else if (res.status != 200) {
            fprintf(stderr, "FAIL: single-shot status=%ld\n", res.status);
            failures++;
        } else if (!res.body || !strstr(res.body, "data: message-0")) {
            fprintf(stderr, "FAIL: single-shot body missing event: '%.*s'\n",
                    (int)res.body_len, res.body ? res.body : "");
            failures++;
        } else {
            printf("PASS: single-shot SSE delivered and closed\n");
        }
        wf_response_free(&res);
    }

    /* Test 3: teardown must not hang or leak (suspended streams are resumed
     * and closed by wf_xrpc_server_free). */
    {
        wf_xrpc_client_free(client);
        client = NULL;
        wf_xrpc_server_free(server);   /* blocks until clean shutdown */
        server = NULL;
        printf("PASS: server teardown clean (no hang)\n");
    }

    if (failures == 0) {
        printf("PASS: XRPC server SSE streaming\n");
        return 0;
    }
    return 1;
}

int main(void) {
    return run_test();
}
