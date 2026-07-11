#include "wolfram/label.h"
#include "wolfram/syntax.h"
#include "wolfram/repo/cbor.h"
#include "wolfram/crypto.h"
#include "wolfram/identity.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WF_LABEL_DEFAULT_RECONNECT_DELAY_MS 3000u
#define WF_LABEL_MAX_RECONNECT_DELAY_MS 30000u
#define WF_LABEL_SLEEP_SLICE_MS 100u

struct wf_label_subscribe_handle {
    wf_label_subscribe_options opts;
    wf_websocket *socket;
    char *service_copy;
    int64_t cursor;
    uint32_t initial_retry_delay_ms;
    uint32_t retry_delay_ms;
    volatile int stopped;
};

static char *wf_label_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *copy = malloc(n);
    if (copy) memcpy(copy, s, n);
    return copy;
}

static void wf_label_sleep_ms(wf_label_subscribe_handle *handle, uint32_t ms) {
    while (handle && !handle->stopped && ms > 0) {
        uint32_t slice = ms > WF_LABEL_SLEEP_SLICE_MS ? WF_LABEL_SLEEP_SLICE_MS : ms;
        struct timespec ts;
        ts.tv_sec = (time_t)(slice / 1000u);
        ts.tv_nsec = (long)(slice % 1000u) * 1000000L;
        nanosleep(&ts, NULL);
        if (ms <= slice) break;
        ms -= slice;
    }
}

static int wf_label_json_integer(cJSON *item, int64_t *out) {
    if (!item || !cJSON_IsNumber(item)) return 0;
    if (item->valuedouble < (double)INT64_MIN || item->valuedouble > (double)INT64_MAX)
        return 0;
    int64_t value = (int64_t)item->valuedouble;
    if ((double)value != item->valuedouble) return 0;
    *out = value;
    return 1;
}

static void wf_label_clear(wf_label *label) {
    if (!label) return;
    free(label->src);
    free(label->uri);
    free(label->cid);
    free(label->val);
    free(label->cts);
    free(label->exp);
    free(label->sig);
    memset(label, 0, sizeof(*label));
}

static void wf_label_batch_clear(wf_label_batch *batch) {
    if (!batch) return;
    for (size_t i = 0; i < batch->count; ++i) {
        wf_label_clear(&batch->items[i]);
    }
    free(batch->items);
    memset(batch, 0, sizeof(*batch));
}

static void wf_label_info_clear(wf_label_info *info) {
    if (!info) return;
    free(info->name);
    free(info->message);
    memset(info, 0, sizeof(*info));
}

static wf_status wf_label_parse_record(cJSON *node, wf_label *label,
                                       int64_t seq, int force_neg) {
    if (!cJSON_IsObject(node) || !label) return WF_ERR_INVALID_ARG;
    memset(label, 0, sizeof(*label));
    label->seq = seq;

    cJSON *member = cJSON_GetObjectItemCaseSensitive(node, "ver");
    if (member) {
        label->has_ver = 1;
        if (!wf_label_json_integer(member, &label->ver)) goto invalid;
    }

    member = cJSON_GetObjectItemCaseSensitive(node, "src");
    if (!member || !cJSON_IsString(member) || !wf_syntax_did_is_valid(member->valuestring))
        goto invalid;
    label->src = wf_label_strdup(member->valuestring);
    if (!label->src) goto alloc_fail;

    member = cJSON_GetObjectItemCaseSensitive(node, "uri");
    if (!member || !cJSON_IsString(member)) goto invalid;
    label->uri = wf_label_strdup(member->valuestring);
    if (!label->uri) goto alloc_fail;

    member = cJSON_GetObjectItemCaseSensitive(node, "cid");
    if (member) {
        label->has_cid = 1;
        if (!cJSON_IsString(member)) goto invalid;
        label->cid = wf_label_strdup(member->valuestring);
        if (!label->cid) goto alloc_fail;
    }

    member = cJSON_GetObjectItemCaseSensitive(node, "val");
    if (!member || !cJSON_IsString(member)) goto invalid;
    label->val = wf_label_strdup(member->valuestring);
    if (!label->val) goto alloc_fail;

    member = cJSON_GetObjectItemCaseSensitive(node, "neg");
    if (member) {
        label->has_neg = 1;
        if (!cJSON_IsBool(member)) goto invalid;
        label->neg = cJSON_IsTrue(member);
    }
    if (force_neg) {
        label->has_neg = 1;
        label->neg = 1;
    }

    member = cJSON_GetObjectItemCaseSensitive(node, "cts");
    if (!member || !cJSON_IsString(member) ||
        !wf_syntax_datetime_is_valid(member->valuestring))
        goto invalid;
    label->cts = wf_label_strdup(member->valuestring);
    if (!label->cts) goto alloc_fail;

    member = cJSON_GetObjectItemCaseSensitive(node, "exp");
    if (member) {
        label->has_exp = 1;
        if (!cJSON_IsString(member) ||
            !wf_syntax_datetime_is_valid(member->valuestring))
            goto invalid;
        label->exp = wf_label_strdup(member->valuestring);
        if (!label->exp) goto alloc_fail;
    }

    member = cJSON_GetObjectItemCaseSensitive(node, "sig");
    if (member) {
        label->has_sig = 1;
        if (!cJSON_IsString(member)) goto invalid;
        label->sig = wf_label_strdup(member->valuestring);
        if (!label->sig) goto alloc_fail;
    }

    return WF_OK;
alloc_fail:
    wf_label_clear(label);
    return WF_ERR_ALLOC;
invalid:
    wf_label_clear(label);
    return WF_ERR_INVALID_ARG;
}

