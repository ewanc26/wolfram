#include "wolfram/xrpc.h"
#include "transport/wii_tls.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct wii_tls_conn { int unused; };

static struct wii_tls_conn connection;
static const char *response_bytes;
static size_t response_length;
static size_t response_offset;
static char request_bytes[8192];
static size_t request_length;
static char connected_host[256];
static uint16_t connected_port;

static void fake_response(const char *response) {
    response_bytes = response;
    response_length = strlen(response);
    response_offset = 0;
    request_length = 0;
    request_bytes[0] = '\0';
    connected_host[0] = '\0';
    connected_port = 0;
}

static size_t occurrences(const char *haystack, const char *needle) {
    size_t count = 0;
    size_t length = strlen(needle);
    while ((haystack = strstr(haystack, needle)) != NULL) {
        count++;
        haystack += length;
    }
    return count;
}

wf_status wii_tls_global_init(void) { return WF_OK; }
wf_status wii_tls_add_ca_pem(const char *pem) {
    return pem ? WF_OK : WF_ERR_INVALID_ARG;
}
int wii_tls_random(void *p, unsigned char *out, size_t len) {
    (void)p;
    memset(out, 0x5a, len);
    return 0;
}

wii_tls_conn *wii_tls_connect(const char *host, uint16_t port) {
    snprintf(connected_host, sizeof(connected_host), "%s", host);
    connected_port = port;
    return &connection;
}

long wii_tls_send(wii_tls_conn *conn, const void *buf, size_t len) {
    (void)conn;
    if (len > sizeof(request_bytes) - request_length - 1) return -WF_ERR_ALLOC;
    memcpy(request_bytes + request_length, buf, len);
    request_length += len;
    request_bytes[request_length] = '\0';
    return (long)len;
}

long wii_tls_recv(wii_tls_conn *conn, void *buf, size_t cap) {
    (void)conn;
    if (response_offset == response_length) return 0;
    size_t remaining = response_length - response_offset;
    size_t count = remaining < cap ? remaining : cap;
    /* Fragment reads so parsing is exercised across arbitrary boundaries. */
    if (count > 7) count = 7;
    memcpy(buf, response_bytes + response_offset, count);
    response_offset += count;
    return (long)count;
}

void wii_tls_close(wii_tls_conn *conn) { (void)conn; }

static void test_authenticated_query(void) {
    static const char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 11\r\n"
        "DPoP-Nonce: nonce-1\r\n\r\n"
        "{\"ok\":true}";
    wf_xrpc_param params[] = {{"actor", "alice test"}, {"limit", "20"}};
    wf_response out = {0};
    wf_xrpc_client *client = wf_xrpc_client_new("https://bsky.social/");
    assert(client);
    wf_xrpc_client_set_auth(client, "token");
    fake_response(response);

    assert(wf_xrpc_query_params(client, "app.bsky.actor.getProfile",
                                params, 2, &out) == WF_OK);
    assert(strcmp(connected_host, "bsky.social") == 0 && connected_port == 443);
    assert(strstr(request_bytes,
                  "GET /xrpc/app.bsky.actor.getProfile?actor=alice%20test&limit=20 "
                  "HTTP/1.1\r\n") != NULL);
    assert(occurrences(request_bytes, "Authorization: Bearer token\r\n") == 1);
    assert(out.status == 200 && out.body_len == 11);
    assert(strcmp(out.body, "{\"ok\":true}") == 0);
    assert(strcmp(out.dpop_nonce, "nonce-1") == 0);
    wf_response_free(&out);
    wf_xrpc_client_free(client);
}

static void test_json_procedure_headers(void) {
    static const char response[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}";
    wf_response out = {0};
    wf_xrpc_client *client = wf_xrpc_client_new("https://example.test");
    assert(client);
    fake_response(response);

    assert(wf_xrpc_procedure(client, "com.atproto.server.createSession",
                             "{\"x\":1}", &out) == WF_OK);
    assert(occurrences(request_bytes, "Content-Type: application/json\r\n") == 1);
    assert(occurrences(request_bytes, "Content-Length: 7\r\n") == 1);
    assert(strstr(request_bytes, "\r\n\r\n{\"x\":1}") != NULL);
    wf_response_free(&out);
    wf_xrpc_client_free(client);
}

static void test_chunked_response(void) {
    static const char response[] =
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "4\r\nWiki\r\n5;ext=yes\r\npedia\r\n0\r\n\r\n";
    wf_response out = {0};
    wf_xrpc_client *client = wf_xrpc_client_new("https://example.test");
    assert(client);
    fake_response(response);
    assert(wf_http_get(client, "https://example.test/data", &out) == WF_OK);
    assert(out.body_len == 9 && strcmp(out.body, "Wikipedia") == 0);
    wf_response_free(&out);
    wf_xrpc_client_free(client);
}

static void test_short_content_length_fails(void) {
    static const char response[] =
        "HTTP/1.1 200 OK\r\nContent-Length: 8\r\n\r\nshort";
    wf_response out = {0};
    wf_xrpc_client *client = wf_xrpc_client_new("https://example.test");
    assert(client);
    fake_response(response);
    assert(wf_http_get(client, "https://example.test/data", &out) ==
           WF_ERR_NETWORK);
    wf_xrpc_client_free(client);
}

int main(void) {
    test_authenticated_query();
    test_json_procedure_headers();
    test_chunked_response();
    test_short_content_length_fails();
    puts("Wii XRPC host transport tests passed");
    return 0;
}
