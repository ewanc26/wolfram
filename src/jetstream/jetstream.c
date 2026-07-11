#include "wolfram/jetstream.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_LIBZSTD
#include <zstd.h>
#endif

#define WF_JETSTREAM_MAX_DECOMPRESSED (16u * 1024u * 1024u)
#define WF_JETSTREAM_MAX_SUBSCRIBER_MESSAGE (10u * 1024u * 1024u)

struct wf_jetstream {
    wf_websocket *socket;
    wf_jetstream_options options;
    char *endpoint;
    char **collections;
    char **dids;
    unsigned char *dictionary;
    int64_t cursor;
    uint32_t retry_delay_ms;
    uint64_t retry_at_ms;
    uint64_t last_ping_ms;     /* last keepalive ping sent (ms) */
    char *pending_update_json;
    size_t pending_update_json_len;
};

struct wf_url_buffer { char *data; size_t len; size_t cap; };

static int wf_url_append(struct wf_url_buffer *buf, const char *text) {
    size_t len = strlen(text), needed = buf->len + len + 1;
    if (needed > buf->cap) {
        size_t cap = buf->cap ? buf->cap : 128;
        while (cap < needed) cap *= 2;
        char *grown = realloc(buf->data, cap);
        if (!grown) return 0;
        buf->data = grown; buf->cap = cap;
    }
    memcpy(buf->data + buf->len, text, len + 1); buf->len += len;
    return 1;
}

static int wf_url_append_encoded(struct wf_url_buffer *buf, const char *text) {
    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' ||
            *p == '.' || *p == '~') {
            char one[2] = {(char)*p, 0}; if (!wf_url_append(buf, one)) return 0;
        } else {
            char escaped[4] = {'%', hex[*p >> 4], hex[*p & 15], 0};
            if (!wf_url_append(buf, escaped)) return 0;
        }
    }
    return 1;
}

static int wf_url_param(struct wf_url_buffer *buf, int *first,
                        const char *name, const char *value) {
    if (!wf_url_append(buf, *first ? "?" : "&") ||
        !wf_url_append(buf, name) || !wf_url_append(buf, "=") ||
        !wf_url_append_encoded(buf, value)) return 0;
    *first = 0; return 1;
}

wf_status wf_jetstream_build_url(const wf_jetstream_options *options,
                                 char **out_url) {
    if (!options || !out_url || !options->endpoint ||
        (strncmp(options->endpoint, "ws://", 5) != 0 &&
         strncmp(options->endpoint, "wss://", 6) != 0) ||
        options->wanted_collections_count > 100 ||
        options->wanted_dids_count > 10000 ||
        (options->wanted_collections_count && !options->wanted_collections) ||
        (options->wanted_dids_count && !options->wanted_dids) ||
        (options->compress && (!options->zstd_dictionary ||
                              !options->zstd_dictionary_len ||
                              !wf_jetstream_zstd_supported())) ||
        (options->reconnect_max_delay_ms && options->reconnect_initial_delay_ms &&
         options->reconnect_max_delay_ms < options->reconnect_initial_delay_ms)) {
        return WF_ERR_INVALID_ARG;
    }
    *out_url = NULL;
    for (size_t i = 0; i < options->wanted_collections_count; ++i) {
        if (!options->wanted_collections[i]) return WF_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < options->wanted_dids_count; ++i) {
        if (!options->wanted_dids[i]) return WF_ERR_INVALID_ARG;
    }
    struct wf_url_buffer buf = {0}; int first = strchr(options->endpoint, '?') == NULL;
    if (!wf_url_append(&buf, options->endpoint)) return WF_ERR_ALLOC;
    for (size_t i = 0; i < options->wanted_collections_count; ++i) {
        if (!wf_url_param(&buf, &first, "wantedCollections", options->wanted_collections[i])) goto alloc_error;
    }
    for (size_t i = 0; i < options->wanted_dids_count; ++i) {
        if (!wf_url_param(&buf, &first, "wantedDids", options->wanted_dids[i])) goto alloc_error;
    }
    char number[32];
    if (options->cursor) {
        snprintf(number, sizeof(number), "%lld", (long long)options->cursor);
        if (!wf_url_param(&buf, &first, "cursor", number)) goto alloc_error;
    }
    if (options->max_message_size_bytes) {
        snprintf(number, sizeof(number), "%u", options->max_message_size_bytes);
        if (!wf_url_param(&buf, &first, "maxMessageSizeBytes", number)) goto alloc_error;
    }
    if (options->compress && !wf_url_param(&buf, &first, "compress", "true"))
        goto alloc_error;
    if (options->require_hello &&
        !wf_url_param(&buf, &first, "requireHello", "true"))
        goto alloc_error;
    *out_url = buf.data; return WF_OK;
alloc_error:
    free(buf.data); return WF_ERR_ALLOC;
}

