/*
 * test_response_headers.c -- wf_response_header and header capture. The
 * capture cases run the real libcurl path against a loopback server.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "test.h"
#include "wolfram/xrpc.h"

static int listen_fd = -1;
static int port;

/* True when header `name` reads as `want`; NULL means absent. */
static int header_is(const wf_response *res, const char *name,
                     const char *want) {
    char *value = NULL;
    wf_status status = wf_response_header(res, name, &value);
    int ok = want ? status == WF_OK && value && strcmp(value, want) == 0
                  : status == WF_ERR_NOT_FOUND && value == NULL;
    free(value);
    return ok;
}

static void send_all(int fd, const char *data, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, data, len);
        if (n <= 0) return;
        data += n;
        len -= (size_t)n;
    }
}

static void send_text(int fd, const char *text) {
    send_all(fd, text, strlen(text));
}

/* The path picks the reply. */
static void serve_one(int fd) {
    char req[4096];
    ssize_t n = read(fd, req, sizeof(req) - 1);
    char out[256];

    if (n <= 0) return;
    req[n] = '\0';
    if (strstr(req, "/redirect")) {
        snprintf(out, sizeof(out),
                 "HTTP/1.1 302 Found\r\nSignature: stale\r\nX-Hop: 1\r\n"
                 "Location: http://127.0.0.1:%d/plain\r\n"
                 "Content-Length: 0\r\nConnection: close\r\n\r\n",
                 port);
        send_text(fd, out);
    } else if (strstr(req, "/trailer")) {
        send_text(fd, "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n"
                      "Signature: head\r\nConnection: close\r\n\r\n"
                      "2\r\n{}\r\n0\r\nSignature: trailer\r\n\r\n");
    } else if (strstr(req, "/huge")) {
        /* Many headers, together past the cap. */
        char line[1100];
        int i;
        send_text(fd, "HTTP/1.1 200 OK\r\nSignature: kept?\r\n");
        memset(line, 'a', sizeof(line));
        memcpy(line, "X-Big: ", 7);
        memcpy(line + 1000, "\r\n", 3);
        for (i = 0; i < 70; i++) send_text(fd, line);
        send_text(fd, "Content-Length: 2\r\nConnection: close\r\n\r\n{}");
    } else {
        send_text(fd, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                      "Atproto-Space-Commit:  ver=1 \r\n"
                      "Signature-Input: a=(\"x\")\r\n"
                      "signature-input: b=(\"y\")\r\n"
                      "X-Empty:\r\n"
                      "Content-Length: 2\r\nConnection: close\r\n\r\n{}");
    }
}

static void *accept_loop(void *arg) {
    (void)arg;
    for (;;) {
        int fd = accept(listen_fd, NULL, NULL);
        if (fd < 0) break;
        serve_one(fd);
        close(fd);
    }
    return NULL;
}

static int start_server(void) {
    struct sockaddr_in a = {0};
    socklen_t length = sizeof(a);
    pthread_t thread;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listen_fd, (struct sockaddr *)&a, sizeof(a)) != 0 ||
        getsockname(listen_fd, (struct sockaddr *)&a, &length) != 0 ||
        listen(listen_fd, 8) != 0)
        return -1;
    port = ntohs(a.sin_port);
    if (pthread_create(&thread, NULL, accept_loop, NULL) != 0) return -1;
    pthread_detach(thread);
    return 0;
}

