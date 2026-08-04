#define _GNU_SOURCE

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
#if defined(WOLFRAM_WIIU)
#include <mbedtls/sha1.h>
#endif

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <sys/socket.h>
#include <openssl/sha.h>

#include "wolfram/log.h"

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

/* Find the rate limiter attached to this exact method+url via
 * wf_xrpc_server_set_route_rate_limiter, or NULL if none was set — declared
 * here so the request-dispatch path (well above where entries are managed)
 * can see it. */
static wf_rate_limiter *wf_server_find_route_rate_limiter(
    wf_xrpc_server *server, const char *method, const char *url);

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

/* Shared by wf_rate_limiter_consume and wf_rate_limiter_consume_status.
 * `out_status` is filled whenever non-NULL, on both WF_OK and
 * WF_ERR_RATE_LIMIT — a caller setting RateLimit-* headers needs bucket
 * state on a successful consume too, not just a rejected one. */
static wf_status wf_rate_limiter_consume_core(wf_rate_limiter *rl,
                                              const char *key,
                                              unsigned int cost,
                                              unsigned int *out_retry_after,
                                              wf_rate_limit_status *out_status) {
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

    if (out_status) {
        out_status->limit = rl->points;
        out_status->duration_seconds = rl->duration_seconds;
    }

    if (b->tokens < (double)cost) {
        /* Seconds until enough tokens are available for this request. */
        double wait = ((double)cost - b->tokens) / refill_rate;
        if (wait < 1.0) wait = 1.0;
        unsigned int wait_s = (unsigned int)(wait + 0.5);
        if (out_retry_after) *out_retry_after = wait_s;
        if (out_status) {
            out_status->remaining = (unsigned int)b->tokens;
            out_status->reset_at = (unsigned int)now + wait_s;
        }
        return WF_ERR_RATE_LIMIT;
    }

    b->tokens -= (double)cost;
    if (out_retry_after) {
        *out_retry_after = 0;
    }
    if (out_status) {
        out_status->remaining = (unsigned int)b->tokens;
        /* Full bucket, nothing pending: report the window itself as when
         * a fresh cycle would complete, rather than "now". */
        double wait = (double)rl->points > b->tokens
            ? ((double)rl->points - b->tokens) / refill_rate
            : (double)rl->duration_seconds;
        out_status->reset_at = (unsigned int)now + (unsigned int)(wait + 0.5);
    }
    return WF_OK;
}

wf_status wf_rate_limiter_consume(wf_rate_limiter *rl,
                                   const char *key,
                                   unsigned int cost,
                                   unsigned int *out_retry_after) {
    return wf_rate_limiter_consume_core(rl, key, cost, out_retry_after, NULL);
}

wf_status wf_rate_limiter_consume_status(wf_rate_limiter *rl,
                                          const char *key,
                                          unsigned int cost,
                                          wf_rate_limit_status *out_status) {
    return wf_rate_limiter_consume_core(rl, key, cost, NULL, out_status);
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
    bool                      is_ws;  /* true if this route is a WebSocket subscription */
    union {
        wf_xrpc_query_handler      query;
        wf_xrpc_procedure_handler  procedure;
        wf_xrpc_sse_handler        sse;
        wf_xrpc_ws_handler         ws;
    } handler;
    void                     *ctx;
    struct wf_route          *next;   /* linked list */
} wf_route;

typedef struct wf_static_route {
    char *path;
    char *content_type;
    unsigned char *body;
    size_t body_len;
    struct wf_static_route *next;
} wf_static_route;

typedef struct wf_http_route {
    char *method;
    char *path;
    /* When set, `path` matches any request path that starts with it, rather
     * than only an exact equal. Exact routes are always preferred, so a prefix
     * can never shadow one. */
    bool prefix;
    wf_http_route_handler handler;
    void *ctx;
    struct wf_http_route *next;
} wf_http_route;

struct wf_xrpc_response_header {
    char *name;
    char *value;
    struct wf_xrpc_response_header *next;
};

/* ------------------------------------------------------------------ */
/* Owned context list (for PDS repo/blob resolver bundles)             */
/* ------------------------------------------------------------------ */

/*
 * A heap allocation the server owns and releases in wf_xrpc_server_free via
 * `free_fn`. Used by wf_xrpc_server_register_pds_repo(_resolver) and the blob
 * equivalents so the per-request resolver bundle is freed with the server
 * without leaking it to the caller. The bundle frees only itself; the
 * fallback stores / resolver ctx remain caller-owned.
 */
struct wf_owned_ctx {
    void                   *ptr;
    void                  (*free_fn)(void *);
    struct wf_owned_ctx    *next;
};

/* ------------------------------------------------------------------ */
/* Server struct                                                       */
/* ------------------------------------------------------------------ */
struct wf_xrpc_server {
    struct MHD_Daemon   *daemon;
    uint16_t             port;
    wf_route            *routes;
    wf_static_route     *static_routes;
    wf_http_route       *http_routes;
    struct wf_owned_ctx *owned_ctxs;       /* heap allocations freed on server free */
    wf_xrpc_auth_cb      auth_cb;
    void                *auth_ctx;
    /* Optional handler for NSIDs with no registered route (see
     * wf_xrpc_server_set_fallback). ctx is borrowed, not owned. */
    wf_xrpc_fallback_handler fallback;
    void                *fallback_ctx;
    /* Owned context installed by wf_xrpc_server_set_auth_middleware; the
     * server frees it (via auth_mw_free) in wf_xrpc_server_free. Cleared when
     * an external auth callback replaces the middleware. */
    void                *auth_mw_ctx;
    void               (*auth_mw_free)(void *);
    wf_xrpc_request_observer observer;         /* optional, not owned */
    void                *observer_ctx;
    wf_rate_limiter     *rate_limiter;        /* global IP-based limiter */
    wf_rate_limiter     *rate_limiter_owned;  /* non-NULL => server frees it */
    wf_rate_limit_entry *rate_limit_entries;   /* per-route list */
    wf_xrpc_sse_stream  *sse_streams;          /* open SSE connections */
    pthread_mutex_t      sse_mutex;            /* guards sse_streams */
    wf_xrpc_ws_stream   *ws_streams;           /* open WebSocket connections */
    pthread_mutex_t      ws_mutex;             /* guards ws_streams + ws_stopping */
    /* Set once teardown has begun. A stream linked after the join loop has
     * drained the list would never be joined, so upgrades are refused from
     * here on rather than accepted into a server that is going away. */
    bool                 ws_stopping;
    /* Upgrades queued but not yet handed to wf_ws_upgrade_handler. MHD gives
     * no callback when a response is destroyed without upgrading, so a client
     * that disappears between the 101 and the handover would otherwise strand
     * the context and stream forever. Guarded by ws_mutex. */
    struct wf_ws_pending *ws_pending;
    bool                 cors_enabled;          /* emit CORS headers when true */
    char                *cors_origin;           /* owned Allow-Origin value */
};

/* Apply the server's CORS policy to an outgoing MHD response (no-op when
 * CORS is disabled). Falls back to "*" when no origin was configured.
 *
 * Allow-Headers on a preflight echoes the browser's requested headers, the
 * same behaviour the reference PDS gets from the `cors` middleware. That lets
 * clients send any atproto header (atproto-proxy, atproto-accept-labelers,
 * X-Bsky-*) without the server maintaining the full set; a fixed list is only
 * the fallback for non-preflight responses. */
static void wf_server_apply_cors(wf_xrpc_server *server,
                                  struct MHD_Connection *conn,
                                  struct MHD_Response *resp) {
    const char *origin;
    const char *req_headers;
    if (!server || !server->cors_enabled) {
        return;
    }
    origin = server->cors_origin ? server->cors_origin : "*";
    MHD_add_response_header(resp, "Access-Control-Allow-Origin", origin);
    req_headers = MHD_lookup_connection_value(
        conn, MHD_HEADER_KIND, "Access-Control-Request-Headers");
    MHD_add_response_header(resp, "Access-Control-Allow-Headers",
                             req_headers && req_headers[0]
                                 ? req_headers
                                 : "Authorization, Content-Type, DPoP, Accept, "
                                   "atproto-proxy, atproto-accept-labelers, "
                                   "X-Bsky-Content-Checksum, "
                                   "X-Bsky-Post-Verification");
    MHD_add_response_header(resp, "Access-Control-Allow-Methods",
                             "GET, POST, OPTIONS");
    MHD_add_response_header(resp, "Access-Control-Expose-Headers",
                             "Content-Type, Retry-After, RateLimit-Limit, "
                             "RateLimit-Reset, RateLimit-Remaining, "
                             "RateLimit-Policy");
}

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
/* WebSocket (RFC 6455) subscription endpoints                          */
/* ------------------------------------------------------------------ */

