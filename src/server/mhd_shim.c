/*
 * mhd_shim.c — the slice of libmicrohttpd that xrpc_server.c uses, over
 * plain BSD sockets.
 *
 * Written against sockets and pthreads only, with no console-specific calls,
 * so it builds natively as well as for the Wii U. That is deliberate: a shim
 * that only compiles for a target nobody can run tests on is a shim nobody can
 * trust. Built natively it stands in for MHD under the existing server tests,
 * which is the only way to find out whether it honours the contract.
 *
 * Concurrency model: one acceptor thread, one thread per connection. MHD
 * offers several; xrpc_server.c asks for MHD_USE_INTERNAL_POLLING_THREAD, and
 * thread-per-connection satisfies it with far less machinery than an event
 * loop. It also makes suspend/resume — which MHD implements by removing a
 * connection from its poll set — a plain condition variable wait.
 *
 * What is deliberately NOT implemented, because xrpc_server.c never asks for
 * it: HTTP pipelining, chunked *request* bodies, keep-alive (every response
 * closes), digest auth, POST/form parsing, and TLS. TLS belongs a layer down;
 * see the send/recv indirection in `struct MHD_Connection`.
 */

#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif

#include "wolfram/compat/microhttpd.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define SHIM_HEADER_MAX 16384 /* request line + headers */
#define SHIM_BODY_CHUNK 8192  /* handed to the handler per call */
#define SHIM_MAX_KV 64        /* headers + query args per request */

/* ------------------------------------------------------------------ */
/* Key/value store — headers and query arguments                       */
/* ------------------------------------------------------------------ */

typedef struct kv {
    char *key;
    char *value;
    enum MHD_ValueKind kind;
} kv;

struct MHD_Response {
    /* Refcounted because MHD_queue_response takes a reference and the caller
     * drops its own with MHD_destroy_response immediately after; the response
     * has to outlive that to be written. */
    int refcount;

    enum { RESP_BUFFER, RESP_CALLBACK, RESP_UPGRADE } kind;

    char *body;
    size_t body_len;
    enum MHD_ResponseMemoryMode mem_mode;

    uint64_t total_size; /* MHD_SIZE_UNKNOWN => chunked */
    MHD_ContentReaderCallback crc;
    void *crc_cls;
    MHD_ContentReaderFreeCallback crfc;

    MHD_UpgradeHandler upgrade;
    void *upgrade_cls;

    kv headers[SHIM_MAX_KV];
    size_t header_count;
};

struct MHD_UpgradeResponseHandle {
    struct MHD_Connection *connection;
};

struct MHD_Connection {
    int fd;
    struct MHD_Daemon *daemon;

    struct sockaddr_storage peer;
    union MHD_ConnectionInfo info;

    kv values[SHIM_MAX_KV];
    size_t value_count;

    struct MHD_Response *queued;
    unsigned int status;

    /* suspend/resume. `suspended` is set under `lock` by the content reader
     * before it returns 0, and cleared by MHD_resume_connection. The reader
     * holds its own stream mutex across the suspend call to serialise against
     * a concurrent send, so this must not block waiting for anything else. */
    pthread_mutex_t lock;
    pthread_cond_t resume_cv;
    bool suspended;

    void *socket_context;
    struct MHD_UpgradeResponseHandle urh;

    struct MHD_Connection *next; /* daemon's live list */
};

struct MHD_Daemon {
    int listen_fd;
    uint16_t port;
    pthread_t acceptor;
    bool acceptor_running;

    MHD_AccessHandlerCallback handler;
    void *handler_cls;
    MHD_RequestCompletedCallback notify_completed;
    void *notify_completed_cls;
    MHD_NotifyConnectionCallback notify_connection;
    void *notify_connection_cls;

    volatile bool stopping;
    union MHD_DaemonInfo info;

    pthread_mutex_t lock;
    struct MHD_Connection *connections;
};

/* Release a connection. Declared here because the upgrade path calls it from
 * MHD_upgrade_action, well before the definition. */