static char *wf_strdup(const char *text) {
    size_t len = strlen(text) + 1;
    char *copy = malloc(len);
    if (copy) memcpy(copy, text, len);
    return copy;
}

static wf_jetstream_event_kind wf_jetstream_kind_from_string(const char *kind) {
    if (!kind) return WF_JETSTREAM_EVENT_UNKNOWN;
    if (strcmp(kind, "commit") == 0) return WF_JETSTREAM_EVENT_COMMIT;
    if (strcmp(kind, "identity") == 0) return WF_JETSTREAM_EVENT_IDENTITY;
    if (strcmp(kind, "account") == 0) return WF_JETSTREAM_EVENT_ACCOUNT;
    if (strcmp(kind, "account_delete") == 0) return WF_JETSTREAM_EVENT_ACCOUNT_DELETE;
    if (strcmp(kind, "sync") == 0) return WF_JETSTREAM_EVENT_SYNC;
    if (strcmp(kind, "fork") == 0) return WF_JETSTREAM_EVENT_FORK;
    if (strcmp(kind, "timeout") == 0) return WF_JETSTREAM_EVENT_TIMEOUT;
    if (strcmp(kind, "migrate") == 0) return WF_JETSTREAM_EVENT_MIGRATE;
    if (strcmp(kind, "info") == 0) return WF_JETSTREAM_EVENT_INFO;
    return WF_JETSTREAM_EVENT_UNKNOWN;
}

