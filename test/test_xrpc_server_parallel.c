/*
 * test_xrpc_server_parallel.c — integration test that proves the XRPC
 * server actually handles requests concurrently when started with a
 * thread pool (thread_count > 1).
 *
 * Starts a local server with 4 worker threads, registers a query handler
 * that records how many requests execute simultaneously, then fires N
 * requests from N client threads. Asserts that the thread pool dispatches
 * truly concurrent handlers (max_concurrent > 1) and that all requests
 * succeed, and exercises the rate limiter under concurrent load.
 */

#include "wolfram/xrpc.h"
#include "wolfram/xrpc_server.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CONCURRENT_REQUESTS 8
#define HANDLER_DELAY_MS 50

/* ------------------------------------------------------------------ */
/* Concurrent handler                                                   */
/* ------------------------------------------------------------------ */

struct parallel_ctx {
    atomic_int active;
    atomic_int max_concurrent;
    atomic_int total;
};

static wf_status parallel_handler(void *ctx, const wf_xrpc_request *req,
                                  wf_xrpc_response *resp) {
    (void)req;
    struct parallel_ctx *pctx = (struct parallel_ctx *)ctx;

    int cur = atomic_fetch_add(&pctx->active, 1) + 1;
    int prev_max = atomic_load(&pctx->max_concurrent);
    while (cur > prev_max) {
        if (atomic_compare_exchange_weak(&pctx->max_concurrent, &prev_max, cur))
            break;
    }

    /* Hold for a slice so overlapping requests are observable. */
    usleep(HANDLER_DELAY_MS * 1000);

    atomic_fetch_sub(&pctx->active, 1);
    atomic_fetch_add(&pctx->total, 1);

    char body[64];
    int n = snprintf(body, sizeof(body), "{\"ok\":true,\"id\":%d}",
                     atomic_load(&pctx->total));
    wf_xrpc_response_set_body(resp, body, (size_t)n);
    return WF_OK;
}

/* ------------------------------------------------------------------ */
/* Client thread                                                       */
/* ------------------------------------------------------------------ */

struct client_arg {
    const char *base_url;
    int index;
    int failed;
    long http_status;
};

static void *client_thread(void *arg) {
    struct client_arg *ca = (struct client_arg *)arg;
    wf_xrpc_client *client = wf_xrpc_client_new(ca->base_url);
    if (!client) {
        ca->failed = 1;
        return NULL;
    }

    wf_response res = {0};
    wf_status s = wf_xrpc_query(client, "io.example.parallel", NULL, &res);
    if (s != WF_OK) {
        fprintf(stderr, "client %d: query failed (status=%d http=%ld)\n",
                ca->index, (int)s, res.status);
        ca->failed = 1;
    } else {
        ca->http_status = res.status;
    }
    wf_response_free(&res);
    wf_xrpc_client_free(client);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Main test                                                           */
/* ------------------------------------------------------------------ */

static int run_test(void) {
    int failures = 0;
    char base_url[64];
    wf_xrpc_server *server = NULL;

    /* Start with 4 worker threads — the key assertion of this test. */
    server = wf_xrpc_server_start("127.0.0.1", 0, 4);
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

    struct parallel_ctx pctx = {.active = 0, .max_concurrent = 0, .total = 0};

    if (wf_xrpc_server_register_query(server, "io.example.parallel",
                                      parallel_handler, &pctx) != WF_OK) {
        fprintf(stderr, "FAIL: register query handler\n");
        wf_xrpc_server_free(server);
        return 1;
    }

    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%u", (unsigned)port);

    /* Launch CONCURRENT_REQUESTS client threads. */
    pthread_t threads[CONCURRENT_REQUESTS];
    struct client_arg args[CONCURRENT_REQUESTS];
    for (int i = 0; i < CONCURRENT_REQUESTS; i++) {
        args[i].base_url = base_url;
        args[i].index = i;
        args[i].failed = 0;
        args[i].http_status = 0;
        if (pthread_create(&threads[i], NULL, client_thread, &args[i]) != 0) {
            fprintf(stderr, "FAIL: pthread_create %d\n", i);
            args[i].failed = 1;
        }
    }

    for (int i = 0; i < CONCURRENT_REQUESTS; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Every request must succeed. */
    for (int i = 0; i < CONCURRENT_REQUESTS; i++) {
        if (args[i].failed) {
            fprintf(stderr, "FAIL: client %d failed\n", i);
            failures++;
        } else if (args[i].http_status != 200) {
            fprintf(stderr, "FAIL: client %d http_status=%ld\n", i,
                    args[i].http_status);
            failures++;
        }
    }

    /* The handler must have seen more than 1 active request at once —
     * that is the whole point of the thread pool. */
    int max = atomic_load(&pctx.max_concurrent);
    int total = atomic_load(&pctx.total);
    if (max < 2) {
        fprintf(stderr,
                "FAIL: max_concurrent=%d (expected >= 2 with thread_count=4)\n",
                max);
        failures++;
    } else {
        printf("PASS: %d concurrent requests, max_concurrent=%d\n",
               CONCURRENT_REQUESTS, max);
    }

    if (total != CONCURRENT_REQUESTS) {
        fprintf(stderr, "FAIL: total=%d (expected %d)\n", total,
                CONCURRENT_REQUESTS);
        failures++;
    }

    /* Rate limiter: configure a tight limiter, fire concurrent requests,
     * and verify some get rejected with 429. */
    {
        wf_rate_limiter *rl =
            wf_rate_limiter_new(2, 60, 16); /* 2 points, 60s window */
        if (!rl) {
            fprintf(stderr, "FAIL: rate_limiter_new\n");
            failures++;
        } else {
            wf_xrpc_server_set_rate_limiter(server, rl);

            int rejected = 0;
            int accepted = 0;
            atomic_store(&pctx.active, 0);
            atomic_store(&pctx.max_concurrent, 0);
            atomic_store(&pctx.total, 0);

            for (int i = 0; i < CONCURRENT_REQUESTS; i++) {
                args[i].base_url = base_url;
                args[i].index = i + 100;
                args[i].failed = 0;
                args[i].http_status = 0;
                if (pthread_create(&threads[i], NULL, client_thread,
                                   &args[i]) != 0) {
                    args[i].failed = 1;
                }
            }
            for (int i = 0; i < CONCURRENT_REQUESTS; i++) {
                pthread_join(threads[i], NULL);
                if (args[i].failed) {
                    /* Could be a 429 transport error or a real failure. */
                    rejected++;
                } else if (args[i].http_status == 429) {
                    rejected++;
                } else if (args[i].http_status == 200) {
                    accepted++;
                }
            }

            if (rejected == 0) {
                fprintf(stderr,
                        "FAIL: rate limiter rejected 0 (expected >0 with "
                        "limit=2, requests=%d)\n",
                        CONCURRENT_REQUESTS);
                failures++;
            } else {
                printf("PASS: rate limiter rejected %d, accepted %d\n",
                       rejected, accepted);
            }

            wf_xrpc_server_set_rate_limiter(server, NULL);
            wf_rate_limiter_free(rl);
        }
    }

    wf_xrpc_server_free(server);

    if (failures == 0) {
        printf("PASS: XRPC server parallel requests (thread pool)\n");
        return 0;
    }
    return 1;
}

int main(void) {
    return run_test();
}