static wf_status wf_label_parse_labels(cJSON *root, int force_neg,
                                       wf_label_message *out) {
    cJSON *seq_member = cJSON_GetObjectItemCaseSensitive(root, "seq");
    cJSON *labels_member = cJSON_GetObjectItemCaseSensitive(root, "labels");
    int64_t seq = 0;
    if (!wf_label_json_integer(seq_member, &seq) || !cJSON_IsArray(labels_member))
        return WF_ERR_INVALID_ARG;

    size_t count = (size_t)cJSON_GetArraySize(labels_member);
    wf_label *items = calloc(count, sizeof(*items));
    if (count && !items) return WF_ERR_ALLOC;

    for (size_t i = 0; i < count; ++i) {
        cJSON *entry = cJSON_GetArrayItem(labels_member, (int)i);
        wf_status status = wf_label_parse_record(entry, &items[i], seq, force_neg);
        if (status != WF_OK) {
            for (size_t j = 0; j <= i; ++j) wf_label_clear(&items[j]);
            free(items);
            return status;
        }
    }

    out->type = WF_LABEL_MESSAGE_LABELS;
    out->data.labels.seq = seq;
    out->data.labels.items = items;
    out->data.labels.count = count;
    return WF_OK;
}

static wf_status wf_label_parse_info(cJSON *root, wf_label_message *out) {
    cJSON *name_member = cJSON_GetObjectItemCaseSensitive(root, "name");
    cJSON *message_member = cJSON_GetObjectItemCaseSensitive(root, "message");
    if (!name_member || !cJSON_IsString(name_member)) return WF_ERR_INVALID_ARG;

    out->type = WF_LABEL_MESSAGE_INFO;
    out->data.info.name = wf_label_strdup(name_member->valuestring);
    if (!out->data.info.name) {
        wf_label_info_clear(&out->data.info);
        return WF_ERR_ALLOC;
    }
    if (message_member) {
        out->data.info.has_message = 1;
        if (!cJSON_IsString(message_member)) {
            wf_label_info_clear(&out->data.info);
            return WF_ERR_INVALID_ARG;
        }
        out->data.info.message = wf_label_strdup(message_member->valuestring);
        if (!out->data.info.message) {
            wf_label_info_clear(&out->data.info);
            return WF_ERR_ALLOC;
        }
    }
    return WF_OK;
}

/* ── signature verification ────────────────────────────────────────────── */

/* The eight schema fields of com.atproto.label#label, in the order they are
 * declared in the lexicon. The DRISL serializer re-sorts the map keys by their
 * canonical byte encoding, so this declaration order only documents intent. */
