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
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "test.h"
#include "wolfram/xrpc.h"
#ifdef WOLFRAM_HAS_SPACES
#include <cJSON.h>

#include "spaces_vectors.h"
#include "wolfram/spaces/httpsig.h"
#endif

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

/* The number of `name` lines in a request, and the value of the first. */
static int request_header(const char *req, const char *name, char *value,
                          size_t size) {
    size_t name_len = strlen(name);
    const char *line = strstr(req, "\r\n");
    int count = 0;

    while (line && strncmp(line, "\r\n\r\n", 4) != 0) {
        line += 2;
        if (strncasecmp(line, name, name_len) == 0 && line[name_len] == ':') {
            const char *start = line + name_len + 1;
            while (*start == ' ') start++;
            if (count == 0 && value)
                snprintf(value, size, "%.*s", (int)strcspn(start, "\r\n"),
                         start);
            count++;
        }
        line = strstr(line, "\r\n");
    }
    return count;
}

#ifdef WOLFRAM_HAS_SPACES
/* What the space host side of the loopback server answers with. */
static struct {
    const char *cnf_kid;
    const char *header;
    const char *signature_input;
    const char *signature;
    const char *body;
} space_reply;

/* Answers only a request whose credential signature verifies. */
static void serve_space(int fd, const char *req) {
    char authorization[2048] = "";
    char audience[256] = "";
    char input[512] = "";
    char signature[256] = "";
    char out[2048];

    if (request_header(req, "Authorization", authorization,
                       sizeof(authorization)) != 1 ||
        request_header(req, "Atproto-Space-Audience", audience,
                       sizeof(audience)) != 1 ||
        request_header(req, "Signature-Input", input, sizeof(input)) != 1 ||
        request_header(req, "Signature", signature, sizeof(signature)) != 1 ||
        wf_space_credential_verify(authorization, audience, input, signature,
                                   space_reply.cnf_kid) != WF_OK) {
        send_text(fd, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n"
                      "Connection: close\r\n\r\n");
        return;
    }
    snprintf(out, sizeof(out),
             "HTTP/1.1 200 OK\r\nAtproto-Space-Commit: %s\r\n"
             "Signature-Input: %s\r\nSignature: %s\r\n"
             "Vary: Accept-Signature\r\nContent-Length: %zu\r\n"
             "Connection: close\r\n\r\n%s",
             space_reply.header, space_reply.signature_input,
             space_reply.signature, strlen(space_reply.body), space_reply.body);
    send_text(fd, out);
}
#endif

