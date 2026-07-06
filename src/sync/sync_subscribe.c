#include "wolfram/sync_subscribe.h"
#include "wolfram/websocket.h"

#include <cbor.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WF_SUBSCRIBE_DEFAULT_SERVICE "wss://bsky.network"
#define WF_SUBSCRIBE_URL_MAX 4096

struct wf_subscribe_handle {
    wf_subscribe_options opts;
    wf_websocket *socket;
    char *service_copy;
    int64_t cursor;
    uint32_t retry_delay_ms;
    volatile int stopped;
};

static char *wf_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *c = malloc(n);
    if (c) memcpy(c, s, n);
    return c;
}

static uint64_t wf_now_ms(void) {
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static cbor_item_t *map_find(cbor_item_t *map, const char *key) {
    if (!cbor_isa_map(map)) return NULL;
    size_t key_len = strlen(key);
    struct cbor_pair *pairs = cbor_map_handle(map);
    size_t count = cbor_map_size(map);
    for (size_t i = 0; i < count; i++) {
        if (cbor_isa_string(pairs[i].key) &&
            cbor_string_length(pairs[i].key) == key_len &&
            memcmp(cbor_string_handle(pairs[i].key), key, key_len) == 0)
            return pairs[i].value;
    }
    return NULL;
}

static int item_int(cbor_item_t *item, int64_t *out) {
    if (cbor_typeof(item) == CBOR_TYPE_UINT) {
        *out = (int64_t)cbor_get_int(item);
        return 1;
    }
    if (cbor_typeof(item) == CBOR_TYPE_NEGINT) {
        *out = -1 - (int64_t)cbor_get_int(item);
        return 1;
    }
    return 0;
}

static const char *item_string(cbor_item_t *item, size_t *len) {
    if (!cbor_isa_string(item)) return NULL;
    *len = cbor_string_length(item);
    return (const char *)cbor_string_handle(item);
}

static unsigned char *copy_bytes(const unsigned char *data, size_t len) {
    unsigned char *c = malloc(len);
    if (c) memcpy(c, data, len);
    return c;
}

static int parse_cid_link(cbor_item_t *item, wf_cid *out) {
    memset(out, 0, sizeof(*out));
    if (cbor_typeof(item) == CBOR_TYPE_TAG && cbor_tag_value(item) == 42) {
        cbor_item_t *tagged = cbor_tag_item(item);
        if (cbor_isa_bytestring(tagged)) {
            size_t len = cbor_bytestring_length(tagged);
            const unsigned char *data = cbor_bytestring_handle(tagged);
            while (len > 1 && data[0] == 0x00) { data++; len--; }
            if (len <= sizeof(out->bytes)) {
                memcpy(out->bytes, data, len);
                out->len = len;
                return 1;
            }
        }
    }
    return 0;
}

/* ── event cleanup ── */

static void event_free(wf_subscribe_event *ev) {
    switch (ev->type) {
    case WF_SUBSCRIBE_EVENT_COMMIT:
        free(ev->data.commit.blocks);
        if (ev->data.commit.ops) {
            for (size_t i = 0; i < ev->data.commit.ops_count; i++)
                free(ev->data.commit.ops[i].path);
            free(ev->data.commit.ops);
        }
        break;
    case WF_SUBSCRIBE_EVENT_SYNC:
        free(ev->data.sync.blocks);
        break;
    case WF_SUBSCRIBE_EVENT_ERROR:
        free(ev->data.error.error);
        free(ev->data.error.message);
        break;
    default:
        break;
    }
    memset(ev, 0, sizeof(*ev));
}

/* ── URL builder ── */

static wf_status build_url(const char *service, int64_t cursor, char **out_url) {
    if (!out_url) return WF_ERR_INVALID_ARG;
    *out_url = NULL;
    const char *svc = service ? service : WF_SUBSCRIBE_DEFAULT_SERVICE;
    size_t slen = strlen(svc);
    while (slen > 0 && svc[slen - 1] == '/') slen--;

    char buf[WF_SUBSCRIBE_URL_MAX];
    int n;
    if (cursor > 0) {
        n = snprintf(buf, sizeof(buf), "%.*s/xrpc/com.atproto.sync.subscribeRepos?cursor=%lld",
                     (int)slen, svc, (long long)cursor);
    } else {
        n = snprintf(buf, sizeof(buf), "%.*s/xrpc/com.atproto.sync.subscribeRepos",
                     (int)slen, svc);
    }
    if (n < 0 || (size_t)n >= sizeof(buf)) return WF_ERR_INVALID_ARG;
    *out_url = wf_strdup(buf);
    return *out_url ? WF_OK : WF_ERR_ALLOC;
}

/* ── event parsers ── */

static int parse_repo_op(cbor_item_t *item, wf_subscribe_repo_op *op) {
    memset(op, 0, sizeof(*op));
    if (!cbor_isa_map(item)) return 0;

    cbor_item_t *action = map_find(item, "action");
    size_t alen = 0;
    const char *astr = action ? item_string(action, &alen) : NULL;
    if (!astr || alen == 0 || alen >= sizeof(op->action)) return 0;
    memcpy(op->action, astr, alen);
    op->action[alen] = '\0';

    cbor_item_t *path = map_find(item, "path");
    size_t plen = 0;
    const char *pstr = path ? item_string(path, &plen) : NULL;
    if (!pstr) return 0;
    op->path = malloc(plen + 1);
    if (!op->path) return 0;
    memcpy(op->path, pstr, plen);
    op->path[plen] = '\0';

    cbor_item_t *cid = map_find(item, "cid");
    if (cid && !cbor_is_null(cid) && !cbor_is_undef(cid))
        op->has_cid = parse_cid_link(cid, &op->cid);

    cbor_item_t *prev = map_find(item, "prev");
    if (prev && !cbor_is_null(prev) && !cbor_is_undef(prev))
        op->has_prev = parse_cid_link(prev, &op->prev);

    return 1;
}

static wf_status parse_commit(cbor_item_t *body, wf_subscribe_event *ev) {
    wf_subscribe_commit *c = &ev->data.commit;
    ev->type = WF_SUBSCRIBE_EVENT_COMMIT;

    cbor_item_t *seq = map_find(body, "seq");
    if (!seq || !item_int(seq, &c->seq)) return WF_ERR_PARSE;
    ev->seq = c->seq;

    cbor_item_t *repo = map_find(body, "repo");
    size_t rlen = 0;
    const char *rstr = repo ? item_string(repo, &rlen) : NULL;
    if (!rstr || rlen >= sizeof(c->did)) return WF_ERR_PARSE;
    memcpy(c->did, rstr, rlen);
    c->did[rlen] = '\0';

    cbor_item_t *commit = map_find(body, "commit");
    if (!commit || !parse_cid_link(commit, &c->commit_cid)) return WF_ERR_PARSE;

    cbor_item_t *rev = map_find(body, "rev");
    size_t revlen = 0;
    const char *revstr = rev ? item_string(rev, &revlen) : NULL;
    if (!revstr || revlen >= sizeof(c->rev)) return WF_ERR_PARSE;
    memcpy(c->rev, revstr, revlen);
    c->rev[revlen] = '\0';

    cbor_item_t *since = map_find(body, "since");
    if (since && cbor_isa_string(since)) {
        size_t slen = 0;
        const char *sstr = item_string(since, &slen);
        if (sstr && slen < sizeof(c->since))
            memcpy(c->since, sstr, slen);
    }

    cbor_item_t *blocks = map_find(body, "blocks");
    if (!blocks || !cbor_isa_bytestring(blocks)) return WF_ERR_PARSE;
    c->blocks_len = cbor_bytestring_length(blocks);
    if (c->blocks_len > 0) {
        c->blocks = copy_bytes(cbor_bytestring_handle(blocks), c->blocks_len);
        if (!c->blocks) return WF_ERR_ALLOC;
    }

    cbor_item_t *ops = map_find(body, "ops");
    if (ops && cbor_isa_array(ops)) {
        c->ops_count = cbor_array_size(ops);
        if (c->ops_count > 0) {
            c->ops = calloc(c->ops_count, sizeof(wf_subscribe_repo_op));
            if (!c->ops) return WF_ERR_ALLOC;
            cbor_item_t **items = cbor_array_handle(ops);
            for (size_t i = 0; i < c->ops_count; i++) {
                if (!parse_repo_op(items[i], &c->ops[i])) {
                    for (size_t j = 0; j < i; j++) free(c->ops[j].path);
                    free(c->ops); c->ops = NULL; c->ops_count = 0;
                    return WF_ERR_PARSE;
                }
            }
        }
    }

    cbor_item_t *time = map_find(body, "time");
    size_t tlen = 0;
    const char *tstr = time ? item_string(time, &tlen) : NULL;
    if (!tstr || tlen >= sizeof(c->time)) return WF_ERR_PARSE;
    memcpy(c->time, tstr, tlen);
    c->time[tlen] = '\0';

    return WF_OK;
}

static wf_status parse_sync(cbor_item_t *body, wf_subscribe_event *ev) {
    wf_subscribe_sync *s = &ev->data.sync;
    ev->type = WF_SUBSCRIBE_EVENT_SYNC;

    cbor_item_t *seq = map_find(body, "seq");
    if (!seq || !item_int(seq, &s->seq)) return WF_ERR_PARSE;
    ev->seq = s->seq;

    cbor_item_t *did = map_find(body, "did");
    size_t dlen = 0;
    const char *dstr = did ? item_string(did, &dlen) : NULL;
    if (!dstr || dlen >= sizeof(s->did)) return WF_ERR_PARSE;
    memcpy(s->did, dstr, dlen);
    s->did[dlen] = '\0';

    cbor_item_t *blocks = map_find(body, "blocks");
    if (!blocks || !cbor_isa_bytestring(blocks)) return WF_ERR_PARSE;
    s->blocks_len = cbor_bytestring_length(blocks);
    if (s->blocks_len > 0) {
        s->blocks = copy_bytes(cbor_bytestring_handle(blocks), s->blocks_len);
        if (!s->blocks) return WF_ERR_ALLOC;
    }

    cbor_item_t *rev = map_find(body, "rev");
    size_t revlen = 0;
    const char *revstr = rev ? item_string(rev, &revlen) : NULL;
    if (!revstr || revlen >= sizeof(s->rev)) return WF_ERR_PARSE;
    memcpy(s->rev, revstr, revlen);
    s->rev[revlen] = '\0';

    cbor_item_t *time = map_find(body, "time");
    size_t tlen = 0;
    const char *tstr = time ? item_string(time, &tlen) : NULL;
    if (!tstr || tlen >= sizeof(s->time)) return WF_ERR_PARSE;
    memcpy(s->time, tstr, tlen);
    s->time[tlen] = '\0';

    return WF_OK;
}

static wf_status parse_identity(cbor_item_t *body, wf_subscribe_event *ev) {
    wf_subscribe_identity *id = &ev->data.identity;
    ev->type = WF_SUBSCRIBE_EVENT_IDENTITY;

    cbor_item_t *seq = map_find(body, "seq");
    if (!seq || !item_int(seq, &id->seq)) return WF_ERR_PARSE;
    ev->seq = id->seq;

    cbor_item_t *did = map_find(body, "did");
    size_t dlen = 0;
    const char *dstr = did ? item_string(did, &dlen) : NULL;
    if (!dstr || dlen >= sizeof(id->did)) return WF_ERR_PARSE;
    memcpy(id->did, dstr, dlen);
    id->did[dlen] = '\0';

    cbor_item_t *time = map_find(body, "time");
    size_t tlen = 0;
    const char *tstr = time ? item_string(time, &tlen) : NULL;
    if (!tstr || tlen >= sizeof(id->time)) return WF_ERR_PARSE;
    memcpy(id->time, tstr, tlen);
    id->time[tlen] = '\0';

    cbor_item_t *handle = map_find(body, "handle");
    if (handle && cbor_isa_string(handle)) {
        size_t hlen = 0;
        const char *hstr = item_string(handle, &hlen);
        if (hstr && hlen < sizeof(id->handle)) {
            memcpy(id->handle, hstr, hlen);
            id->handle[hlen] = '\0';
            id->has_handle = 1;
        }
    }

    return WF_OK;
}

static wf_status parse_account(cbor_item_t *body, wf_subscribe_event *ev) {
    wf_subscribe_account *ac = &ev->data.account;
    ev->type = WF_SUBSCRIBE_EVENT_ACCOUNT;

    cbor_item_t *seq = map_find(body, "seq");
    if (!seq || !item_int(seq, &ac->seq)) return WF_ERR_PARSE;
    ev->seq = ac->seq;

    cbor_item_t *did = map_find(body, "did");
    size_t dlen = 0;
    const char *dstr = did ? item_string(did, &dlen) : NULL;
    if (!dstr || dlen >= sizeof(ac->did)) return WF_ERR_PARSE;
    memcpy(ac->did, dstr, dlen);
    ac->did[dlen] = '\0';

    cbor_item_t *time = map_find(body, "time");
    size_t tlen = 0;
    const char *tstr = time ? item_string(time, &tlen) : NULL;
    if (!tstr || tlen >= sizeof(ac->time)) return WF_ERR_PARSE;
    memcpy(ac->time, tstr, tlen);
    ac->time[tlen] = '\0';

    cbor_item_t *active = map_find(body, "active");
    if (active && cbor_is_bool(active)) ac->active = cbor_get_bool(active);

    cbor_item_t *status = map_find(body, "status");
    if (status && cbor_isa_string(status)) {
        size_t slen = 0;
        const char *sstr = item_string(status, &slen);
        if (sstr && slen < sizeof(ac->status)) {
            memcpy(ac->status, sstr, slen);
            ac->status[slen] = '\0';
            ac->has_status = 1;
        }
    }

    return WF_OK;
}

static wf_status parse_info(cbor_item_t *body, wf_subscribe_event *ev) {
    wf_subscribe_info *info = &ev->data.info;
    ev->type = WF_SUBSCRIBE_EVENT_INFO;
    ev->seq = 0;

    cbor_item_t *name = map_find(body, "name");
    size_t nlen = 0;
    const char *nstr = name ? item_string(name, &nlen) : NULL;
    if (!nstr || nlen >= sizeof(info->name)) return WF_ERR_PARSE;
    memcpy(info->name, nstr, nlen);
    info->name[nlen] = '\0';

    cbor_item_t *message = map_find(body, "message");
    if (message && cbor_isa_string(message)) {
        size_t mlen = 0;
        const char *mstr = item_string(message, &mlen);
        if (mstr && mlen < sizeof(info->message)) {
            memcpy(info->message, mstr, mlen);
            info->message[mlen] = '\0';
            info->has_message = 1;
        }
    }

    return WF_OK;
}

static wf_status parse_error_body(cbor_item_t *body, wf_subscribe_event *ev) {
    ev->type = WF_SUBSCRIBE_EVENT_ERROR;
    ev->seq = 0;

    cbor_item_t *err = map_find(body, "error");
    size_t elen = 0;
    const char *estr = err ? item_string(err, &elen) : NULL;
    if (!estr) return WF_ERR_PARSE;

    ev->data.error.error = malloc(elen + 1);
    if (!ev->data.error.error) return WF_ERR_ALLOC;
    memcpy(ev->data.error.error, estr, elen);
    ev->data.error.error[elen] = '\0';

    cbor_item_t *msg = map_find(body, "message");
    if (msg && cbor_isa_string(msg)) {
        size_t mlen = 0;
        const char *mstr = item_string(msg, &mlen);
        if (mstr) {
            ev->data.error.message = malloc(mlen + 1);
            if (!ev->data.error.message) return WF_ERR_ALLOC;
            memcpy(ev->data.error.message, mstr, mlen);
            ev->data.error.message[mlen] = '\0';
        }
    }

    return WF_OK;
}

/* ── handle lifecycle ── */

static void handle_close(wf_subscribe_handle *handle) {
    wf_websocket_free(handle->socket);
    handle->socket = NULL;
}

static wf_status handle_connect(wf_subscribe_handle *handle) {
    char *url = NULL;
    wf_status s = build_url(handle->opts.service, handle->cursor, &url);
    if (s != WF_OK) return s;
    s = wf_websocket_connect(url, &handle->socket);
    free(url);
    return s;
}

static wf_status subscribe_loop(wf_subscribe_handle *handle) {
    uint32_t initial_delay = handle->opts.reconnect_delay_ms > 0
                                 ? (uint32_t)handle->opts.reconnect_delay_ms : 3000;
    uint32_t max_delay = handle->opts.max_retry_seconds > 0
                             ? (uint32_t)handle->opts.max_retry_seconds * 1000 : 64000;

    handle->retry_delay_ms = initial_delay;

    wf_status conn_status = handle_connect(handle);
    if (conn_status != WF_OK) return conn_status;

    while (!handle->stopped) {
        wf_websocket_message msg = {0};
        wf_status s = wf_websocket_receive(handle->socket, &msg);

        if (s == WF_ERR_NETWORK || s == WF_ERR_WOULD_BLOCK) {
            wf_websocket_message_free(&msg);
            handle_close(handle);
            if (handle->opts.on_error)
                handle->opts.on_error(s, "websocket receive failed",
                                     handle->opts.userdata);

            uint64_t now = wf_now_ms();
            uint64_t wait = now + handle->retry_delay_ms;
            while (!handle->stopped) {
                uint64_t remaining = wait > wf_now_ms() ? wait - wf_now_ms() : 0;
                if (remaining == 0) break;
                struct timespec ts = {(long)(remaining / 1000),
                                     (long)(remaining % 1000) * 1000000L};
                nanosleep(&ts, NULL);
            }
            if (handle->stopped) break;

            conn_status = handle_connect(handle);
            if (conn_status != WF_OK) {
                if (handle->retry_delay_ms < max_delay) {
                    uint64_t doubled = (uint64_t)handle->retry_delay_ms * 2;
                    handle->retry_delay_ms = doubled > max_delay
                                                 ? max_delay : (uint32_t)doubled;
                }
                continue;
            }
            handle->retry_delay_ms = initial_delay;
            continue;
        }

        if (s != WF_OK) {
            wf_websocket_message_free(&msg);
            break;
        }

        if (msg.type != WF_WEBSOCKET_BINARY || msg.len == 0) {
            wf_websocket_message_free(&msg);
            continue;
        }

        struct cbor_load_result lr = {0};
        cbor_item_t *header = cbor_load(msg.data, msg.len, &lr);
        if (!header || lr.error.code != CBOR_ERR_NONE || lr.read >= msg.len) {
            cbor_decref(&header);
            wf_websocket_message_free(&msg);
            continue;
        }

        cbor_item_t *body = cbor_load(msg.data + lr.read, msg.len - lr.read, &lr);
        if (!body || lr.error.code != CBOR_ERR_NONE) {
            cbor_decref(&header);
            cbor_decref(&body);
            wf_websocket_message_free(&msg);
            continue;
        }

        int64_t op_val = 0;
        cbor_item_t *op_item = map_find(header, "op");
        int is_error = op_item && item_int(op_item, &op_val) && op_val < 0;

        if (is_error) {
            wf_subscribe_event ev = {0};
            s = parse_error_body(body, &ev);
            cbor_decref(&header);
            cbor_decref(&body);
            wf_websocket_message_free(&msg);

            if (s == WF_OK) {
                if (handle->opts.on_event)
                    handle->opts.on_event(&ev, handle->opts.userdata);
                if (handle->opts.on_error)
                    handle->opts.on_error(WF_ERR_PARSE, ev.data.error.error,
                                         handle->opts.userdata);
                event_free(&ev);
            }
            continue;
        }

        cbor_item_t *t_item = map_find(header, "t");
        size_t t_len = 0;
        const char *t_str = t_item ? item_string(t_item, &t_len) : NULL;

        wf_subscribe_event ev = {0};
        s = WF_ERR_PARSE;

        if (t_str && t_len > 0) {
            if (t_len == 6 && memcmp(t_str, "#commit", 7) == 0)
                s = parse_commit(body, &ev);
            else if (t_len == 4 && memcmp(t_str, "#sync", 5) == 0)
                s = parse_sync(body, &ev);
            else if (t_len == 8 && memcmp(t_str, "#identity", 9) == 0)
                s = parse_identity(body, &ev);
            else if (t_len == 7 && memcmp(t_str, "#account", 8) == 0)
                s = parse_account(body, &ev);
            else if (t_len == 4 && memcmp(t_str, "#info", 5) == 0)
                s = parse_info(body, &ev);
        }

        cbor_decref(&header);
        cbor_decref(&body);
        wf_websocket_message_free(&msg);

        if (s == WF_OK) {
            if (ev.type == WF_SUBSCRIBE_EVENT_COMMIT ||
                ev.type == WF_SUBSCRIBE_EVENT_SYNC ||
                ev.type == WF_SUBSCRIBE_EVENT_IDENTITY ||
                ev.type == WF_SUBSCRIBE_EVENT_ACCOUNT) {
                if (ev.seq > handle->cursor)
                    handle->cursor = ev.seq;
            }

            if (ev.type == WF_SUBSCRIBE_EVENT_INFO &&
                strcmp(ev.data.info.name, "OutdatedCursor") == 0) {
                handle->cursor = 0;
                handle_close(handle);
            }

            if (handle->opts.on_event)
                handle->opts.on_event(&ev, handle->opts.userdata);
            event_free(&ev);
        }
    }

    handle_close(handle);
    return WF_OK;
}

wf_status wf_subscribe_start(const wf_subscribe_options *opts,
                             wf_subscribe_handle **out) {
    if (!opts || !out || !opts->on_event) return WF_ERR_INVALID_ARG;
    if (opts->service && strncmp(opts->service, "ws://", 5) != 0 &&
                         strncmp(opts->service, "wss://", 6) != 0)
        return WF_ERR_INVALID_ARG;
    if (opts->max_retry_seconds < 0 || opts->reconnect_delay_ms < 0)
        return WF_ERR_INVALID_ARG;

    *out = NULL;
    wf_subscribe_handle *handle = calloc(1, sizeof(*handle));
    if (!handle) return WF_ERR_ALLOC;

    handle->opts = *opts;
    if (opts->service) {
        handle->service_copy = wf_strdup(opts->service);
        if (!handle->service_copy) { free(handle); return WF_ERR_ALLOC; }
        handle->opts.service = handle->service_copy;
    }
    handle->cursor = opts->has_cursor ? opts->cursor : 0;

    *out = handle;
    wf_status s = subscribe_loop(handle);

    wf_websocket_free(handle->socket);
    free(handle->service_copy);
    free(handle);
    *out = NULL;
    return s;
}

void wf_subscribe_stop(wf_subscribe_handle *handle) {
    if (!handle) return;
    handle->stopped = 1;
}