static const char *k_label_field_names[8] = {
    "ver", "src", "uri", "cid", "val", "neg", "cts", "exp"
};

static wf_cbor_item *wf_label_cbor_str(const char *s) {
    if (!s) return NULL;
    wf_cbor_item *it = calloc(1, sizeof(*it));
    if (!it) return NULL;
    it->type = WF_CBOR_STRING;
    it->string.str = (char *)s; /* borrowed; never freed by us */
    it->string.len = strlen(s);
    return it;
}

static wf_cbor_item *wf_label_cbor_uint(int64_t v) {
    wf_cbor_item *it = calloc(1, sizeof(*it));
    if (!it) return NULL;
    if (v < 0) {
        it->type = WF_CBOR_NEGATIVE;
        it->neginteger = (uint64_t)(-(v + 1));
    } else {
        it->type = WF_CBOR_UNSIGNED;
        it->uinteger = (uint64_t)v;
    }
    return it;
}

static wf_cbor_item *wf_label_cbor_bool(int b) {
    wf_cbor_item *it = calloc(1, sizeof(*it));
    if (!it) return NULL;
    it->type = WF_CBOR_SIMPLE;
    it->simple_value = b ? 21 : 20; /* CBOR true (0xf5) / false (0xf4) */
    return it;
}

static wf_cbor_item *wf_label_cbor_null(void) {
    wf_cbor_item *it = calloc(1, sizeof(*it));
    if (!it) return NULL;
    it->type = WF_CBOR_SIMPLE;
    it->simple_value = 22; /* CBOR null (0xf6) */
    return it;
}

/*
 * Reconstruct the DRISL-canonical DAG-CBOR encoding of a label exactly as the
 * atproto reference signs it: every schema field except `sig`, with `cid`,
 * `neg`, and `exp` emitted as CBOR null when absent (matching createLabelV1's
 * `opts.x ?? null`), and `ver` always present (defaulting to 1 when the parsed
 * label omits it). The `$type` field is intentionally excluded. The serialized
 * bytes are the message that was SHA-256 hashed and signed by the labeler.
 *
 * On WF_OK, *out is heap-allocated and owned by the caller (free() it).
 */
static wf_status wf_label_build_signed_cbor(const wf_label *label,
                                            unsigned char **out,
                                            size_t *out_len) {
    if (!label || !out || !out_len) return WF_ERR_INVALID_ARG;
    *out = NULL;
    *out_len = 0;

    /* Required fields per the lexicon. */
    if (!label->src || !label->uri || !label->val || !label->cts)
        return WF_ERR_INVALID_ARG;

    wf_cbor_item *keys[8] = {0};
    wf_cbor_item *vals[8] = {0};

    int64_t ver = label->has_ver ? label->ver : 1;
    keys[0] = wf_label_cbor_str(k_label_field_names[0]);
    vals[0] = wf_label_cbor_uint(ver);
    keys[1] = wf_label_cbor_str(k_label_field_names[1]);
    vals[1] = wf_label_cbor_str(label->src);
    keys[2] = wf_label_cbor_str(k_label_field_names[2]);
    vals[2] = wf_label_cbor_str(label->uri);
    keys[3] = wf_label_cbor_str(k_label_field_names[3]);
    vals[3] = label->has_cid ? wf_label_cbor_str(label->cid)
                             : wf_label_cbor_null();
    keys[4] = wf_label_cbor_str(k_label_field_names[4]);
    vals[4] = wf_label_cbor_str(label->val);
    keys[5] = wf_label_cbor_str(k_label_field_names[5]);
    vals[5] = label->has_neg ? wf_label_cbor_bool(label->neg)
                             : wf_label_cbor_null();
    keys[6] = wf_label_cbor_str(k_label_field_names[6]);
    vals[6] = wf_label_cbor_str(label->cts);
    keys[7] = wf_label_cbor_str(k_label_field_names[7]);
    vals[7] = label->has_exp ? wf_label_cbor_str(label->exp)
                             : wf_label_cbor_null();

    wf_status status = WF_OK;
    for (size_t i = 0; i < 8; ++i) {
        if (!keys[i] || !vals[i]) { status = WF_ERR_ALLOC; goto done; }
    }

    wf_cbor_item map;
    memset(&map, 0, sizeof(map));
    map.type = WF_CBOR_MAP;
    map.map.count = 8;
    map.map.pairs = calloc(8, sizeof(wf_cbor_pair));
    if (!map.map.pairs) { status = WF_ERR_ALLOC; goto done; }
    for (size_t i = 0; i < 8; ++i) {
        map.map.pairs[i].key = keys[i];
        map.map.pairs[i].value = vals[i];
    }

    unsigned char *cbor = wf_cbor_serialize(&map, out_len);
    free(map.map.pairs);
    if (!cbor) { status = WF_ERR_ALLOC; goto done; }
    *out = cbor;

done:
    for (size_t i = 0; i < 8; ++i) {
        free(keys[i]);
        free(vals[i]);
    }
    return status;
}

