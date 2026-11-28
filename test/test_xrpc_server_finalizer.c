/*
 * test_xrpc_server_finalizer.c — the response finalizer.
 *
 * The finalizer exists so a consumer can put the same headers on every
 * response from one place (MetalBear's RateLimit-* headers are the first
 * use). That only works if no response gets past it, so each way the server
 * produces a response is exercised here over a raw socket, and the header the
 * finalizer adds is counted on the wire: exactly once on each, with exactly
 * one finalizer call per request.
 */

#define _POSIX_C_SOURCE 200809L

#include "wolfram/xrpc.h"
#include "wolfram/xrpc_server.h"
#include "test.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static struct {
    pthread_mutex_t lock;
    int calls;
    wf_xrpc_response_kind last_kind;
    int last_status;
    char last_path[128];
    char last_subject[64];
    int saw_rate_limit_header;
    size_t full_count;
    wf_status full_status;
    wf_status invalid_status;
    char first_one[16];
    int missing_two;
} seen;

static const char *kind_name(wf_xrpc_response_kind kind) {
    switch (kind) {
        case WF_XRPC_RESPONSE_BUFFERED:
            return "buffered";
        case WF_XRPC_RESPONSE_SSE:
            return "sse";
        case WF_XRPC_RESPONSE_WS_UPGRADE:
            return "ws";
        case WF_XRPC_RESPONSE_PREFLIGHT:
            return "preflight";
    }
    return "?";
}

