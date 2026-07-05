/**
 * repo.c — DAG-CBOR decode, CID/CAR stubs.
 *
 * DAG-CBOR is a strict canonical subset of CBOR:
 *   - No tags (major type 6)
 *   - No floats, no undefined (major type 7 values 23–27)
 *   - No indefinite-length (additional info 31)
 *   - Canonical integer encoding (shortest form)
 *   - Map keys sorted by byte-sorted CBOR encoding
 */

#include "wolfram/repo.h"

#include <stdlib.h>
#include <string.h>

/* ── internal helpers ──────────────────────────────────────── */

static uint64_t wf_read_u64_be(const unsigned char *p, size_t n) {
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++) v = (v << 8) | p[i];
    return v;
}

/* Parse one CBOR item (recursive). `data` is the original buffer
 * start, used for map key offset calculations. */
static wf_cbor_item *wf_cbor_parse_item(const unsigned char **pos,
                                         const unsigned char *end,
                                         const unsigned char *data) {
    if (*pos >= end) return NULL;

    unsigned char b = *(*pos)++;
    unsigned major = (b >> 5) & 7;
    unsigned info  = b & 0x1f;

    if (info > 27) return NULL;

    uint64_t arg = 0;
    if (info <= 23) {
        arg = info;
    } else if (info == 24) {
        if (*pos >= end) return NULL;
        arg = *(*pos)++;
        if (arg < 24) return NULL;
    } else {
        unsigned n = 1u << (info - 24);
        if ((size_t)(end - *pos) < n) return NULL;
        arg = wf_read_u64_be(*pos, n);
        *pos += n;
        if (info == 25 && arg <= 0xff) return NULL;
        if (info == 26 && arg <= 0xffff) return NULL;
        if (info == 27 && arg <= 0xffffffffULL) return NULL;
    }

    wf_cbor_item *item = calloc(1, sizeof(*item));
    if (!item) return NULL;

    switch (major) {
    case 0:
        item->type = WF_CBOR_UNSIGNED;
        item->uinteger = arg;
        return item;

    case 1:
        item->type = WF_CBOR_NEGATIVE;
        item->neginteger = arg;
        return item;

    case 2: {
        if ((size_t)(end - *pos) < arg) { free(item); return NULL; }
        item->type = WF_CBOR_BYTES;
        item->bytes.len = (size_t)arg;
        if (arg > 0) {
            item->bytes.data = malloc((size_t)arg);
            if (!item->bytes.data) { free(item); return NULL; }
            memcpy(item->bytes.data, *pos, (size_t)arg);
        } else {
            item->bytes.data = NULL;
        }
        *pos += (size_t)arg;
        return item;
    }

    case 3: {
        if ((size_t)(end - *pos) < arg) { free(item); return NULL; }
        item->type = WF_CBOR_STRING;
        item->string.len = (size_t)arg;
        item->string.str = malloc((size_t)arg + 1);
        if (!item->string.str && arg) { free(item); return NULL; }
        if (arg > 0) memcpy(item->string.str, *pos, (size_t)arg);
        item->string.str[(size_t)arg] = '\0';
        *pos += (size_t)arg;
        return item;
    }

    case 4: {
        size_t n = (size_t)arg;
        item->type = WF_CBOR_ARRAY;
        item->children.count = n;
        item->children.items = NULL;
        if (n == 0) return item;
        item->children.items = calloc(n, sizeof(wf_cbor_item *));
        if (!item->children.items) { free(item); return NULL; }
        for (size_t i = 0; i < n; i++) {
            wf_cbor_item *child = wf_cbor_parse_item(pos, end, data);
            if (!child) { wf_cbor_free(item); return NULL; }
            item->children.items[i] = child;
        }
        return item;
    }

    case 5: {
        size_t n = (size_t)arg;
        item->type = WF_CBOR_MAP;
        item->map.count = n;
        item->map.pairs = NULL;
        if (n == 0) return item;

        item->map.pairs = calloc(n, sizeof(wf_cbor_pair));
        if (!item->map.pairs) { free(item); return NULL; }

        size_t *k_off = malloc(n * sizeof(size_t));
        size_t *k_len = malloc(n * sizeof(size_t));
        if (!k_off || !k_len) {
            free(k_off); free(k_len);
            wf_cbor_free(item);
            return NULL;
        }

        for (size_t i = 0; i < n; i++) {
            k_off[i] = (size_t)(*pos - data);
            item->map.pairs[i].key = wf_cbor_parse_item(pos, end, data);
            if (!item->map.pairs[i].key) {
                item->map.count = i;
                wf_cbor_free(item);
                free(k_off); free(k_len);
                return NULL;
            }
            k_len[i] = (size_t)(*pos - data) - k_off[i];

            item->map.pairs[i].value = wf_cbor_parse_item(pos, end, data);
            if (!item->map.pairs[i].value) {
                item->map.count = i + 1;
                wf_cbor_free(item);
                free(k_off); free(k_len);
                return NULL;
            }
        }

        /* Validate map key sorting */
        for (size_t i = 1; i < n; i++) {
            size_t prev_len = k_len[i - 1], cur_len = k_len[i];
            size_t cmp_len  = prev_len < cur_len ? prev_len : cur_len;
            int cmp = memcmp(data + k_off[i - 1], data + k_off[i], cmp_len);
            if (cmp > 0 || (cmp == 0 && prev_len > cur_len)) {
                wf_cbor_free(item);
                free(k_off); free(k_len);
                return NULL;
            }
        }

        free(k_off); free(k_len);
        return item;
    }

    case 6:
        free(item);
        return NULL;

    case 7:
        if (info <= 23) {
            if (info == 20) { item->type = WF_CBOR_SIMPLE; item->simple_value = 0; return item; }
            if (info == 21) { item->type = WF_CBOR_SIMPLE; item->simple_value = 1; return item; }
            if (info == 22) { item->type = WF_CBOR_SIMPLE; item->simple_value = 2; return item; }
        }
        free(item);
        return NULL;

    default:
        free(item);
        return NULL;
    }
}