static uint64_t wf_now_ms(void) {
    struct timespec now;
    if (timespec_get(&now, TIME_UTC) != TIME_UTC) return 0;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void wf_jetstream_release_options(wf_jetstream *stream) {
    free(stream->endpoint);
    for (size_t i = 0; i < stream->options.wanted_collections_count; ++i)
        free(stream->collections[i]);
    for (size_t i = 0; i < stream->options.wanted_dids_count; ++i)
        free(stream->dids[i]);
    free(stream->collections); free(stream->dids); free(stream->dictionary);
}

static int wf_jetstream_copy_options(wf_jetstream *stream,
                                     const wf_jetstream_options *options) {
    stream->options = *options;
    stream->endpoint = wf_strdup(options->endpoint);
    if (!stream->endpoint) return 0;
    stream->options.endpoint = stream->endpoint;
    if (options->wanted_collections_count) {
        stream->collections = calloc(options->wanted_collections_count, sizeof(char *));
        if (!stream->collections) return 0;
        for (size_t i = 0; i < options->wanted_collections_count; ++i) {
            stream->collections[i] = wf_strdup(options->wanted_collections[i]);
            if (!stream->collections[i]) return 0;
        }
        stream->options.wanted_collections = (const char * const *)stream->collections;
    }
    if (options->wanted_dids_count) {
        stream->dids = calloc(options->wanted_dids_count, sizeof(char *));
        if (!stream->dids) return 0;
        for (size_t i = 0; i < options->wanted_dids_count; ++i) {
            stream->dids[i] = wf_strdup(options->wanted_dids[i]);
            if (!stream->dids[i]) return 0;
        }
        stream->options.wanted_dids = (const char * const *)stream->dids;
    }
    if (options->zstd_dictionary_len) {
        stream->dictionary = malloc(options->zstd_dictionary_len);
        if (!stream->dictionary) return 0;
        memcpy(stream->dictionary, options->zstd_dictionary,
               options->zstd_dictionary_len);
        stream->options.zstd_dictionary = stream->dictionary;
    }
    stream->cursor = options->cursor;
    stream->retry_delay_ms = options->reconnect_initial_delay_ms
                                 ? options->reconnect_initial_delay_ms : 250;
    return 1;
}

static wf_status wf_jetstream_open(wf_jetstream *stream) {
    char *url = NULL;
    stream->options.cursor = stream->cursor;
    wf_status status = wf_jetstream_build_url(&stream->options, &url);
    if (status == WF_OK) status = wf_websocket_connect(url, &stream->socket);
    free(url);
    return status;
}

wf_status wf_jetstream_connect(const wf_jetstream_options *options,
                               wf_jetstream **out) {
    if (!out) return WF_ERR_INVALID_ARG;
    *out = NULL; char *url = NULL;
    wf_status status = wf_jetstream_build_url(options, &url);
    if (status != WF_OK) return status;
    free(url);
    wf_jetstream *stream = calloc(1, sizeof(*stream));
    if (!stream) return WF_ERR_ALLOC;
    if (!wf_jetstream_copy_options(stream, options)) {
        wf_jetstream_release_options(stream); free(stream); return WF_ERR_ALLOC;
    }
    status = wf_jetstream_open(stream);
    if (status != WF_OK) {
        wf_jetstream_release_options(stream); free(stream); return status;
    }
    *out = stream; return WF_OK;
}

static int wf_jetstream_update_valid(const wf_jetstream_options_update *update) {
    if (!update || update->wanted_collections_count > 100 ||
        update->wanted_dids_count > 10000 ||
        (update->wanted_collections_count && !update->wanted_collections) ||
        (update->wanted_dids_count && !update->wanted_dids)) return 0;
    for (size_t i = 0; i < update->wanted_collections_count; ++i)
        if (!update->wanted_collections[i] || !update->wanted_collections[i][0]) return 0;
    for (size_t i = 0; i < update->wanted_dids_count; ++i)
        if (!update->wanted_dids[i] || !update->wanted_dids[i][0]) return 0;
    return 1;
}

wf_status wf_jetstream_options_update_json(
    const wf_jetstream_options_update *update,
    char **out_json, size_t *out_json_len) {
    if (!out_json || !out_json_len || !wf_jetstream_update_valid(update))
        return WF_ERR_INVALID_ARG;
    *out_json = NULL; *out_json_len = 0;
    cJSON *root = cJSON_CreateObject();
    cJSON *payload = cJSON_CreateObject();
    cJSON *collections = cJSON_CreateArray();
    cJSON *dids = cJSON_CreateArray();
    if (!root || !payload || !collections || !dids ||
        !cJSON_AddStringToObject(root, "type", "options_update")) goto alloc;
    cJSON_AddItemToObject(root, "payload", payload); payload = NULL;
    cJSON *body = cJSON_GetObjectItemCaseSensitive(root, "payload");
    cJSON_AddItemToObject(body, "wantedCollections", collections); collections = NULL;
    cJSON_AddItemToObject(body, "wantedDids", dids); dids = NULL;
    cJSON *collection_array = cJSON_GetObjectItemCaseSensitive(body, "wantedCollections");
    cJSON *did_array = cJSON_GetObjectItemCaseSensitive(body, "wantedDids");
    for (size_t i = 0; i < update->wanted_collections_count; ++i) {
        cJSON *value = cJSON_CreateString(update->wanted_collections[i]);
        if (!value || !cJSON_AddItemToArray(collection_array, value)) {
            cJSON_Delete(value); goto alloc;
        }
    }
    for (size_t i = 0; i < update->wanted_dids_count; ++i) {
        cJSON *value = cJSON_CreateString(update->wanted_dids[i]);
        if (!value || !cJSON_AddItemToArray(did_array, value)) {
            cJSON_Delete(value); goto alloc;
        }
    }
    if (!cJSON_AddNumberToObject(body, "maxMessageSizeBytes",
                                 update->max_message_size_bytes)) goto alloc;
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;
    size_t len = strlen(json);
    if (len > WF_JETSTREAM_MAX_SUBSCRIBER_MESSAGE) {
        free(json); return WF_ERR_INVALID_ARG;
    }
    *out_json = json; *out_json_len = len;
    return WF_OK;
alloc:
    cJSON_Delete(root); cJSON_Delete(payload);
    cJSON_Delete(collections); cJSON_Delete(dids);
    return WF_ERR_ALLOC;
}

wf_status wf_jetstream_update_options(
    wf_jetstream *stream, const wf_jetstream_options_update *update) {
    if (!stream || !stream->socket) return WF_ERR_INVALID_ARG;
    char *json = NULL; size_t json_len = 0;
    wf_status status = wf_jetstream_options_update_json(update, &json, &json_len);
    if (status != WF_OK) return status;
    if (stream->pending_update_json &&
        (stream->pending_update_json_len != json_len ||
         memcmp(stream->pending_update_json, json, json_len) != 0)) {
        free(json); return WF_ERR_INVALID_ARG;
    }
    if (!stream->pending_update_json) {
        stream->pending_update_json = json;
        stream->pending_update_json_len = json_len;
    } else free(json);
    status = wf_websocket_send_text(stream->socket,
                                    stream->pending_update_json,
                                    stream->pending_update_json_len);
    if (status != WF_ERR_WOULD_BLOCK) {
        free(stream->pending_update_json); stream->pending_update_json = NULL;
        stream->pending_update_json_len = 0;
    }
    return status;
}

int wf_jetstream_zstd_supported(void) {
#ifdef HAVE_LIBZSTD
    return 1;
#else
    return 0;
#endif
}

wf_status wf_jetstream_event_parse_zstd(const void *compressed,
                                        size_t compressed_len,
                                        const void *dictionary,
                                        size_t dictionary_len,
                                        wf_jetstream_event *out) {
    if (!compressed || !compressed_len || !dictionary || !dictionary_len || !out)
        return WF_ERR_INVALID_ARG;
#ifdef HAVE_LIBZSTD
    ZSTD_DCtx *context = ZSTD_createDCtx();
    if (!context) return WF_ERR_ALLOC;
    size_t result = ZSTD_DCtx_loadDictionary(context, dictionary, dictionary_len);
    if (ZSTD_isError(result)) { ZSTD_freeDCtx(context); return WF_ERR_PARSE; }
    size_t capacity = 8192, length = 0;
    unsigned char *json = malloc(capacity + 1);
    if (!json) { ZSTD_freeDCtx(context); return WF_ERR_ALLOC; }
    ZSTD_inBuffer input = {compressed, compressed_len, 0};
    do {
        if (length == capacity) {
            if (capacity >= WF_JETSTREAM_MAX_DECOMPRESSED) {
                free(json); ZSTD_freeDCtx(context); return WF_ERR_PARSE;
            }
            capacity *= 2;
            unsigned char *grown = realloc(json, capacity + 1);
            if (!grown) { free(json); ZSTD_freeDCtx(context); return WF_ERR_ALLOC; }
            json = grown;
        }
        ZSTD_outBuffer output = {json + length, capacity - length, 0};
        result = ZSTD_decompressStream(context, &output, &input);
        if (ZSTD_isError(result)) {
            free(json); ZSTD_freeDCtx(context); return WF_ERR_PARSE;
        }
        length += output.pos;
    } while (input.pos < input.size || result != 0);
    ZSTD_freeDCtx(context);
    json[length] = '\0';
    wf_status status = wf_jetstream_event_parse((const char *)json, length, out);
    free(json);
    return status;
#else
    (void)compressed; (void)compressed_len; (void)dictionary;
    (void)dictionary_len; (void)out;
    return WF_ERR_INVALID_ARG;
#endif
}

wf_status wf_jetstream_event_parse(const char *json, size_t json_len,
                                   wf_jetstream_event *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_ParseWithLength(json, json_len);
    cJSON *kind = root ? cJSON_GetObjectItemCaseSensitive(root, "kind") : NULL;
    cJSON *did = root ? cJSON_GetObjectItemCaseSensitive(root, "did") : NULL;
    cJSON *time_us = root ? cJSON_GetObjectItemCaseSensitive(root, "time_us") : NULL;
    /* `info` frames (e.g. OutdatedCursor) carry neither time_us nor seq, so
     * they are exempt from the timestamp gate; the reference ignores them. */
    int is_info = cJSON_IsString(kind) && strcmp(kind->valuestring, "info") == 0;
    if (!cJSON_IsObject(root) || !cJSON_IsString(kind) || !cJSON_IsString(did) ||
        (!is_info &&
         (!cJSON_IsNumber(time_us) || time_us->valuedouble < 0 ||
          time_us->valuedouble > 9007199254740991.0 ||
          (double)(int64_t)time_us->valuedouble != time_us->valuedouble))) {
        cJSON_Delete(root); return WF_ERR_PARSE;
    }
    size_t did_len = strlen(did->valuestring);
    out->did = malloc(did_len + 1);
    out->json = malloc(json_len + 1);
    if (!out->did || !out->json) {
        cJSON_Delete(root); wf_jetstream_event_free(out); return WF_ERR_ALLOC;
    }
    memcpy(out->did, did->valuestring, did_len + 1);
    memcpy(out->json, json, json_len); out->json[json_len] = '\0';
    out->kind = wf_jetstream_kind_from_string(kind->valuestring);
    out->time_us = (int64_t)(is_info ? 0 : time_us->valuedouble);
    out->json_len = json_len;
    cJSON_Delete(root); return WF_OK;
}

wf_status wf_jetstream_next(wf_jetstream *stream, wf_jetstream_event *out) {
    if (!stream || !out) return WF_ERR_INVALID_ARG;
    if (!stream->socket) {
        uint64_t now = wf_now_ms();
        if (now < stream->retry_at_ms) return WF_ERR_WOULD_BLOCK;
        wf_status reconnect = wf_jetstream_open(stream);
        if (reconnect != WF_OK) {
            uint32_t maximum = stream->options.reconnect_max_delay_ms
                                   ? stream->options.reconnect_max_delay_ms : 30000;
            stream->retry_at_ms = now + stream->retry_delay_ms;
            if (stream->retry_delay_ms < maximum) {
                uint64_t doubled = (uint64_t)stream->retry_delay_ms * 2;
                stream->retry_delay_ms = doubled > maximum ? maximum : (uint32_t)doubled;
            }
            return WF_ERR_WOULD_BLOCK;
        }
        stream->last_ping_ms = wf_now_ms();
    }
    for (;;) {
        wf_websocket_message message = {0};
        wf_status status = wf_websocket_receive(stream->socket, &message);
        if (status == WF_ERR_NETWORK) {
            wf_websocket_free(stream->socket); stream->socket = NULL;
            stream->retry_at_ms = wf_now_ms() + stream->retry_delay_ms;
            return WF_ERR_WOULD_BLOCK;
        }
        if (status == WF_ERR_WOULD_BLOCK) {
            /* Idle: the socket is healthy but has no frame yet. Send a
             * keepalive ping when the idle window elapses. (Missed-pong
             * termination is intentionally NOT implemented: libcurl
             * auto-handles PING/PONG and does not surface PONG to the
             * application receive path, and we must not alter the transport
             * framing in websocket.c.) */
            uint64_t now = wf_now_ms();
            uint32_t ping_interval = stream->options.ping_interval_ms
                                         ? stream->options.ping_interval_ms : 30000;
            if (now - stream->last_ping_ms >= ping_interval) {
                wf_websocket_send_ping(stream->socket);
                stream->last_ping_ms = now;
            }
            return WF_ERR_WOULD_BLOCK;
        }
        if (status != WF_OK) return status;
        if (message.type == WF_WEBSOCKET_TEXT && !stream->options.compress)
            status = wf_jetstream_event_parse((const char *)message.data, message.len, out);
        else if (message.type == WF_WEBSOCKET_BINARY && stream->options.compress)
            status = wf_jetstream_event_parse_zstd(message.data, message.len,
                        stream->dictionary, stream->options.zstd_dictionary_len, out);
        else status = WF_ERR_PARSE;
        wf_websocket_message_free(&message);
        if (status != WF_OK) return status;

        /* Info frames (e.g. OutdatedCursor) carry no time_us and must not be
         * surfaced as events or errors. On OutdatedCursor the cursor is
         * outdated, so reset it to 0 so the next reconnect replays from the
         * start. The connection stays open either way. */
        if (out->kind == WF_JETSTREAM_EVENT_INFO) {
            cJSON *root = cJSON_ParseWithLength(out->json, out->json_len);
            cJSON *name = root ? cJSON_GetObjectItemCaseSensitive(root, "name") : NULL;
            if (name && cJSON_IsString(name) &&
                strcmp(name->valuestring, "OutdatedCursor") == 0)
                stream->cursor = 0;
            cJSON_Delete(root);
            wf_jetstream_event_free(out);
            continue;
        }

        stream->cursor = out->time_us;
        stream->last_ping_ms = wf_now_ms();
        stream->retry_delay_ms = stream->options.reconnect_initial_delay_ms
                                     ? stream->options.reconnect_initial_delay_ms : 250;
        stream->retry_at_ms = 0;
        return WF_OK;
    }
}

uint32_t wf_jetstream_reconnect_after_ms(const wf_jetstream *stream) {
    if (!stream || stream->socket || !stream->retry_at_ms) return 0;
    uint64_t now = wf_now_ms();
    uint64_t remaining = stream->retry_at_ms > now ? stream->retry_at_ms - now : 0;
    return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
}

void wf_jetstream_event_free(wf_jetstream_event *event) {
    if (!event) return; free(event->did); free(event->json); memset(event, 0, sizeof(*event));
}

/* ── typed payload parsing ── */

static char *wf_jetstream_dup_string(cJSON *parent, const char *key) {
    cJSON *item = parent ? cJSON_GetObjectItemCaseSensitive(parent, key) : NULL;
    if (!item || !cJSON_IsString(item) || !item->valuestring) return NULL;
    return wf_strdup(item->valuestring);
}

static char *wf_jetstream_dup_seq(cJSON *parent, const char *key) {
    cJSON *item = parent ? cJSON_GetObjectItemCaseSensitive(parent, key) : NULL;
    if (!item || !cJSON_IsNumber(item)) return NULL;
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)item->valuedouble);
    return wf_strdup(buf);
}

