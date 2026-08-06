/*
 * chat_mod_events.c — chat.bsky.moderation.subscribeModEvents: the framed
 * DAG-CBOR moderation event stream, split out of chat_typed.c since it is a
 * WebSocket subscription loop rather than an HTTP request/response wrapper
 * like the rest of that file.
 */

#include "wolfram/chat_typed.h"

#include "wolfram/agent.h"
#include "wolfram/xrpc.h"
#include "wolfram/websocket.h"
#include <cJSON.h>
#include <cbor.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "_internal.h"

/* Local copies of the small string helpers (kept static per TU, matching
 * the rest of the *_typed.c files under src/agent/). */
static char *wf_chat_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

static wf_status wf_chat_set_string(char **dst, const char *src) {
    char *copy = wf_chat_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

/* ── moderation.subscribeModEvents ─────────────────────────────────────── */

static void wf_chat_mod_event_reset(wf_chat_mod_event *e) {
    if (!e) return;
    free(e->type);
    free(e->convo_id);
    free(e->rev);
    free(e->created_at);
    free(e->actor_did);
    free(e->subject_did);
    memset(e, 0, sizeof(*e));
}

wf_status wf_agent_parse_mod_event(const char *json, size_t json_len,
                                   wf_chat_mod_event *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;

    wf_status status = WF_OK;
    cJSON *tp = cJSON_GetObjectItemCaseSensitive(root, "$type");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(root, "convoId");
    cJSON *rev = cJSON_GetObjectItemCaseSensitive(root, "rev");
    cJSON *ca = cJSON_GetObjectItemCaseSensitive(root, "createdAt");
    cJSON *ad = cJSON_GetObjectItemCaseSensitive(root, "actorDid");
    cJSON *sd = cJSON_GetObjectItemCaseSensitive(root, "subjectDid");
    if (cJSON_IsString(tp) && tp->valuestring)
        status = wf_chat_set_string(&out->type, tp->valuestring);
    if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring)
        status = wf_chat_set_string(&out->convo_id, cid->valuestring);
    if (status == WF_OK && cJSON_IsString(rev) && rev->valuestring)
        status = wf_chat_set_string(&out->rev, rev->valuestring);
    if (status == WF_OK && cJSON_IsString(ca) && ca->valuestring)
        status = wf_chat_set_string(&out->created_at, ca->valuestring);
    if (status == WF_OK && cJSON_IsString(ad) && ad->valuestring)
        status = wf_chat_set_string(&out->actor_did, ad->valuestring);
    if (status == WF_OK && cJSON_IsString(sd) && sd->valuestring)
        status = wf_chat_set_string(&out->subject_did, sd->valuestring);

    if (status != WF_OK) wf_chat_mod_event_reset(out);
    cJSON_Delete(root);
    return status;
}

void wf_chat_mod_event_free(wf_chat_mod_event *e) {
    if (!e) return;
    wf_chat_mod_event_reset(e);
}

/* ── framed DAG-CBOR decode (mirrors sync_subscribe.c helper style) ─────── */