/* ── public API ────────────────────────────────────────────── */

wf_cbor_item *wf_cbor_parse(const unsigned char *data, size_t len) {
    if (!data || len == 0) return NULL;

    const unsigned char *pos = data;
    const unsigned char *end = data + len;

    wf_cbor_item *root = wf_cbor_parse_item(&pos, end, data);
    if (!root) return NULL;

    if (pos != end) {
        wf_cbor_free(root);
        return NULL;
    }

    return root;
}

void wf_cbor_free(wf_cbor_item *item) {
    if (!item) return;

    switch (item->type) {
    case WF_CBOR_BYTES:
        free(item->bytes.data);
        break;
    case WF_CBOR_STRING:
        free(item->string.str);
        break;
    case WF_CBOR_ARRAY:
        for (size_t i = 0; i < item->children.count; i++)
            wf_cbor_free(item->children.items[i]);
        free(item->children.items);
        break;
    case WF_CBOR_MAP:
        for (size_t i = 0; i < item->map.count; i++) {
            wf_cbor_free(item->map.pairs[i].key);
            wf_cbor_free(item->map.pairs[i].value);
        }
        free(item->map.pairs);
        break;
    default:
        break;
    }

    free(item);
}

/* ── CID stubs ─────────────────────────────────────────────── */

char *wf_cid_to_string(const wf_cid *cid) {
    (void)cid;
    return NULL;
}

wf_status wf_cid_of_block(const unsigned char *cbor, size_t cbor_len,
                           wf_cid *out) {
    (void)cbor;
    (void)cbor_len;
    (void)out;
    return WF_ERR_INVALID_ARG;
}

/* ── CAR stubs ─────────────────────────────────────────────── */

wf_status wf_car_parse(const unsigned char *car, size_t car_len, wf_car *out) {
    (void)car;
    (void)car_len;
    (void)out;
    return WF_ERR_INVALID_ARG;
}

void wf_car_free(wf_car *car) {
    if (!car) return;
    for (size_t i = 0; i < car->block_count; i++)
        free(car->blocks[i].data);
    free(car->blocks);
    free(car->roots);
    car->blocks = NULL;
    car->roots = NULL;
    car->block_count = 0;
    car->root_count = 0;
}
