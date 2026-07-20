/**
 * xrpc_wii.c — Real XRPC transport for the Nintendo Wii.
 *
 * Implements the full wire-level XRPC/HTTP client over lwIP (DNS + TCP) and
 * mbedTLS (TLS) via the wii_tls wrapper. Replaces the previous honest stub
 * with a working HTTPS/1.1 client: URL building, parameter encoding, header
 * construction, response parsing (Content-Length and chunked), DPoP-Nonce
 * capture, and one-shot token refresh+retry — mirroring the desktop
 * (libcurl) transport semantics.
 */

#include "wolfram/xrpc.h"
#include "wolfram/version.h"
#include "wii_tls.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

struct wf_xrpc_client {
    char *base_url;
    char *auth_header;
    wf_xrpc_handler_fn handler;
    void *handler_userdata;
    wf_xrpc_refresh_fn refresh_cb;
    void *refresh_userdata;
    int refreshing;
};

/* ── Growable byte buffer ───────────────────────────────────────────── */

struct wf_buffer {
    char  *data;
    size_t len;
    size_t cap;
};

static int wf_buffer_append(struct wf_buffer *b, const void *src, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 4096;
        while (cap < b->len + n + 1) cap *= 2;
        char *grown = realloc(b->data, cap);
        if (!grown) return -1;
        b->data = grown;
        b->cap = cap;
    }
    if (n) memcpy(b->data + b->len, src, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

/* ── URL helpers ────────────────────────────────────────────────────── */

/* Strip a trailing slash, if any, so we can join paths predictably. */
static char *wf_normalise_base(const char *base_url) {
    size_t len = strlen(base_url);
    while (len > 0 && base_url[len - 1] == '/') len--;
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, base_url, len);
    out[len] = '\0';
    return out;
}

/* Percent-encode `in` per RFC 3986 (unreserved characters pass through).
 * Returns a caller-owned string, or NULL on allocation failure. */
static char *wf_url_encode(const char *in) {
    static const char hex[] = "0123456789ABCDEF";
    /* Worst case: every byte becomes %XX (3x). */
    char *out = malloc(strlen(in) * 3 + 1);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; in[i]; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out[j++] = (char)c;
        } else {
            out[j++] = '%';
            out[j++] = hex[(c >> 4) & 0xF];
            out[j++] = hex[c & 0xF];
        }
    }
    out[j] = '\0';
    return out;
}

/* Parse an absolute URL into host/port/path. `path` includes the query
 * string. Returns WF_OK or WF_ERR_INVALID_ARG. */
static wf_status wf_parse_url(const char *url, char *host, size_t host_cap,
                              uint16_t *port, char *path, size_t path_cap) {
    const char *p = url;
    const char *scheme_end = strstr(url, "://");
    int is_https = 1;
    if (scheme_end) {
        size_t slen = (size_t)(scheme_end - url);
        is_https = (slen == 5 && strncmp(url, "https", 5) == 0);
        p = scheme_end + 3;
    }
    *port = is_https ? 443 : 80;

    const char *host_start = p;
    const char *host_end = p;
    while (*host_end && *host_end != ':' && *host_end != '/' &&
           *host_end != '?') {
        host_end++;
    }
    size_t hlen = (size_t)(host_end - host_start);
    if (hlen == 0 || hlen >= host_cap) return WF_ERR_INVALID_ARG;
    memcpy(host, host_start, hlen);
    host[hlen] = '\0';

    p = host_end;
    if (*p == ':') {
        p++;
        long parsed = 0;
        while (isdigit((unsigned char)*p)) parsed = parsed * 10 + (*p++ - '0');
        if (parsed > 0 && parsed <= 65535) *port = (uint16_t)parsed;
    }

    const char *path_start = p;
    if (*path_start != '/') path_start = "/";  /* implicit root */
    size_t plen = strlen(path_start);
    if (plen + 1 > path_cap) return WF_ERR_INVALID_ARG;
    memcpy(path, path_start, plen + 1);
    return WF_OK;
}

/* ── Response parsing ───────────────────────────────────────────────── */