static wf_jetstream_commit_op wf_jetstream_commit_op_from_string(
    const char *op) {
    if (!op) return WF_JETSTREAM_COMMIT_UNKNOWN;
    if (strcmp(op, "create") == 0) return WF_JETSTREAM_COMMIT_CREATE;
    if (strcmp(op, "update") == 0) return WF_JETSTREAM_COMMIT_UPDATE;
    if (strcmp(op, "delete") == 0) return WF_JETSTREAM_COMMIT_DELETE;
    return WF_JETSTREAM_COMMIT_UNKNOWN;
}

static wf_status wf_jetstream_commit_parse(cJSON *raw, wf_jetstream_commit *out) {
    if (!cJSON_IsObject(raw)) return WF_ERR_PARSE;
    memset(out, 0, sizeof(*out));
    cJSON *operation = cJSON_GetObjectItemCaseSensitive(raw, "operation");
    if (!cJSON_IsString(operation) || !operation->valuestring)
        return WF_ERR_PARSE;
    out->operation = wf_jetstream_commit_op_from_string(operation->valuestring);
    if (out->operation == WF_JETSTREAM_COMMIT_UNKNOWN) return WF_ERR_PARSE;
    out->collection = wf_jetstream_dup_string(raw, "collection");
    out->rkey = wf_jetstream_dup_string(raw, "rkey");
    out->cid = wf_jetstream_dup_string(raw, "cid");
    out->rev = wf_jetstream_dup_string(raw, "rev");
    /* `record` is the owned record JSON for create/update; deletes omit it. */
    cJSON *record = cJSON_GetObjectItemCaseSensitive(raw, "record");
    if (record && !cJSON_IsNull(record)) {
        out->record = cJSON_Duplicate(record, 1);
        out->has_record = out->record ? 1 : 0;
    }
    if (!out->collection || !out->rkey) return WF_ERR_PARSE;
    return WF_OK;
}