/* The path picks the reply. */
static void serve_one(int fd) {
    char req[8192];
    ssize_t n = read(fd, req, sizeof(req) - 1);
    char out[256];

    if (n <= 0) return;
    req[n] = '\0';
#ifdef WOLFRAM_HAS_SPACES
    if (strstr(req, "/space")) {
        serve_space(fd, req);
        return;
    }
#endif
    if (strstr(req, "/auth")) {
        char first[128] = "";
        int count = request_header(req, "Authorization", first, sizeof(first));
        snprintf(out, sizeof(out),
                 "HTTP/1.1 200 OK\r\nX-Auth-Count: %d\r\nX-Auth: %s\r\n"
                 "Content-Length: 2\r\nConnection: close\r\n\r\n{}",
                 count, first);
        send_text(fd, out);
    } else if (strstr(req, "/redirect")) {
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

/* A caller's Authorization replaces the client's, whatever its case. */
static void test_one_authorization(void) {
    char base[64];
    char url[96];
    wf_xrpc_client *c;
    wf_response r = {0};
    wf_http_header own = {"authorization", "Own value"};

    snprintf(base, sizeof(base), "http://127.0.0.1:%d", port);
    snprintf(url, sizeof(url), "%s/auth", base);
    c = wf_xrpc_client_new(base);
    wf_xrpc_client_set_auth(c, "session");
    WF_CHECK(wf_xrpc_client_set_capture_headers(c, 1) == WF_OK);

#define SENT(expected)                                                         \
    do {                                                                       \
        WF_CHECK(header_is(&r, "X-Auth-Count", "1"));                          \
        WF_CHECK(header_is(&r, "X-Auth", expected));                           \
        wf_response_free(&r);                                                  \
    } while (0)

    WF_CHECK(wf_http_get_with_headers(c, url, NULL, 0, &r) == WF_OK);
    SENT("Bearer session");
    WF_CHECK(wf_http_get_with_headers(c, url, &own, 1, &r) == WF_OK);
    SENT("Own value");
    WF_CHECK(wf_http_post(c, url, "application/json", "{}", &own, 1, &r) ==
             WF_OK);
    SENT("Own value");
    WF_CHECK(wf_xrpc_upload_blob_with_headers(c, "auth", "x", 1, "text/plain",
                                              &own, 1, &r) == WF_OK);
    SENT("Own value");
#undef SENT
    wf_xrpc_client_free(c);
}

#ifdef WOLFRAM_HAS_SPACES
/*
 * A whole credentialed read: sign the request, read the MAC headers through the
 * transport and verify the commit, with the field guide's values.
 */
static void test_space_read(void) {
    cJSON *root = vec_load("httpsig.json");
    const cJSON *mac = cJSON_GetObjectItemCaseSensitive(root, "mac");
    const cJSON *vec = cJSON_GetObjectItemCaseSensitive(mac, "commit");
    const cJSON *cred = cJSON_GetArrayItem(
        cJSON_GetObjectItemCaseSensitive(root, "credential_signatures"), 1);
    char *body =
        cJSON_PrintUnformatted(cJSON_GetObjectItemCaseSensitive(vec, "json"));
    const char *accept = vec_str(vec, "accept_signature");
    unsigned char secret[32];
    char authorization[2048];
    char signature_input[512];
    char base[64];
    char url[96];
    char *covered = NULL;
    char *input = NULL;
    char *mac_signature = NULL;
    wf_signing_key key;
    wf_space_signature signed_request = {0};
    wf_space_commit commit = {0};
    wf_xrpc_client *c;
    wf_response r = {0};
    unsigned i;

    WF_CHECK(root && body && accept);
    if (!root || !body || !accept) return;
    for (i = 0; i < 32; i++) {
        unsigned byte = 0;
        WF_CHECK(sscanf(vec_str(mac, "secret") + 2 * i, "%2x", &byte) == 1);
        secret[i] = (unsigned char)byte;
    }
    snprintf(authorization, sizeof(authorization),
             "Atproto-Space-Credential %s", vec_str(cred, "credential"));
    snprintf(signature_input, sizeof(signature_input), "mac=%s",
             accept + strlen("mac="));
    space_reply.cnf_kid = vec_str(cred, "cnf_kid");
    space_reply.header = vec_str(vec, "header");
    space_reply.signature_input = signature_input;
    space_reply.signature = vec_str(vec, "signature");
    space_reply.body = body;

    WF_CHECK(wf_signing_key_from_hex(WF_KEY_TYPE_P256,
                                     vec_str(root, "credential_key_hex"),
                                     &key) == WF_OK);
    WF_CHECK(wf_space_credential_sign(&key, authorization,
                                      vec_str(cred, "audience"),
                                      &signed_request) == WF_OK);

    snprintf(base, sizeof(base), "http://127.0.0.1:%d", port);
    snprintf(url, sizeof(url), "%s/space", base);
    c = wf_xrpc_client_new(base);
    /* A signed-in client must not add its session to a credentialed read. */
    wf_xrpc_client_set_auth(c, "session");
    WF_CHECK(wf_xrpc_client_set_capture_headers(c, 1) == WF_OK);
    {
        wf_http_header headers[] = {
            {"Authorization", authorization},
            {"Atproto-Space-Audience", vec_str(cred, "audience")},
            {"Signature-Input", signed_request.signature_input},
            {"Signature", signed_request.signature},
            {"Accept-Signature", accept},
        };
        WF_CHECK(wf_http_get_with_headers(c, url, headers, 5, &r) == WF_OK);
    }
    WF_CHECK(wf_response_header(&r, "Atproto-Space-Commit", &covered) == WF_OK);
    WF_CHECK(wf_response_header(&r, "Signature-Input", &input) == WF_OK);
    WF_CHECK(wf_response_header(&r, "Signature", &mac_signature) == WF_OK);
    WF_CHECK(r.body &&
             wf_space_commit_parse_json(r.body, r.body_len, &commit) == WF_OK);
    WF_CHECK(commit.repo &&
             wf_space_commit_verify(&commit, commit.repo, commit.space,
                                    vec_str(mac, "key_share"), secret, covered,
                                    input, mac_signature) == WF_OK);

    wf_space_commit_free(&commit);
    wf_space_signature_free(&signed_request);
    wf_response_free(&r);
    wf_xrpc_client_free(c);
    free(covered);
    free(input);
    free(mac_signature);
    cJSON_free(body);
    cJSON_Delete(root);
}
#endif

int main(void) {
    test_storage();
    if (start_server() != 0) {
        fprintf(stderr, "could not start loopback server\n");
        return 1;
    }
    test_network();
    test_one_authorization();
#ifdef WOLFRAM_HAS_SPACES
    test_space_read();
#endif
    WF_TEST_SUMMARY();
}
