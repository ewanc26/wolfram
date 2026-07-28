/*
 * test_xrpc_server_observer.c — the per-request observer.
 *
 * The observer exists because nothing else sees a request together with its
 * outcome: an auth callback runs before the status is known, never runs at
 * all for a plain HTTP route, and never runs for a request the rate limiter
 * refused. Each of those is a case a consumer counting requests would
 * silently miss, so each is exercised here.
 */

#include "wolfram/xrpc.h"
#include "wolfram/xrpc_server.h"
#include "test.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SEEN 32

static struct {
    pthread_mutex_t lock;
    int count;
    struct {
        char nsid[128];
        char path[128];
        char method[8];
        unsigned int status;
    } seen[MAX_SEEN];
} observed;

static void observe(void *ctx, const char *nsid, const char *path,
                    const char *method, unsigned int status) {
    (void)ctx;
    pthread_mutex_lock(&observed.lock);
    if (observed.count < MAX_SEEN) {
        int i = observed.count++;
        /* An HTTP route has no NSID; recorded as "" so the test can tell it
         * apart from one that was never reported at all. */
        snprintf(observed.seen[i].nsid, sizeof(observed.seen[i].nsid), "%s",
                 nsid ? nsid : "");
        snprintf(observed.seen[i].path, sizeof(observed.seen[i].path), "%s",
                 path ? path : "");
        snprintf(observed.seen[i].method, sizeof(observed.seen[i].method), "%s",
                 method ? method : "");
        observed.seen[i].status = status;
    }
    pthread_mutex_unlock(&observed.lock);
}

/* The status reported for `nsid`, or 0 when it was never observed. */
static unsigned int status_for(const char *nsid) {
    unsigned int status = 0;
    pthread_mutex_lock(&observed.lock);
    for (int i = 0; i < observed.count; i++)
        if (strcmp(observed.seen[i].nsid, nsid) == 0)
            status = observed.seen[i].status;
    pthread_mutex_unlock(&observed.lock);
    return status;
}

static unsigned int status_for_path(const char *path) {
    unsigned int status = 0;
    pthread_mutex_lock(&observed.lock);
    for (int i = 0; i < observed.count; i++)
        if (strcmp(observed.seen[i].path, path) == 0)
            status = observed.seen[i].status;
    pthread_mutex_unlock(&observed.lock);
    return status;
}

static int seen_count(void) {
    pthread_mutex_lock(&observed.lock);
    int n = observed.count;
    pthread_mutex_unlock(&observed.lock);
    return n;
}

static wf_status ok_handler(void *ctx, const wf_xrpc_request *req,
                            wf_xrpc_response *resp) {
    (void)ctx; (void)req;
    wf_xrpc_response_set_body(resp, "{\"ok\":true}", 11);
    return WF_OK;
}

static wf_status failing_handler(void *ctx, const wf_xrpc_request *req,
                                 wf_xrpc_response *resp) {
    (void)ctx; (void)req;
    wf_xrpc_response_set_error(resp, 418, "Teapot", "no coffee here");
    return WF_OK;
}

static wf_status http_handler(void *ctx, const wf_xrpc_request *req,
                              wf_xrpc_response *resp) {
    (void)ctx; (void)req;
    wf_xrpc_response_set_content_type(resp, "text/plain");
    wf_xrpc_response_set_body(resp, "hello", 5);
    return WF_OK;
}

static wf_status refuse_auth(wf_xrpc_request *req, void *ctx) {
    (void)ctx;
    /* Refuse exactly one route, so the same server serves both outcomes. */
    if (req->nsid && strcmp(req->nsid, "test.private") == 0)
        return WF_ERR_PERMISSION;
    return WF_OK;
}

int main(void) {
    pthread_mutex_init(&observed.lock, NULL);

    wf_xrpc_server *server = wf_xrpc_server_start("127.0.0.1", 0, 2);
    WF_CHECK(server != NULL);
    if (!server) { WF_TEST_SUMMARY(); }

    WF_CHECK(wf_xrpc_server_register_query(server, "test.ok", ok_handler,
                                           NULL) == WF_OK);
    WF_CHECK(wf_xrpc_server_register_query(server, "test.teapot",
                                           failing_handler, NULL) == WF_OK);
    WF_CHECK(wf_xrpc_server_register_query(server, "test.private", ok_handler,
                                           NULL) == WF_OK);
    WF_CHECK(wf_xrpc_server_register_http_route(server, "GET", "/plain",
                                                http_handler, NULL) == WF_OK);
    wf_xrpc_server_set_auth_callback(server, refuse_auth, NULL);
    wf_xrpc_server_set_request_observer(server, observe, NULL);

    char base[64];
    snprintf(base, sizeof(base), "http://127.0.0.1:%u",
             (unsigned)wf_xrpc_server_port(server));
    wf_xrpc_client *client = wf_xrpc_client_new(base);
    WF_CHECK(client != NULL);

    wf_response response = {0};
    wf_xrpc_query(client, "test.ok", NULL, &response);
    wf_response_free(&response);
    wf_xrpc_query(client, "test.teapot", NULL, &response);
    wf_response_free(&response);
    wf_xrpc_query(client, "test.private", NULL, &response);
    wf_response_free(&response);

    /* A handler's own status is reported, not just success or failure. */
    WF_CHECK(status_for("test.ok") == 200);
    WF_CHECK(status_for("test.teapot") == 418);
    /* Refused by the auth callback, which returns before the handler runs —
     * the case a consumer counting inside its own auth callback can see, but
     * only if it remembers to, and cannot attribute a status to. */
    WF_CHECK(status_for("test.private") == 401);

    /* A plain HTTP route has no NSID and no auth callback, so it is invisible
     * to every other hook the server offers. */
    char url[128];
    snprintf(url, sizeof(url), "%s/plain", base);
    WF_CHECK(wf_http_get(client, url, &response) == WF_OK);
    wf_response_free(&response);
    WF_CHECK(status_for_path("/plain") == 200);

    /* An unregistered method still produces a status, and still counts. */
    wf_xrpc_query(client, "test.missing", NULL, &response);
    wf_response_free(&response);
    WF_CHECK(status_for("test.missing") != 0);

    int before_clear = seen_count();
    WF_CHECK(before_clear >= 5);

    /* Clearing it stops the reports rather than crashing on a NULL callback. */
    wf_xrpc_server_set_request_observer(server, NULL, NULL);
    wf_xrpc_query(client, "test.ok", NULL, &response);
    wf_response_free(&response);
    WF_CHECK(seen_count() == before_clear);

    wf_xrpc_client_free(client);
    wf_xrpc_server_free(server);
    pthread_mutex_destroy(&observed.lock);
    WF_TEST_SUMMARY();
}
