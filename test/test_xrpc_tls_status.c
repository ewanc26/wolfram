/*
 * test_xrpc_tls_status.c -- the libcurl transport reports a failed TLS
 * handshake as WF_ERR_TLS and an unreachable peer as WF_ERR_NETWORK (#141).
 *
 * "TLS failure" here is a local server that accepts a TCP connection, answers
 * a TLS ClientHello with plain HTTP bytes and closes: the client reached the
 * peer but cannot set up a secure channel. No network beyond loopback.
 */

#include "wolfram/xrpc.h"

#include "test.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int listen_fd = -1;

static void *not_tls_server(void *arg) {
    (void)arg;
    int c = accept(listen_fd, NULL, NULL);
    if (c >= 0) {
        char buf[512];
        (void)read(c, buf, sizeof buf);
        const char *reply = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
        (void)write(c, reply, strlen(reply));
        close(c);
    }
    return NULL;
}

static int listen_local(int *port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof a;
    if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) != 0 ||
        listen(fd, 1) != 0 || getsockname(fd, (struct sockaddr *)&a, &len) != 0)
        return -1;
    *port = ntohs(a.sin_port);
    return fd;
}

int main(void) {
    int port = 0;
    listen_fd = listen_local(&port);
    WF_CHECK(listen_fd >= 0);
    if (listen_fd < 0) WF_TEST_SUMMARY();

    pthread_t th;
    pthread_create(&th, NULL, not_tls_server, NULL);
    char url[64];
    snprintf(url, sizeof url, "https://127.0.0.1:%d", port);
    wf_xrpc_client *c = wf_xrpc_client_new(url);
    wf_response r;
    memset(&r, 0, sizeof r);
    wf_status st =
        wf_xrpc_query(c, "com.atproto.server.describeServer", NULL, &r);
    WF_CHECK(st == WF_ERR_TLS);
    if (st != WF_ERR_TLS)
        fprintf(stderr, "  expected WF_ERR_TLS (%d), got %d\n", WF_ERR_TLS,
                (int)st);
    wf_response_free(&r);
    wf_xrpc_client_free(c);
    pthread_join(th, NULL);
    close(listen_fd);

    /* Nothing listening on that port any more: unreachable, not TLS. */
    snprintf(url, sizeof url, "https://127.0.0.1:%d", port);
    c = wf_xrpc_client_new(url);
    memset(&r, 0, sizeof r);
    st = wf_xrpc_query(c, "com.atproto.server.describeServer", NULL, &r);
    WF_CHECK(st == WF_ERR_NETWORK);
    if (st != WF_ERR_NETWORK)
        fprintf(stderr, "  expected WF_ERR_NETWORK, got %d\n", (int)st);
    wf_response_free(&r);
    wf_xrpc_client_free(c);
    WF_TEST_SUMMARY();
}