wf_status wf_label_verify_signature_with_key(const char *signing_key_didkey,
                                             const wf_label *label) {
    if (!signing_key_didkey || !label) return WF_ERR_INVALID_ARG;
    if (!label->has_sig || !label->sig || label->sig[0] == '\0')
        return WF_ERR_INVALID_ARG;

    unsigned char *cbor = NULL;
    size_t cbor_len = 0;
    wf_status status = wf_label_build_signed_cbor(label, &cbor, &cbor_len);
    if (status != WF_OK) return status;

    unsigned char *sig = NULL;
    size_t sig_len = 0;
    status = wf_crypto_base64url_decode(label->sig, &sig, &sig_len);
    if (status != WF_OK) {
        free(cbor);
        return status;
    }

    /* wf_verify SHA-256-hashes `cbor` internally and checks the ECDSA
     * signature, exactly matching the labeler's signing procedure. */
    status = wf_verify(signing_key_didkey, cbor, cbor_len, sig, sig_len);
    free(cbor);
    free(sig);
    return status;
}

wf_status wf_label_verify_signature(wf_xrpc_client *client,
                                    const wf_label *label) {
    if (!client || !label) return WF_ERR_INVALID_ARG;
    if (!label->src || !wf_syntax_did_is_valid(label->src))
        return WF_ERR_INVALID_ARG;

    wf_did_document doc = {0};
    wf_status status = wf_did_resolve(client, label->src, &doc);
    if (status != WF_OK) return status;

    /* The spec says to use the labeler's signing key (its `#atproto`
     * verification method) and, if absent, treat the signature as invalid
     * rather than falling back to other keys. */
    if (!doc.signing_key) {
        wf_did_document_free(&doc);
        return WF_ERR_NOT_FOUND;
    }

    status = wf_label_verify_signature_with_key(doc.signing_key, label);
    wf_did_document_free(&doc);
    return status;
}

static wf_status wf_label_dispatch_record(const wf_label_subscribe_handle *handle,
                                           const wf_label *label) {
    if (!handle || !label) return WF_ERR_INVALID_ARG;

    /* Best-effort, non-fatal signature verification. When enabled the result
     * is surfaced through on_error so the caller can decide what to do; the
     * label is still dispatched regardless of the outcome. */
    if (handle->opts.verify_signatures && handle->opts.verify_client) {
        wf_status vs = wf_label_verify_signature(handle->opts.verify_client,
                                                 label);
        if (vs != WF_OK && handle->opts.on_error) {
            handle->opts.on_error(vs, "label signature verification failed",
                                  handle->opts.userdata);
        }
    }

    if (label->neg) {
        if (handle->opts.on_neg) handle->opts.on_neg(label, handle->opts.userdata);
        else if (handle->opts.on_label) handle->opts.on_label(label, handle->opts.userdata);
    } else {
        if (handle->opts.on_label) handle->opts.on_label(label, handle->opts.userdata);
        else if (handle->opts.on_neg) handle->opts.on_neg(label, handle->opts.userdata);
    }
    return WF_OK;
}