static void test_storage(void) {
    wf_response res = {0};
    char *value = (char *)1;
    char *big = malloc(WF_RESPONSE_MAX_HEADER_BYTES);
    wf_status status;

    WF_CHECK(header_is(&res, "Signature", NULL));
    WF_CHECK(wf_response_add_header(&res, "Signature", " \ta=:AA==: ") ==
             WF_OK);
    WF_CHECK(wf_response_add_header(&res, "Signature-Input", "x") == WF_OK);
    WF_CHECK(wf_response_add_header(&res, "SIGNATURE", "b=:AA==:") == WF_OK);
    WF_CHECK(header_is(&res, "signature", "a=:AA==:, b=:AA==:"));
    WF_CHECK(header_is(&res, "Signature-input", "x"));
    /* A prefix or extension of a name is a different field. */
    WF_CHECK(header_is(&res, "Signatur", NULL));
    WF_CHECK(header_is(&res, "Signature-Inpu", NULL));
    WF_CHECK(wf_response_header(&res, "signature:", &value) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(value == NULL);

    WF_CHECK(wf_response_add_header(&res, "bad name", "v") ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_response_add_header(&res, "x:y", "v") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_response_add_header(&res, "", "v") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_response_add_header(&res, "x", "v\r\nSignature: forged") ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(header_is(&res, "x", NULL));

    WF_CHECK(big != NULL);
    if (big) {
        memset(big, 'a', WF_RESPONSE_MAX_HEADER_BYTES - 1);
        big[WF_RESPONSE_MAX_HEADER_BYTES - 1] = '\0';
        status = wf_response_add_header(&res, "x-big", big);
        WF_CHECK(status == WF_ERR_VALIDATION);
        free(big);
    }
    wf_response_free(&res);
    WF_CHECK(res.header_block == NULL);
}

static wf_status header_handler(void *ud, const char *m, const char *url,
                                const char *ct, const char *body, size_t bl,
                                const wf_http_header *h, size_t hc,
                                wf_response *out) {
    (void)ud;
    (void)m;
    (void)url;
    (void)ct;
    (void)body;
    (void)bl;
    (void)h;
    (void)hc;
    out->status = 200;
    return wf_response_add_header(out, "Signature", "from-handler");
}

static void test_network(void) {
    char base[64];
    char url[96];
    wf_xrpc_client *c;
    wf_xrpc_pending *pending = NULL;
    wf_response r = {0};

    snprintf(base, sizeof(base), "http://127.0.0.1:%d", port);
    c = wf_xrpc_client_new(base);
    WF_CHECK(c != NULL);

    /* Off by default. */
    WF_CHECK(wf_xrpc_query(c, "plain", NULL, &r) == WF_OK);
    WF_CHECK(r.header_block == NULL);
    WF_CHECK(header_is(&r, "Content-Type", NULL));
    wf_response_free(&r);

    WF_CHECK(wf_xrpc_client_set_capture_headers(c, 1) == WF_OK);
    WF_CHECK(wf_xrpc_query(c, "plain", NULL, &r) == WF_OK);
    WF_CHECK(header_is(&r, "content-type", "application/json"));
    WF_CHECK(header_is(&r, "atproto-space-commit", "ver=1"));
    WF_CHECK(header_is(&r, "Signature-Input", "a=(\"x\"), b=(\"y\")"));
    WF_CHECK(header_is(&r, "X-Empty", ""));
    WF_CHECK(header_is(&r, "HTTP/1.1 200 OK", NULL));
    wf_response_free(&r);

    /* Only the final response of a redirect chain. */
    snprintf(url, sizeof(url), "%s/redirect", base);
    WF_CHECK(wf_http_get(c, url, &r) == WF_OK);
    WF_CHECK(header_is(&r, "X-Hop", NULL));
    WF_CHECK(header_is(&r, "Signature", NULL));
    WF_CHECK(header_is(&r, "X-Empty", ""));
    wf_response_free(&r);

    /* A trailer cannot add to a header. */
    snprintf(url, sizeof(url), "%s/trailer", base);
    WF_CHECK(wf_http_get(c, url, &r) == WF_OK);
    WF_CHECK(header_is(&r, "Signature", "head"));
    wf_response_free(&r);

    /* Past the cap nothing is kept, but the request still succeeds. */
    snprintf(url, sizeof(url), "%s/huge", base);
    WF_CHECK(wf_http_get(c, url, &r) == WF_OK);
    WF_CHECK(r.body_len == 2);
    WF_CHECK(header_is(&r, "Signature", NULL));
    wf_response_free(&r);

    /* The async path captures too. */
    WF_CHECK(wf_xrpc_query_async(c, "plain", NULL, &pending) == WF_OK);
    WF_CHECK(wf_xrpc_pending_wait(pending) == WF_OK);
    WF_CHECK(wf_xrpc_pending_result(pending, &r) == WF_OK);
    WF_CHECK(header_is(&r, "atproto-space-commit", "ver=1"));
    wf_response_free(&r);
    wf_xrpc_pending_free(pending);

    WF_CHECK(wf_xrpc_client_set_capture_headers(c, 0) == WF_OK);
    WF_CHECK(wf_xrpc_query(c, "plain", NULL, &r) == WF_OK);
    WF_CHECK(r.header_block == NULL);
    wf_response_free(&r);

    /* A test handler supplies headers itself. */
    wf_xrpc_set_handler(c, header_handler, NULL);
    WF_CHECK(wf_xrpc_query(c, "plain", NULL, &r) == WF_OK);
    WF_CHECK(header_is(&r, "Signature", "from-handler"));
    wf_response_free(&r);

    WF_CHECK(wf_xrpc_client_set_capture_headers(NULL, 1) == WF_ERR_INVALID_ARG);
    wf_xrpc_client_free(c);
}

int main(void) {
    test_storage();
    if (start_server() != 0) {
        fprintf(stderr, "could not start loopback server\n");
        return 1;
    }
    test_network();
    WF_TEST_SUMMARY();
}