static int wf_find_header_value(const char *headers, const char *name,
                                char *out, size_t out_cap) {
    /* header block (no body) is NUL-terminated within the parse window. */
    size_t nlen = strlen(name);
    const char *p = headers;
    while (*p) {
        if (strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
            const char *v = p + nlen + 1;
            while (*v == ' ' || *v == '\t') v++;
            size_t i = 0;
            while (v[i] && v[i] != '\r' && v[i] != '\n') {
                if (i + 1 < out_cap) out[i] = v[i];
                i++;
            }
            out[i < out_cap ? i : out_cap - 1] = '\0';
            return 1;
        }
        /* advance to next line */
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return 0;
}

static int wf_has_header(const wf_http_header *headers, size_t count,
                         const char *name) {
    for (size_t i = 0; i < count; i++) {
        if (headers[i].name && strcasecmp(headers[i].name, name) == 0)
            return 1;
    }
    return 0;
}

/* Read the full HTTP response (headers + body) over `conn` into `buf`,
 * capturing the DPoP-Nonce. Sets *status and returns WF_OK / WF_ERR_* . */
static wf_status wf_http_read_response(wii_tls_conn *conn, struct wf_buffer *buf,
                                       long *status, char **dpop_nonce) {
    /* Phase 1: read until we have the complete header block. */
    int header_done = 0;
    size_t header_end = 0;
    for (;;) {
        if (buf->len >= 3) {
            /* scan for "\r\n\r\n" */
            for (size_t i = 0; i + 3 < buf->len; i++) {
                if (buf->data[i] == '\r' && buf->data[i + 1] == '\n' &&
                    buf->data[i + 2] == '\r' && buf->data[i + 3] == '\n') {
                    header_end = i + 4;
                    header_done = 1;
                    break;
                }
            }
        }
        if (header_done) break;

        char tmp[1024];
        long n = wii_tls_recv(conn, tmp, sizeof(tmp));
        if (n < 0) return WF_ERR_NETWORK;
        if (n == 0) {
            /* Connection closed before headers completed. */
            if (buf->len == 0) return WF_ERR_NETWORK;
            break;
        }
        if (wf_buffer_append(buf, tmp, (size_t)n) != 0) return WF_ERR_ALLOC;
    }

    if (!header_done && buf->len == 0) return WF_ERR_NETWORK;

    /* Parse status line: "HTTP/1.1 200 OK". */
    *status = 0;
    if (buf->len >= 8 && strncmp(buf->data, "HTTP/", 5) == 0) {
        const char *code = buf->data + 5;
        while (*code && *code != ' ') code++;
        if (*code == ' ') *status = strtol(code + 1, NULL, 10);
    }

    /* Isolate the header window for name lookups. */
    char *hdr_win = buf->data;
    if (header_done) hdr_win[header_end - 2] = '\0';  /* terminate before final CRLF */

    char cl_buf[32] = {0};
    long content_length = -1;
    int chunked = 0;
    char nonce_buf[256] = {0};

    if (wf_find_header_value(hdr_win, "Content-Length", cl_buf, sizeof(cl_buf)))
        content_length = strtol(cl_buf, NULL, 10);
    if (wf_find_header_value(hdr_win, "Transfer-Encoding", cl_buf,
                             sizeof(cl_buf)) &&
        strncasecmp(cl_buf, "chunked", 7) == 0)
        chunked = 1;
    if (wf_find_header_value(hdr_win, "DPoP-Nonce", nonce_buf, sizeof(nonce_buf))) {
        *dpop_nonce = strdup(nonce_buf);
    }

    /* Phase 2: read the body. */
    size_t body_have = header_done ? (buf->len - header_end) : 0;
    size_t body_want = (size_t)(content_length > 0 ? content_length : 0);

    if (chunked) {
        /* We request Connection: close, so collect the complete framed body
         * before decoding it. Keeping source and destination buffers separate
         * avoids overwriting a later chunk while compacting an earlier one. */
        for (;;) {
            char tmp[1024];
            long n = wii_tls_recv(conn, tmp, sizeof(tmp));
            if (n < 0) return WF_ERR_NETWORK;
            if (n == 0) break;
            if (wf_buffer_append(buf, tmp, (size_t)n) != 0) return WF_ERR_ALLOC;
        }

        struct wf_buffer decoded = {0};
        size_t off = header_end;
        for (;;) {
            size_t line_end = off;
            while (line_end + 1 < buf->len &&
                   !(buf->data[line_end] == '\r' &&
                     buf->data[line_end + 1] == '\n')) line_end++;
            if (line_end + 1 >= buf->len) {
                free(decoded.data);
                return WF_ERR_PARSE;
            }
            size_t sz = 0;
            size_t i = off;
            int digits = 0;
            while (i < line_end) {
                char c = buf->data[i];
                if (c == ';') break;
                int d = -1;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                if (d < 0) {
                    free(decoded.data);
                    return WF_ERR_PARSE;
                }
                digits = 1;
                sz = (sz << 4) | (size_t)d;
                i++;
            }
            if (!digits) {
                free(decoded.data);
                return WF_ERR_PARSE;
            }
            size_t data_start = line_end + 2;
            if (sz == 0) break;
            if (sz > buf->len - data_start ||
                buf->len - data_start - sz < 2 ||
                buf->data[data_start + sz] != '\r' ||
                buf->data[data_start + sz + 1] != '\n' ||
                wf_buffer_append(&decoded, buf->data + data_start, sz) != 0) {
                free(decoded.data);
                return WF_ERR_PARSE;
            }
            off = data_start + sz + 2;
        }
        if (!decoded.data && wf_buffer_append(&decoded, NULL, 0) != 0)
            return WF_ERR_ALLOC;
        free(buf->data);
        *buf = decoded;
        body_have = decoded.len;
    } else if (content_length > 0) {
        while (body_have < body_want) {
            char tmp[1024];
            size_t need = body_want - body_have;
            long n = wii_tls_recv(conn, tmp, need < sizeof(tmp) ? need : sizeof(tmp));
            if (n < 0) return WF_ERR_NETWORK;
            if (n == 0) break;  /* short read; accept what we have */
            if (wf_buffer_append(buf, tmp, (size_t)n) != 0) return WF_ERR_ALLOC;
            body_have += (size_t)n;
        }
        if (body_have != body_want) return WF_ERR_NETWORK;
    } else {
        /* No length and not chunked: read until the peer closes. */
        for (;;) {
            char tmp[1024];
            long n = wii_tls_recv(conn, tmp, sizeof(tmp));
            if (n < 0) return WF_ERR_NETWORK;
            if (n == 0) break;
            if (wf_buffer_append(buf, tmp, (size_t)n) != 0) return WF_ERR_ALLOC;
            body_have += (size_t)n;
        }
    }

    /* The body is the final body_have bytes; shift it to the front so
     * out->body points at a clean, NUL-terminated buffer. */
    if (header_done && !chunked) {
        memmove(buf->data, buf->data + header_end, body_have);
    }
    buf->len = body_have;
    buf->data[body_have] = '\0';
    return WF_OK;
}

/* ── Core request ───────────────────────────────────────────────────── */

static wf_status wf_xrpc_perform(wf_xrpc_client *client, const char *method,
                                 const char *url, const char *content_type,
                                 const void *body, size_t body_len,
                                 const wf_http_header *extra, size_t extra_count,
                                 wf_response *out) {
    memset(out, 0, sizeof(*out));

    /* Test seam: a handler replaces real network I/O. */
    if (client->handler) {
        /* Build a transient header array (mirrors desktop behaviour). */
        wf_http_header *harr = NULL;
        size_t hcount = 0;
        if (extra && extra_count) {
            harr = calloc(extra_count, sizeof(*harr));
            if (!harr) return WF_ERR_ALLOC;
            for (size_t i = 0; i < extra_count; i++) {
                harr[i].name = strdup(extra[i].name);
                harr[i].value = strdup(extra[i].value);
            }
            hcount = extra_count;
        }
        wf_status s = client->handler(client->handler_userdata, method, url,
                                      content_type, (const char *)body,
                                      body_len, harr, hcount, out);
        for (size_t i = 0; i < hcount; i++) {
            free((void *)harr[i].name);
            free((void *)harr[i].value);
        }
        free(harr);
        return s;
    }

    char host[256];
    uint16_t port = 443;
    char path[2048];
    if (wf_parse_url(url, host, sizeof(host), &port, path, sizeof(path)) != WF_OK)
        return WF_ERR_INVALID_ARG;

    wii_tls_conn *conn = wii_tls_connect(host, port);
    if (!conn) return WF_ERR_NETWORK;

    /* Build the request head. */
    struct wf_buffer req = {0};
    char *cl = NULL;
    if (body_len > 0) {
        cl = malloc(24);
        snprintf(cl, 24, "%zu", body_len);
    }
    const char *host_header = host;  /* path already carries query */
    (void)host_header;

    char line[320];
    int ok = 1;
    ok &= wf_buffer_append(&req, method, strlen(method)) == 0;
    ok &= wf_buffer_append(&req, " ", 1) == 0;
    ok &= wf_buffer_append(&req, path, strlen(path)) == 0;
    ok &= wf_buffer_append(&req, " HTTP/1.1\r\n", 11) == 0;
    snprintf(line, sizeof(line), "Host: %s\r\n", host);
    ok &= wf_buffer_append(&req, line, strlen(line)) == 0;
    snprintf(line, sizeof(line), "User-Agent: wolfram/%s\r\n",
             WOLFRAM_VERSION_STRING);
    ok &= wf_buffer_append(&req, line, strlen(line)) == 0;
    ok &= wf_buffer_append(&req, "Connection: close\r\n", 19) == 0;
    if (client->auth_header &&
        !wf_has_header(extra, extra_count, "Authorization")) {
        ok &= wf_buffer_append(&req, client->auth_header,
                               strlen(client->auth_header)) == 0;
        ok &= wf_buffer_append(&req, "\r\n", 2) == 0;
    }
    if (content_type &&
        !wf_has_header(extra, extra_count, "Content-Type")) {
        snprintf(line, sizeof(line), "Content-Type: %s\r\n", content_type);
        ok &= wf_buffer_append(&req, line, strlen(line)) == 0;
    }
    if (cl) {
        snprintf(line, sizeof(line), "Content-Length: %s\r\n", cl);
        ok &= wf_buffer_append(&req, line, strlen(line)) == 0;
    }
    for (size_t i = 0; i < extra_count; i++) {
        snprintf(line, sizeof(line), "%s: %s\r\n",
                 extra[i].name ? extra[i].name : "",
                 extra[i].value ? extra[i].value : "");
        ok &= wf_buffer_append(&req, line, strlen(line)) == 0;
    }
    ok &= wf_buffer_append(&req, "\r\n", 2) == 0;
    free(cl);
    if (!ok) {
        free(req.data);
        wii_tls_close(conn);
        return WF_ERR_ALLOC;
    }

    /* Send head, then body. */
    long sent = wii_tls_send(conn, req.data, req.len);
    free(req.data);
    if (sent < 0) {
        wii_tls_close(conn);
        return WF_ERR_NETWORK;
    }
    if (body && body_len > 0) {
        sent = wii_tls_send(conn, body, body_len);
        if (sent < 0) {
            wii_tls_close(conn);
            return WF_ERR_NETWORK;
        }
    }

    /* Read response. */
    struct wf_buffer resp = {0};
    long status = 0;
    char *nonce = NULL;
    wf_status rc = wf_http_read_response(conn, &resp, &status, &nonce);
    wii_tls_close(conn);

    if (rc != WF_OK) {
        free(resp.data);
        free(nonce);
        return rc;
    }

    out->status = status;
    out->body = resp.data;       /* ownership transferred to caller */
    out->body_len = resp.len;
    out->dpop_nonce = nonce;

    if (status < 200 || status >= 300) return WF_ERR_HTTP;
    return WF_OK;
}

/* Detect an expired/invalid access token, mirroring the desktop test. */
static int wf_xrpc_response_is_expired(const wf_response *out) {
    if (!out) return 0;
    if (out->status == 401) return 1;
    char *err = NULL;
    if (wf_xrpc_error(out, &err, NULL) == WF_OK && err) {
        int match = strcmp(err, "ExpiredToken") == 0 ||
                    strcmp(err, "InvalidToken") == 0;
        free(err);
        return match;
    }
    free(err);
    return 0;
}

/* Build the request headers (auth + optional content-type) for one attempt. */
static wf_status wf_xrpc_build_headers(wf_xrpc_client *client, int is_post,
                                       const char *content_type,
                                       wf_http_header **out_headers,
                                       size_t *out_count) {
    wf_http_header *arr = calloc(is_post ? 2 : 1, sizeof(*arr));
    if (!arr) return WF_ERR_ALLOC;
    size_t n = 0;
    if (client->auth_header) {
        arr[n].name = "Authorization";
        arr[n].value = client->auth_header + strlen("Authorization: ");
        n++;
    }
    if (is_post && content_type) {
        arr[n].name = "Content-Type";
        arr[n].value = content_type;
        n++;
    }
    *out_headers = arr;
    *out_count = n;
    return WF_OK;
}

/* Shared request path for both query (GET) and POST variants, with one
 * refresh+retry on an expired token. */
static wf_status wf_xrpc_request(wf_xrpc_client *client,
                                 const char *nsid,
                                 const char *query_string,
                                 const void *body,
                                 size_t body_len,
                                 const char *content_type,
                                 int is_post,
                                 wf_response *out) {
    if (!client || !nsid || !out || (body_len > 0 && !body) ||
        (is_post && !content_type)) {
        return WF_ERR_INVALID_ARG;
    }

    int had_auth = client->auth_header != NULL;
    wf_status status = WF_OK;
    char *url = NULL;

    for (int attempt = 0; ; attempt++) {
        free(url);
        size_t url_cap = strlen(client->base_url) + strlen("/xrpc/") +
                         strlen(nsid) + 1 +
                         (query_string ? strlen(query_string) + 1 : 0);
        url = malloc(url_cap);
        if (!url) return WF_ERR_ALLOC;
        if (query_string && query_string[0] != '\0') {
            snprintf(url, url_cap, "%s/xrpc/%s?%s", client->base_url, nsid,
                     query_string);
        } else {
            snprintf(url, url_cap, "%s/xrpc/%s", client->base_url, nsid);
        }

        wf_http_header *headers = NULL;
        size_t hcount = 0;
        status = wf_xrpc_build_headers(client, is_post, content_type,
                                       &headers, &hcount);
        if (status != WF_OK) break;

        status = wf_xrpc_perform(client, is_post ? "POST" : "GET", url,
                                 content_type, body, body_len,
                                 headers, hcount, out);
        free(headers);

        if (attempt == 0 && had_auth && client->refresh_cb &&
            !client->refreshing && wf_xrpc_response_is_expired(out)) {
            client->refreshing = 1;
            wf_status refreshed = client->refresh_cb(client->refresh_userdata);
            client->refreshing = 0;
            if (refreshed == WF_OK) {
                wf_response_free(out);
                continue;
            }
        }
        break;
    }

    free(url);
    return status;
}

/* ── Public API (unchanged signatures) ──────────────────────────────── */

wf_xrpc_client *wf_xrpc_client_new(const char *service_base_url) {
    if (!service_base_url || service_base_url[0] == '\0') return NULL;
    wf_xrpc_client *client = calloc(1, sizeof(*client));
    if (!client) return NULL;
    client->base_url = wf_normalise_base(service_base_url);
    if (!client->base_url) { free(client); return NULL; }
    return client;
}

void wf_xrpc_client_free(wf_xrpc_client *client) {
    if (!client) return;
    free(client->base_url);
    free(client->auth_header);
    free(client);
}

void wf_xrpc_set_handler(wf_xrpc_client *client, wf_xrpc_handler_fn fn,
                         void *userdata) {
    if (!client) return;
    client->handler = fn;
    client->handler_userdata = userdata;
}

void wf_xrpc_client_set_auth(wf_xrpc_client *client, const char *access_jwt) {
    if (!client) return;
    free(client->auth_header);
    client->auth_header = NULL;
    if (!access_jwt) return;
    size_t needed = strlen("Authorization: Bearer ") + strlen(access_jwt) + 1;
    client->auth_header = malloc(needed);
    if (client->auth_header) {
        snprintf(client->auth_header, needed, "Authorization: Bearer %s",
                 access_jwt);
    }
}

wf_status wf_xrpc_client_set_base_url(wf_xrpc_client *client,
                                      const char *service_base_url) {
    if (!client || !service_base_url || service_base_url[0] == '\0')
        return WF_ERR_INVALID_ARG;
    char *normalised = wf_normalise_base(service_base_url);
    if (!normalised) return WF_ERR_ALLOC;
    free(client->base_url);
    client->base_url = normalised;
    return WF_OK;
}

void wf_xrpc_client_set_refresh_handler(wf_xrpc_client *client,
                                         wf_xrpc_refresh_fn fn, void *userdata) {
    if (!client) return;
    client->refresh_cb = fn;
    client->refresh_userdata = userdata;
}

char *wf_xrpc_get_base_url(wf_xrpc_client *client) {
    if (!client || !client->base_url) return NULL;
    return strdup(client->base_url);
}

void wf_response_free(wf_response *res) {
    if (!res) return;
    free(res->body);
    free(res->dpop_nonce);
    res->body = NULL;
    res->body_len = 0;
    res->dpop_nonce = NULL;
    res->status = 0;
}

wf_status wf_xrpc_query(wf_xrpc_client *client,
                         const char *nsid,
                         const char *query_string,
                         wf_response *out) {
    return wf_xrpc_request(client, nsid, query_string, NULL, 0, NULL, 0, out);
}

wf_status wf_xrpc_query_params(wf_xrpc_client *client,
                                const char *nsid,
                                const wf_xrpc_param *params,
                                size_t param_count,
                                wf_response *out) {
    if (!client || !nsid || !out || (param_count > 0 && !params))
        return WF_ERR_INVALID_ARG;
    if (param_count == 0)
        return wf_xrpc_query(client, nsid, NULL, out);

    size_t query_len = 1;
    wf_status status = WF_OK;
    char **names = calloc(param_count, sizeof(*names));
    char **values = calloc(param_count, sizeof(*values));
    if (!names || !values) {
        free(names); free(values);
        return WF_ERR_ALLOC;
    }
    for (size_t i = 0; i < param_count; i++) {
        if (!params[i].name || !params[i].value) { status = WF_ERR_INVALID_ARG; break; }
        names[i] = wf_url_encode(params[i].name);
        values[i] = wf_url_encode(params[i].value);
        if (!names[i] || !values[i]) { status = WF_ERR_ALLOC; break; }
        query_len += strlen(names[i]) + 1 + strlen(values[i]);
        if (i > 0) query_len++;
    }

    char *query = NULL;
    if (status == WF_OK) {
        query = malloc(query_len);
        if (!query) status = WF_ERR_ALLOC;
    }
    if (status == WF_OK) {
        size_t off = 0;
        for (size_t i = 0; i < param_count; i++) {
            int w = snprintf(query + off, query_len - off, "%s%s=%s",
                             i ? "&" : "", names[i], values[i]);
            if (w < 0 || (size_t)w >= query_len - off) { status = WF_ERR_ALLOC; break; }
            off += (size_t)w;
        }
    }
    for (size_t i = 0; i < param_count; i++) { free(names[i]); free(values[i]); }
    free(names); free(values);

    if (status == WF_OK) {
        status = wf_xrpc_query(client, nsid, query, out);
    }
    free(query);
    return status;
}

wf_status wf_xrpc_procedure(wf_xrpc_client *client,
                             const char *nsid,
                             const char *json_body,
                             wf_response *out) {
    if (!client || !nsid || !out) return WF_ERR_INVALID_ARG;
    size_t body_len = json_body ? strlen(json_body) : 0;
    return wf_xrpc_request(client, nsid, NULL, json_body, body_len,
                           "application/json", 1, out);
}

wf_status wf_xrpc_upload_blob(wf_xrpc_client *client,
                               const char *nsid,
                               const void *data,
                               size_t data_len,
                               const char *content_type,
                               wf_response *out) {
    if (!client || !nsid || !data || data_len == 0 || !content_type ||
        !*content_type || !out)
        return WF_ERR_INVALID_ARG;
    return wf_xrpc_upload_blob_with_headers(client, nsid, data, data_len,
                                            content_type, NULL, 0, out);
}

wf_status wf_xrpc_upload_blob_with_headers(
    wf_xrpc_client *client, const char *nsid, const void *data,
    size_t data_len, const char *content_type, const wf_http_header *headers,
    size_t header_count, wf_response *out) {
    if (!client || !nsid || !data || data_len == 0 || !content_type ||
        !*content_type || !out || (header_count && !headers))
        return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    char *base_url = wf_xrpc_get_base_url(client);
    if (!base_url) return WF_ERR_ALLOC;
    size_t url_cap = strlen(base_url) + strlen("/xrpc/") + strlen(nsid) + 1;
    char *url = malloc(url_cap);
    if (!url) { free(base_url); return WF_ERR_ALLOC; }
    snprintf(url, url_cap, "%s/xrpc/%s", base_url, nsid);
    free(base_url);

    /* Combine caller headers with the standard Content-Type. */
    size_t total = header_count + 1;
    wf_http_header *all = calloc(total, sizeof(*all));
    if (!all) { free(url); return WF_ERR_ALLOC; }
    all[0].name = "Content-Type";
    all[0].value = content_type;
    for (size_t i = 0; i < header_count; i++) all[1 + i] = headers[i];

    wf_status status = wf_xrpc_perform(client, "POST", url, content_type,
                                       data, data_len, all, total, out);
    free(all);
    free(url);
    return status;
}

wf_status wf_http_get_with_headers(wf_xrpc_client *client, const char *url,
                                   const wf_http_header *extra,
                                   size_t extra_count, wf_response *out) {
    if (!client || !url || !out || (extra_count && !extra))
        return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    return wf_xrpc_perform(client, "GET", url, NULL, NULL, 0,
                           extra, extra_count, out);
}

wf_status wf_http_get(wf_xrpc_client *client, const char *url, wf_response *out) {
    return wf_http_get_with_headers(client, url, NULL, 0, out);
}

wf_status wf_http_post(wf_xrpc_client *client, const char *url,
                        const char *content_type, const char *body,
                        const wf_http_header *extra, size_t extra_count,
                        wf_response *out) {
    if (!client || !url || !content_type || !body || !out ||
        (extra_count && !extra))
        return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    return wf_xrpc_perform(client, "POST", url, content_type, body,
                           strlen(body), extra, extra_count, out);
}

/* ── Error envelope decoding ─────────────────────────────────────────── */

wf_status wf_xrpc_error(const wf_response *resp,
                        char **out_error, char **out_message) {
    if (out_error) *out_error = NULL;
    if (out_message) *out_message = NULL;
    if (!resp) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_ParseWithLength(resp->body ? resp->body : "",
                                        resp->body_len);
    if (!root) return WF_ERR_PARSE;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_NOT_FOUND;
    }
    cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "error");
    if (!cJSON_IsString(err) || err->valuestring == NULL) {
        cJSON_Delete(root);
        return WF_ERR_NOT_FOUND;
    }
    char *e = NULL, *m = NULL;
    const cJSON *msg = cJSON_GetObjectItemCaseSensitive(root, "message");
    if (cJSON_IsString(msg) && msg->valuestring) {
        size_t n = strlen(msg->valuestring) + 1;
        m = malloc(n);
        if (m) memcpy(m, msg->valuestring, n);
    }
    size_t n = strlen(err->valuestring) + 1;
    e = malloc(n);
    if (e) memcpy(e, err->valuestring, n);
    if (!e) { free(m); cJSON_Delete(root); return WF_ERR_PARSE; }

    if (out_error) *out_error = e; else free(e);
    if (out_message) *out_message = m; else free(m);
    cJSON_Delete(root);
    return WF_OK;
}