static void wf_label_dispatch_message(wf_label_subscribe_handle *handle,
                                      wf_label_message *message) {
    if (!handle || !message) return;
    if (message->type == WF_LABEL_MESSAGE_LABELS) {
        for (size_t i = 0; i < message->data.labels.count && !handle->stopped; ++i) {
            wf_label_dispatch_record(handle, &message->data.labels.items[i]);
            if (message->data.labels.items[i].seq > handle->cursor)
                handle->cursor = message->data.labels.items[i].seq;
        }
        return;
    }

    if (message->type == WF_LABEL_MESSAGE_INFO) {
        if (handle->opts.on_info) {
            handle->opts.on_info(&message->data.info, handle->opts.userdata);
        }
        if (message->data.info.name &&
            strcmp(message->data.info.name, "OutdatedCursor") == 0) {
            handle->cursor = 0;
            wf_websocket_free(handle->socket);
            handle->socket = NULL;
        }
    }
}

wf_status wf_label_message_parse(const char *json, size_t json_len,
                                 wf_label_message *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    cJSON *type_member = cJSON_GetObjectItemCaseSensitive(root, "$type");
    if (!type_member) type_member = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(type_member)) {
        cJSON_Delete(root);
        return WF_ERR_INVALID_ARG;
    }

    wf_status status = WF_ERR_INVALID_ARG;
    if (strcmp(type_member->valuestring, "#labels") == 0) {
        status = wf_label_parse_labels(root, 0, out);
    } else if (strcmp(type_member->valuestring, "#neg") == 0) {
        status = wf_label_parse_labels(root, 1, out);
    } else if (strcmp(type_member->valuestring, "#info") == 0) {
        status = wf_label_parse_info(root, out);
    }

    cJSON_Delete(root);
    if (status != WF_OK) wf_label_message_free(out);
    return status;
}

void wf_label_message_free(wf_label_message *message) {
    if (!message) return;
    switch (message->type) {
    case WF_LABEL_MESSAGE_LABELS:
        wf_label_batch_clear(&message->data.labels);
        break;
    case WF_LABEL_MESSAGE_INFO:
        wf_label_info_clear(&message->data.info);
        break;
    default:
        break;
    }
    memset(message, 0, sizeof(*message));
}