/** A live WebSocket connection, created after a successful upgrade. */
struct wf_xrpc_ws_stream {
    struct MHD_Connection        *conn;   /* owning MHD connection */
    struct MHD_UpgradeResponseHandle *urh;/* upgrade handle (for close) */
    wf_xrpc_server               *server; /* back-pointer for teardown */
    char                         *nsid;   /* route NSID (diagnostics) */
    int                           sock;   /* raw socket fd (server→client) */
    pthread_t                     thread; /* upgrade worker thread */
    pthread_mutex_t               mutex;  /* guards closed + serialises writes */
    pthread_cond_t                worker_cond; /* signalled when refs reach zero */
    unsigned int                  worker_refs; /* retained producer workers */
    bool                          closed; /* end-of-stream requested */
    /* Set once the spawning thread has recorded `thread`. The worker must not
     * touch (or free) the stream until then — see wf_ws_upgrade_handler. */
    bool                          thread_ready;
    /* Set under ws_mutex by wf_xrpc_server_stop when it takes responsibility
     * for joining this worker. The worker detaches itself when this is false,
     * so a connection that ends on its own leaves nothing to reap. Guarded by
     * ws_mutex, not mutex: it is decided at the same moment as list removal. */
    bool                          reaped;
    struct wf_xrpc_ws_stream     *next;   /* global list in server */
};

/** An upgrade that has been queued but not yet handed over. */
typedef struct wf_ws_pending {
    struct MHD_Connection    *conn;
    struct wf_ws_upgrade_ctx *uc;
    struct wf_ws_pending     *next;
} wf_ws_pending;

/** Closure handed to libmicrohttpd's upgrade handler. */
typedef struct wf_ws_upgrade_ctx {
    wf_xrpc_server      *server;
    wf_route            *route;
    wf_xrpc_ws_stream   *stream;
    wf_xrpc_request      req;     /* request copy for the user handler */
} wf_ws_upgrade_ctx;

/** RFC 4648 standard base64 with '=' padding (for Sec-WebSocket-Accept). */
static void wf_ws_base64_encode(const unsigned char *in, size_t len, char *out) {
    static const char tab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i, o = 0;
    for (i = 0; i + 3 <= len; i += 3) {
        unsigned int n = ((unsigned int)in[i] << 16) |
                         ((unsigned int)in[i + 1] << 8) | (unsigned int)in[i + 2];
        out[o++] = tab[(n >> 18) & 0x3f];
        out[o++] = tab[(n >> 12) & 0x3f];
        out[o++] = tab[(n >> 6) & 0x3f];
        out[o++] = tab[n & 0x3f];
    }
    if (i < len) {
        unsigned int n = ((unsigned int)in[i] << 16) |
                         ((i + 1 < len) ? ((unsigned int)in[i + 1] << 8) : 0);
        out[o++] = tab[(n >> 18) & 0x3f];
        out[o++] = tab[(n >> 12) & 0x3f];
        out[o++] = (i + 1 < len) ? tab[(n >> 6) & 0x3f] : '=';
        out[o++] = '=';
    }
    out[o] = '\0';
}

/** Write the full buffer to the socket; returns 0 on success, -1 on a hard
 *  failure (EOF, reset, poll error), or -2 when the peer's receive window
 *  stayed full for the whole 2s write timeout — a slow consumer, not a
 *  broken connection, and the two are distinguished so the caller can tell
 *  the client why it is being disconnected (see WF_ERR_TIMEOUT below). */
static int wf_ws_write_all(int sock, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(sock, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = { sock, POLLOUT, 0 };
                int pr = poll(&pfd, 1, 2000);
                if (pr == 0) return -2;
                if (pr < 0) return -1;
                continue;
            }
            return -1;
        }
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

/** Read exactly `len` bytes with a per-read timeout; 0 ok, -1 error/EOF/timeout. */
static int wf_ws_read_exact(int sock, void *buf, size_t len, int timeout_ms) {
    char *p = (char *)buf;
    size_t off = 0;
    while (off < len) {
        struct pollfd pfd = { sock, POLLIN, 0 };
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr < 0) { if (errno == EINTR) continue; return -1; }
        if (pr == 0) return -1;
        ssize_t n = read(sock, p + off, len - off);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

/** Write a server→client frame (UNMASKED) of the given opcode.
 *  Caller MUST hold s->mutex (serialises all writes to the socket). */
static wf_status wf_ws_write_frame_locked(wf_xrpc_ws_stream *s, uint8_t opcode,
                                          const void *data, size_t len) {
    unsigned char hdr[10];
    size_t hlen = 0;
    hdr[0] = (unsigned char)(0x80 | (opcode & 0x0f));
    if (len < 126) {
        hdr[1] = (unsigned char)len;
        hlen = 2;
    } else if (len <= 0xFFFF) {
        hdr[1] = 126;
        hdr[2] = (unsigned char)((len >> 8) & 0xff);
        hdr[3] = (unsigned char)(len & 0xff);
        hlen = 4;
    } else {
        hdr[1] = 127;
        for (int i = 0; i < 8; i++) {
            hdr[2 + i] = (unsigned char)((len >> (8 * (7 - i))) & 0xff);
        }
        hlen = 10;
    }
    int hr = wf_ws_write_all(s->sock, hdr, hlen);
    int dr = (hr == 0 && len > 0) ? wf_ws_write_all(s->sock, data, len) : 0;
    if (hr == -2 || dr == -2) return WF_ERR_TIMEOUT;
    if (hr != 0 || dr != 0) return WF_ERR_NETWORK;
    return WF_OK;
}

/**
 * Half-close the socket and drain whatever the peer still has queued, so the
 * subsequent close() cannot turn into an RST.
 *
 * shutdown(SHUT_WR) puts a FIN after everything we have already written, so
 * the peer sees an ordinary end-of-stream once it has read our frames. We then
 * read (and discard) until the peer's own FIN arrives, because it is *unread
 * inbound* data that makes close() send RST — a client that pinged us, or sent
 * its own close frame, is enough. The wait is bounded: a peer that never
 * finishes costs us WS_LINGER_MS, not a stuck worker thread.
 */
#define WS_LINGER_MS 500

static void wf_ws_shutdown_gracefully(int sock) {
    unsigned char scratch[1024];
    int remaining = WS_LINGER_MS;

    if (sock < 0) {
        return;
    }
    shutdown(sock, SHUT_WR);

    while (remaining > 0) {
        struct pollfd pfd = { sock, POLLIN, 0 };
        int step = remaining < 50 ? remaining : 50;
        int pr = poll(&pfd, 1, step);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return;
        }
        if (pr == 0) {
            remaining -= step;
            continue;
        }
        ssize_t n = read(sock, scratch, sizeof(scratch));
        if (n <= 0) {
            /* 0 = peer FIN (the good case); <0 = already reset or errored. */
            return;
        }
        /* Discarded deliberately: the stream is over, and the only purpose of
         * reading is to leave the receive queue empty. */
    }
}

/* Release an upgrade that never happened: the context, the request copy it
 * carries, and the half-built stream. Mirrors the cleanup the upgrade handler
 * performs when it refuses. */
static void wf_ws_discard_upgrade(wf_ws_upgrade_ctx *uc) {
    if (!uc) return;
    wf_xrpc_ws_stream *stream = uc->stream;
    free((void *)uc->req.nsid);
    free((void *)uc->req.auth_header);
    free((void *)uc->req.dpop_header);
    cJSON_Delete(uc->req.params);
    free(uc->req.authed_subject);
    if (stream) {
        free(stream->nsid);
        pthread_cond_destroy(&stream->worker_cond);
        pthread_mutex_destroy(&stream->mutex);
        free(stream);
    }
    free(uc);
}

/* Record an upgrade as queued-but-not-handed-over. */
static void wf_ws_pending_add(wf_xrpc_server *server,
                              struct MHD_Connection *conn,
                              wf_ws_upgrade_ctx *uc) {
    wf_ws_pending *node = calloc(1, sizeof(*node));
    if (!node) return;   /* Losing the record only costs us the safety net. */
    node->conn = conn;
    node->uc = uc;
    pthread_mutex_lock(&server->ws_mutex);
    node->next = server->ws_pending;
    server->ws_pending = node;
    pthread_mutex_unlock(&server->ws_mutex);
}

