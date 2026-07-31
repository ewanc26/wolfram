#include "wolfram/sync_subscribe.h"
#include "wolfram/websocket.h"
#include "wolfram/crypto.h"

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
    uint64_t last_ping_ms;     /* last keepalive ping sent (ms) */
    volatile int stopped;
};

/* Default idle interval between client keepalive pings when the option is 0. */
#define WF_SUBSCRIBE_DEFAULT_PING_INTERVAL_MS 30000

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

/* Return 1 if the integer item is minimally encoded (RFC 8949 §2.1). */
static int int_is_minimal(const cbor_item_t *item) {
    if (!item) return 0;
    switch (cbor_typeof(item)) {
    case CBOR_TYPE_UINT:
        return cbor_get_int(item) <= UINT8_MAX && cbor_uint_set_uint(item, cbor_get_int(item)) == CBOR_OK;
    case CBOR_TYPE_NEGINT:
        return cbor_get_int(item) <= UINT8_MAX && cbor_negint_set_negint(item, -1 - cbor_get_int(item)) == CBOR_OK;
    default:
        return 1; /* not an integer, skip */
    }
}

/* Check recursively if a CBOR map has keys in canonical DAG-CBOR order. */
static wf_status check_canonical_map(cbor_item_t *map, size_t depth) {
    if (depth > 20) return WF_OK; /* arbitrary depth limit */
    if (!cbor_isa_map(map)) return WF_OK;

    struct cbor_pair *pairs = cbor_map_handle(map);
    size_t count = cbor_map_size(map);
    for (size_t i = 0; i < count; i++) {
        /* Only string keys are meaningful for ordering */
        if (!cbor_isa_string(pairs[i].key)) continue;

        /* Compare with previous key */
        if (i > 0 && cbor_isa_string(pairs[i - 1].key)) {
            size_t prev_len = cbor_string_length(pairs[i - 1].key);
            size_t cur_len = cbor_string_length(pairs[i].key);
            if (prev_len != cur_len) {
                return WF_ERR_PARSE;
            }
            if (memcmp(cbor_string_handle(pairs[i - 1].key), cbor_string_handle(pairs[i].key), cur_len) < 0) {
                return WF_ERR_PARSE;
            }
        }

        /* Recurse on value */
        if (cbor_typeof(pairs[i].value) == CBOR_TYPE_MAP ||
            cbor_typeof(pairs[i].value) == CBOR_TYPE_ARRAY) {
            wf_status s = check_canonical_map(pairs[i].value, depth + 1);
            if (s != WF_OK) return s;
        }
    }
    return WF_OK;
}

/* Strict CID link validation: CID tag 42 must have 0x00 prefix. */
static wf_status parse_cid_link_with_strict_check(cbor_item_t *item, const unsigned char *full_data, size_t full_len) {
    if (cbor_typeof(item) == CBOR_TYPE_TAG && cbor_tag_value(item) == 42) {
        cbor_item_t *tagged = cbor_tag_item(item);
        if (cbor_isa_bytestring(tagged)) {
            size_t len = cbor_bytestring_length(tagged);
            const unsigned char *data = cbor_bytestring_handle(tagged);
            if (len < 1 || data[0] != 0x00) {
                return WF_ERR_PARSE;
            }
        }
    }
    return WF_OK;
}

static wf_status parse_cid_link_strict(cbor_item_t *item, wf_cid *out) {
    if (!out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (cbor_typeof(item) != CBOR_TYPE_TAG || cbor_tag_value(item) != 42) return WF_ERR_PARSE;
    cbor_item_t *tagged = cbor_tag_item(item);
    if (!cbor_isa_bytestring(tagged)) return WF_ERR_PARSE;
    size_t len = cbor_bytestring_length(tagged);
    const unsigned char *data = cbor_bytestring_handle(tagged);
    if (len < 1 || data[0] != 0x00) return WF_ERR_PARSE;
    len--; data++;
    if (len > sizeof(out->bytes)) return WF_ERR_PARSE;
    memcpy(out->bytes, data, len);
    out->len = len;
    return WF_OK;
}

static wf_status is_canonical_dag_cbor(cbor_item_t *item) {
    if (!item) return WF_ERR_PARSE;
    switch (cbor_typeof(item)) {
    case CBOR_TYPE_MAP:
        wf_status s = check_canonical_map(item, 0);
        if (s != WF_OK) return s;
        /* recurse on all values */
        struct cbor_pair *pairs = cbor_map_handle(item);
        size_t count = cbor_map_size(item);
        for (size_t i = 0; i < count; i++) {
            wf_status s = is_canonical_dag_cbor(pairs[i].value);
            if (s != WF_OK) return s;
        }
        break;
    case CBOR_TYPE_ARRAY:
        for (size_t i = 0; i < cbor_array_size(item); i++) {
            wf_status s = is_canonical_dag_cbor(cbor_array_handle(item)[i]);
            if (s != WF_OK) return s;
        }
        break;
    default:
        break;
    }
    return WF_OK;
}

static int is_minimally_encoded(cbor_item_t *item) {
    if (!item) return 0;
    switch (cbor_typeof(item)) {
    case CBOR_TYPE_MAP:
        /* Check each key-value pair */
        struct cbor_pair *pairs = cbor_map_handle(item);
        size_t count = cbor_map_size(item);
        for (size_t i = 0; i < count; i++) {
            if (!int_is_minimal(pairs[i].key)) return 0;
            if (!is_minimally_encoded(pairs[i].value)) return 0;
        }
        break;
    case CBOR_TYPE_ARRAY:
        for (size_t i = 0; i < cbor_array_size(item); i++) {
            if (!is_minimally_encoded(cbor_array_handle(item)[i])) return 0;
        }
        break;
    case CBOR_TYPE_UINT:
    case CBOR_TYPE_NEGINT:
        if (!int_is_minimal(item)) return 0;
        break;
    default:
        break;
    }
    return 1;
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
    case WF_SUBSCRIBE_EVENT_LABELS:
        if (ev->data.labels.labels) {
            for (size_t i = 0; i < ev->data.labels.labels_count; i++) {
                wf_label *lab = &ev->data.labels.labels[i];
                free(lab->src);
                free(lab->uri);
                free(lab->cid);
                free(lab->val);
                free(lab->cts);
                free(lab->exp);
                free(lab->sig);
            }
            free(ev->data.labels.labels);
        }
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
        op->has_cid = parse_cid_link_strict(cid, &op->cid);

    cbor_item_t *prev = map_find(item, "prev");
    if (prev && !cbor_is_null(prev) && !cbor_is_undef(prev))
        op->has_prev = parse_cid_link_strict(prev, &op->prev);

    return 1;
}