static void connection_finish(struct MHD_Connection *conn, void *con_cls);

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static char *dup_range(const char *start, size_t len) {
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static bool kv_add(kv *table, size_t *count, enum MHD_ValueKind kind, char *key,
                   char *value) {
    if (*count >= SHIM_MAX_KV) {
        free(key);
        free(value);
        return false;
    }
    table[*count].kind = kind;
    table[*count].key = key;
    table[*count].value = value;
    (*count)++;
    return true;
}

static void kv_clear(kv *table, size_t *count) {
    for (size_t i = 0; i < *count; i++) {
        free(table[i].key);
        free(table[i].value);
    }
    *count = 0;
}

/* Case-insensitive compare; HTTP header names are not case sensitive. */
static int ci_equal(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a - 'A' + 'a' : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b - 'A' + 'a' : *b;
        if (ca != cb) return 0;
    }
    return *a == *b;
}

/*
 * Percent-decode in place, `+` meaning space.
 *
 * MHD hands the handler decoded query arguments, and xrpc_server.c relies on
 * that: an `at://` URI or a DID in a query string arrives percent-encoded, and
 * a handler comparing it raw would never match.
 */
static void url_decode(char *s) {
    char *out = s;
    for (char *p = s; *p; p++) {
        if (*p == '%' && p[1] && p[2]) {
            int hi = p[1], lo = p[2];
            hi = (hi >= '0' && hi <= '9')   ? hi - '0'
                 : (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10
                 : (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10
                                            : -1;
            lo = (lo >= '0' && lo <= '9')   ? lo - '0'
                 : (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10
                 : (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10
                                            : -1;
            if (hi >= 0 && lo >= 0) {
                *out++ = (char)((hi << 4) | lo);
                p += 2;
                continue;
            }
        }
        *out++ = (*p == '+') ? ' ' : *p;
    }
    *out = '\0';
}

static bool write_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        p += (size_t)n;
        len -= (size_t)n;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Responses                                                           */
/* ------------------------------------------------------------------ */

static struct MHD_Response *response_new(void) {
    struct MHD_Response *r =
        (struct MHD_Response *)calloc(1, sizeof(struct MHD_Response));
    if (r) r->refcount = 1;
    return r;
}

static void response_unref(struct MHD_Response *r) {
    if (!r) return;
    if (--r->refcount > 0) return;
    if (r->kind == RESP_BUFFER && r->mem_mode != MHD_RESPMEM_PERSISTENT)
        free(r->body);
    if (r->kind == RESP_CALLBACK && r->crfc) r->crfc(r->crc_cls);
    kv_clear(r->headers, &r->header_count);
    free(r);
}

struct MHD_Response *
MHD_create_response_from_buffer(size_t size, void *buffer,
                                enum MHD_ResponseMemoryMode mode) {
    struct MHD_Response *r = response_new();
    if (!r) return NULL;
    r->kind = RESP_BUFFER;
    r->body_len = size;
    r->mem_mode = mode;
    if (mode == MHD_RESPMEM_MUST_COPY) {
        r->body = (char *)malloc(size ? size : 1);
        if (!r->body) {
            free(r);
            return NULL;
        }
        memcpy(r->body, buffer, size);
    } else {
        /* PERSISTENT borrows the caller's buffer; MUST_FREE takes it over. */
        r->body = (char *)buffer;
    }
    return r;
}

struct MHD_Response *
MHD_create_response_from_callback(uint64_t size, size_t block_size,
                                  MHD_ContentReaderCallback crc, void *crc_cls,
                                  MHD_ContentReaderFreeCallback crfc) {
    (void)block_size;
    struct MHD_Response *r = response_new();
    if (!r) return NULL;
    r->kind = RESP_CALLBACK;
    r->total_size = size;
    r->crc = crc;
    r->crc_cls = crc_cls;
    r->crfc = crfc;
    return r;
}

struct MHD_Response *MHD_create_response_for_upgrade(MHD_UpgradeHandler uh,
                                                     void *uh_cls) {
    struct MHD_Response *r = response_new();
    if (!r) return NULL;
    r->kind = RESP_UPGRADE;
    r->upgrade = uh;
    r->upgrade_cls = uh_cls;
    return r;
}

enum MHD_Result MHD_add_response_header(struct MHD_Response *response,
                                        const char *header,
                                        const char *content) {
    if (!response || !header || !content) return MHD_NO;
    char *k = dup_range(header, strlen(header));
    char *v = dup_range(content, strlen(content));
    if (!k || !v) {
        free(k);
        free(v);
        return MHD_NO;
    }
    return kv_add(response->headers, &response->header_count,
                  MHD_RESPONSE_HEADER_KIND, k, v)
               ? MHD_YES
               : MHD_NO;
}

void MHD_destroy_response(struct MHD_Response *response) {
    response_unref(response);
}

enum MHD_Result MHD_queue_response(struct MHD_Connection *connection,
                                   unsigned int status_code,
                                   struct MHD_Response *response) {
    if (!connection || !response) return MHD_NO;
    if (connection->queued) response_unref(connection->queued);
    response->refcount++; /* the connection's reference */
    connection->queued = response;
    connection->status = status_code;
    return MHD_YES;
}

/* ------------------------------------------------------------------ */
/* Connection values                                                   */
/* ------------------------------------------------------------------ */

const char *MHD_lookup_connection_value(struct MHD_Connection *connection,
                                        enum MHD_ValueKind kind,
                                        const char *key) {
    if (!connection || !key) return NULL;
    for (size_t i = 0; i < connection->value_count; i++) {
        if (connection->values[i].kind != kind) continue;
        /* Header names are case-insensitive; query argument names are not. */
        bool match = (kind == MHD_HEADER_KIND)
                         ? ci_equal(connection->values[i].key, key) != 0
                         : strcmp(connection->values[i].key, key) == 0;
        if (match) return connection->values[i].value;
    }
    return NULL;
}

int MHD_get_connection_values(struct MHD_Connection *connection,
                              enum MHD_ValueKind kind,
                              MHD_KeyValueIterator iterator,
                              void *iterator_cls) {
    if (!connection) return 0;
    int n = 0;
    for (size_t i = 0; i < connection->value_count; i++) {
        if (connection->values[i].kind != kind) continue;
        n++;
        if (iterator && iterator(iterator_cls, kind, connection->values[i].key,
                                 connection->values[i].value) == MHD_NO)
            break;
    }
    return n;
}

const union MHD_ConnectionInfo *
MHD_get_connection_info(struct MHD_Connection *connection,
                        enum MHD_ConnectionInfoType info_type, ...) {
    if (!connection || info_type != MHD_CONNECTION_INFO_CLIENT_ADDRESS)
        return NULL;
    connection->info.client_addr = (struct sockaddr *)&connection->peer;
    return &connection->info;
}

const union MHD_DaemonInfo *MHD_get_daemon_info(struct MHD_Daemon *daemon,
                                                enum MHD_DaemonInfoType type,
                                                ...) {
    if (!daemon || type != MHD_DAEMON_INFO_BIND_PORT) return NULL;
    daemon->info.port = daemon->port;
    return &daemon->info;
}

/* ------------------------------------------------------------------ */
/* Suspend / resume                                                    */
/* ------------------------------------------------------------------ */

void MHD_suspend_connection(struct MHD_Connection *connection) {
    if (!connection) return;
    pthread_mutex_lock(&connection->lock);
    connection->suspended = true;
    pthread_mutex_unlock(&connection->lock);
}

void MHD_resume_connection(struct MHD_Connection *connection) {
    if (!connection) return;
    pthread_mutex_lock(&connection->lock);
    connection->suspended = false;
    pthread_cond_signal(&connection->resume_cv);
    pthread_mutex_unlock(&connection->lock);
}

enum MHD_Result MHD_upgrade_action(struct MHD_UpgradeResponseHandle *urh,
                                   enum MHD_UpgradeAction action, ...) {
    if (!urh || !urh->connection) return MHD_NO;
    if (action != MHD_UPGRADE_ACTION_CLOSE) return MHD_YES;
    /*
     * The upgrade handler was given `&conn->urh` and keeps it for as long as
     * the upgraded connection lives — typically handing it to a WebSocket
     * worker thread that outlives the request. So the connection cannot be
     * released when its own thread returns, the way a normal one is; it is
     * released here, when the owner says it is done.
     */
    struct MHD_Connection *conn = urh->connection;
    urh->connection = NULL;
    int fd = conn->fd;
    conn->fd = -1;
    if (fd >= 0) close(fd);
    connection_finish(conn, NULL);
    return MHD_YES;
}

/* ------------------------------------------------------------------ */
/* Request parsing                                                     */
/* ------------------------------------------------------------------ */

/* Split `a=1&b=2` into decoded MHD_GET_ARGUMENT_KIND values. */
static void parse_query(struct MHD_Connection *conn, char *query) {
    char *p = query;
    while (p && *p) {
        char *amp = strchr(p, '&');
        if (amp) *amp = '\0';
        char *eq = strchr(p, '=');
        char *k, *v;
        if (eq) {
            *eq = '\0';
            k = dup_range(p, strlen(p));
            v = dup_range(eq + 1, strlen(eq + 1));
        } else {
            /* A bare `?flag` is a key with an empty value, as in MHD. */
            k = dup_range(p, strlen(p));
            v = dup_range("", 0);
        }
        if (k && v) {
            url_decode(k);
            url_decode(v);
            kv_add(conn->values, &conn->value_count, MHD_GET_ARGUMENT_KIND, k,
                   v);
        } else {
            free(k);
            free(v);
        }
        if (!amp) break;
        p = amp + 1;
    }
}

/*
 * Read until the end of the header block.
 *
 * Returns the total bytes in `buf`, with `*header_len` set to the offset just
 * past the blank line — anything beyond that is body (or, for an upgrade,
 * data the client pipelined) and must be preserved rather than re-read.
 */
static ssize_t read_headers(int fd, char *buf, size_t cap, size_t *header_len) {
    size_t have = 0;
    while (have < cap) {
        char *end = NULL;
        if (have >= 4) {
            buf[have] = '\0';
            end = strstr(buf, "\r\n\r\n");
        }
        if (end) {
            *header_len = (size_t)(end - buf) + 4;
            return (ssize_t)have;
        }
        ssize_t n = recv(fd, buf + have, cap - have - 1, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        have += (size_t)n;
    }
    return -1; /* headers larger than the cap */
}

/* Parse the request line and headers already in `buf`. */
static bool parse_request(struct MHD_Connection *conn, char *buf, char **method,
                          char **url, char **version, char **query) {
    char *line_end = strstr(buf, "\r\n");
    if (!line_end) return false;
    *line_end = '\0';

    char *sp1 = strchr(buf, ' ');
    if (!sp1) return false;
    *sp1 = '\0';
    char *sp2 = strchr(sp1 + 1, ' ');
    if (!sp2) return false;
    *sp2 = '\0';

    *method = buf;
    *url = sp1 + 1;
    *version = sp2 + 1;

    char *q = strchr(*url, '?');
    if (q) {
        *q = '\0';
        *query = q + 1;
    } else {
        *query = NULL;
    }

    char *p = line_end + 2;
    while (*p && strncmp(p, "\r\n", 2) != 0) {
        char *eol = strstr(p, "\r\n");
        if (!eol) break;
        *eol = '\0';
        char *colon = strchr(p, ':');
        if (colon) {
            *colon = '\0';
            char *value = colon + 1;
            while (*value == ' ' || *value == '\t') value++;
            char *k = dup_range(p, strlen(p));
            char *v = dup_range(value, strlen(value));
            if (k && v) {
                kv_add(conn->values, &conn->value_count, MHD_HEADER_KIND, k, v);
            } else {
                free(k);
                free(v);
            }
        }
        p = eol + 2;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Response writing                                                    */
/* ------------------------------------------------------------------ */

static const char *status_text(unsigned int code) {
    switch (code) {
        case 101:
            return "Switching Protocols";
        case 200:
            return "OK";
        case 201:
            return "Created";
        case 204:
            return "No Content";
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 409:
            return "Conflict";
        case 413:
            return "Content Too Large";
        case 429:
            return "Too Many Requests";
        case 500:
            return "Internal Server Error";
        case 501:
            return "Not Implemented";
        default:
            return "OK";
    }
}

static bool send_status_and_headers(struct MHD_Connection *conn,
                                    struct MHD_Response *r, const char *extra) {
    char head[2048];
    int n = snprintf(head, sizeof(head), "HTTP/1.1 %u %s\r\n", conn->status,
                     status_text(conn->status));
    if (n < 0 || (size_t)n >= sizeof(head)) return false;
    if (!write_all(conn->fd, head, (size_t)n)) return false;

    for (size_t i = 0; i < r->header_count; i++) {
        n = snprintf(head, sizeof(head), "%s: %s\r\n", r->headers[i].key,
                     r->headers[i].value);
        if (n < 0 || (size_t)n >= sizeof(head)) return false;
        if (!write_all(conn->fd, head, (size_t)n)) return false;
    }
    if (extra && !write_all(conn->fd, extra, strlen(extra))) return false;
    return write_all(conn->fd, "\r\n", 2);
}

/*
 * Stream a callback response as HTTP/1.1 chunked.
 *
 * A reader returning 0 means "nothing now": MHD would drop the connection out
 * of its poll set until resumed, and here the thread waits on the condition
 * variable the resume signals. Returning MHD_CONTENT_READER_END_OF_STREAM ends
 * the body.
 */
static void write_callback_body(struct MHD_Connection *conn,
                                struct MHD_Response *r) {
    char buf[SHIM_BODY_CHUNK];
    uint64_t pos = 0;
    for (;;) {
        ssize_t n = r->crc(r->crc_cls, pos, buf, sizeof(buf));
        if (n == MHD_CONTENT_READER_END_OF_STREAM) break;
        if (n < 0) break;
        if (n == 0) {
            pthread_mutex_lock(&conn->lock);
            while (conn->suspended && !conn->daemon->stopping)
                pthread_cond_wait(&conn->resume_cv, &conn->lock);
            bool stopping = conn->daemon->stopping;
            pthread_mutex_unlock(&conn->lock);
            if (stopping) break;
            continue;
        }
        char size_line[32];
        int m = snprintf(size_line, sizeof(size_line), "%zx\r\n", (size_t)n);
        if (m < 0 || !write_all(conn->fd, size_line, (size_t)m)) break;
        if (!write_all(conn->fd, buf, (size_t)n)) break;
        if (!write_all(conn->fd, "\r\n", 2)) break;
        pos += (uint64_t)n;
    }
    write_all(conn->fd, "0\r\n\r\n", 5);
}

/* ------------------------------------------------------------------ */
/* Connection thread                                                   */
/* ------------------------------------------------------------------ */

static void connection_unlink(struct MHD_Daemon *d, struct MHD_Connection *c) {
    pthread_mutex_lock(&d->lock);
    struct MHD_Connection **pp = &d->connections;
    while (*pp) {
        if (*pp == c) {
            *pp = c->next;
            break;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&d->lock);
}

static void *connection_main(void *arg) {
    struct MHD_Connection *conn = (struct MHD_Connection *)arg;
    struct MHD_Daemon *d = conn->daemon;
    char *buf = (char *)malloc(SHIM_HEADER_MAX);
    void *con_cls = NULL;
    bool upgraded = false;

    if (!buf) goto done;

    size_t header_len = 0;
    ssize_t total = read_headers(conn->fd, buf, SHIM_HEADER_MAX, &header_len);
    if (total < 0) goto done;

    char *method = NULL, *url = NULL, *version = NULL, *query = NULL;
    if (!parse_request(conn, buf, &method, &url, &version, &query)) goto done;
    if (query) parse_query(conn, query);
    url_decode(url);

    if (d->notify_connection)
        d->notify_connection(d->notify_connection_cls, conn,
                             &conn->socket_context,
                             MHD_CONNECTION_NOTIFY_STARTED);

    /*
     * The handler contract: first call with no body to let it set up
     * `con_cls`, then a call per body chunk, then a final call with
     * `upload_data_size == 0` at which point it queues the response.
     */
    size_t zero = 0;
    if (d->handler(d->handler_cls, conn, url, method, version, NULL, &zero,
                   &con_cls) == MHD_NO)
        goto done;

    const char *cl =
        MHD_lookup_connection_value(conn, MHD_HEADER_KIND, "Content-Length");
    long long content_length = cl ? strtoll(cl, NULL, 10) : 0;
    if (content_length < 0) content_length = 0;

    /* Body bytes already sitting in the header buffer. */
    size_t body_have = (size_t)total - header_len;
    char *body_start = buf + header_len;
    long long remaining = content_length;

    while (remaining > 0) {
        if (body_have == 0) {
            ssize_t n = recv(conn->fd, buf, SHIM_HEADER_MAX, 0);
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                goto done;
            }
            body_have = (size_t)n;
            body_start = buf;
        }
        size_t take = body_have;
        if ((long long)take > remaining) take = (size_t)remaining;
        size_t given = take;
        if (d->handler(d->handler_cls, conn, url, method, version, body_start,
                       &given, &con_cls) == MHD_NO)
            goto done;
        /* The handler sets `given` to 0 once it has consumed the chunk. */
        size_t consumed = take - given;
        if (consumed == 0) consumed = take;
        body_start += consumed;
        body_have -= consumed;
        remaining -= (long long)consumed;
    }

    zero = 0;
    if (d->handler(d->handler_cls, conn, url, method, version, NULL, &zero,
                   &con_cls) == MHD_NO)
        goto done;

    struct MHD_Response *r = conn->queued;
    if (!r) goto done;

    if (r->kind == RESP_UPGRADE) {
        /*
         * Hand the socket to the upgrade handler and stop managing it here.
         * Anything the client pipelined past the headers goes with it as
         * `extra_in`: for a WebSocket that can be the first frame, and losing
         * it desynchronises the stream.
         */
        if (!send_status_and_headers(conn, r, NULL)) goto done;
        conn->urh.connection = conn;
        upgraded = true;
        size_t extra = (size_t)total - header_len;
        r->upgrade(r->upgrade_cls, conn, con_cls,
                   extra ? buf + header_len : NULL, extra, conn->fd,
                   &conn->urh);
    } else if (r->kind == RESP_CALLBACK) {
        if (r->total_size == MHD_SIZE_UNKNOWN) {
            send_status_and_headers(conn, r,
                                    "Transfer-Encoding: chunked\r\n"
                                    "Connection: close\r\n");
            write_callback_body(conn, r);
        } else {
            char extra[64];
            snprintf(extra, sizeof(extra),
                     "Content-Length: %llu\r\nConnection: close\r\n",
                     (unsigned long long)r->total_size);
            send_status_and_headers(conn, r, extra);
            char chunk[SHIM_BODY_CHUNK];
            uint64_t pos = 0;
            while (pos < r->total_size) {
                ssize_t n = r->crc(r->crc_cls, pos, chunk, sizeof(chunk));
                if (n <= 0) break;
                if (!write_all(conn->fd, chunk, (size_t)n)) break;
                pos += (uint64_t)n;
            }
        }
    } else {
        char extra[64];
        snprintf(extra, sizeof(extra),
                 "Content-Length: %zu\r\nConnection: close\r\n", r->body_len);
        if (send_status_and_headers(conn, r, extra) && r->body_len)
            write_all(conn->fd, r->body, r->body_len);
    }

done:
    free(buf);
    /*
     * An upgraded connection is not ours to release: its handler holds
     * `&conn->urh` and the socket, and both have to stay valid until it calls
     * MHD_upgrade_action(CLOSE), which finishes the job. Unlink it so a
     * concurrent MHD_stop_daemon does not wait on it, but leave it allocated.
     */
    connection_unlink(d, conn);
    if (upgraded) return NULL;
    connection_finish(conn, con_cls);
    return NULL;
}

/* Release a connection: notify, drop the queued response, close and free. */
static void connection_finish(struct MHD_Connection *conn, void *con_cls) {
    struct MHD_Daemon *d = conn->daemon;
    if (d->notify_completed)
        d->notify_completed(d->notify_completed_cls, conn, &con_cls,
                            MHD_REQUEST_TERMINATED_COMPLETED_OK);
    if (d->notify_connection)
        d->notify_connection(d->notify_connection_cls, conn,
                             &conn->socket_context,
                             MHD_CONNECTION_NOTIFY_CLOSED);
    if (conn->queued) response_unref(conn->queued);
    kv_clear(conn->values, &conn->value_count);
    if (conn->fd >= 0) close(conn->fd);
    pthread_cond_destroy(&conn->resume_cv);
    pthread_mutex_destroy(&conn->lock);
    free(conn);
}

/* ------------------------------------------------------------------ */
/* Daemon                                                              */
/* ------------------------------------------------------------------ */

static void *acceptor_main(void *arg) {
    struct MHD_Daemon *d = (struct MHD_Daemon *)arg;
    while (!d->stopping) {
        struct sockaddr_storage peer;
        socklen_t plen = sizeof(peer);
        int fd = accept(d->listen_fd, (struct sockaddr *)&peer, &plen);
        if (fd < 0) {
            if (errno == EINTR) continue;
            break; /* listener closed by stop */
        }
        if (d->stopping) {
            close(fd);
            break;
        }
        struct MHD_Connection *conn =
            (struct MHD_Connection *)calloc(1, sizeof(struct MHD_Connection));
        if (!conn) {
            close(fd);
            continue;
        }
        conn->fd = fd;
        conn->daemon = d;
        memcpy(&conn->peer, &peer, sizeof(peer));
        pthread_mutex_init(&conn->lock, NULL);
        pthread_cond_init(&conn->resume_cv, NULL);

        pthread_mutex_lock(&d->lock);
        conn->next = d->connections;
        d->connections = conn;
        pthread_mutex_unlock(&d->lock);

        pthread_t tid;
        if (pthread_create(&tid, NULL, connection_main, conn) != 0) {
            connection_unlink(d, conn);
            close(fd);
            pthread_cond_destroy(&conn->resume_cv);
            pthread_mutex_destroy(&conn->lock);
            free(conn);
            continue;
        }
        pthread_detach(tid);
    }
    return NULL;
}

struct MHD_Daemon *MHD_start_daemon(unsigned int flags, uint16_t port,
                                    void *apc, void *apc_cls,
                                    MHD_AccessHandlerCallback dh, void *dh_cls,
                                    ...) {
    (void)flags;
    (void)apc;
    (void)apc_cls;
    if (!dh) return NULL;

    struct MHD_Daemon *d =
        (struct MHD_Daemon *)calloc(1, sizeof(struct MHD_Daemon));
    if (!d) return NULL;
    d->handler = dh;
    d->handler_cls = dh_cls;
    d->listen_fd = -1;
    pthread_mutex_init(&d->lock, NULL);

    /* The option list mirrors MHD's varargs form: each option is followed by
     * the arguments that option takes. Only the three xrpc_server.c passes are
     * recognised; the rest are consumed as two pointers, which is their shape
     * in every case it uses. */
    va_list ap;
    va_start(ap, dh_cls);
    for (;;) {
        int opt = va_arg(ap, int);
        if (opt == MHD_OPTION_END) break;
        void *a = va_arg(ap, void *);
        void *b = va_arg(ap, void *);
        switch (opt) {
            case MHD_OPTION_NOTIFY_COMPLETED:
                d->notify_completed = (MHD_RequestCompletedCallback)a;
                d->notify_completed_cls = b;
                break;
            case MHD_OPTION_NOTIFY_CONNECTION:
                d->notify_connection = (MHD_NotifyConnectionCallback)a;
                d->notify_connection_cls = b;
                break;
            default:
                break; /* EXTERNAL_LOGGER and friends */
        }
    }
    va_end(ap);

    d->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (d->listen_fd < 0) goto fail;
    int one = 1;
    setsockopt(d->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(d->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        goto fail;
    if (listen(d->listen_fd, 16) != 0) goto fail;

    /* Port 0 means "any free port", and the caller needs to be told which one
     * it got — MHD_get_daemon_info(BIND_PORT) is how the tests find it. */
    socklen_t alen = sizeof(addr);
    if (getsockname(d->listen_fd, (struct sockaddr *)&addr, &alen) == 0)
        d->port = ntohs(addr.sin_port);
    else
        d->port = port;

    if (pthread_create(&d->acceptor, NULL, acceptor_main, d) != 0) goto fail;
    d->acceptor_running = true;
    return d;

fail:
    if (d->listen_fd >= 0) close(d->listen_fd);
    pthread_mutex_destroy(&d->lock);
    free(d);
    return NULL;
}

void MHD_stop_daemon(struct MHD_Daemon *daemon) {
    if (!daemon) return;
    daemon->stopping = true;

    /* Closing the listener breaks the acceptor out of accept(). */
    if (daemon->listen_fd >= 0) {
        close(daemon->listen_fd);
        daemon->listen_fd = -1;
    }
    if (daemon->acceptor_running) pthread_join(daemon->acceptor, NULL);

    /* Wake anything parked in a suspended content reader so its thread can
     * see `stopping` and unwind, rather than waiting for a resume that is
     * never coming. */
    pthread_mutex_lock(&daemon->lock);
    for (struct MHD_Connection *c = daemon->connections; c; c = c->next) {
        pthread_mutex_lock(&c->lock);
        c->suspended = false;
        pthread_cond_signal(&c->resume_cv);
        pthread_mutex_unlock(&c->lock);
    }
    pthread_mutex_unlock(&daemon->lock);

    /* Connection threads are detached and free themselves; give them a moment
     * to drain before the daemon goes away under them. nanosleep rather than
     * usleep because newlib does not declare the latter for every target. */
    for (int i = 0; i < 200; i++) {
        pthread_mutex_lock(&daemon->lock);
        bool empty = daemon->connections == NULL;
        pthread_mutex_unlock(&daemon->lock);
        if (empty) break;
        struct timespec ts = {0, 10 * 1000 * 1000};
        nanosleep(&ts, NULL);
    }
    pthread_mutex_destroy(&daemon->lock);
    free(daemon);
}
