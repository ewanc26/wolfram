/*
 * xrpc_server.c — minimal XRPC server on libmicrohttpd.
 *
 * Routes /xrpc/<nsid> GET/POST requests to registered handlers. Supports
 * optional auth middleware, query parameter parsing, and JSON body parsing.
 *
 * Requires libmicrohttpd (built only when WOLFRAM_BUILD_SERVER=ON).
 */

#include "wolfram/xrpc_server.h"

#include <cJSON.h>
#include <microhttpd.h>

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Simple growable buffer used to accumulate POST request bodies. */
typedef struct post_buf {
    char *data;
    size_t len;
    size_t cap;
} post_buf;

static int post_buf_append(post_buf *b, const char *data, size_t len) {
    if (!b || (!data && len > 0)) return 0;
    if (len == 0) return 1;
    size_t needed = b->len + len;
    if (needed > b->cap) {
        size_t new_cap = b->cap ? b->cap * 2 : 4096;
        while (new_cap < needed) new_cap *= 2;
        char *tmp = (char *)realloc(b->data, new_cap);
        if (!tmp) return 0;
        b->data = tmp;
        b->cap = new_cap;
    }
    memcpy(b->data + b->len, data, len);
    b->len = needed;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Per-route rate limiter support                                    */
/* ------------------------------------------------------------------ */

/** A single token-bucket entry keyed by an arbitrary string. */
typedef struct wf_rate_bucket {
    char                  *key;
    double                 tokens;
    time_t                 last_refill;
    struct wf_rate_bucket *next;
} wf_rate_bucket;

/** A single route whose rate limiting configuration was attached */
typedef struct wf_rate_limit_entry {
    char *route_key;            /* "GET:/xrpc/io.example.ping" */
    wf_rate_limiter *rl;       /* owned reference */
    struct wf_rate_limit_entry *next;
} wf_rate_limit_entry;

struct wf_rate_limiter {
    unsigned int     points;          /* Max tokens (burst capacity) */
    unsigned int     duration_seconds;/* Refill window */
    unsigned int     bucket_count;    /* Hash table size */
    wf_rate_bucket **buckets;         /* Hash table array, owned */
};

/** FNV-1a hash for a NUL-terminated string. */
static unsigned int wf_rl_hash(const char *key, unsigned int mod) {
    unsigned int h = 2166136261U;
    while (*key) {
        h ^= (unsigned char)*key++;
        h *= 16777619U;
    }
    return h % mod;
}

wf_rate_limiter *wf_rate_limiter_new(unsigned int points,
                                      unsigned int duration_seconds,
                                      unsigned int bucket_count) {
    wf_rate_limiter *rl;

    if (points == 0 || duration_seconds == 0) {
        return NULL;
    }
    if (bucket_count == 0) {
        bucket_count = 256;
    }
    rl = (wf_rate_limiter *)calloc(1, sizeof(*rl));
    if (!rl) {
        return NULL;
    }
    rl->points = points;
    rl->duration_seconds = duration_seconds;
    rl->bucket_count = bucket_count;
    rl->buckets = (wf_rate_bucket **)calloc(bucket_count, sizeof(wf_rate_bucket *));
    if (!rl->buckets) {
        free(rl);
        return NULL;
    }
    return rl;
}

void wf_rate_limiter_free(wf_rate_limiter *rl) {
    unsigned int i;
    if (!rl) {
        return;
    }
    for (i = 0; i < rl->bucket_count; i++) {
        wf_rate_bucket *b = rl->buckets[i];
        while (b) {
            wf_rate_bucket *next = b->next;
            free(b->key);
            free(b);
            b = next;
        }
    }
    free(rl->buckets);
    free(rl);
}

wf_status wf_rate_limiter_consume(wf_rate_limiter *rl,
                                   const char *key,
                                   unsigned int cost,
                                   unsigned int *out_retry_after) {
    unsigned int idx;
    wf_rate_bucket *b;
    time_t now;
    double refill_rate;
    double elapsed;

    if (!rl || !key || cost == 0) {
        return WF_ERR_INVALID_ARG;
    }

    now = time(NULL);
    refill_rate = (double)rl->points / (double)rl->duration_seconds;

    idx = wf_rl_hash(key, rl->bucket_count);
    b = rl->buckets[idx];

    /* Look up existing bucket */
    while (b) {
        if (strcmp(b->key, key) == 0) {
            break;
        }
        b = b->next;
    }

    if (b) {
        /* Refill tokens based on elapsed time */
        elapsed = difftime(now, b->last_refill);
        if (elapsed > 0) {
            b->tokens += elapsed * refill_rate;
            if (b->tokens > (double)rl->points) {
                b->tokens = (double)rl->points;
            }
        }
    } else {
        /* Create new bucket */
        b = (wf_rate_bucket *)calloc(1, sizeof(*b));
        if (!b) {
            return WF_ERR_ALLOC;
        }
        b->key = strdup(key);
        if (!b->key) {
            free(b);
            return WF_ERR_ALLOC;
        }
        b->tokens = (double)rl->points;
        b->last_refill = now;
        b->next = rl->buckets[idx];
        rl->buckets[idx] = b;
    }

    b->last_refill = now;

    if (b->tokens < (double)cost) {
        if (out_retry_after) {
            /* Seconds until one token is available */
            double wait = ((double)cost - b->tokens) / refill_rate;
            if (wait < 1.0) wait = 1.0;
            *out_retry_after = (unsigned int)(wait + 0.5);
        }
        return WF_ERR_RATE_LIMIT;
    }

    b->tokens -= (double)cost;
    if (out_retry_after) {
        *out_retry_after = 0;
    }
    return WF_OK;
}

/* ------------------------------------------------------------------ */
/* Route entry                                                         */
/* ------------------------------------------------------------------ */
typedef enum {
    WF_ROUTE_QUERY,
    WF_ROUTE_PROCEDURE,
} wf_route_kind;

typedef struct wf_route {
    char                     *nsid;
    wf_route_kind             kind;
    bool                      is_sse; /* true if this route uses Server-Sent Events */
    union {
        wf_xrpc_query_handler      query;
        wf_xrpc_procedure_handler  procedure;
        wf_xrpc_sse_handler        sse;
    } handler;
    void                     *ctx;
    struct wf_route          *next;   /* linked list */
} wf_route;

/* ------------------------------------------------------------------ */
/* Server struct                                                       */
/* ------------------------------------------------------------------ */
struct wf_xrpc_server {
    struct MHD_Daemon   *daemon;
    uint16_t             port;
    wf_route            *routes;
    wf_xrpc_auth_cb      auth_cb;
    void                *auth_ctx;
    wf_rate_limiter     *rate_limiter;        /* global IP-based limiter */
    wf_rate_limit_entry *rate_limit_entries;   /* per-route list */
    wf_xrpc_sse_stream  *sse_streams;          /* open SSE connections */
    pthread_mutex_t      sse_mutex;            /* guards sse_streams */
};

/* ------------------------------------------------------------------ */
/* Server-Sent Events (SSE) streaming                                  */
/* ------------------------------------------------------------------ */

/** A buffered chunk of bytes queued for the SSE connection. */
typedef struct wf_sse_chunk {
    char                 *data;
    size_t                len;
    size_t                off;        /* bytes already copied out */
    struct wf_sse_chunk  *next;
} wf_sse_chunk;

struct wf_xrpc_sse_stream {
    struct MHD_Connection *conn;       /* owning MHD connection */
    wf_xrpc_server        *server;     /* back-pointer for teardown */
    char                  *nsid;       /* route NSID (for diagnostics) */
    pthread_mutex_t        mutex;      /* guards chunks / closed / started */
    wf_sse_chunk          *chunks;     /* pending output, head */
    wf_sse_chunk          *chunks_tail;/* pending output, tail */
    bool                   closed;     /* end-of-stream requested */
    bool                   started;    /* at least one frame queued */
    struct wf_xrpc_sse_stream *next;   /* global list in server */
};

/** Fallback definitions for libmicrohttpd versions lacking these macros. */
#ifndef MHD_CONTENT_READER_END_OF_STREAM
#define MHD_CONTENT_READER_END_OF_STREAM ((ssize_t)-1)
#endif

/** Append a byte range to the stream's pending queue. Caller holds mutex. */
static wf_status wf_sse_append_locked(wf_xrpc_sse_stream *s,
                                      const char *data, size_t len) {
    wf_sse_chunk *c;
    if (len == 0) {
        return WF_OK;
    }
    c = (wf_sse_chunk *)calloc(1, sizeof(*c));
    if (!c) {
        return WF_ERR_ALLOC;
    }
    c->data = (char *)malloc(len);
    if (!c->data) {
        free(c);
        return WF_ERR_ALLOC;
    }
    memcpy(c->data, data, len);
    c->len = len;
    c->off = 0;
    if (s->chunks_tail) {
        s->chunks_tail->next = c;
    } else {
        s->chunks = c;
    }
    s->chunks_tail = c;
    return WF_OK;
}

/** Copy up to `max` pending bytes into `buf`. Caller holds mutex. */
static size_t wf_sse_drain_locked(wf_xrpc_sse_stream *s, char *buf,
                                  size_t max) {
    size_t wrote = 0;
    while (s->chunks && wrote < max) {
        wf_sse_chunk *c = s->chunks;
        size_t avail = c->len - c->off;
        size_t take = max - wrote;
        if (take > avail) {
            take = avail;
        }
        memcpy(buf + wrote, c->data + c->off, take);
        wrote += take;
        c->off += take;
        if (c->off >= c->len) {
            s->chunks = c->next;
            if (s->chunks_tail == c) {
                s->chunks_tail = NULL;
            }
            free(c->data);
            free(c);
        }
    }
    return wrote;
}

/** Create an SSE stream bound to a connection. Caller registers it. */
static wf_xrpc_sse_stream *wf_sse_stream_new(wf_xrpc_server *server,
                                             struct MHD_Connection *conn,
                                             const char *nsid) {
    wf_xrpc_sse_stream *s;
    s = (wf_xrpc_sse_stream *)calloc(1, sizeof(*s));
    if (!s) {
        return NULL;
    }
    s->server = server;
    s->conn = conn;
    s->nsid = nsid ? strdup(nsid) : NULL;
    if (nsid && !s->nsid) {
        free(s);
        return NULL;
    }
    if (pthread_mutex_init(&s->mutex, NULL) != 0) {
        free(s->nsid);
        free(s);
        return NULL;
    }
    return s;
}

/** MHD content-reader callback: feed buffered bytes, suspend when empty. */
static ssize_t wf_sse_content_reader(void *cls, uint64_t pos, char *buf,
                                     size_t max) {
    wf_xrpc_sse_stream *s = (wf_xrpc_sse_stream *)cls;
    ssize_t wrote;
    bool closed;
    (void)pos;

    pthread_mutex_lock(&s->mutex);
    wrote = (ssize_t)wf_sse_drain_locked(s, buf, max);
    if (wrote > 0) {
        pthread_mutex_unlock(&s->mutex);
        return wrote;
    }
    closed = s->closed;
    if (!closed) {
        /* No data available: park the connection until resumed. Calling
         * MHD_suspend_connection while holding the stream mutex serialises
         * against wf_xrpc_server_sse_send, which prevents a lost wake-up. */
        MHD_suspend_connection(s->conn);
    }
    pthread_mutex_unlock(&s->mutex);

    if (closed) {
        return MHD_CONTENT_READER_END_OF_STREAM;
    }
    return 0; /* suspended; MHD calls again after MHD_resume_connection */
}

/** MHD content-reader free callback: unlink and release the stream. */
static void wf_sse_stream_free(void *cls) {
    wf_xrpc_sse_stream *s = (wf_xrpc_sse_stream *)cls;
    wf_xrpc_server *server = s->server;

    if (server) {
        pthread_mutex_lock(&server->sse_mutex);
        wf_xrpc_sse_stream **pp = &server->sse_streams;
        while (*pp) {
            if (*pp == s) {
                *pp = s->next;
                break;
            }
            pp = &(*pp)->next;
        }
        pthread_mutex_unlock(&server->sse_mutex);
    }

    while (s->chunks) {
        wf_sse_chunk *nx = s->chunks->next;
        free(s->chunks->data);
        free(s->chunks);
        s->chunks = nx;
    }
    free(s->nsid);
    pthread_mutex_destroy(&s->mutex);
    free(s);
}

/** Register a freshly created stream in the server's open-SSE list. */
static void wf_sse_register(wf_xrpc_server *server, wf_xrpc_sse_stream *s) {
    pthread_mutex_lock(&server->sse_mutex);
    s->next = server->sse_streams;
    server->sse_streams = s;
    pthread_mutex_unlock(&server->sse_mutex);
}

wf_status wf_xrpc_server_sse_send(wf_xrpc_sse_stream *stream,
                                  const char *event, const char *data) {
    wf_xrpc_sse_stream *s = stream;
    char *frame = NULL;
    size_t frame_cap = 0, frame_len = 0;
    wf_status rc = WF_OK;

    if (!s || s->closed) {
        /* TODO: stream is closed or invalid; cannot send. */
        return WF_ERR_INVALID_ARG;
    }
    if (!data) {
        data = "";
    }

    /* Optional "event:" line. */
    if (event && event[0] != '\0') {
        size_t need = strlen("event: ") + strlen(event) + strlen("\n");
        if (frame_len + need + 1 > frame_cap) {
            char *tmp = (char *)realloc(frame, frame_len + need + 1);
            if (!tmp) { rc = WF_ERR_ALLOC; goto out; }
            frame = tmp;
            frame_cap = frame_len + need + 1;
        }
        frame_len += (size_t)snprintf(frame + frame_len, frame_cap - frame_len,
                                      "event: %s\n", event);
    }

    /* "data:" lines — one per newline in the payload. */
    {
        const char *p = data;
        const char *line = data;
        while (1) {
            const char *nl = strchr(p, '\n');
            size_t llen = nl ? (size_t)(nl - line) : strlen(line);
            size_t need = strlen("data: ") + llen + strlen("\n");
            if (frame_len + need + 1 > frame_cap) {
                char *tmp = (char *)realloc(frame, frame_len + need + 16);
                if (!tmp) { rc = WF_ERR_ALLOC; goto out; }
                frame = tmp;
                frame_cap = frame_len + need + 16;
            }
            frame_len += (size_t)snprintf(frame + frame_len,
                                          frame_cap - frame_len,
                                          "data: %.*s\n", (int)llen, line);
            if (!nl) {
                break;
            }
            p = nl + 1;
            line = p;
        }
    }

    /* Trailing blank line terminates the event. */
    {
        size_t need = strlen("\n");
        if (frame_len + need + 1 > frame_cap) {
            char *tmp = (char *)realloc(frame, frame_len + need + 1);
            if (!tmp) { rc = WF_ERR_ALLOC; goto out; }
            frame = tmp;
            frame_cap = frame_len + need + 1;
        }
        frame_len += (size_t)snprintf(frame + frame_len, frame_cap - frame_len,
                                      "\n");
    }

    pthread_mutex_lock(&s->mutex);
    rc = wf_sse_append_locked(s, frame, frame_len);
    if (rc == WF_OK) {
        s->started = true;
    }
    pthread_mutex_unlock(&s->mutex);

    if (rc == WF_OK) {
        MHD_resume_connection(s->conn);
    }

out:
    free(frame);
    return rc;
}

wf_status wf_xrpc_server_sse_send_raw(wf_xrpc_sse_stream *stream,
                                      const char *frame, size_t len) {
    wf_xrpc_sse_stream *s = stream;
    wf_status rc;
    if (!s || s->closed || !frame) {
        /* TODO: stream is closed/invalid or frame is NULL. */
        return WF_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s->mutex);
    rc = wf_sse_append_locked(s, frame, len);
    if (rc == WF_OK) {
        s->started = true;
    }
    pthread_mutex_unlock(&s->mutex);
    if (rc == WF_OK) {
        MHD_resume_connection(s->conn);
    }
    return rc;
}

wf_status wf_xrpc_server_sse_close(wf_xrpc_sse_stream *stream) {
    wf_xrpc_sse_stream *s = stream;
    if (!s || s->closed) {
        /* TODO: stream already closed or invalid. */
        return WF_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s->mutex);
    s->closed = true;
    pthread_mutex_unlock(&s->mutex);
    MHD_resume_connection(s->conn);
    return WF_OK;
}

/* ------------------------------------------------------------------ */
/* Response helpers                                                     */
/* ------------------------------------------------------------------ */

void wf_xrpc_response_set_body(wf_xrpc_response *resp,
                               const char *body, size_t body_len) {
    if (!resp) {
        return;
    }
    free(resp->body);
    if (body && body_len > 0) {
        resp->body = (char *)malloc(body_len + 1);
        if (resp->body) {
            memcpy(resp->body, body, body_len);
            resp->body[body_len] = '\0';
            resp->body_len = body_len;
        }
    } else {
        resp->body = NULL;
        resp->body_len = 0;
    }
}

void wf_xrpc_response_set_error(wf_xrpc_response *resp,
                                int http_status,
                                const char *error,
                                const char *message) {
    cJSON *obj;
    char  *json;

    if (!resp) {
        return;
    }
    free(resp->body);
    resp->body = NULL;
    resp->body_len = 0;
    resp->http_status = http_status;

    obj = cJSON_CreateObject();
    if (!obj) {
        return;
    }
    if (error && error[0] != '\0') {
        cJSON_AddStringToObject(obj, "error", error);
    }
    if (message && message[0] != '\0') {
        cJSON_AddStringToObject(obj, "message", message);
    }
    json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (json) {
        resp->body = json;
        resp->body_len = strlen(json);
    }
}

void wf_xrpc_response_set_error_body(wf_xrpc_response *resp,
                                     int http_status,
                                     const char *body, size_t body_len) {
    if (!resp) {
        return;
    }
    free(resp->body);
    resp->http_status = http_status;
    wf_xrpc_response_set_body(resp, body, body_len);
}

/* ------------------------------------------------------------------ */
/* URL parsing — extract NSID from /xrpc/<nsid>                       */
/* ------------------------------------------------------------------ */
static char *wf_server_extract_nsid(const char *url) {
    const char prefix[] = "/xrpc/";
    size_t prefix_len = sizeof(prefix) - 1;
    const char *start;
    const char *end;
    size_t len;

    if (!url) {
        return NULL;
    }
    if (strncmp(url, prefix, prefix_len) != 0) {
        return NULL;
    }
    start = url + prefix_len;
    end = strchr(start, '?');
    if (!end) {
        end = strchr(start, '#');
    }
    if (!end) {
        end = start + strlen(start);
    }
    len = (size_t)(end - start);
    if (len == 0) {
        return NULL;
    }
    char *nsid = (char *)malloc(len + 1);
    if (!nsid) {
        return NULL;
    }
    memcpy(nsid, start, len);
    nsid[len] = '\0';
    return nsid;
}

/* ------------------------------------------------------------------ */
/* Query string parsing — build cJSON object from MHD GET arguments   */
/* ------------------------------------------------------------------ */
struct qs_ctx {
    cJSON *obj;
};

static enum MHD_Result wf_server_qs_iter(void *cls,
                                          enum MHD_ValueKind kind,
                                          const char *key,
                                          const char *value) {
    (void)kind;
    struct qs_ctx *ctx = (struct qs_ctx *)cls;
    if (key && value) {
        cJSON_AddStringToObject(ctx->obj, key, value);
    }
    return MHD_YES;
}

static cJSON *wf_server_get_query_params(struct MHD_Connection *conn) {
    struct qs_ctx ctx;
    ctx.obj = cJSON_CreateObject();
    if (!ctx.obj) {
        return NULL;
    }
    MHD_get_connection_values(conn, MHD_GET_ARGUMENT_KIND,
                               &wf_server_qs_iter, &ctx);
    if (cJSON_GetArraySize(ctx.obj) == 0) {
        cJSON_Delete(ctx.obj);
        return NULL;
    }
    return ctx.obj;
}

/* ------------------------------------------------------------------ */
/* Route lookup                                                        */
/* ------------------------------------------------------------------ */
static wf_route *wf_server_find_route(const wf_xrpc_server *server,
                                       const char *nsid,
                                       wf_route_kind kind) {
    for (wf_route *r = server->routes; r; r = r->next) {
        if (r->kind == kind && strcmp(r->nsid, nsid) == 0) {
            return r;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* MHD request handler                                                  */
/* ------------------------------------------------------------------ */

/* Forward declaration — defined later (CORS preflight). */
static enum MHD_Result wf_server_mhd_options(void *cls,
                                              struct MHD_Connection *conn,
                                              const char *url,
                                              const char *method,
                                              const char *version,
                                              const char *upload_data,
                                              size_t *upload_data_size,
                                              void **con_cls);

static enum MHD_Result wf_server_mhd_handler(void *cls,
                                              struct MHD_Connection *conn,
                                              const char *url,
                                              const char *method,
                                              const char *version,
                                              const char *upload_data,
                                              size_t *upload_data_size,
                                              void **con_cls) {
    (void)version;
    wf_xrpc_server *server = (wf_xrpc_server *)cls;
    enum MHD_Result ret;
    struct MHD_Response *mhd_resp;
    wf_xrpc_response resp = WF_XRPC_RESPONSE_INIT;
    wf_xrpc_request req;
    wf_route_kind kind;
    wf_route *route;
    char *nsid = NULL;
    cJSON *params = NULL;
    const char *auth_header;

    /* CORS preflight (OPTIONS) — handled directly, no route lookup. */
    if (strcmp(method, "OPTIONS") == 0) {
        return wf_server_mhd_options(cls, conn, url, method, version,
                                     upload_data, upload_data_size, con_cls);
    }

    /* One-time initialisation per connection */
    if (*con_cls == NULL) {
        /* For POST requests, allocate a buffer to accumulate body data */
        if (strcmp(method, "POST") == 0) {
            post_buf *pb = (post_buf *)calloc(1, sizeof(post_buf));
            if (!pb) {
                return MHD_NO;
            }
            *con_cls = (void *)pb;
        } else {
            *con_cls = (void *)1;
        }
        return MHD_YES;
    }

    /* For POST requests, accumulate body data and delay processing */
    if (strcmp(method, "POST") == 0) {
        post_buf *pb = (post_buf *)*con_cls;
        if (*upload_data_size > 0) {
            if (!post_buf_append(pb, upload_data, *upload_data_size)) {
                free(pb->data);
                free(pb);
                *con_cls = NULL;
                return MHD_NO;
            }
            *upload_data_size = 0;
            return MHD_YES;
        }
        /* upload_data_size == 0 means upload complete — process now */
        nsid = wf_server_extract_nsid(url);
        if (!nsid) {
            wf_xrpc_response_set_error(&resp, 400, "InvalidRequest",
                                        "URL must be /xrpc/<nsid>");
            free(pb->data);
            free(pb);
            *con_cls = NULL;
            goto send;
        }
        if (pb->len > 0) {
            params = cJSON_ParseWithLength(pb->data, pb->len);
        }
        if (!params) {
            params = cJSON_Parse("{}");
        }
        free(pb->data);
        free(pb);
        *con_cls = NULL;
        kind = WF_ROUTE_PROCEDURE;
        goto process;
    }

    /* GET request — parse NSID and query params */
    nsid = wf_server_extract_nsid(url);
    if (!nsid) {
        wf_xrpc_response_set_error(&resp, 400, "InvalidRequest",
                                    "URL must be /xrpc/<nsid>");
        goto send;
    }
    kind = WF_ROUTE_QUERY;
    params = wf_server_get_query_params(conn);

process:

    /* Look up route */
    route = wf_server_find_route(server, nsid, kind);
    if (!route) {
        wf_xrpc_response_set_error(&resp, 404, "MethodNotFound",
                                    "No handler registered for this NSID");
        goto send;
    }

    /* Rate limiter — charge 1 token against client IP */
    if (server->rate_limiter) {
        const union MHD_ConnectionInfo *ci;
        char ip_str[INET6_ADDRSTRLEN];
        unsigned int retry_after = 0;

        ci = MHD_get_connection_info(conn,
                                      MHD_CONNECTION_INFO_CLIENT_ADDRESS);
        if (ci && ci->client_addr) {
            if (ci->client_addr->sa_family == AF_INET) {
                inet_ntop(AF_INET,
                          &((struct sockaddr_in *)ci->client_addr)->sin_addr,
                          ip_str, sizeof(ip_str));
            } else if (ci->client_addr->sa_family == AF_INET6) {
                inet_ntop(AF_INET6,
                          &((struct sockaddr_in6 *)ci->client_addr)->sin6_addr,
                          ip_str, sizeof(ip_str));
            } else {
                (void)snprintf(ip_str, sizeof(ip_str), "unknown");
            }

            if (wf_rate_limiter_consume(server->rate_limiter, ip_str,
                                        1, &retry_after) != WF_OK) {
                struct MHD_Response *mhd_rl;
                char body[192];
                char ra_str[16];
                int n;

                n = snprintf(body, sizeof(body),
                             "{\"error\":\"RateLimitExceeded\","
                             "\"message\":\"Rate limit exceeded. "
                             "Retry after %u seconds.\"}",
                             retry_after);
                if (n < 0 || (size_t)n >= sizeof(body)) n = (int)sizeof(body) - 1;

                mhd_rl = MHD_create_response_from_buffer(
                    (size_t)n, body, MHD_RESPMEM_MUST_COPY);
                if (mhd_rl) {
                    snprintf(ra_str, sizeof(ra_str), "%u", retry_after);
                    MHD_add_response_header(mhd_rl, "Content-Type",
                                             "application/json");
                    MHD_add_response_header(mhd_rl, "Retry-After", ra_str);
                    MHD_queue_response(conn, 429, mhd_rl);
                    MHD_destroy_response(mhd_rl);
                }
                ret = MHD_YES;
                goto cleanup;
            }
        }
    }

    /* Auth callback */
    auth_header = MHD_lookup_connection_value(conn, MHD_HEADER_KIND,
                                               "Authorization");
    if (server->auth_cb) {
        wf_xrpc_request auth_req;
        memset(&auth_req, 0, sizeof(auth_req));
        auth_req.nsid = nsid;
        auth_req.method = method;
        auth_req.auth_header = auth_header;
        auth_req.params = params;
        auth_req.handler_ctx = route->ctx;
        if (server->auth_cb(&auth_req, server->auth_ctx) != WF_OK) {
            wf_xrpc_response_set_error(&resp, 401, "AuthenticationRequired",
                                        "Authentication required");
            goto send;
        }
    }

    /* Build request and call handler */
    memset(&req, 0, sizeof(req));
    req.nsid = nsid;
    req.method = method;
    req.auth_header = auth_header;
    req.params = params;
    req.handler_ctx = route->ctx;

    if (route->is_sse) {
        goto sse_stream;
    }

    if (kind == WF_ROUTE_QUERY) {
        route->handler.query(route->ctx, &req, &resp);
    } else {
        route->handler.procedure(route->ctx, &req, &resp);
    }

send:
    /* Build MHD response */
    if (!resp.body) {
        resp.body = strdup("");
        resp.body_len = 0;
    }
    mhd_resp = MHD_create_response_from_buffer(resp.body_len, resp.body,
                                                MHD_RESPMEM_MUST_FREE);
    /* Body ownership transferred to MHD — prevent double-free */
    resp.body = NULL;

    if (!mhd_resp) {
        ret = MHD_NO;
        goto cleanup;
    }

    if (route && route->is_sse) {
        MHD_add_response_header(mhd_resp, "Content-Type", "text/event-stream");
    } else {
        MHD_add_response_header(mhd_resp, "Content-Type", "application/json");
    }
    MHD_add_response_header(mhd_resp, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(mhd_resp, "Access-Control-Allow-Headers",
                             "Authorization, Content-Type");
    MHD_add_response_header(mhd_resp, "Access-Control-Allow-Methods",
                             "GET, POST, OPTIONS");
    MHD_add_response_header(mhd_resp, "Access-Control-Expose-Headers",
                             "Content-Type");

    ret = MHD_queue_response(conn, resp.http_status, mhd_resp);
    MHD_destroy_response(mhd_resp);

    goto cleanup;

sse_stream:
    {
        wf_xrpc_sse_stream *stream;
        struct MHD_Response *mhd_resp_sse;

        stream = wf_sse_stream_new(server, conn, nsid);
        if (!stream) {
            wf_xrpc_response_set_error(&resp, 500, "InternalError",
                                       "Failed to allocate SSE stream");
            goto send;
        }
        wf_sse_register(server, stream);

        /* Hand the stream to the user handler. The handler should return
         * promptly and stream from a worker thread (see header docs). */
        route->handler.sse(route->ctx, &req, stream);

        mhd_resp_sse = MHD_create_response_from_callback(
            MHD_SIZE_UNKNOWN, 4096,
            wf_sse_content_reader, stream, wf_sse_stream_free);
        if (!mhd_resp_sse) {
            /* Detach and release the stream; report an error. */
            pthread_mutex_lock(&server->sse_mutex);
            if (server->sse_streams == stream) {
                server->sse_streams = stream->next;
            } else {
                wf_xrpc_sse_stream *pr = server->sse_streams;
                while (pr && pr->next != stream) {
                    pr = pr->next;
                }
                if (pr) {
                    pr->next = stream->next;
                }
            }
            pthread_mutex_unlock(&server->sse_mutex);
            wf_sse_stream_free(stream);
            wf_xrpc_response_set_error(&resp, 500, "InternalError",
                                       "Failed to create SSE response");
            goto send;
        }

        MHD_add_response_header(mhd_resp_sse, "Content-Type",
                                "text/event-stream");
        MHD_add_response_header(mhd_resp_sse, "Cache-Control", "no-cache");
        MHD_add_response_header(mhd_resp_sse, "Connection", "keep-alive");
        MHD_add_response_header(mhd_resp_sse, "X-Accel-Buffering", "no");
        MHD_add_response_header(mhd_resp_sse, "Access-Control-Allow-Origin",
                                "*");
        MHD_add_response_header(mhd_resp_sse, "Access-Control-Allow-Headers",
                                "Authorization, Content-Type");
        MHD_add_response_header(mhd_resp_sse, "Access-Control-Expose-Headers",
                                "Content-Type");

        ret = MHD_queue_response(conn, MHD_HTTP_OK, mhd_resp_sse);
        MHD_destroy_response(mhd_resp_sse);
        /* The connection is left open: wf_sse_content_reader suspends it
         * whenever the buffer is empty, and SSE sends resume it. */
        goto cleanup;
    }

cleanup:
    free(nsid);
    if (params) {
        cJSON_Delete(params);
    }
    free(resp.body); /* safe: NULL if already transferred */
    return ret;
}

/* ------------------------------------------------------------------ */
/* OPTIONS handler (CORS preflight)                                     */
/* ------------------------------------------------------------------ */
static enum MHD_Result wf_server_mhd_options(void *cls,
                                              struct MHD_Connection *conn,
                                              const char *url,
                                              const char *method,
                                              const char *version,
                                              const char *upload_data,
                                              size_t *upload_data_size,
                                              void **con_cls) {
    (void)cls;
    (void)url;
    (void)method;
    (void)version;
    (void)upload_data;
    (void)upload_data_size;
    (void)con_cls;
    struct MHD_Response *resp;
    enum MHD_Result ret;

    resp = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    if (!resp) {
        return MHD_NO;
    }
    MHD_add_response_header(resp, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(resp, "Access-Control-Allow-Headers",
                             "Authorization, Content-Type");
    MHD_add_response_header(resp, "Access-Control-Allow-Methods",
                             "GET, POST, OPTIONS");
    MHD_add_response_header(resp, "Access-Control-Max-Age", "86400");
    ret = MHD_queue_response(conn, 204, resp);
    MHD_destroy_response(resp);
    return ret;
}

/* ------------------------------------------------------------------ */
/* Server lifecycle                                                     */
/* ------------------------------------------------------------------ */

wf_xrpc_server *wf_xrpc_server_start(const char *address, uint16_t port,
                                      unsigned int thread_count) {
    wf_xrpc_server *server;

    if (!address) {
        return NULL;
    }
    server = (wf_xrpc_server *)calloc(1, sizeof(*server));
    if (!server) {
        return NULL;
    }
    server->port = port;
    server->sse_streams = NULL;
    if (pthread_mutex_init(&server->sse_mutex, NULL) != 0) {
        free(server);
        return NULL;
    }

    if (thread_count == 0) {
        thread_count = 4;
    }

    server->daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD | MHD_ALLOW_SUSPEND_RESUME,
        port,
        NULL, NULL,                       /* Accept policy */
        &wf_server_mhd_handler, server,   /* Main handler */
        MHD_OPTION_NOTIFY_COMPLETED, NULL, NULL,
        MHD_OPTION_EXTERNAL_LOGGER, NULL, NULL,
        MHD_OPTION_END);
    if (!server->daemon) {
        free(server);
        return NULL;
    }

    /* Query the bound port (in case port == 0) */
    const union MHD_DaemonInfo *info = MHD_get_daemon_info(
        server->daemon, MHD_DAEMON_INFO_BIND_PORT);
    if (info) {
        server->port = info->port;
    }

    return server;
}

void wf_xrpc_server_stop(wf_xrpc_server *server) {
    if (!server || !server->daemon) {
        return;
    }
    /* Resume and mark any still-suspended SSE connections for closure so the
     * daemon can finish draining them and exit cleanly (no hung threads). */
    pthread_mutex_lock(&server->sse_mutex);
    for (wf_xrpc_sse_stream *s = server->sse_streams; s; s = s->next) {
        pthread_mutex_lock(&s->mutex);
        s->closed = true;
        pthread_mutex_unlock(&s->mutex);
        MHD_resume_connection(s->conn);
    }
    pthread_mutex_unlock(&server->sse_mutex);

    MHD_stop_daemon(server->daemon);
    server->daemon = NULL;
}

static void wf_server_free_rate_limit_entries(wf_rate_limit_entry *head);

void wf_xrpc_server_free(wf_xrpc_server *server) {
    wf_route *r;

    if (!server) {
        return;
    }
    wf_xrpc_server_stop(server);
    r = server->routes;
    while (r) {
        wf_route *next = r->next;
        free(r->nsid);
        free(r);
        r = next;
    }
    if (server->rate_limit_entries) {
        wf_server_free_rate_limit_entries(server->rate_limit_entries);
    }
    pthread_mutex_destroy(&server->sse_mutex);
    free(server);
}

uint16_t wf_xrpc_server_port(const wf_xrpc_server *server) {
    return server ? server->port : 0;
}

/* ------------------------------------------------------------------ */
/* Route registration                                                   */
/* ------------------------------------------------------------------ */

wf_status wf_xrpc_server_register_query(wf_xrpc_server *server,
                                         const char *nsid,
                                         wf_xrpc_query_handler handler,
                                         void *ctx) {
    wf_route *r;

    if (!server || !nsid || !handler) {
        return WF_ERR_INVALID_ARG;
    }
    r = (wf_route *)calloc(1, sizeof(*r));
    if (!r) {
        return WF_ERR_ALLOC;
    }
    r->nsid = strdup(nsid);
    if (!r->nsid) {
        free(r);
        return WF_ERR_ALLOC;
    }
    r->kind = WF_ROUTE_QUERY;
    r->handler.query = handler;
    r->ctx = ctx;
    r->next = server->routes;
    server->routes = r;
    return WF_OK;
}

wf_status wf_xrpc_server_register_procedure(wf_xrpc_server *server,
                                             const char *nsid,
                                             wf_xrpc_procedure_handler handler,
                                             void *ctx) {
    wf_route *r;

    if (!server || !nsid || !handler) {
        return WF_ERR_INVALID_ARG;
    }
    r = (wf_route *)calloc(1, sizeof(*r));
    if (!r) {
        return WF_ERR_ALLOC;
    }
    r->nsid = strdup(nsid);
    if (!r->nsid) {
        free(r);
        return WF_ERR_ALLOC;
    }
    r->kind = WF_ROUTE_PROCEDURE;
    r->handler.procedure = handler;
    r->ctx = ctx;
    r->next = server->routes;
    server->routes = r;
    return WF_OK;
}

/* Register a Server-Sent Events (SSE) endpoint. The connection is kept open
   and frames are pushed with wf_xrpc_server_sse_send until closed with
   wf_xrpc_server_sse_close. A handler that sends a single frame and closes
   produces a single-shot SSE response. */
wf_status wf_xrpc_server_register_sse(wf_xrpc_server *server,
                                       const char *nsid,
                                       wf_xrpc_sse_handler handler,
                                       void *ctx) {
    wf_route *r;
    if (!server || !nsid || !handler) {
        return WF_ERR_INVALID_ARG;
    }
    r = (wf_route *)calloc(1, sizeof(*r));
    if (!r) {
        return WF_ERR_ALLOC;
    }
    r->nsid = strdup(nsid);
    if (!r->nsid) {
        free(r);
        return WF_ERR_ALLOC;
    }
    r->kind = WF_ROUTE_QUERY;
    r->handler.sse = handler;
    r->ctx = ctx;
    r->is_sse = true;
    r->next = server->routes;
    server->routes = r;
    return WF_OK;
}

void wf_xrpc_server_set_auth_callback(wf_xrpc_server *server,
                                       wf_xrpc_auth_cb cb, void *ctx) {
    if (!server) {
        return;
    }
    server->auth_cb = cb;
    server->auth_ctx = ctx;
}

static void wf_server_free_rate_limit_entries(wf_rate_limit_entry *head) {
    wf_rate_limit_entry *cur = head;
    while (cur) {
        wf_rate_limit_entry *next = cur->next;
        free(cur->route_key);
        if (cur->rl) wf_rate_limiter_free(cur->rl);
        free(cur);
        cur = next;
    }
}

/* Add a per-route rate limiter (method+url)
   Transfers ownership of 'rl' to the server */
wf_status wf_server_set_route_rate_limiter(wf_xrpc_server *server,
                                           const char *method,
                                           const char *url,
                                           wf_rate_limiter *rl) {
    wf_rate_limit_entry *entry;

    if (!server || !method || !url) {
        return WF_ERR_INVALID_ARG;
    }
    if (!rl) {
        return WF_OK;
    }
    entry = (wf_rate_limit_entry *)calloc(1, sizeof(*entry));
    if (!entry) {
        wf_rate_limiter_free(rl);
        return WF_ERR_ALLOC;
    }
    entry->route_key = strdup(url);
    if (!entry->route_key) {
        free(entry);
        wf_rate_limiter_free(rl);
        return WF_ERR_ALLOC;
    }
    entry->rl = rl;
    entry->next = server->rate_limit_entries;
    server->rate_limit_entries = entry;
    return WF_OK;
}

void wf_xrpc_server_set_route_rate_limiter(wf_xrpc_server *server,
                                           const char *method,
                                           const char *url,
                                           wf_rate_limiter *rl) {
    if (!server) return;
    if (wf_server_set_route_rate_limiter(server, method, url, rl) != WF_OK) {
        if (rl) wf_rate_limiter_free(rl);
    }
}

/* Set the global IP-based rate limiter. The limiter is borrowed by the
   server: the caller retains ownership and is responsible for freeing it
   (typically after the server is destroyed). Passing NULL detaches it. */
void wf_xrpc_server_set_rate_limiter(wf_xrpc_server *server,
                                     wf_rate_limiter *rl) {
    if (!server) {
        return;
    }
    server->rate_limiter = rl;
}