/*
 * Remove a pending record and return the context it held, or NULL when there
 * is no match. Matches on `uc` when given, otherwise on `conn`; with both NULL
 * it takes whatever is at the head, which is how teardown drains the list.
 * The caller decides what the removal means — the handler claims the upgrade,
 * connection close and teardown discard it.
 */
static wf_ws_upgrade_ctx *wf_ws_pending_take(wf_xrpc_server *server,
                                             struct MHD_Connection *conn,
                                             wf_ws_upgrade_ctx *uc) {
    wf_ws_upgrade_ctx *found = NULL;
    pthread_mutex_lock(&server->ws_mutex);
    wf_ws_pending **pp = &server->ws_pending;
    while (*pp) {
        if ((uc && (*pp)->uc == uc) ||
            (!uc && conn && (*pp)->conn == conn) ||
            (!uc && !conn)) {
            wf_ws_pending *node = *pp;
            *pp = node->next;
            found = node->uc;
            free(node);
            break;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&server->ws_mutex);
    return found;
}

/*
 * MHD connection lifecycle hook.
 *
 * The only reason this exists: MHD never tells us when an upgrade response is
 * destroyed without upgrading. A client that vanishes between the queued 101
 * and the handover leaves its context and stream allocated with nothing left
 * holding a pointer to them. Connection close is the one event that covers
 * that case, and by then a successful upgrade has already claimed its record.
 */
static void wf_ws_notify_connection(void *cls, struct MHD_Connection *conn,
                                    void **socket_context,
                                    enum MHD_ConnectionNotificationCode code) {
    (void)socket_context;
    if (code != MHD_CONNECTION_NOTIFY_CLOSED) return;
    wf_xrpc_server *server = cls;
    if (!server) return;
    wf_ws_discard_upgrade(wf_ws_pending_take(server, conn, NULL));
}

/** Upgrade worker: run the user handler then drain client control frames. */
static void *wf_ws_serve_thread(void *arg) {
    wf_ws_upgrade_ctx *uc = (wf_ws_upgrade_ctx *)arg;
    wf_xrpc_ws_stream *s = uc->stream;
    wf_route *route = uc->route;
    wf_xrpc_request req = uc->req;
    free(uc);

    /* Wait until the spawning thread has stored our thread id.
     *
     * Everything below can finish and free `s` — a handler that pushes a few
     * frames and closes takes microseconds — so running ahead of that store
     * left wf_ws_upgrade_handler writing through freed memory, crashing the
     * whole process on the MHD polling thread. It is rare (about 1 run in 300
     * under parallel load) and fatal, so the ordering is enforced rather than
     * hoped for. */
    pthread_mutex_lock(&s->mutex);
    while (!s->thread_ready) {
        pthread_cond_wait(&s->worker_cond, &s->mutex);
    }
    pthread_mutex_unlock(&s->mutex);

    /* 1) Invoke the user handler (it pushes frames / may close). */
    route->handler.ws(route->ctx, &req, s);

    /* 2) Control-frame loop: read client frames, answer ping, honour close. */
    for (;;) {
        bool closed;
        pthread_mutex_lock(&s->mutex);
        closed = s->closed;
        pthread_mutex_unlock(&s->mutex);
        if (closed) {
            break;
        }

        struct pollfd pfd = { s->sock, POLLIN, 0 };
        int pr = poll(&pfd, 1, 250);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) {
            continue; /* timed out: re-check closed flag */
        }

        unsigned char h[2];
        if (wf_ws_read_exact(s->sock, h, 2, 1000) != 0) break;
        uint8_t opcode = (uint8_t)(h[0] & 0x0f);
        bool masked = (h[1] & 0x80) != 0;
        uint64_t plen = (uint64_t)(h[1] & 0x7f);
        if (plen == 126) {
            unsigned char e[2];
            if (wf_ws_read_exact(s->sock, e, 2, 1000) != 0) break;
            plen = ((uint64_t)e[0] << 8) | (uint64_t)e[1];
        } else if (plen == 127) {
            unsigned char e[8];
            if (wf_ws_read_exact(s->sock, e, 8, 1000) != 0) break;
            plen = 0;
            for (int i = 0; i < 8; i++) plen = (plen << 8) | (uint64_t)e[i];
        }
        unsigned char mask[4] = { 0, 0, 0, 0 };
        if (masked) {
            if (wf_ws_read_exact(s->sock, mask, 4, 1000) != 0) break;
        }
        unsigned char *payload = NULL;
        if (plen > 0) {
            if (plen > 16 * 1024 * 1024) break; /* safety guard */
            payload = (unsigned char *)malloc((size_t)plen ? (size_t)plen : 1);
            if (!payload) break;
            if (wf_ws_read_exact(s->sock, payload, (size_t)plen, 2000) != 0) {
                free(payload);
                break;
            }
            if (masked) {
                for (uint64_t i = 0; i < plen; i++) payload[i] ^= mask[i & 3];
            }
        }

        if (opcode == 0x9) {                 /* ping → pong */
            pthread_mutex_lock(&s->mutex);
            wf_ws_write_frame_locked(s, 0xA, payload, (size_t)plen);
            pthread_mutex_unlock(&s->mutex);
        } else if (opcode == 0x8) {          /* close */
            pthread_mutex_lock(&s->mutex);
            if (!s->closed) {
                s->closed = true;
                wf_ws_write_frame_locked(s, 0x8, payload, (size_t)plen);
            }
            pthread_mutex_unlock(&s->mutex);
            free(payload);
            break;
        }
        free(payload);
    }

    /* Prevent retained producer workers from racing the stream teardown. */
    pthread_mutex_lock(&s->mutex);
    s->closed = true;
    while (s->worker_refs > 0) {
        pthread_cond_wait(&s->worker_cond, &s->mutex);
    }
    pthread_mutex_unlock(&s->mutex);

    /* 3) Tear down the upgraded socket via libmicrohttpd — but only after the
     * peer has had a chance to collect what we already sent. Closing a socket
     * that still has unread data in its receive queue makes the kernel send
     * RST instead of FIN, and a received RST discards the peer's receive
     * buffer: frames we delivered perfectly well are destroyed before a slow
     * subscriber can read them. For a firehose the lost frames are the last
     * ones before the disconnect, which are exactly the ones a consumer needs
     * to resume from the right cursor. */
    /*
     * Leave the server's list before closing the socket, not after.
     *
     * MHD_upgrade_action(CLOSE) closes the fd, and the kernel is free to hand
     * that same number straight back to the next accepted connection. While
     * this stream was still listed, wf_xrpc_server_stop would read the stale
     * number and shutdown() somebody else's live connection.
     *
     * The same critical section decides who joins this thread: if stop() has
     * already claimed it (`reaped`) it holds our id and will join, so we must
     * not detach. Otherwise nothing will ever join us and we detach here —
     * without that, every finished WebSocket connection would leak a thread
     * descriptor and its stack for the life of the process.
     */
    bool reaped = false;
    if (s->server) {
        pthread_mutex_lock(&s->server->ws_mutex);
        wf_xrpc_ws_stream **pp = &s->server->ws_streams;
        while (*pp) {
            if (*pp == s) { *pp = s->next; break; }
            pp = &(*pp)->next;
        }
        reaped = s->reaped;
        pthread_mutex_unlock(&s->server->ws_mutex);
    }

    wf_ws_shutdown_gracefully(s->sock);
    MHD_upgrade_action(s->urh, MHD_UPGRADE_ACTION_CLOSE);

    if (!reaped) {
        pthread_detach(pthread_self());
    }

    free((void *)req.nsid);
    free((void *)req.auth_header);
    free((void *)req.dpop_header);
    cJSON_Delete(req.params);
    free(req.authed_subject);
    free(s->nsid);
    pthread_cond_destroy(&s->worker_cond);
    pthread_mutex_destroy(&s->mutex);
    free(s);
    return NULL;
}

