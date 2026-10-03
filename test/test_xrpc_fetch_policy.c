/**
 * test_xrpc_fetch_policy.c — the per-client fetch policy (https-only, redirect
 * cap, overall deadline, user agent) and the guarantee that a shared client can
 * be used from several threads.
 *
 * The https-only refusal is checked through the handler seam. Redirect cap,
 * deadline and thread safety run against a tiny loopback HTTP server, so they
 * exercise the real libcurl path without touching the network.
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "test.h"
#include "wolfram/xrpc.h"

enum mode { MODE_REDIRECT_LOOP, MODE_HANG, MODE_BODY };

static int listen_fd = -1;
static int port;
static atomic_int stop;
static atomic_int requests;
static _Atomic enum mode mode;
static char last_ua[128];
static pthread_mutex_t ua_lock = PTHREAD_MUTEX_INITIALIZER;

static void serve_one(int fd) {
    char req[2048];
    ssize_t n = read(fd, req, sizeof(req) - 1);
    if (n <= 0) return;
    req[n] = '\0';
    atomic_fetch_add(&requests, 1);
    const char *ua = strstr(req, "User-Agent: ");
    if (ua) {
        pthread_mutex_lock(&ua_lock);
        snprintf(last_ua, sizeof(last_ua), "%.*s",
                 (int)strcspn(ua + 12, "\r\n"), ua + 12);
        pthread_mutex_unlock(&ua_lock);
    }
    char out[512];
    enum mode m = atomic_load(&mode);
    if (m == MODE_HANG) {
        /* Accept, read, never answer: only an overall deadline ends this. */
        while (!atomic_load(&stop)) usleep(20000);
        return;
    }
    if (m == MODE_REDIRECT_LOOP) {
        int len =
            snprintf(out, sizeof(out),
                     "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:%d/"
                     "next\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                     port);
        (void)!write(fd, out, (size_t)len);
        return;
    }
    int len = snprintf(out, sizeof(out),
                       "HTTP/1.1 200 OK\r\nContent-Length: 200\r\nConnection: "
                       "close\r\n\r\n");
    (void)!write(fd, out, (size_t)len);
    char body[200];
    memset(body, 'x', sizeof(body));
    (void)!write(fd, body, sizeof(body));
}

/* One thread per connection so a hung request never blocks the others. */
static void *conn_thread(void *arg) {
    int fd = (int)(intptr_t)arg;
    serve_one(fd);
    close(fd);
    return NULL;
}

static void *accept_loop(void *arg) {
    (void)arg;
    while (!atomic_load(&stop)) {
        int fd = accept(listen_fd, NULL, NULL);
        if (fd < 0) break;
        pthread_t t;
        if (pthread_create(&t, NULL, conn_thread, (void *)(intptr_t)fd) == 0) {
            pthread_detach(t);
        } else {
            close(fd);
        }
    }
    return NULL;
}

static int start_server(pthread_t *t) {
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listen_fd, (struct sockaddr *)&a, sizeof(a)) != 0) return -1;
    socklen_t l = sizeof(a);
    getsockname(listen_fd, (struct sockaddr *)&a, &l);
    port = ntohs(a.sin_port);
    if (listen(listen_fd, 16) != 0) return -1;
    return pthread_create(t, NULL, accept_loop, NULL);
}

static wf_status ok_handler(void *ud, const char *m, const char *url,
                            const char *ct, const char *body, size_t bl,
                            const wf_http_header *h, size_t hc,
                            wf_response *out) {
    (void)m;
    (void)url;
    (void)ct;
    (void)body;
    (void)bl;
    (void)h;
    (void)hc;
    out->status = 200;
    (*(int *)ud)++;
    return WF_OK;
}

struct worker {
    wf_xrpc_client *c;
    char url[64];
    int ok;
};

static void *fetch_worker(void *arg) {
    struct worker *w = arg;
    for (int i = 0; i < 5; i++) {
        wf_response r = {0};
        if (wf_http_get_limited(w->c, w->url, 4096, &r) == WF_OK &&
            r.body_len == 200) {
            w->ok++;
        }
        wf_response_free(&r);
    }
    return NULL;
}