static void finalize(void *ctx, const wf_xrpc_request *req,
                     wf_xrpc_response_kind kind, wf_xrpc_response *resp,
                     wf_xrpc_header_sink *sink) {
    (void)ctx;
    pthread_mutex_lock(&seen.lock);
    seen.calls++;
    seen.last_kind = kind;
    seen.last_status = resp->http_status;
    snprintf(seen.last_path, sizeof(seen.last_path), "%s",
             req && req->path ? req->path : "");
    snprintf(seen.last_subject, sizeof(seen.last_subject), "%s",
             req && req->authed_subject ? req->authed_subject : "");
    /* Charge once: a response that already carries the headers is left. */
    seen.saw_rate_limit_header =
        wf_xrpc_response_get_header(resp, "ratelimit-limit") != NULL;
    pthread_mutex_unlock(&seen.lock);

    (void)wf_xrpc_header_sink_add(sink, "X-Finalized", kind_name(kind));

    if (req->nsid && strcmp(req->nsid, "test.status") == 0) {
        resp->http_status = 202;
    } else if (req->nsid && strcmp(req->nsid, "test.full") == 0) {
        wf_status st = WF_OK;
        while (st == WF_OK) st = wf_xrpc_header_sink_add(sink, "X-Fill", "1");
        wf_status bad = wf_xrpc_header_sink_add(sink, "X-Bad", "a\r\nb");
        pthread_mutex_lock(&seen.lock);
        seen.full_status = st;
        seen.full_count = wf_xrpc_header_sink_count(sink);
        seen.invalid_status = bad;
        pthread_mutex_unlock(&seen.lock);
    } else if (req->nsid && strcmp(req->nsid, "test.headers") == 0) {
        /* Case-insensitive, and the first one the handler added wins. */
        const char *one = wf_xrpc_response_get_header(resp, "X-ONE");
        pthread_mutex_lock(&seen.lock);
        snprintf(seen.first_one, sizeof(seen.first_one), "%s", one ? one : "");
        seen.missing_two = wf_xrpc_response_get_header(resp, "X-Two") == NULL;
        pthread_mutex_unlock(&seen.lock);
    } else if (req->nsid && strcmp(req->nsid, "test.large") == 0) {
        /* Byte budget: one value that cannot fit is refused. */
        static char big[WF_XRPC_HEADER_SINK_MAX_BYTES];
        memset(big, 'v', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        wf_status st = wf_xrpc_header_sink_add(sink, "X-Big", big);
        pthread_mutex_lock(&seen.lock);
        seen.full_status = st;
        seen.full_count = wf_xrpc_header_sink_count(sink);
        pthread_mutex_unlock(&seen.lock);
    }
}

static int calls(void) {
    pthread_mutex_lock(&seen.lock);
    int n = seen.calls;
    pthread_mutex_unlock(&seen.lock);
    return n;
}

/* ---- handlers ---------------------------------------------------- */

static wf_status ok_handler(void *ctx, const wf_xrpc_request *req,
                            wf_xrpc_response *resp) {
    (void)ctx;
    (void)req;
    wf_xrpc_response_set_body(resp, "{\"ok\":true}", 11);
    return WF_OK;
}

static wf_status headers_handler(void *ctx, const wf_xrpc_request *req,
                                 wf_xrpc_response *resp) {
    (void)ctx;
    (void)req;
    (void)wf_xrpc_response_add_header(resp, "X-One", "first");
    (void)wf_xrpc_response_add_header(resp, "x-one", "second");
    wf_xrpc_response_set_body(resp, "{}", 2);
    return WF_OK;
}

static wf_status not_found_fallback(void *ctx, const wf_xrpc_request *req,
                                    wf_xrpc_response *resp) {
    (void)ctx;
    (void)req;
    wf_xrpc_response_set_error(resp, 404, "NotFound", "no such method");
    return WF_OK;
}

static wf_status auth(wf_xrpc_request *req, void *ctx) {
    (void)ctx;
    if (req->nsid && strcmp(req->nsid, "test.private") == 0)
        return WF_ERR_PERMISSION;
    if (req->nsid && strcmp(req->nsid, "test.stream") == 0) {
        req->authed_subject = strdup("did:example:alice");
        req->authed_principal_kind = WF_XRPC_PRINCIPAL_USER;
    }
    return WF_OK;
}

static int stream_state = 1;

static wf_status s_begin(void *ctx, const wf_xrpc_request *req,
                         void **out_stream_ctx, wf_xrpc_response *resp) {
    (void)ctx;
    (void)req;
    (void)resp;
    *out_stream_ctx = &stream_state;
    return WF_OK;
}
static wf_status s_write(void *ctx, void *stream_ctx, const unsigned char *data,
                         size_t data_len, wf_xrpc_response *resp) {
    (void)ctx;
    (void)stream_ctx;
    (void)data;
    (void)data_len;
    (void)resp;
    return WF_OK;
}
static wf_status s_finish(void *ctx, void *stream_ctx, wf_xrpc_response *resp) {
    (void)ctx;
    (void)stream_ctx;
    wf_xrpc_response_set_body(resp, "{\"streamed\":true}", 17);
    return WF_OK;
}
static void s_cleanup(void *ctx, void *stream_ctx, bool completed) {
    (void)ctx;
    (void)stream_ctx;
    (void)completed;
}

static void *sse_closer(void *arg) {
    wf_xrpc_sse_stream *stream = arg;
    (void)wf_xrpc_server_sse_send(stream, NULL, "hello");
    wf_xrpc_server_sse_close(stream);
    return NULL;
}

static wf_status sse_handler(void *ctx, const wf_xrpc_request *req,
                             wf_xrpc_sse_stream *stream) {
    (void)ctx;
    (void)req;
    pthread_t tid;
    if (pthread_create(&tid, NULL, sse_closer, stream) != 0)
        return WF_ERR_ALLOC;
    pthread_detach(tid);
    return WF_OK;
}

static void *ws_closer(void *arg) {
    wf_xrpc_ws_stream *stream = arg;
    wf_xrpc_server_ws_close(stream, 1000);
    wf_xrpc_server_ws_release(stream);
    return NULL;
}

static wf_status ws_handler(void *ctx, const wf_xrpc_request *req,
                            wf_xrpc_ws_stream *stream) {
    (void)ctx;
    (void)req;
    pthread_t tid;
    if (wf_xrpc_server_ws_retain(stream) != WF_OK) return WF_ERR_INVALID_ARG;
    if (pthread_create(&tid, NULL, ws_closer, stream) != 0) {
        wf_xrpc_server_ws_release(stream);
        return WF_ERR_ALLOC;
    }
    pthread_detach(tid);
    return WF_OK;
}

/* ---- raw HTTP ---------------------------------------------------- */

static uint16_t port;

/* Send `request` and read the reply into `out`: to the end of the headers
 * when `headers_only`, otherwise until the server closes. Returns the HTTP
 * status, or 0 on failure. */
static int exchange(const char *request, int headers_only, char *out,
                    size_t cap) {
    out[0] = '\0';
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct timeval tv = {5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return 0;
    }
    size_t len = strlen(request);
    if (send(fd, request, len, 0) != (ssize_t)len) {
        close(fd);
        return 0;
    }
    size_t used = 0;
    while (used + 1 < cap) {
        ssize_t n = recv(fd, out + used, cap - 1 - used, 0);
        if (n <= 0) break;
        used += (size_t)n;
        out[used] = '\0';
        if (headers_only && strstr(out, "\r\n\r\n")) break;
    }
    close(fd);
    int status = 0;
    if (sscanf(out, "HTTP/1.%*d %d", &status) != 1) return 0;
    return status;
}

/* Occurrences of header `name` in the header block of `reply`. */
static int header_count(const char *reply, const char *name) {
    const char *end = strstr(reply, "\r\n\r\n");
    size_t name_len = strlen(name);
    int count = 0;
    for (const char *line = strstr(reply, "\r\n"); line && line < end;
         line = strstr(line + 2, "\r\n")) {
        if (strncasecmp(line + 2, name, name_len) == 0 &&
            line[2 + name_len] == ':')
            count++;
    }
    return count;
}

static int has_header_value(const char *reply, const char *name,
                            const char *value) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\r\n%s: %s\r\n", name, value);
    const char *end = strstr(reply, "\r\n\r\n");
    const char *at = strcasestr(reply, needle);
    return at && end && at < end + 2;
}