static wf_status wf_jetstream_identity_parse(cJSON *raw,
                                             wf_jetstream_identity *out) {
    if (!cJSON_IsObject(raw)) return WF_ERR_PARSE;
    memset(out, 0, sizeof(*out));
    out->handle = wf_jetstream_dup_string(raw, "handle");
    if (!out->handle) return WF_ERR_PARSE;
    return WF_OK;
}

static wf_status wf_jetstream_account_parse(cJSON *raw,
                                            wf_jetstream_account *out) {
    if (!cJSON_IsObject(raw)) return WF_ERR_PARSE;
    memset(out, 0, sizeof(*out));
    cJSON *active = cJSON_GetObjectItemCaseSensitive(raw, "active");
    if (!cJSON_IsBool(active)) return WF_ERR_PARSE;
    out->active = cJSON_IsTrue(active);
    out->status = wf_jetstream_dup_string(raw, "status");
    return WF_OK;
}

static wf_status wf_jetstream_sync_parse(cJSON *raw, wf_jetstream_event_kind kind,
                                         wf_jetstream_sync *out) {
    if (raw && !cJSON_IsObject(raw)) return WF_ERR_PARSE;
    memset(out, 0, sizeof(*out));
    out->kind = kind;
    if (!raw) return WF_OK;
    out->seq = wf_jetstream_dup_seq(raw, "seq");
    out->did = wf_jetstream_dup_string(raw, "did");
    if (!out->did) out->did = wf_jetstream_dup_string(raw, "repo");
    out->time = wf_jetstream_dup_string(raw, "time");
    out->rev = wf_jetstream_dup_string(raw, "rev");
    cJSON *too_big = cJSON_GetObjectItemCaseSensitive(raw, "tooBig");
    if (too_big && cJSON_IsBool(too_big)) out->too_big = cJSON_IsTrue(too_big);
    return WF_OK;
}