wf_status wf_label_build_url(const char *service, int64_t cursor,
                             char **out_url) {
    if (!service || !service[0] || !out_url || cursor < 0) return WF_ERR_INVALID_ARG;
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

    const char *path = "/xrpc/" WF_LABEL_SUBSCRIBE_NSID;
    const char *query = strchr(body, '?');
    const char *existing = strstr(body, path);
    size_t body_len = query ? (size_t)(query - body) : strlen(body);
    if (existing == NULL) {
        while (body_len > 0 && body[body_len - 1] == '/') body_len--;
    }
    size_t query_tail_len = query ? strlen(query) : 0;
    size_t path_len = existing ? 0 : strlen(path);

    char cursor_part[32] = {0};
    size_t cursor_len = 0;
    if (cursor > 0) {
        const char *separator = query ? "&cursor=" : "?cursor=";
        cursor_len = (size_t)snprintf(cursor_part, sizeof(cursor_part),
                                      "%s%lld", separator, (long long)cursor);
    }

    size_t total = strlen(scheme) + body_len + path_len + query_tail_len + cursor_len + 1;
    char *url = malloc(total);
    if (!url) return WF_ERR_ALLOC;

    char *p = url;
    size_t scheme_len = strlen(scheme);
    memcpy(p, scheme, scheme_len);
    p += scheme_len;
    memcpy(p, body, body_len);
    p += body_len;
    if (!existing) {
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

static wf_status wf_label_open(wf_label_subscribe_handle *handle) {
    if (!handle) return WF_ERR_INVALID_ARG;
    char *url = NULL;
    wf_status status = wf_label_build_url(handle->opts.service, handle->cursor, &url);
    if (status != WF_OK) return status;
    status = wf_websocket_connect(url, &handle->socket);
    free(url);
    return status;
}

static wf_status wf_label_reconnect(wf_label_subscribe_handle *handle) {
    if (!handle) return WF_ERR_INVALID_ARG;
    wf_websocket_free(handle->socket);
    handle->socket = NULL;
    if (handle->stopped) return WF_OK;

    wf_label_sleep_ms(handle, handle->retry_delay_ms);
    if (handle->stopped) return WF_OK;

    wf_status status = wf_label_open(handle);
    if (status == WF_OK) {
        handle->retry_delay_ms = handle->initial_retry_delay_ms;
        return WF_OK;
    }

    if (handle->retry_delay_ms < WF_LABEL_MAX_RECONNECT_DELAY_MS) {
        uint64_t doubled = (uint64_t)handle->retry_delay_ms * 2u;
        handle->retry_delay_ms = doubled > WF_LABEL_MAX_RECONNECT_DELAY_MS
                                     ? WF_LABEL_MAX_RECONNECT_DELAY_MS
                                     : (uint32_t)doubled;
    }
    return status;
}

wf_status wf_label_subscribe_start(const wf_label_subscribe_options *opts,
                                   wf_label_subscribe_handle **out) {
    if (!opts || !out || !opts->service || !opts->service[0] ||
        (!opts->on_label && !opts->on_neg && !opts->on_info)) {
        return WF_ERR_INVALID_ARG;
    }
    if (opts->has_cursor && opts->cursor < 0) return WF_ERR_INVALID_ARG;

    *out = NULL;
    wf_label_subscribe_handle *handle = calloc(1, sizeof(*handle));
    if (!handle) return WF_ERR_ALLOC;

    handle->opts = *opts;
    handle->cursor = opts->has_cursor ? opts->cursor : 0;
    handle->initial_retry_delay_ms = opts->reconnect_delay_ms
                                         ? opts->reconnect_delay_ms
                                         : WF_LABEL_DEFAULT_RECONNECT_DELAY_MS;
    handle->retry_delay_ms = handle->initial_retry_delay_ms;

    handle->service_copy = wf_label_strdup(opts->service);
    if (!handle->service_copy) {
        free(handle);
        return WF_ERR_ALLOC;
    }
    handle->opts.service = handle->service_copy;

    wf_status status = wf_label_open(handle);
    if (status != WF_OK) {
        free(handle->service_copy);
        free(handle);
        return status;
    }

    *out = handle;
    while (!handle->stopped) {
        if (!handle->socket) {
            status = wf_label_reconnect(handle);
            if (status != WF_OK) {
                if (handle->opts.on_error) {
                    handle->opts.on_error(status, "reconnect failed",
                                          handle->opts.userdata);
                }
                continue;
            }
        }

        wf_websocket_message msg = {0};
        status = wf_websocket_receive(handle->socket, &msg);

        if (status == WF_ERR_WOULD_BLOCK) {
            wf_websocket_message_free(&msg);
            wf_label_sleep_ms(handle, WF_LABEL_SLEEP_SLICE_MS);
            continue;
        }

        if (status == WF_ERR_NETWORK) {
            wf_websocket_message_free(&msg);
            if (handle->opts.on_error) {
                handle->opts.on_error(status, "websocket receive failed",
                                      handle->opts.userdata);
            }
            wf_websocket_free(handle->socket);
            handle->socket = NULL;
            continue;
        }

        if (status != WF_OK) {
            wf_websocket_message_free(&msg);
            if (handle->opts.on_error) {
                handle->opts.on_error(status, "websocket receive failed",
                                      handle->opts.userdata);
            }
            wf_websocket_free(handle->socket);
            handle->socket = NULL;
            continue;
        }

        wf_label_message message = {0};
        status = wf_label_message_parse((const char *)msg.data, msg.len, &message);
        wf_websocket_message_free(&msg);
        if (status != WF_OK) {
            if (handle->opts.on_error) {
                handle->opts.on_error(status, "invalid label frame",
                                      handle->opts.userdata);
            }
            continue;
        }

        wf_label_dispatch_message(handle, &message);
        wf_label_message_free(&message);
    }

    wf_websocket_free(handle->socket);
    handle->socket = NULL;
    free(handle->service_copy);
    free(handle);
    *out = NULL;
    return WF_OK;
}

void wf_label_subscribe_stop(wf_label_subscribe_handle *handle) {
    if (!handle) return;
    handle->stopped = 1;
}