int main(void) {
    /* https-only: refused before the transport (handler never runs). */
    {
        int called = 0;
        wf_xrpc_client *c = wf_xrpc_client_new("https://example.com");
        wf_xrpc_set_handler(c, ok_handler, &called);
        wf_xrpc_client_set_https_only(c, 1);
        wf_response r = {0};
        WF_CHECK(wf_http_get(c, "http://example.com/a.jpg", &r) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_http_get(c, "ftp://example.com/a.jpg", &r) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(called == 0);
        WF_CHECK(wf_http_get(c, "HTTPS://example.com/a.jpg", &r) == WF_OK);
        WF_CHECK(called == 1);
        wf_response_free(&r);
        wf_xrpc_client_set_https_only(c, 0);
        WF_CHECK(wf_http_get(c, "http://example.com/a.jpg", &r) == WF_OK);
        WF_CHECK(called == 2);
        wf_response_free(&r);
        wf_xrpc_client_free(c);
    }

    pthread_t srv;
    if (start_server(&srv) != 0) {
        fprintf(stderr, "could not start loopback server\n");
        return 1;
    }
    char url[64];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/x", port);

    /* Redirect cap: a loop is cut off after exactly N follow-ups. */
    {
        atomic_store(&mode, MODE_REDIRECT_LOOP);
        wf_xrpc_client *c = wf_xrpc_client_new("http://127.0.0.1");
        wf_xrpc_client_set_max_redirects(c, 3);
        atomic_store(&requests, 0);
        wf_response r = {0};
        WF_CHECK(wf_http_get(c, url, &r) == WF_ERR_NETWORK);
        WF_CHECK(atomic_load(&requests) == 4); /* original + 3 redirects */
        wf_response_free(&r);

        wf_xrpc_client_set_max_redirects(c, 0); /* following disabled */
        atomic_store(&requests, 0);
        WF_CHECK(wf_http_get(c, url, &r) == WF_ERR_HTTP); /* the 302 itself */
        WF_CHECK(atomic_load(&requests) == 1);
        wf_response_free(&r);

        wf_xrpc_client_set_max_redirects(c, -1); /* default */
        atomic_store(&requests, 0);
        WF_CHECK(wf_http_get(c, url, &r) == WF_ERR_NETWORK);
        WF_CHECK(atomic_load(&requests) == WF_XRPC_DEFAULT_MAX_REDIRECTS + 1);
        wf_response_free(&r);
        wf_xrpc_client_free(c);
    }

    /* User agent override, and the size cap still applies per call. */
    {
        atomic_store(&mode, MODE_BODY);
        wf_xrpc_client *c = wf_xrpc_client_new("http://127.0.0.1");
        WF_CHECK(wf_xrpc_client_set_user_agent(c, "policy-test/1") == WF_OK);
        wf_response r = {0};
        WF_CHECK(wf_http_get_limited(c, url, 4096, &r) == WF_OK);
        WF_CHECK(r.body_len == 200);
        pthread_mutex_lock(&ua_lock);
        WF_CHECK(strcmp(last_ua, "policy-test/1") == 0);
        pthread_mutex_unlock(&ua_lock);
        wf_response_free(&r);
        WF_CHECK(wf_http_get_limited(c, url, 100, &r) == WF_ERR_NETWORK);
        wf_response_free(&r);
        wf_xrpc_client_free(c);
    }

    /* One client shared by several threads. */
    {
        atomic_store(&mode, MODE_BODY);
        wf_xrpc_client *c = wf_xrpc_client_new("http://127.0.0.1");
        pthread_t th[4];
        struct worker w[4];
        for (int i = 0; i < 4; i++) {
            w[i].c = c;
            w[i].ok = 0;
            snprintf(w[i].url, sizeof(w[i].url), "%s", url);
            pthread_create(&th[i], NULL, fetch_worker, &w[i]);
        }
        int ok = 0;
        for (int i = 0; i < 4; i++) {
            pthread_join(th[i], NULL);
            ok += w[i].ok;
        }
        WF_CHECK(ok == 20);
        wf_xrpc_client_free(c);
    }

    /* Overall deadline: a server that never answers is abandoned. */
    {
        atomic_store(&mode, MODE_HANG);
        wf_xrpc_client *c = wf_xrpc_client_new("http://127.0.0.1");
        wf_xrpc_client_set_total_timeout_ms(c, 400);
        wf_response r = {0};
        WF_CHECK(wf_http_get(c, url, &r) == WF_ERR_NETWORK);
        wf_response_free(&r);
        wf_xrpc_client_free(c);
    }

    atomic_store(&stop, 1);
    shutdown(listen_fd, SHUT_RDWR);
    close(listen_fd);
    pthread_join(srv, NULL);
    WF_TEST_SUMMARY();
}