/** libmicrohttpd upgrade callback: hand off the raw socket to a worker. */
static void wf_ws_upgrade_handler(void *cls,
                                  struct MHD_Connection *connection,
                                  void *req_cls,
                                  const char *extra_in,
                                  size_t extra_in_size,
                                  MHD_socket sock,
                                  struct MHD_UpgradeResponseHandle *urh) {
    (void)connection;
    (void)req_cls;
    (void)extra_in;
    (void)extra_in_size;
    wf_ws_upgrade_ctx *uc = (wf_ws_upgrade_ctx *)cls;
    wf_xrpc_server *server = uc->server;
    /* Hold the stream directly: the worker frees `uc` as its first act, so
     * `uc` must not be dereferenced once pthread_create has succeeded. */
    wf_xrpc_ws_stream *stream = uc->stream;
    /* The handover happened, so connection close must not also free this. */
    wf_ws_pending_take(server, NULL, uc);
    pthread_t tid;

    /*
     * MHD hands us MHD_INVALID_SOCKET when it could not complete the upgrade
     * itself — it cannot allocate the internal socketpair under load, or the
     * connection died first. Spawning a worker on that means poll()/read() on
     * fd -1 for the life of the stream: EBADF at best, and on a platform where
     * the cast lands on a live descriptor, reads and writes against somebody
     * else's connection. Refuse before anything is spawned or published.
     */
    if (sock == MHD_INVALID_SOCKET) {
        MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
        free((void *)uc->req.nsid);
        free((void *)uc->req.auth_header);
        free((void *)uc->req.dpop_header);
        cJSON_Delete(uc->req.params);
        free(uc->req.authed_subject);
        free(stream->nsid);
        pthread_cond_destroy(&stream->worker_cond);
        pthread_mutex_destroy(&stream->mutex);
        free(stream);
        free(uc);
        return;
    }

    stream->sock = (int)sock;
    stream->urh = urh;

    /*
     * Spawn the worker and publish the stream under one hold of ws_mutex.
     *
     * The list must never contain a stream whose `thread` has not been stored:
     * wf_xrpc_server_stop reads that field under this same lock to decide what
     * to join, and would otherwise join a zero id. The worker is parked on
     * thread_ready throughout, so touching the stream here is safe even though
     * the thread is already running.
     */
    pthread_mutex_lock(&server->ws_mutex);

    if (server->ws_stopping) {
        /* Teardown has started; this connection would never be joined. */
        pthread_mutex_unlock(&server->ws_mutex);
        MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
        free((void *)uc->req.nsid);
        free((void *)uc->req.auth_header);
        free((void *)uc->req.dpop_header);
        cJSON_Delete(uc->req.params);
        free(uc->req.authed_subject);
        free(stream->nsid);
        pthread_cond_destroy(&stream->worker_cond);
        pthread_mutex_destroy(&stream->mutex);
        free(stream);
        free(uc);
        return;
    }

    if (pthread_create(&tid, NULL, wf_ws_serve_thread, uc) != 0) {
        /* Could not spawn a worker: close the upgrade immediately. */
        pthread_mutex_unlock(&server->ws_mutex);
        MHD_upgrade_action(urh, MHD_UPGRADE_ACTION_CLOSE);
        free((void *)uc->req.nsid);
        free((void *)uc->req.auth_header);
        free((void *)uc->req.dpop_header);
        cJSON_Delete(uc->req.params);
        free(uc->req.authed_subject);
        free(stream->nsid);
        pthread_cond_destroy(&stream->worker_cond);
        pthread_mutex_destroy(&stream->mutex);
        free(stream);
        free(uc);
        return;
    }

    /* Record the id and publish the stream, both under ws_mutex, so stop()
     * never observes a listed stream with an unwritten thread. */
    stream->thread = tid;
    stream->next = server->ws_streams;
    server->ws_streams = stream;
    pthread_mutex_unlock(&server->ws_mutex);

    /* Release the worker. Releasing stream->mutex publishes the store above,
     * which the worker acquires when it observes thread_ready. */
    pthread_mutex_lock(&stream->mutex);
    stream->thread_ready = true;
    pthread_cond_broadcast(&stream->worker_cond);
    pthread_mutex_unlock(&stream->mutex);
}

/**
 * Perform the RFC 6455 handshake and queue the 101 upgrade response.
 * Returns MHD_YES if the upgrade was queued (caller returns MHD_YES), or
 * MHD_NO if the request was not a valid WebSocket upgrade (caller should
 * send a 400 error).
 */