static cbor_item_t *wf_chat_cbor_map_find(cbor_item_t *map, const char *key) {
    if (!map || !cbor_isa_map(map)) return NULL;
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

static int wf_chat_cbor_item_int(cbor_item_t *item, int64_t *out) {
    if (!item) return 0;
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

/* Copy a CBOR text string field into an owned C string. Returns WF_OK when the
 * field is absent (leaving *dst untouched), copies it when present, or
 * WF_ERR_ALLOC on failure. */
static wf_status wf_chat_cbor_copy_string(cbor_item_t *map, const char *key,
                                          char **dst) {
    cbor_item_t *item = wf_chat_cbor_map_find(map, key);
    if (!item || !cbor_isa_string(item)) return WF_OK;
    size_t len = cbor_string_length(item);
    char *copy = malloc(len + 1);
    if (!copy) return WF_ERR_ALLOC;
    memcpy(copy, cbor_string_handle(item), len);
    copy[len] = '\0';
    free(*dst);
    *dst = copy;
    return WF_OK;
}

/* Fill a wf_chat_mod_event from a decoded body map. The union tag is supplied
 * separately from the frame header `t` (the wire body has no $type). Only the
 * bounded identifying fields the struct exposes are extracted; unknown / union-
 * specific fields are ignored. */
static wf_status wf_chat_mod_event_from_cbor(cbor_item_t *body,
                                             const char *type, size_t type_len,
                                             wf_chat_mod_event *out) {
    memset(out, 0, sizeof(*out));
    if (!body || !cbor_isa_map(body)) return WF_ERR_PARSE;

    wf_status s = WF_OK;
    if (type && type_len > 0) {
        char *t = malloc(type_len + 1);
        if (!t) return WF_ERR_ALLOC;
        memcpy(t, type, type_len);
        t[type_len] = '\0';
        out->type = t;
    }

    if (s == WF_OK)
        s = wf_chat_cbor_copy_string(body, "convoId", &out->convo_id);
    if (s == WF_OK) s = wf_chat_cbor_copy_string(body, "rev", &out->rev);
    if (s == WF_OK)
        s = wf_chat_cbor_copy_string(body, "createdAt", &out->created_at);
    if (s == WF_OK)
        s = wf_chat_cbor_copy_string(body, "actorDid", &out->actor_did);
    if (s == WF_OK)
        s = wf_chat_cbor_copy_string(body, "subjectDid", &out->subject_did);

    if (s != WF_OK) wf_chat_mod_event_reset(out);
    return s;
}

wf_status wf_chat_mod_frame_parse_cbor(const unsigned char *frame, size_t len,
                                       wf_chat_mod_frame *out) {
    if (!frame || !out || len == 0) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    /* Two independent CBOR items back to back: header map ++ body map. */
    struct cbor_load_result lr = {0};
    cbor_item_t *header = cbor_load(frame, len, &lr);
    if (!header || lr.error.code != CBOR_ERR_NONE || lr.read >= len ||
        !cbor_isa_map(header)) {
        if (header) cbor_decref(&header);
        return WF_ERR_PARSE;
    }
    size_t header_read = lr.read;

    cbor_item_t *body = cbor_load(frame + header_read, len - header_read, &lr);
    if (!body || lr.error.code != CBOR_ERR_NONE) {
        cbor_decref(&header);
        if (body) cbor_decref(&body);
        return WF_ERR_PARSE;
    }

    int64_t op_val = 0;
    cbor_item_t *op_item = wf_chat_cbor_map_find(header, "op");
    int is_error =
        op_item && wf_chat_cbor_item_int(op_item, &op_val) && op_val < 0;

    wf_status s;
    if (is_error) {
        out->is_error = 1;
        s = WF_OK;
        cbor_item_t *err = wf_chat_cbor_map_find(body, "error");
        if (err && cbor_isa_string(err)) {
            s = wf_chat_cbor_copy_string(body, "error", &out->error);
        } else {
            /* An error frame must carry a code. */
            s = WF_ERR_PARSE;
        }
        if (s == WF_OK)
            s = wf_chat_cbor_copy_string(body, "message", &out->message);
    } else {
        cbor_item_t *t_item = wf_chat_cbor_map_find(header, "t");
        size_t t_len = 0;
        const char *t_str = NULL;
        if (t_item && cbor_isa_string(t_item)) {
            t_len = cbor_string_length(t_item);
            t_str = (const char *)cbor_string_handle(t_item);
        }
        s = wf_chat_mod_event_from_cbor(body, t_str, t_len, &out->event);
        if (s == WF_OK) {
            int64_t seq = 0;
            cbor_item_t *seq_item = wf_chat_cbor_map_find(body, "seq");
            if (seq_item && wf_chat_cbor_item_int(seq_item, &seq))
                out->seq = seq;
        }
    }

    cbor_decref(&header);
    cbor_decref(&body);
    if (s != WF_OK) wf_chat_mod_frame_free(out);
    return s;
}

void wf_chat_mod_frame_free(wf_chat_mod_frame *frame) {
    if (!frame) return;
    wf_chat_mod_event_reset(&frame->event);
    free(frame->error);
    free(frame->message);
    memset(frame, 0, sizeof(*frame));
}

/* ── subscribeModEvents URL builder ─────────────────────────────────────── */

wf_status wf_chat_mod_events_build_url(const char *service, const char *cursor,
                                       char **out_url) {
    if (!service || !service[0] || !out_url) return WF_ERR_INVALID_ARG;
    *out_url = NULL;

    const char *scheme = NULL;
    const char *body = NULL;
    if (strncmp(service, "wss://", 6) == 0) {
        scheme = "wss://";
        body = service + 6;
    } else if (strncmp(service, "ws://", 5) == 0) {
        scheme = "ws://";
        body = service + 5;
    } else if (strncmp(service, "https://", 8) == 0) {
        scheme = "wss://";
        body = service + 8;
    } else if (strncmp(service, "http://", 7) == 0) {
        scheme = "ws://";
        body = service + 7;
    } else {
        return WF_ERR_INVALID_ARG;
    }

    const char *path = "/xrpc/" WF_CHAT_MOD_EVENTS_NSID;
    const char *query = strchr(body, '?');
    const char *existing = strstr(body, path);
    size_t body_len = query ? (size_t)(query - body) : strlen(body);
    if (existing == NULL) {
        while (body_len > 0 && body[body_len - 1] == '/') body_len--;
    }
    size_t query_tail_len = query ? strlen(query) : 0;
    size_t path_len = existing ? 0 : strlen(path);

    /* cursor is a lexicon string parameter (a seq number rendered as text). */
    char cursor_part[128] = {0};
    size_t cursor_len = 0;
    if (cursor && cursor[0]) {
        const char *separator = query ? "&cursor=" : "?cursor=";
        int n = snprintf(cursor_part, sizeof(cursor_part), "%s%s", separator,
                         cursor);
        if (n < 0 || (size_t)n >= sizeof(cursor_part))
            return WF_ERR_INVALID_ARG;
        cursor_len = (size_t)n;
    }

    size_t scheme_len = strlen(scheme);
    size_t total =
        scheme_len + body_len + path_len + query_tail_len + cursor_len + 1;
    char *url = malloc(total);
    if (!url) return WF_ERR_ALLOC;

    char *p = url;
    memcpy(p, scheme, scheme_len);
    p += scheme_len;
    memcpy(p, body, body_len);
    p += body_len;
    if (path_len) {
        memcpy(p, path, path_len);
        p += path_len;
    }
    if (query_tail_len) {
        memcpy(p, query, query_tail_len);
        p += query_tail_len;
    }
    if (cursor_len) {
        memcpy(p, cursor_part, cursor_len);
        p += cursor_len;
    }
    *p = '\0';
    *out_url = url;
    return WF_OK;
}

/* ── subscribeModEvents subscription loop ───────────────────────────────── */

#define WF_CHAT_MOD_DEFAULT_RECONNECT_DELAY_MS 3000u
#define WF_CHAT_MOD_MAX_RECONNECT_DELAY_MS 30000u
#define WF_CHAT_MOD_SLEEP_SLICE_MS 100u

struct wf_chat_mod_events_handle {
    wf_chat_mod_events_options opts;
    wf_websocket *socket;
    char *service_copy;
    char *cursor_copy; /* initial cursor string (from opts.cursor) */
    char
        *token_copy; /* optional bearer access token (from opts.access_token) */
    int64_t last_seq; /* highest seq observed on this stream */
    int have_seq;     /* whether last_seq is meaningful */
    uint32_t initial_retry_delay_ms;
    uint32_t retry_delay_ms;
    volatile int stopped;
};

static void wf_chat_mod_sleep_ms(wf_chat_mod_events_handle *h, uint32_t ms) {
    while (h && !h->stopped && ms > 0) {
        uint32_t slice =
            ms > WF_CHAT_MOD_SLEEP_SLICE_MS ? WF_CHAT_MOD_SLEEP_SLICE_MS : ms;
        struct timespec ts;
        ts.tv_sec = (time_t)(slice / 1000u);
        ts.tv_nsec = (long)(slice % 1000u) * 1000000L;
        nanosleep(&ts, NULL);
        if (ms <= slice) break;
        ms -= slice;
    }
}

static wf_status wf_chat_mod_open(wf_chat_mod_events_handle *h) {
    char cursor_buf[32];
    const char *cursor = h->cursor_copy;
    if (h->have_seq) {
        snprintf(cursor_buf, sizeof(cursor_buf), "%lld",
                 (long long)h->last_seq);
        cursor = cursor_buf;
    }
    char *url = NULL;
    wf_status s = wf_chat_mod_events_build_url(h->opts.service, cursor, &url);
    if (s != WF_OK) return s;

    /* subscribeModEvents is a "private endpoint": attach the session's bearer
     * token as an Authorization header on the upgrade request, the same way
     * an authenticated XRPC call would carry it. No token (never logged in)
     * connects unauthenticated, same as before this existed. */
    if (h->token_copy && h->token_copy[0]) {
        char auth_header[1024];
        int n = snprintf(auth_header, sizeof(auth_header),
                         "Authorization: Bearer %s", h->token_copy);
        if (n < 0 || (size_t)n >= sizeof(auth_header)) {
            free(url);
            return WF_ERR_INVALID_ARG;
        }
        const char *headers[] = {auth_header};
        s = wf_websocket_connect_with_headers(url, headers, 1, &h->socket);
    } else {
        s = wf_websocket_connect(url, &h->socket);
    }
    free(url);
    return s;
}

static wf_status wf_chat_mod_reconnect(wf_chat_mod_events_handle *h) {
    wf_websocket_free(h->socket);
    h->socket = NULL;
    if (h->stopped) return WF_OK;

    wf_chat_mod_sleep_ms(h, h->retry_delay_ms);
    if (h->stopped) return WF_OK;

    wf_status s = wf_chat_mod_open(h);
    if (s == WF_OK) {
        h->retry_delay_ms = h->initial_retry_delay_ms;
        return WF_OK;
    }
    if (h->retry_delay_ms < WF_CHAT_MOD_MAX_RECONNECT_DELAY_MS) {
        uint64_t doubled = (uint64_t)h->retry_delay_ms * 2u;
        h->retry_delay_ms = doubled > WF_CHAT_MOD_MAX_RECONNECT_DELAY_MS
                                ? WF_CHAT_MOD_MAX_RECONNECT_DELAY_MS
                                : (uint32_t)doubled;
    }
    return s;
}

static void wf_chat_mod_dispatch(wf_chat_mod_events_handle *h,
                                 wf_chat_mod_frame *frame) {
    if (frame->is_error) {
        /* Server-sent error frame (op == -1), e.g. FutureCursor /
         * ConsumerTooSlow. */
        if (h->opts.on_error)
            h->opts.on_error(WF_ERR_HTTP, frame->error, h->opts.userdata);
        return;
    }
    if (frame->seq > 0) {
        if (!h->have_seq || frame->seq > h->last_seq) {
            h->last_seq = frame->seq;
            h->have_seq = 1;
        }
    }
    if (h->opts.on_event)
        h->opts.on_event(&frame->event, frame->seq, h->opts.userdata);
}

wf_status wf_chat_mod_events_start(const wf_chat_mod_events_options *opts,
                                   wf_chat_mod_events_handle **out) {
    if (!opts || !out || !opts->service || !opts->service[0] || !opts->on_event)
        return WF_ERR_INVALID_ARG;

    *out = NULL;
    wf_chat_mod_events_handle *h = calloc(1, sizeof(*h));
    if (!h) return WF_ERR_ALLOC;

    h->opts = *opts;
    h->initial_retry_delay_ms = opts->reconnect_delay_ms
                                    ? opts->reconnect_delay_ms
                                    : WF_CHAT_MOD_DEFAULT_RECONNECT_DELAY_MS;
    h->retry_delay_ms = h->initial_retry_delay_ms;

    h->service_copy = wf_chat_strdup(opts->service);
    if (!h->service_copy) {
        free(h);
        return WF_ERR_ALLOC;
    }
    h->opts.service = h->service_copy;
    if (opts->cursor && opts->cursor[0]) {
        h->cursor_copy = wf_chat_strdup(opts->cursor);
        if (!h->cursor_copy) {
            free(h->service_copy);
            free(h);
            return WF_ERR_ALLOC;
        }
        h->opts.cursor = h->cursor_copy;
    }
    if (opts->access_token && opts->access_token[0]) {
        h->token_copy = wf_chat_strdup(opts->access_token);
        if (!h->token_copy) {
            free(h->cursor_copy);
            free(h->service_copy);
            free(h);
            return WF_ERR_ALLOC;
        }
        h->opts.access_token = h->token_copy;
    }

    wf_status status = wf_chat_mod_open(h);
    if (status != WF_OK) {
        free(h->token_copy);
        free(h->cursor_copy);
        free(h->service_copy);
        free(h);
        return status;
    }

    *out = h;
    while (!h->stopped) {
        if (!h->socket) {
            status = wf_chat_mod_reconnect(h);
            if (status != WF_OK) {
                if (h->opts.on_error)
                    h->opts.on_error(status, "reconnect failed",
                                     h->opts.userdata);
                continue;
            }
        }

        wf_websocket_message msg = {0};
        status = wf_websocket_receive(h->socket, &msg);

        if (status == WF_ERR_WOULD_BLOCK) {
            wf_websocket_message_free(&msg);
            wf_chat_mod_sleep_ms(h, WF_CHAT_MOD_SLEEP_SLICE_MS);
            continue;
        }
        if (status != WF_OK) {
            wf_websocket_message_free(&msg);
            if (h->opts.on_error)
                h->opts.on_error(status, "websocket receive failed",
                                 h->opts.userdata);
            wf_websocket_free(h->socket);
            h->socket = NULL;
            continue;
        }
        if (msg.type != WF_WEBSOCKET_BINARY || msg.len == 0) {
            wf_websocket_message_free(&msg);
            continue;
        }

        wf_chat_mod_frame frame = {0};
        status = wf_chat_mod_frame_parse_cbor(msg.data, msg.len, &frame);
        wf_websocket_message_free(&msg);
        if (status != WF_OK) {
            if (h->opts.on_error)
                h->opts.on_error(status, "invalid mod-event frame",
                                 h->opts.userdata);
            continue;
        }

        wf_chat_mod_dispatch(h, &frame);
        wf_chat_mod_frame_free(&frame);
    }

    wf_websocket_free(h->socket);
    h->socket = NULL;
    free(h->token_copy);
    free(h->cursor_copy);
    free(h->service_copy);
    free(h);
    *out = NULL;
    return WF_OK;
}

void wf_chat_mod_events_stop(wf_chat_mod_events_handle *handle) {
    if (!handle) return;
    handle->stopped = 1;
}

wf_status wf_agent_chat_subscribe_mod_events_typed(
    wf_agent *agent, const char *cursor, wf_chat_mod_event_cb on_event,
    wf_chat_mod_events_error_cb on_error, void *userdata) {
    if (!agent || !on_event) return WF_ERR_INVALID_ARG;

    /* Resolve the chat-service endpoint exactly as the other chat wrappers do,
     * then reuse its base URL for the WebSocket subscription. */
    if (wf_agent_chat_service_resolve(agent) != WF_OK) return WF_ERR_NOT_FOUND;
    wf_xrpc_client *cc = agent->chat_client;
    if (!cc) return WF_ERR_NOT_FOUND;
    char *base = wf_xrpc_get_base_url(cc);
    if (!base) return WF_ERR_ALLOC;

    /* subscribeModEvents is a "private endpoint"; carry the agent's current
     * bearer token onto the WS upgrade the same way an authenticated XRPC
     * call would. A never-logged-in agent has no access_jwt, so this leaves
     * the connection unauthenticated rather than fabricating a credential. */
    wf_session_data session;
    memset(&session, 0, sizeof(session));
    wf_agent_get_session_data(agent, &session);

    wf_chat_mod_events_options opts = {0};
    opts.service = base;
    opts.cursor = cursor;
    opts.access_token = session.access_jwt;
    opts.on_event = on_event;
    opts.on_error = on_error;
    opts.userdata = userdata;

    wf_chat_mod_events_handle *handle = NULL;
    wf_status s = wf_chat_mod_events_start(&opts, &handle);
    wf_agent_session_data_free(&session);
    free(base);
    return s;
}