static char reply[16384];

/* One request: its status, exactly one finalizer call, exactly one header. */
static void expect(const char *what, const char *request, int headers_only,
                   int want_status, const char *want_kind) {
    int before = calls();
    int status = exchange(request, headers_only, reply, sizeof(reply));
    /* SSE/WS replies are read only to their headers; the finalizer has run
     * before they were sent, so the count is already settled. */
    int after = calls();
    if (status != want_status)
        fprintf(stderr, "%s: status %d, want %d\n", what, status, want_status);
    WF_CHECK(status == want_status);
    if (after - before != 1)
        fprintf(stderr, "%s: %d finalizer calls\n", what, after - before);
    WF_CHECK(after - before == 1);
    if (header_count(reply, "X-Finalized") != 1)
        fprintf(stderr, "%s: X-Finalized x%d\n%s\n", what,
                header_count(reply, "X-Finalized"), reply);
    WF_CHECK(header_count(reply, "X-Finalized") == 1);
    WF_CHECK(has_header_value(reply, "X-Finalized", want_kind));
}

#define GET(path)                                                              \
    "GET " path " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"

int main(void) {
    pthread_mutex_init(&seen.lock, NULL);

    wf_xrpc_server *server = wf_xrpc_server_start("127.0.0.1", 0, 2);
    WF_CHECK(server != NULL);
    if (!server) WF_TEST_SUMMARY();
    port = wf_xrpc_server_port(server);

    const wf_xrpc_streaming_procedure_handler streaming = {
        .begin = s_begin,
        .write = s_write,
        .finish = s_finish,
        .cleanup = s_cleanup,
    };
    WF_CHECK(wf_xrpc_server_register_query(server, "test.ok", ok_handler,
                                           NULL) == WF_OK);
    WF_CHECK(wf_xrpc_server_register_query(server, "test.status", ok_handler,
                                           NULL) == WF_OK);
    WF_CHECK(wf_xrpc_server_register_query(server, "test.full", ok_handler,
                                           NULL) == WF_OK);
    WF_CHECK(wf_xrpc_server_register_query(server, "test.large", ok_handler,
                                           NULL) == WF_OK);
    WF_CHECK(wf_xrpc_server_register_query(server, "test.headers",
                                           headers_handler, NULL) == WF_OK);
    WF_CHECK(wf_xrpc_server_register_query(server, "test.private", ok_handler,
                                           NULL) == WF_OK);
    WF_CHECK(wf_xrpc_server_register_query(server, "test.limited", ok_handler,
                                           NULL) == WF_OK);
    WF_CHECK(wf_xrpc_server_register_streaming_procedure(
                 server, "test.stream", &streaming, NULL) == WF_OK);
    WF_CHECK(wf_xrpc_server_register_sse(server, "test.sse", sse_handler,
                                         NULL) == WF_OK);
    WF_CHECK(wf_xrpc_server_register_ws(server, "test.ws", ws_handler, NULL) ==
             WF_OK);
    wf_xrpc_server_set_auth_callback(server, auth, NULL);
    wf_xrpc_server_set_fallback(server, not_found_fallback, NULL);
    wf_rate_limiter *limiter = wf_rate_limiter_new(1, 3600, 0);
    WF_CHECK(limiter != NULL);
    /* The server owns a route limiter and frees it with itself. */
    wf_xrpc_server_set_route_rate_limiter(server, "GET", "/xrpc/test.limited",
                                          limiter);
    wf_xrpc_server_set_response_finalizer(server, finalize, NULL);

    /* A registered handler. */
    expect("handler", GET("/xrpc/test.ok"), 0, 200, "buffered");
    WF_CHECK(strstr(reply, "{\"ok\":true}") != NULL);
    WF_CHECK(seen.last_kind == WF_XRPC_RESPONSE_BUFFERED);
    WF_CHECK(seen.last_status == 200);

    /* A status the finalizer changes is the one sent. */
    expect("status", GET("/xrpc/test.status"), 0, 202, "buffered");

    /* The fallback's own 404. */
    expect("fallback", GET("/xrpc/test.unknown"), 0, 404, "buffered");
    WF_CHECK(strcmp(seen.last_path, "/xrpc/test.unknown") == 0);

    /* Server-written errors: auth refusal, wrong method, not XRPC. */
    expect("auth 401", GET("/xrpc/test.private"), 0, 401, "buffered");
    expect("wrong method",
           "POST /xrpc/test.ok HTTP/1.1\r\nHost: 127.0.0.1\r\n"
           "Content-Length: 0\r\nConnection: close\r\n\r\n",
           0, 400, "buffered");
    expect("not xrpc", GET("/elsewhere"), 0, 400, "buffered");

    /* The server's rate limiter: first passes, second is its 429, which
     * already carries RateLimit-* so a finalizer can tell not to charge. */
    expect("limited 1", GET("/xrpc/test.limited"), 0, 200, "buffered");
    WF_CHECK(seen.saw_rate_limit_header == 0);
    expect("limited 429", GET("/xrpc/test.limited"), 0, 429, "buffered");
    WF_CHECK(seen.saw_rate_limit_header == 1);
    WF_CHECK(header_count(reply, "RateLimit-Limit") == 1);

    /* A streaming procedure, with the principal its auth established. */
    expect("streaming",
           "POST /xrpc/test.stream HTTP/1.1\r\nHost: 127.0.0.1\r\n"
           "Content-Type: application/octet-stream\r\nContent-Length: 5\r\n"
           "Connection: close\r\n\r\nhello",
           0, 200, "buffered");
    WF_CHECK(strstr(reply, "{\"streamed\":true}") != NULL);
    WF_CHECK(strcmp(seen.last_subject, "did:example:alice") == 0);

    /* An SSE stream and a WebSocket upgrade. */
    expect("sse", GET("/xrpc/test.sse"), 1, 200, "sse");
    WF_CHECK(seen.last_kind == WF_XRPC_RESPONSE_SSE);
    expect("ws",
           "GET /xrpc/test.ws HTTP/1.1\r\nHost: 127.0.0.1\r\n"
           "Upgrade: websocket\r\nConnection: Upgrade\r\n"
           "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
           "Sec-WebSocket-Version: 13\r\n\r\n",
           1, 101, "ws");
    WF_CHECK(seen.last_status == 101);
    /* A WS route asked for without the upgrade is the 400, finalized once. */
    expect("ws 400", GET("/xrpc/test.ws"), 0, 400, "buffered");

    /* A CORS preflight. */
    expect("preflight",
           "OPTIONS /xrpc/test.ok HTTP/1.1\r\nHost: 127.0.0.1\r\n"
           "Connection: close\r\n\r\n",
           0, 204, "preflight");

    /* No fallback: the server's 501. */
    wf_xrpc_server_set_fallback(server, NULL, NULL);
    expect("501", GET("/xrpc/test.unknown"), 0, 501, "buffered");

    /* A full sink refuses with WF_ERR_STATE and keeps what it holds. */
    expect("full", GET("/xrpc/test.full"), 0, 200, "buffered");
    WF_CHECK(seen.full_status == WF_ERR_STATE);
    WF_CHECK(seen.full_count == WF_XRPC_HEADER_SINK_MAX_HEADERS);
    WF_CHECK(seen.invalid_status == WF_ERR_INVALID_ARG);
    WF_CHECK(header_count(reply, "X-Fill") ==
             WF_XRPC_HEADER_SINK_MAX_HEADERS - 1);
    WF_CHECK(header_count(reply, "X-Bad") == 0);

    expect("large", GET("/xrpc/test.large"), 0, 200, "buffered");
    WF_CHECK(seen.full_status == WF_ERR_STATE);
    WF_CHECK(seen.full_count == 1);
    WF_CHECK(header_count(reply, "X-Big") == 0);

    /* The sink's own argument checks. */
    WF_CHECK(wf_xrpc_header_sink_add(NULL, "A", "b") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_xrpc_header_sink_count(NULL) == 0);
    WF_CHECK(wf_xrpc_response_get_header(NULL, "A") == NULL);
    expect("headers", GET("/xrpc/test.headers"), 0, 200, "buffered");
    WF_CHECK(strcmp(seen.first_one, "first") == 0);
    WF_CHECK(seen.missing_two == 1);

    /* Clearing it stops the calls and the header. */
    wf_xrpc_server_set_response_finalizer(server, NULL, NULL);
    int before = calls();
    WF_CHECK(exchange(GET("/xrpc/test.ok"), 0, reply, sizeof(reply)) == 200);
    WF_CHECK(calls() == before);
    WF_CHECK(header_count(reply, "X-Finalized") == 0);

    wf_xrpc_server_free(server);
    pthread_mutex_destroy(&seen.lock);
    WF_TEST_SUMMARY();
}