static enum MHD_Result wf_server_ws_handshake(wf_xrpc_server *server,
                                              wf_route *route,
                                              struct MHD_Connection *conn,
                                              const wf_xrpc_request *request) {
    const char *nsid = request ? request->nsid : NULL;
    const char *upgrade = MHD_lookup_connection_value(conn, MHD_HEADER_KIND,
                                                       "Upgrade");
    const char *connection = MHD_lookup_connection_value(conn, MHD_HEADER_KIND,
                                                          "Connection");
    const char *key = MHD_lookup_connection_value(conn, MHD_HEADER_KIND,
                                                  "Sec-WebSocket-Key");
    const char *version = MHD_lookup_connection_value(conn, MHD_HEADER_KIND,
                                                      "Sec-WebSocket-Version");
    wf_xrpc_ws_stream *stream;
    wf_ws_upgrade_ctx *uc;
    struct MHD_Response *resp;
#if defined(WOLFRAM_WIIU)
    /* No OpenSSL on the console; mbedTLS supplies the SHA-1 that RFC 6455
     * requires for Sec-WebSocket-Accept. */
    unsigned char digest[20];
#else
    unsigned char digest[SHA_DIGEST_LENGTH];
#endif
    char accept[40];
    char concat[256];

    /* Validate the handshake request per RFC 6455 §4.2.1. */
    if (!upgrade || strcasecmp(upgrade, "websocket") != 0) return MHD_NO;
    if (!connection || strcasestr(connection, "upgrade") == NULL) return MHD_NO;
    if (!version || strcmp(version, "13") != 0) return MHD_NO;
    if (!key || key[0] == '\0') return MHD_NO;

    snprintf(concat, sizeof(concat),
             "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
#if defined(WOLFRAM_WIIU)
    mbedtls_sha1((const unsigned char *)concat, strlen(concat), digest);
    wf_ws_base64_encode(digest, 20, accept);
#else
    SHA1((const unsigned char *)concat, strlen(concat), digest);
    wf_ws_base64_encode(digest, SHA_DIGEST_LENGTH, accept);
#endif

    stream = (wf_xrpc_ws_stream *)calloc(1, sizeof(*stream));
    if (!stream) return MHD_NO;
    stream->server = server;
    stream->nsid = nsid ? strdup(nsid) : NULL;
    if (nsid && !stream->nsid) {
        free(stream);
        return MHD_NO;
    }
    if (pthread_mutex_init(&stream->mutex, NULL) != 0) {
        free(stream->nsid);
        free(stream);
        return MHD_NO;
    }
    if (pthread_cond_init(&stream->worker_cond, NULL) != 0) {
        pthread_mutex_destroy(&stream->mutex);
        free(stream->nsid);
        free(stream);
        return MHD_NO;
    }

    uc = (wf_ws_upgrade_ctx *)calloc(1, sizeof(*uc));
    if (!uc) {
        pthread_cond_destroy(&stream->worker_cond);
        pthread_mutex_destroy(&stream->mutex);
        free(stream->nsid);
        free(stream);
        return MHD_NO;
    }
    uc->server = server;
    uc->route = route;
    uc->stream = stream;
    uc->req.nsid = nsid ? strdup(nsid) : NULL;
    uc->req.method = "GET";
    uc->req.auth_header = request && request->auth_header
                              ? strdup(request->auth_header) : NULL;
    uc->req.dpop_header = request && request->dpop_header
                              ? strdup(request->dpop_header) : NULL;
    uc->req.params = request && request->params
                         ? cJSON_Duplicate(request->params, true) : NULL;
    uc->req.handler_ctx = route->ctx;
    uc->req.authed_subject = request && request->authed_subject
                                 ? strdup(request->authed_subject) : NULL;
    uc->req.authed_principal_kind = request
                                        ? request->authed_principal_kind
                                        : WF_XRPC_PRINCIPAL_NONE;
    if ((nsid && !uc->req.nsid) ||
        (request && request->auth_header && !uc->req.auth_header) ||
        (request && request->dpop_header && !uc->req.dpop_header) ||
        (request && request->params && !uc->req.params) ||
        (request && request->authed_subject && !uc->req.authed_subject)) {
        free((void *)uc->req.nsid);
        free((void *)uc->req.auth_header);
        free((void *)uc->req.dpop_header);
        cJSON_Delete(uc->req.params);
        free(uc->req.authed_subject);
        free(uc);
        pthread_cond_destroy(&stream->worker_cond);
        pthread_mutex_destroy(&stream->mutex);
        free(stream->nsid);
        free(stream);
        return MHD_NO;
    }

    resp = MHD_create_response_for_upgrade(wf_ws_upgrade_handler, uc);
    if (!resp) {
        free((void *)uc->req.nsid);
        free((void *)uc->req.auth_header);
        free((void *)uc->req.dpop_header);
        cJSON_Delete(uc->req.params);
        free(uc->req.authed_subject);
        free(uc);
        pthread_cond_destroy(&stream->worker_cond);
        pthread_mutex_destroy(&stream->mutex);
        free(stream->nsid);
        free(stream);
        return MHD_NO;
    }
    MHD_add_response_header(resp, "Upgrade", "websocket");
    MHD_add_response_header(resp, "Connection", "Upgrade");
    MHD_add_response_header(resp, "Sec-WebSocket-Accept", accept);
    /* Track it until the handover: see wf_ws_notify_connection. */
    wf_ws_pending_add(server, conn, uc);
    MHD_queue_response(conn, MHD_HTTP_SWITCHING_PROTOCOLS, resp);
    MHD_destroy_response(resp);
    return MHD_YES;
}

wf_status wf_xrpc_server_ws_send(wf_xrpc_ws_stream *stream,
                                 const void *data, size_t len) {
    wf_xrpc_ws_stream *s = stream;
    if (!s) {
        return WF_ERR_INVALID_ARG;
    }
    if (!data && len > 0) {
        return WF_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s->mutex);
    wf_status rc = s->closed
                       ? WF_ERR_INVALID_ARG
                       : wf_ws_write_frame_locked(s, 0x2, data ? data : "", len);
    pthread_mutex_unlock(&s->mutex);
    return rc;
}

wf_status wf_xrpc_server_ws_ping(wf_xrpc_ws_stream *stream) {
    wf_xrpc_ws_stream *s = stream;
    if (!s) return WF_ERR_INVALID_ARG;
    pthread_mutex_lock(&s->mutex);
    /* Opcode 0x9, empty payload. A conforming peer replies with a pong, but
     * the value here is the outbound byte itself: it resets the idle timer on
     * every proxy between us and the subscriber. */
    wf_status rc = s->closed ? WF_ERR_INVALID_ARG
                             : wf_ws_write_frame_locked(s, 0x9, "", 0);
    pthread_mutex_unlock(&s->mutex);
    return rc;
}

wf_status wf_xrpc_server_ws_retain(wf_xrpc_ws_stream *stream) {
    if (!stream) return WF_ERR_INVALID_ARG;
    pthread_mutex_lock(&stream->mutex);
    if (stream->closed) {
        pthread_mutex_unlock(&stream->mutex);
        return WF_ERR_INVALID_ARG;
    }
    stream->worker_refs++;
    pthread_mutex_unlock(&stream->mutex);
    return WF_OK;
}

void wf_xrpc_server_ws_release(wf_xrpc_ws_stream *stream) {
    if (!stream) return;
    pthread_mutex_lock(&stream->mutex);
    if (stream->worker_refs > 0) stream->worker_refs--;
    if (stream->worker_refs == 0) pthread_cond_broadcast(&stream->worker_cond);
    pthread_mutex_unlock(&stream->mutex);
}

int wf_xrpc_server_ws_is_closed(wf_xrpc_ws_stream *stream) {
    int closed;
    if (!stream) return 1;
    pthread_mutex_lock(&stream->mutex);
    closed = stream->closed ? 1 : 0;
    pthread_mutex_unlock(&stream->mutex);
    return closed;
}

wf_status wf_xrpc_server_ws_close(wf_xrpc_ws_stream *stream, uint16_t code) {
    wf_xrpc_ws_stream *s = stream;
    unsigned char body[2];
    if (!s) return WF_ERR_INVALID_ARG;
    body[0] = (unsigned char)((code >> 8) & 0xff);
    body[1] = (unsigned char)(code & 0xff);
    pthread_mutex_lock(&s->mutex);
    if (s->closed) {
        pthread_mutex_unlock(&s->mutex);
        return WF_ERR_INVALID_ARG;
    }
    s->closed = true;
    wf_ws_write_frame_locked(s, 0x8, body, 2);
    pthread_mutex_unlock(&s->mutex);
    return WF_OK;
}

wf_status wf_xrpc_server_register_ws(wf_xrpc_server *server,
                                     const char *nsid,
                                     wf_xrpc_ws_handler handler,
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
    r->handler.ws = handler;
    r->ctx = ctx;
    r->is_ws = true;
    r->next = server->routes;
    server->routes = r;
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

void wf_xrpc_response_set_content_type(wf_xrpc_response *resp,
                                        const char *content_type) {
    if (!resp) {
        return;
    }
    free(resp->content_type);
    resp->content_type = content_type ? strdup(content_type) : NULL;
}

wf_status wf_xrpc_response_add_header(wf_xrpc_response *resp,
                                      const char *name, const char *value) {
    if (!resp || !name || !name[0] || !value || strchr(name, ':') ||
        strchr(name, '\r') || strchr(name, '\n') || strchr(value, '\r') ||
        strchr(value, '\n'))
        return WF_ERR_INVALID_ARG;
    wf_xrpc_response_header *header = calloc(1, sizeof(*header));
    if (!header) return WF_ERR_ALLOC;
    header->name = strdup(name);
    header->value = strdup(value);
    if (!header->name || !header->value) {
        free(header->name);
        free(header->value);
        free(header);
        return WF_ERR_ALLOC;
    }
    header->next = resp->headers;
    resp->headers = header;
    return WF_OK;
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

static enum MHD_Result wf_server_qs_build_iter(void *cls,
                                                enum MHD_ValueKind kind,
                                                const char *key,
                                                const char *value) {
    (void)kind;
    struct qs_build_ctx {
        char *buf;
        size_t len;
        size_t cap;
    } *c = (struct qs_build_ctx *)cls;
    size_t needed = (key ? strlen(key) : 0) + (value ? strlen(value) : 0) + 2;
    if (c->len > 0) needed++;
    if (c->len + needed + 1 > c->cap) {
        size_t newcap = (c->cap + needed) * 2;
        char *grown = realloc(c->buf, newcap);
        if (!grown) return MHD_NO;
        c->buf = grown;
        c->cap = newcap;
    }
    if (c->len > 0) c->buf[c->len++] = '&';
    if (key) {
        strcpy(c->buf + c->len, key);
        c->len += strlen(key);
    }
    c->buf[c->len++] = '=';
    if (value) {
        strcpy(c->buf + c->len, value);
        c->len += strlen(value);
    }
    return MHD_YES;
}

static char *wf_server_build_raw_query(struct MHD_Connection *conn) {
    struct qs_build_ctx {
        char *buf;
        size_t len;
        size_t cap;
    } ctx = {0};
    MHD_get_connection_values(conn, MHD_GET_ARGUMENT_KIND,
                              &wf_server_qs_build_iter, &ctx);
    return ctx.buf;
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

static wf_static_route *wf_server_find_static_route(
        const wf_xrpc_server *server, const char *path) {
    for (wf_static_route *route = server->static_routes; route;
         route = route->next)
        if (strcmp(route->path, path) == 0) return route;
    return NULL;
}

static wf_http_route *wf_server_find_http_route(
        const wf_xrpc_server *server, const char *method, const char *path) {
    wf_http_route *best_prefix = NULL;
    size_t best_len = 0;
    for (wf_http_route *route = server->http_routes; route;
         route = route->next) {
        if (strcmp(route->method, method) != 0) continue;
        if (!route->prefix) {
            if (strcmp(route->path, path) == 0) return route;
            continue;
        }
        /* Longest prefix wins, so nested prefixes stay predictable. */
        size_t len = strlen(route->path);
        if (strncmp(route->path, path, len) == 0 && len > best_len) {
            best_prefix = route;
            best_len = len;
        }
    }
    return best_prefix;
}

static bool wf_server_has_http_path(const wf_xrpc_server *server,
                                    const char *path) {
    for (wf_http_route *route = server->http_routes; route;
         route = route->next)
        if (strcmp(route->path, path) == 0) return true;
    return false;
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

/* Report a finished request to the observer, if one is installed. Called
 * from every path that decides a status, the rate limiter's 429 included —
 * a refusal the caller never sees counted is the one an operator most needs
 * to see. */
static void wf_server_observe(wf_xrpc_server *server, const char *nsid,
                              const char *path, const char *method,
                              unsigned int status) {
    if (server && server->observer)
        server->observer(server->observer_ctx, nsid, path, method, status);
}

/* Extract the client's address into `out` ("unknown" if it cannot be
 * determined). Shared by the built-in rate limiter and wf_xrpc_request's
 * client_ip, which previously duplicated this dance independently. */
static void wf_server_client_ip(struct MHD_Connection *conn, char *out,
                                size_t out_len) {
    const union MHD_ConnectionInfo *ci =
        MHD_get_connection_info(conn, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
    if (ci && ci->client_addr) {
        if (ci->client_addr->sa_family == AF_INET) {
            inet_ntop(AF_INET,
                      &((struct sockaddr_in *)ci->client_addr)->sin_addr,
                      out, out_len);
            return;
#if !defined(WOLFRAM_WIIU)
        } else if (ci->client_addr->sa_family == AF_INET6) {
            inet_ntop(AF_INET6,
                      &((struct sockaddr_in6 *)ci->client_addr)->sin6_addr,
                      out, out_len);
            return;
#endif
        }
    }
    (void)snprintf(out, out_len, "unknown");
}

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
    wf_xrpc_request req = {0};
    wf_route_kind kind;
    wf_route *route = NULL;
    wf_http_route *http_route = NULL;
    char *nsid = NULL;
    cJSON *params = NULL;
    const char *auth_header;
    const char *dpop_header;
    const char *cookie_header;
    post_buf *raw_pb = NULL;   /* Kept alive past parsing so handlers can read
                                  the raw POST body (e.g. blob uploads). */

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
        http_route = wf_server_find_http_route(server, method, url);
        if (http_route) {
            if (pb->len > 0)
                params = cJSON_ParseWithLength(pb->data, pb->len);
            raw_pb = pb;
            *con_cls = NULL;
            goto process;
        }
        if (wf_server_has_http_path(server, url)) {
            resp.http_status = 405;
            free(pb->data);
            free(pb);
            *con_cls = NULL;
            goto send;
        }
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
        /* Keep the raw body buffer alive so handlers (e.g. blob uploads) can
         * read req->body / req->body_len. Freed in cleanup. */
        raw_pb = pb;
        *con_cls = NULL;
        kind = WF_ROUTE_PROCEDURE;
        goto process;
    }

    /* Fixed public GET routes live outside the XRPC namespace. */
    wf_static_route *static_route = wf_server_find_static_route(server, url);
    if (static_route) {
        wf_xrpc_response_set_body(&resp, (const char *)static_route->body,
                                  static_route->body_len);
        wf_xrpc_response_set_content_type(&resp, static_route->content_type);
        goto send;
    }

    http_route = wf_server_find_http_route(server, method, url);
    if (http_route) {
        params = wf_server_get_query_params(conn);
        goto process;
    }
    if (wf_server_has_http_path(server, url)) {
        resp.http_status = 405;
        goto send;
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

#if defined(WOLFRAM_WIIU)
    /* wut's headers have no IPv6; a console on a home LAN is v4-only. */
    char client_ip_str[INET_ADDRSTRLEN];
#else
    char client_ip_str[INET6_ADDRSTRLEN];
#endif
    wf_server_client_ip(conn, client_ip_str, sizeof(client_ip_str));

    /* Look up route. Distinguish a wrong HTTP method (the NSID is registered
     * but for the opposite kind) from an entirely unregistered NSID, so we emit
     * the canonical XRPC error names and status codes. */
    if (!http_route) route = wf_server_find_route(server, nsid, kind);
    if (!http_route && !route) {
        wf_route_kind other = (kind == WF_ROUTE_QUERY)
                                  ? WF_ROUTE_PROCEDURE
                                  : WF_ROUTE_QUERY;
        if (wf_server_find_route(server, nsid, other)) {
            wf_xrpc_response_set_error(
                &resp, 400, "InvalidRequest",
                "Incorrect HTTP method for this endpoint");
        } else if (server->fallback) {
            /* Unknown NSID with a fallback installed (e.g. an AppView
             * proxy). The request struct is not built yet at this point, so
             * assemble a minimal one here; the fallback runs before the auth
             * callback and must verify any bearer token itself. */
            wf_xrpc_request freq;
            memset(&freq, 0, sizeof(freq));
            freq.nsid = nsid;
            freq.path = url;
            freq.method = method;
            freq.auth_header = MHD_lookup_connection_value(
                conn, MHD_HEADER_KIND, "Authorization");
            freq.dpop_header = MHD_lookup_connection_value(
                conn, MHD_HEADER_KIND, "DPoP");
            freq.params = params;
            freq.client_ip = client_ip_str;
            freq.handler_ctx = server->fallback_ctx;
            freq.raw_query = wf_server_build_raw_query(conn);
            freq.atproto_proxy = MHD_lookup_connection_value(
                conn, MHD_HEADER_KIND, "atproto-proxy");
            freq.content_type = MHD_lookup_connection_value(
                conn, MHD_HEADER_KIND, "Content-Type");
            freq.host_header = MHD_lookup_connection_value(
                conn, MHD_HEADER_KIND, "Host");
            if (raw_pb) {
                freq.body = (const unsigned char *)raw_pb->data;
                freq.body_len = raw_pb->len;
            }
            server->fallback(server->fallback_ctx, &freq, &resp);
        } else {
            wf_xrpc_response_set_error(
                &resp, 501, "MethodNotImplemented",
                "No handler registered for this NSID");
        }
        goto send;
    }

    /* Rate limiter — charge 1 token against client IP. A route-specific
     * limiter set via wf_xrpc_server_set_route_rate_limiter replaces the
     * global one for that exact method+url, matching its documented
     * contract ("defaults to IP-based limiter if none set"); otherwise the
     * server-wide limiter applies as before. */
    wf_rate_limiter *active_rate_limiter =
        wf_server_find_route_rate_limiter(server, method, url);
    if (!active_rate_limiter) active_rate_limiter = server->rate_limiter;
    if (active_rate_limiter) {
        wf_rate_limit_status rl_status = {0};

        if (wf_rate_limiter_consume_status(active_rate_limiter, client_ip_str,
                                           1, &rl_status) != WF_OK) {
            struct MHD_Response *mhd_rl;
            char body[192];
            char num[16];
            int n;
            /* seconds-until-available, for Retry-After: derived from
             * reset_at rather than recomputed, so this always agrees with
             * what the RateLimit-Reset header below claims. */
            time_t now = time(NULL);
            unsigned int retry_after =
                rl_status.reset_at > (unsigned int)now
                    ? rl_status.reset_at - (unsigned int)now
                    : 1;

            n = snprintf(body, sizeof(body),
                         "{\"error\":\"RateLimitExceeded\","
                         "\"message\":\"Rate limit exceeded. "
                         "Retry after %u seconds.\"}",
                         retry_after);
            if (n < 0 || (size_t)n >= sizeof(body)) n = (int)sizeof(body) - 1;

            mhd_rl = MHD_create_response_from_buffer(
                (size_t)n, body, MHD_RESPMEM_MUST_COPY);
            if (mhd_rl) {
                MHD_add_response_header(mhd_rl, "Content-Type",
                                         "application/json");
                snprintf(num, sizeof(num), "%u", retry_after);
                MHD_add_response_header(mhd_rl, "Retry-After", num);
                /* RateLimit-*, matching the reference PDS's
                 * rate-limiter-http.ts setStatusHeaders exactly. */
                snprintf(num, sizeof(num), "%u", rl_status.limit);
                MHD_add_response_header(mhd_rl, "RateLimit-Limit", num);
                snprintf(num, sizeof(num), "%u", rl_status.reset_at);
                MHD_add_response_header(mhd_rl, "RateLimit-Reset", num);
                snprintf(num, sizeof(num), "%u", rl_status.remaining);
                MHD_add_response_header(mhd_rl, "RateLimit-Remaining", num);
                snprintf(num, sizeof(num), "%u;w=%u", rl_status.limit,
                        rl_status.duration_seconds);
                MHD_add_response_header(mhd_rl, "RateLimit-Policy", num);
                wf_server_apply_cors(server, conn, mhd_rl);
                MHD_queue_response(conn, 429, mhd_rl);
                MHD_destroy_response(mhd_rl);
            }
            wf_server_observe(server, nsid, url, method, 429);
            ret = MHD_YES;
            goto cleanup;
        }
    }

    /* Auth callback */
    auth_header = MHD_lookup_connection_value(conn, MHD_HEADER_KIND,
                                                "Authorization");
    dpop_header = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "DPoP");
    cookie_header = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Cookie");
    char *authed_subject = NULL;
    wf_xrpc_principal_kind authed_kind = WF_XRPC_PRINCIPAL_NONE;
    if (!http_route && server->auth_cb) {
        wf_xrpc_request auth_req;
        memset(&auth_req, 0, sizeof(auth_req));
        auth_req.nsid = nsid;
        auth_req.path = url;
        auth_req.method = method;
        auth_req.auth_header = auth_header;
        auth_req.dpop_header = dpop_header;
        auth_req.cookie_header = cookie_header;
        auth_req.params = params;
        auth_req.handler_ctx = route->ctx;
        auth_req.host_header = MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Host");
        auth_req.client_ip = client_ip_str;
        wf_status auth_status = server->auth_cb(&auth_req, server->auth_ctx);
        if (auth_status != WF_OK) {
            if (auth_status == WF_ERR_CONFLICT)
                wf_xrpc_response_set_error(&resp, 400, "RepoDeactivated",
                                           "Repository is deactivated");
            else
                wf_xrpc_response_set_error(&resp, 401,
                                           "AuthenticationRequired",
                                           "Authentication required");
            goto send;
        }
        /* The auth callback may have authenticated a principal; capture its
         * subject (server takes ownership from here). */
        authed_subject = auth_req.authed_subject;
        auth_req.authed_subject = NULL;
        authed_kind = auth_req.authed_principal_kind;
    }

    /* Build request and call handler */
    memset(&req, 0, sizeof(req));
    req.nsid = nsid;
    req.path = url;
    req.method = method;
    req.auth_header = auth_header;
    req.dpop_header = dpop_header;
    req.cookie_header = cookie_header;
    req.params = params;
    req.client_ip = client_ip_str;
    req.handler_ctx = http_route ? http_route->ctx : route->ctx;
    /* Raw POST body (kept alive in raw_pb) + request Content-Type. */
    if (raw_pb) {
        req.body = (const unsigned char *)raw_pb->data;
        req.body_len = raw_pb->len;
    }
    req.content_type =
        MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Content-Type");
    req.host_header =
        MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Host");
    req.raw_query = wf_server_build_raw_query(conn);
    req.atproto_proxy =
        MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "atproto-proxy");
    /* Carry the authenticated principal (if any) established above.
     * `authed_subject` is server-owned and freed in cleanup. */
    req.authed_subject = authed_subject;
    req.authed_principal_kind = authed_kind;

    if (route && route->is_ws) {
        enum MHD_Result wsr = wf_server_ws_handshake(server, route, conn, &req);
        if (wsr == MHD_YES) {
            ret = MHD_YES;
            goto cleanup;
        }
        /* Not a valid WebSocket upgrade request. */
        wf_xrpc_response_set_error(&resp, 400, "InvalidRequest",
                                   "WebSocket upgrade required");
        goto send;
    }

    if (route && route->is_sse) {
        goto sse_stream;
    }

    if (route && !http_route && !static_route) {
        WF_LOG_DEBUG("xrpc",
            "REQ %s %s nsid=%s auth=%s host=%s",
            method, url,
            nsid ? nsid : "-",
            auth_header ? "yes" : "no",
            req.host_header ? req.host_header : "-");
    }

    if (http_route) {
        http_route->handler(http_route->ctx, &req, &resp);
    } else if (kind == WF_ROUTE_QUERY) {
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
    } else if (resp.content_type) {
        /* Handler requested a custom Content-Type (e.g. raw blob bytes). */
        MHD_add_response_header(mhd_resp, "Content-Type", resp.content_type);
    } else {
        MHD_add_response_header(mhd_resp, "Content-Type", "application/json");
    }
    for (wf_xrpc_response_header *header = resp.headers; header;
         header = header->next)
        MHD_add_response_header(mhd_resp, header->name, header->value);
    wf_server_apply_cors(server, conn, mhd_resp);

    wf_server_observe(server, nsid, url, method, resp.http_status);
    ret = MHD_queue_response(conn, resp.http_status, mhd_resp);
    MHD_destroy_response(mhd_resp);

    goto cleanup;

sse_stream:
    {
        wf_xrpc_sse_stream *stream;
        struct MHD_Response *mhd_resp_sse;

        stream = wf_sse_stream_new(server, conn, nsid);
        if (!stream) {
            wf_xrpc_response_set_error(&resp, 500, "InternalServerError",
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
            wf_xrpc_response_set_error(&resp, 500, "InternalServerError",
                                       "Failed to create SSE response");
            goto send;
        }

        MHD_add_response_header(mhd_resp_sse, "Content-Type",
                                "text/event-stream");
        MHD_add_response_header(mhd_resp_sse, "Cache-Control", "no-cache");
        MHD_add_response_header(mhd_resp_sse, "Connection", "keep-alive");
        MHD_add_response_header(mhd_resp_sse, "X-Accel-Buffering", "no");
        wf_server_apply_cors(server, conn, mhd_resp_sse);

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
    free(resp.content_type);
    while (resp.headers) {
        wf_xrpc_response_header *next = resp.headers->next;
        free(resp.headers->name);
        free(resp.headers->value);
        free(resp.headers);
        resp.headers = next;
    }
    if (raw_pb) {
        free(raw_pb->data);
        free(raw_pb);
    }
    free(req.authed_subject); /* server-owned; NULL when no principal */
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
    wf_xrpc_server *server = (wf_xrpc_server *)cls;
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
    wf_server_apply_cors(server, conn, resp);
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
    server->ws_streams = NULL;
    server->cors_enabled = true;
    server->cors_origin = (char *)strdup("*"); /* NULL falls back to "*" */
    if (pthread_mutex_init(&server->sse_mutex, NULL) != 0) {
        free(server);
        return NULL;
    }
    if (pthread_mutex_init(&server->ws_mutex, NULL) != 0) {
        pthread_mutex_destroy(&server->sse_mutex);
        free(server);
        return NULL;
    }

    if (thread_count == 0) {
        thread_count = 4;
    }

    server->daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD | MHD_ALLOW_SUSPEND_RESUME |
            MHD_ALLOW_UPGRADE,
        port,
        NULL, NULL,                       /* Accept policy */
        &wf_server_mhd_handler, server,   /* Main handler */
        MHD_OPTION_NOTIFY_COMPLETED, NULL, NULL,
        MHD_OPTION_NOTIFY_CONNECTION, &wf_ws_notify_connection, server,
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

    /* Mark open WebSocket connections closed and wake their poll loops so the
     * upgrade worker threads exit promptly (they join in wf_xrpc_server_free).
     * Latch ws_stopping first so no further upgrade can join the list behind
     * the drain loop below. */
    pthread_mutex_lock(&server->ws_mutex);
    server->ws_stopping = true;
    for (wf_xrpc_ws_stream *s = server->ws_streams; s; s = s->next) {
        pthread_mutex_lock(&s->mutex);
        s->closed = true;
        pthread_mutex_unlock(&s->mutex);
        shutdown(s->sock, SHUT_RDWR);
    }
    pthread_mutex_unlock(&server->ws_mutex);

    /*
     * Join the WebSocket workers BEFORE tearing down the daemon: each one
     * calls MHD_upgrade_action() to close its upgrade, which is only valid
     * while the daemon is alive.
     *
     * Pop one stream at a time and claim it, rather than snapshotting into a
     * fixed array: a bounded snapshot that still cleared the whole list left
     * every worker past the limit unlinked and unjoined, running on into a
     * freed server. Sixty-five firehose subscribers is not an exotic load.
     * Claiming under ws_mutex also settles ownership — the worker sees
     * `reaped` and leaves the join to us instead of detaching.
     */
    for (;;) {
        wf_xrpc_ws_stream *s;
        pthread_t tid;

        pthread_mutex_lock(&server->ws_mutex);
        s = server->ws_streams;
        if (s) {
            server->ws_streams = s->next;
            s->reaped = true;
            tid = s->thread;
        }
        pthread_mutex_unlock(&server->ws_mutex);

        if (!s) {
            break;
        }
        pthread_join(tid, NULL);
    }

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

    /* Anything still queued for upgrade when the daemon went away will never
     * be handed over, and its connection-close notification can no longer
     * arrive. Release those here so shutdown does not strand them. */
    for (;;) {
        wf_ws_upgrade_ctx *pending = wf_ws_pending_take(server, NULL, NULL);
        if (!pending) break;
        wf_ws_discard_upgrade(pending);
    }

    /* stop() has already joined every WebSocket upgrade worker thread (and
     * torn down the daemon), so no streams remain and routes can be freed. */
    r = server->routes;
    while (r) {
        wf_route *next = r->next;
        free(r->nsid);
        free(r);
        r = next;
    }
    wf_static_route *static_route = server->static_routes;
    while (static_route) {
        wf_static_route *next = static_route->next;
        free(static_route->path);
        free(static_route->content_type);
        free(static_route->body);
        free(static_route);
        static_route = next;
    }
    wf_http_route *http_route = server->http_routes;
    while (http_route) {
        wf_http_route *next = http_route->next;
        free(http_route->method);
        free(http_route->path);
        free(http_route);
        http_route = next;
    }
    if (server->rate_limit_entries) {
        wf_server_free_rate_limit_entries(server->rate_limit_entries);
    }
    pthread_mutex_destroy(&server->sse_mutex);
    pthread_mutex_destroy(&server->ws_mutex);
    free(server->cors_origin);
    if (server->rate_limiter_owned) {
        wf_rate_limiter_free(server->rate_limiter_owned);
    }
    if (server->auth_mw_ctx && server->auth_mw_free) {
        server->auth_mw_free(server->auth_mw_ctx);
    }
    /* Release resolver bundles (and any other owned contexts) installed by
     * wf_xrpc_server_own_ctx, each via its own free function. */
    struct wf_owned_ctx *oc = server->owned_ctxs;
    while (oc) {
        struct wf_owned_ctx *next = oc->next;
        if (oc->free_fn) oc->free_fn(oc->ptr);
        free(oc);
        oc = next;
    }
    free(server);
}

uint16_t wf_xrpc_server_port(const wf_xrpc_server *server) {
    return server ? server->port : 0;
}

/*
 * Register a heap allocation the server owns and frees (via `free_fn`) in
 * wf_xrpc_server_free. Used by the PDS repo/blob resolver registrations so the
 * per-request resolver bundle is released with the server. Returns WF_ERR_ALLOC
 * on allocation failure (in which case nothing is recorded); the caller keeps
 * ownership of `ptr` in that case. `ptr` may be NULL (no-op).
 */
wf_status wf_xrpc_server_own_ctx(wf_xrpc_server *server, void *ptr,
                                 void (*free_fn)(void *)) {
    if (!server || !ptr) return WF_ERR_INVALID_ARG;
    struct wf_owned_ctx *node =
        (struct wf_owned_ctx *)malloc(sizeof(*node));
    if (!node) return WF_ERR_ALLOC;
    node->ptr = ptr;
    node->free_fn = free_fn;
    node->next = server->owned_ctxs;
    server->owned_ctxs = node;
    return WF_OK;
}

wf_status wf_xrpc_server_register_static_get(wf_xrpc_server *server,
                                             const char *path,
                                             const char *content_type,
                                             const void *body,
                                             size_t body_len) {
    if (!server || !path || path[0] != '/' || !content_type ||
        (!body && body_len > 0))
        return WF_ERR_INVALID_ARG;
    if (wf_server_find_static_route(server, path) ||
        wf_server_has_http_path(server, path))
        return WF_ERR_INVALID_ARG;
    wf_static_route *route = calloc(1, sizeof(*route));
    if (!route) return WF_ERR_ALLOC;
    route->path = strdup(path);
    route->content_type = strdup(content_type);
    route->body = malloc(body_len ? body_len : 1);
    if (!route->path || !route->content_type || !route->body) {
        free(route->path);
        free(route->content_type);
        free(route->body);
        free(route);
        return WF_ERR_ALLOC;
    }
    if (body_len) memcpy(route->body, body, body_len);
    route->body_len = body_len;
    route->next = server->static_routes;
    server->static_routes = route;
    return WF_OK;
}

static wf_status wf_server_add_http_route(wf_xrpc_server *server,
                                          const char *method,
                                          const char *path,
                                          bool prefix,
                                          wf_http_route_handler handler,
                                          void *ctx) {
    if (!server || !method || !path || path[0] != '/' || !handler ||
        (strcmp(method, "GET") != 0 && strcmp(method, "POST") != 0))
        return WF_ERR_INVALID_ARG;
    if (wf_server_find_static_route(server, path) ||
        wf_server_find_http_route(server, method, path))
        return WF_ERR_INVALID_ARG;
    wf_http_route *route = calloc(1, sizeof(*route));
    if (!route) return WF_ERR_ALLOC;
    route->method = strdup(method);
    route->path = strdup(path);
    if (!route->method || !route->path) {
        free(route->method);
        free(route->path);
        free(route);
        return WF_ERR_ALLOC;
    }
    route->prefix = prefix;
    route->handler = handler;
    route->ctx = ctx;
    route->next = server->http_routes;
    server->http_routes = route;
    return WF_OK;
}

wf_status wf_xrpc_server_register_http_route(wf_xrpc_server *server,
                                             const char *method,
                                             const char *path,
                                             wf_http_route_handler handler,
                                             void *ctx) {
    return wf_server_add_http_route(server, method, path, false, handler, ctx);
}

wf_status wf_xrpc_server_register_http_prefix(wf_xrpc_server *server,
                                              const char *method,
                                              const char *prefix,
                                              wf_http_route_handler handler,
                                              void *ctx) {
    return wf_server_add_http_route(server, method, prefix, true, handler, ctx);
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

void wf_xrpc_server_set_cors(wf_xrpc_server *server, bool enabled,
                              const char *origin) {
    if (!server) {
        return;
    }
    server->cors_enabled = enabled;
    free(server->cors_origin);
    server->cors_origin = (origin && origin[0]) ? strdup(origin) : NULL;
}

void wf_xrpc_server_set_request_observer(wf_xrpc_server *server,
                                         wf_xrpc_request_observer cb,
                                         void *ctx) {
    if (!server) return;
    server->observer = cb;
    server->observer_ctx = ctx;
}

void wf_xrpc_server_set_auth_callback(wf_xrpc_server *server,
                                        wf_xrpc_auth_cb cb, void *ctx) {
    if (!server) {
        return;
    }
    /* Installing an external auth callback supersedes any middleware that was
     * previously attached; release its owned context. */
    if (server->auth_mw_ctx && server->auth_mw_free) {
        server->auth_mw_free(server->auth_mw_ctx);
    }
    server->auth_mw_ctx = NULL;
    server->auth_mw_free = NULL;
    server->auth_cb = cb;
    server->auth_ctx = ctx;
}

void wf_xrpc_server_set_fallback(wf_xrpc_server *server,
                                 wf_xrpc_fallback_handler handler, void *ctx) {
    if (!server) {
        return;
    }
    server->fallback = handler;
    server->fallback_ctx = ctx;
}

void wf_xrpc_server_set_auth_callback_owned(wf_xrpc_server *server,
                                            wf_xrpc_auth_cb cb, void *ctx,
                                            void *mw_ctx,
                                            void (*mw_free)(void *)) {
    if (!server) {
        return;
    }
    /* Release any previously attached middleware context first. */
    if (server->auth_mw_ctx && server->auth_mw_free) {
        server->auth_mw_free(server->auth_mw_ctx);
    }
    server->auth_cb = cb;
    server->auth_ctx = ctx;
    server->auth_mw_ctx = mw_ctx;
    server->auth_mw_free = mw_free;
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

static wf_rate_limiter *wf_server_find_route_rate_limiter(
    wf_xrpc_server *server, const char *method, const char *url) {
    if (!server || !method || !url) return NULL;
    char key[512];
    int n = snprintf(key, sizeof(key), "%s:%s", method, url);
    if (n < 0 || (size_t)n >= sizeof(key)) return NULL;
    for (wf_rate_limit_entry *e = server->rate_limit_entries; e; e = e->next) {
        if (e->route_key && strcmp(e->route_key, key) == 0) return e->rl;
    }
    return NULL;
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
    /* "GET:/xrpc/io.example.ping", matching the struct's documented format —
     * method included so a POST and a GET to the same path don't collide. */
    {
        size_t n = strlen(method) + 1 + strlen(url) + 1;
        entry->route_key = malloc(n);
        if (entry->route_key)
            snprintf(entry->route_key, n, "%s:%s", method, url);
    }
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

/**
 * Attach an owned global rate limiter. Unlike wf_xrpc_server_set_rate_limiter
 * (which borrows), the server takes ownership and frees it in
 * wf_xrpc_server_free. Passing NULL is a no-op.
 */
void wf_xrpc_server_set_rate_limiter_owned(wf_xrpc_server *server,
                                            wf_rate_limiter *rl) {
    if (!server || !rl) {
        return;
    }
    if (server->rate_limiter_owned &&
        server->rate_limiter_owned != rl) {
        wf_rate_limiter_free(server->rate_limiter_owned);
    }
    server->rate_limiter_owned = rl;
    server->rate_limiter = rl;
}