wf_status wf_jetstream_event_parse_typed(const char *json, size_t json_len,
                                         wf_jetstream_event_typed *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_ParseWithLength(json, json_len);
    cJSON *kind = root ? cJSON_GetObjectItemCaseSensitive(root, "kind") : NULL;
    cJSON *did = root ? cJSON_GetObjectItemCaseSensitive(root, "did") : NULL;
    cJSON *seq = root ? cJSON_GetObjectItemCaseSensitive(root, "seq") : NULL;
    cJSON *time_us = root ? cJSON_GetObjectItemCaseSensitive(root, "time_us")
                          : NULL;
    /* Per the documented contract, only `kind` and `did` gate parsing. Info
     * frames carry neither seq nor time_us, so those are optional. */
    int is_info = cJSON_IsString(kind) && strcmp(kind->valuestring, "info") == 0;
    int time_ok = is_info || (cJSON_IsNumber(time_us) && time_us->valuedouble >= 0 &&
                   time_us->valuedouble <= 9007199254740991.0 &&
                   (double)(int64_t)time_us->valuedouble == time_us->valuedouble);
    if (!cJSON_IsObject(root) || !cJSON_IsString(kind) ||
        !cJSON_IsString(did) || !did->valuestring ||
        (!is_info && !cJSON_IsNumber(seq)) ||
        !time_ok) {
        cJSON_Delete(root); return WF_ERR_PARSE;
    }
    out->kind = wf_jetstream_kind_from_string(kind->valuestring);
    out->did = wf_strdup(did->valuestring);
    if (!out->did) { cJSON_Delete(root); return WF_ERR_ALLOC; }
    out->seq = (int64_t)seq->valuedouble;
    out->time_us = (int64_t)time_us->valuedouble;

    wf_status status = WF_ERR_PARSE;
    switch (out->kind) {
    case WF_JETSTREAM_EVENT_COMMIT: {
        cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "commit");
        status = wf_jetstream_commit_parse(payload, &out->commit);
        break;
    }
    case WF_JETSTREAM_EVENT_IDENTITY: {
        cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "identity");
        status = wf_jetstream_identity_parse(payload, &out->identity);
        break;
    }
    case WF_JETSTREAM_EVENT_ACCOUNT:
    case WF_JETSTREAM_EVENT_ACCOUNT_DELETE: {
        cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "account");
        status = wf_jetstream_account_parse(payload, &out->account);
        break;
    }
    case WF_JETSTREAM_EVENT_SYNC:
    case WF_JETSTREAM_EVENT_FORK:
    case WF_JETSTREAM_EVENT_TIMEOUT:
    case WF_JETSTREAM_EVENT_MIGRATE:
    case WF_JETSTREAM_EVENT_INFO: {
        cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, kind->valuestring);
        status = wf_jetstream_sync_parse(payload, out->kind, &out->sync);
        break;
    }
    default:
        status = WF_ERR_PARSE;
        break;
    }

    cJSON_Delete(root);
    if (status != WF_OK) { wf_jetstream_event_typed_free(out); return status; }
    return WF_OK;
}

void wf_jetstream_event_typed_free(wf_jetstream_event_typed *ev) {
    if (!ev) return;
    free(ev->did);
    free(ev->commit.collection); free(ev->commit.rkey);
    cJSON_Delete(ev->commit.record);
    free(ev->commit.cid); free(ev->commit.rev);
    free(ev->identity.handle);
    free(ev->account.status);
    free(ev->sync.seq); free(ev->sync.did); free(ev->sync.time);
    free(ev->sync.rev);
    memset(ev, 0, sizeof(*ev));
}

void wf_jetstream_free(wf_jetstream *stream) {
    if (!stream) return;
    wf_websocket_free(stream->socket);
    free(stream->pending_update_json);
    wf_jetstream_release_options(stream);
    free(stream);
}
