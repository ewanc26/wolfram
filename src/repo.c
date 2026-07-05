/**
 * repo.c — DAG-CBOR decode, CID computation.
 *
 * DAG-CBOR is a strict canonical subset of CBOR with full constraint
 * validation. CID computation uses OpenSSL for SHA-256 hashing.
 */

#include "wolfram/repo.h"

#include <openssl/sha.h>
#include <stdlib.h>
#include <string.h>

/* ── DAG-CBOR decoder ─────────────────────────────────────── */

static uint64_t wf_read_u64_be(const unsigned char *p, size_t n) {
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++) v = (v << 8) | p[i];
    return v;
}

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
        /* DAG-CBOR allows tag 42 (CID) wrapping a byte string. */
        if (arg != 42) { free(item); return NULL; }
        free(item);
        return wf_cbor_parse_item(pos, end, data);

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

/* ── CID computation ───────────────────────────────────────── */

/*
 * RFC 4648 base32 (lowercase, no padding) encoder.
 * Output buffer must be at least ((len * 8 + 4) / 5) + 1 bytes.
 */
static void wf_base32_encode(const unsigned char *in, size_t in_len,
                              char *out) {
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz234567";
    size_t i = 0, o = 0;
    while (i < in_len) {
        uint64_t buf = 0;
        int bits = 0;
        for (int n = 0; n < 5 && i < in_len; n++, i++) {
            buf = (buf << 8) | in[i];
            bits += 8;
        }
        int need = (bits + 4) / 5;
        for (int c = 0; c < need; c++) {
            int shift = bits - 5;
            if (shift >= 0) {
                out[o++] = alphabet[(buf >> shift) & 0x1f];
            } else {
                out[o++] = alphabet[(buf << (-shift)) & 0x1f];
            }
            bits -= 5;
        }
    }
    out[o] = '\0';
}

/*
 * Build a raw CIDv1 byte array from a SHA-256 digest.
 * cid_buf must be at least 36 bytes.
 * Returns the number of bytes written (always 36).
 */
static size_t wf_cid_from_hash(const unsigned char hash[32],
                                unsigned char *cid_buf) {
    cid_buf[0] = 0x01;              /* CIDv1 */
    cid_buf[1] = 0x71;              /* dag-cbor multicodec */
    cid_buf[2] = 0x12;              /* sha2-256 multihash code */
    cid_buf[3] = 0x20;              /* 32-byte digest length */
    memcpy(cid_buf + 4, hash, 32);  /* the digest itself */
    return 36;
}

char *wf_cid_to_string(const wf_cid *cid) {
    if (!cid || cid->len == 0) return NULL;

    /* base32 output: ceil(len*8/5) chars + 'b' prefix + NUL */
    size_t b32_len = (cid->len * 8 + 4) / 5;
    char *str = malloc(b32_len + 2);
    if (!str) return NULL;

    str[0] = 'b';
    wf_base32_encode(cid->bytes, cid->len, str + 1);
    return str;
}

wf_status wf_cid_of_block(const unsigned char *cbor, size_t cbor_len,
                           wf_cid *out) {
    if (!cbor || cbor_len == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(cbor, cbor_len, hash);

    out->len = wf_cid_from_hash(hash, out->bytes);
    return WF_OK;
}

/* ── LEB128 varint reader ─────────────────────────────────── */

static uint64_t wf_read_varint(const unsigned char *data, size_t len,
                                size_t *used) {
    uint64_t value = 0;
    size_t i;
    for (i = 0; i < len && i < 10; i++) {
        value |= ((uint64_t)(data[i] & 0x7f)) << (7 * i);
        if (!(data[i] & 0x80)) {
            *used = i + 1;
            return value;
        }
    }
    *used = 0;
    return 0;
}

/* ── CAR parse ────────────────────────────────────────────── */

wf_status wf_car_parse(const unsigned char *data, size_t len, wf_car *out) {
    if (!data || len == 0 || !out) return WF_ERR_INVALID_ARG;

    memset(out, 0, sizeof(*out));

    size_t pos = 0;

    /* ── Header ── */

    size_t used;
    uint64_t hdr_len = wf_read_varint(data + pos, len - pos, &used);
    if (used == 0) return WF_ERR_INVALID_ARG;
    pos += used;
    if (pos + hdr_len > len) return WF_ERR_INVALID_ARG;

    wf_cbor_item *hdr = wf_cbor_parse(data + pos, (size_t)hdr_len);
    if (!hdr) return WF_ERR_INVALID_ARG;
    pos += (size_t)hdr_len;

    if (hdr->type != WF_CBOR_MAP) { wf_cbor_free(hdr); return WF_ERR_INVALID_ARG; }

    int found_roots = 0;
    wf_cbor_item *roots_arr = NULL;

    for (size_t i = 0; i < hdr->map.count; i++) {
        wf_cbor_item *k = hdr->map.pairs[i].key;
        if (k->type == WF_CBOR_STRING && k->string.len == 7 &&
            memcmp(k->string.str, "version", 7) == 0) {
            wf_cbor_item *v = hdr->map.pairs[i].value;
            if (v->type != WF_CBOR_UNSIGNED || v->uinteger != 1) {
                wf_cbor_free(hdr);
                return WF_ERR_INVALID_ARG;
            }
        } else if (k->type == WF_CBOR_STRING && k->string.len == 5 &&
                   memcmp(k->string.str, "roots", 5) == 0) {
            wf_cbor_item *v = hdr->map.pairs[i].value;
            if (v->type != WF_CBOR_ARRAY || v->children.count == 0) {
                wf_cbor_free(hdr);
                return WF_ERR_INVALID_ARG;
            }
            roots_arr = v;
            found_roots = 1;
        }
    }

    if (!found_roots) { wf_cbor_free(hdr); return WF_ERR_INVALID_ARG; }

    out->root_count = roots_arr->children.count;
    out->roots = calloc(out->root_count, sizeof(wf_cid));
    if (!out->roots) { wf_cbor_free(hdr); return WF_ERR_ALLOC; }

    for (size_t i = 0; i < out->root_count; i++) {
        wf_cbor_item *root_item = roots_arr->children.items[i];
        if (root_item->type != WF_CBOR_BYTES && root_item->type != WF_CBOR_UNSIGNED) {
            wf_car_free(out);
            out->roots = NULL;
            wf_cbor_free(hdr);
            return WF_ERR_INVALID_ARG;
        }
        if (root_item->type == WF_CBOR_BYTES) {
            if (root_item->bytes.len != 36) {
                wf_car_free(out);
                out->roots = NULL;
                wf_cbor_free(hdr);
                return WF_ERR_INVALID_ARG;
            }
            memcpy(out->roots[i].bytes, root_item->bytes.data, 36);
            out->roots[i].len = 36;
        } else {
            out->roots[i].len = 0;
        }
    }

    wf_cbor_free(hdr);
    hdr = NULL;

    /* ── Blocks ── */

    while (pos < len) {
        uint64_t section_len = wf_read_varint(data + pos, len - pos, &used);
        if (used == 0) { wf_car_free(out); return WF_ERR_INVALID_ARG; }
        pos += used;
        if (pos + section_len > len) { wf_car_free(out); return WF_ERR_INVALID_ARG; }

        if (section_len < 36) { wf_car_free(out); return WF_ERR_INVALID_ARG; }

        /* Reallocate block array */
        size_t new_count = out->block_count + 1;
        wf_car_block *new_blocks = realloc(out->blocks,
                                            new_count * sizeof(wf_car_block));
        if (!new_blocks) { wf_car_free(out); return WF_ERR_ALLOC; }
        out->blocks = new_blocks;

        wf_car_block *blk = &out->blocks[out->block_count];
        memcpy(blk->cid.bytes, data + pos, 36);
        blk->cid.len = 36;

        size_t data_len = (size_t)section_len - 36;
        blk->data = NULL;
        blk->data_len = data_len;
        if (data_len > 0) {
            blk->data = malloc(data_len);
            if (!blk->data) { wf_car_free(out); return WF_ERR_ALLOC; }
            memcpy(blk->data, data + pos + 36, data_len);
        }

        out->block_count = new_count;
        pos += (size_t)section_len;
    }

    return WF_OK;
}

void wf_car_free(wf_car *car) {
    if (!car) return;
    for (size_t i = 0; i < car->block_count; i++)
        free(car->blocks[i].data);
    free(car->blocks);
    free(car->roots);
    car->roots = NULL;
    car->blocks = NULL;
    car->block_count = 0;
    car->root_count = 0;
}

wf_car_block *wf_car_find_block(wf_car *car, const wf_cid *cid) {
    if (!car || !cid) return NULL;
    for (size_t i = 0; i < car->block_count; i++) {
        if (car->blocks[i].cid.len == cid->len &&
            memcmp(car->blocks[i].cid.bytes, cid->bytes, cid->len) == 0) {
            return &car->blocks[i];
        }
    }
    return NULL;
}

/* ── Commit parse ─────────────────────────────────────────── */

/*
 * CBOR helper: find a text key in a map and return its value item.
 * The map must have sorted keys. Returns NULL if not found.
 */
static wf_cbor_item *wf_cbor_map_find(wf_cbor_item *item,
                                       const char *key, size_t key_len) {
    if (!item || item->type != WF_CBOR_MAP) return NULL;
    for (size_t i = 0; i < item->map.count; i++) {
        wf_cbor_item *k = item->map.pairs[i].key;
        if (k->type == WF_CBOR_STRING && k->string.len == key_len &&
            memcmp(k->string.str, key, key_len) == 0) {
            return item->map.pairs[i].value;
        }
    }
    return NULL;
}

wf_status wf_commit_parse(const unsigned char *cbor, size_t len,
                           wf_commit *out) {
    if (!cbor || len == 0 || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    wf_cbor_item *obj = wf_cbor_parse(cbor, len);
    if (!obj) return WF_ERR_PARSE;
    if (obj->type != WF_CBOR_MAP) { wf_cbor_free(obj); return WF_ERR_PARSE; }

    /* did */
    wf_cbor_item *did_item = wf_cbor_map_find(obj, "did", 3);
    if (!did_item || did_item->type != WF_CBOR_STRING) {
        wf_cbor_free(obj); return WF_ERR_PARSE;
    }
    if (did_item->string.len >= sizeof(out->did)) {
        wf_cbor_free(obj); return WF_ERR_PARSE;
    }
    memcpy(out->did, did_item->string.str, did_item->string.len);
    out->did[did_item->string.len] = '\0';

    /* version */
    wf_cbor_item *ver_item = wf_cbor_map_find(obj, "version", 7);
    if (!ver_item || ver_item->type != WF_CBOR_UNSIGNED) {
        wf_cbor_free(obj); return WF_ERR_PARSE;
    }
    out->version = (int)ver_item->uinteger;

    /* data (CID link — byte string from tag 42) */
    wf_cbor_item *data_item = wf_cbor_map_find(obj, "data", 4);
    if (!data_item || data_item->type != WF_CBOR_BYTES ||
        data_item->bytes.len != 36) {
        wf_cbor_free(obj); return WF_ERR_PARSE;
    }
    memcpy(out->data.bytes, data_item->bytes.data, 36);
    out->data.len = 36;

    /* rev */
    wf_cbor_item *rev_item = wf_cbor_map_find(obj, "rev", 3);
    if (!rev_item || rev_item->type != WF_CBOR_STRING) {
        wf_cbor_free(obj); return WF_ERR_PARSE;
    }
    if (rev_item->string.len >= sizeof(out->rev)) {
        wf_cbor_free(obj); return WF_ERR_PARSE;
    }
    memcpy(out->rev, rev_item->string.str, rev_item->string.len);
    out->rev[rev_item->string.len] = '\0';

    /* prev (nullable CID link — may be null or absent) */
    wf_cbor_item *prev_item = wf_cbor_map_find(obj, "prev", 4);
    if (prev_item && prev_item->type == WF_CBOR_BYTES &&
        prev_item->bytes.len == 36) {
        memcpy(out->prev.bytes, prev_item->bytes.data, 36);
        out->prev.len = 36;
        out->has_prev = 1;
    }

    wf_cbor_free(obj);
    return WF_OK;
}

/* ── MST ───────────────────────────────────────────────────── */

unsigned wf_mst_key_layer(const unsigned char *key, size_t key_len) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(key, key_len, hash);

    unsigned count = 0;
    for (int i = 0; i < (int)sizeof(hash); i++) {
        unsigned char b = hash[i];
        if (b <  64) count++;
        if (b <  16) count++;
        if (b <   4) count++;
        if (b ==  0) count++;
        else break;
    }
    return count;
}

static int wf_mst_key_cmp(const unsigned char *a, size_t a_len,
                           const unsigned char *b, size_t b_len) {
    size_t min = a_len < b_len ? a_len : b_len;
    int cmp = memcmp(a, b, min);
    if (cmp != 0) return cmp;
    return (int)(a_len - b_len);
}

wf_status wf_mst_node_parse(const unsigned char *cbor, size_t len,
                             const wf_cid *cid, wf_mst_node *out) {
    if (!cbor || len == 0 || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (cid) out->cid = *cid;

    wf_cbor_item *obj = wf_cbor_parse(cbor, len);
    if (!obj) return WF_ERR_PARSE;
    if (obj->type != WF_CBOR_MAP) { wf_cbor_free(obj); return WF_ERR_PARSE; }

    /* "l" — left subtree CID (nullable) */
    wf_cbor_item *l_item = wf_cbor_map_find(obj, "l", 1);
    if (l_item && l_item->type == WF_CBOR_BYTES && l_item->bytes.len == 36) {
        memcpy(out->left.bytes, l_item->bytes.data, 36);
        out->left.len = 36;
    }

    /* "e" — entries array */
    wf_cbor_item *e_item = wf_cbor_map_find(obj, "e", 1);
    if (!e_item || e_item->type != WF_CBOR_ARRAY) {
        wf_cbor_free(obj); return WF_ERR_PARSE;
    }

    out->count = e_item->children.count;
    if (out->count == 0) {
        wf_cbor_free(obj);
        return WF_OK;
    }

    out->entries = calloc(out->count, sizeof(wf_mst_entry));
    if (!out->entries) { wf_cbor_free(obj); return WF_ERR_ALLOC; }

    unsigned char *prev_key = NULL;
    size_t prev_key_len = 0;

    for (size_t i = 0; i < out->count; i++) {
        wf_cbor_item *entry = e_item->children.items[i];
        if (entry->type != WF_CBOR_MAP) { wf_mst_node_free(out); wf_cbor_free(obj); return WF_ERR_PARSE; }

        wf_mst_entry *ent = &out->entries[i];

        /* "p" — prefix length */
        wf_cbor_item *p_item = wf_cbor_map_find(entry, "p", 1);
        if (!p_item || p_item->type != WF_CBOR_UNSIGNED) {
            wf_mst_node_free(out); wf_cbor_free(obj); return WF_ERR_PARSE;
        }
        uint64_t prefix_len = p_item->uinteger;
        if (prefix_len > prev_key_len) {
            wf_mst_node_free(out); wf_cbor_free(obj); return WF_ERR_PARSE;
        }

        /* "k" — key suffix */
        wf_cbor_item *k_item = wf_cbor_map_find(entry, "k", 1);
        if (!k_item || k_item->type != WF_CBOR_BYTES) {
            wf_mst_node_free(out); wf_cbor_free(obj); return WF_ERR_PARSE;
        }

        /* Reconstruct full key */
        size_t full_len = (size_t)prefix_len + k_item->bytes.len;
        ent->key = malloc(full_len);
        if (!ent->key) { wf_mst_node_free(out); wf_cbor_free(obj); return WF_ERR_ALLOC; }
        if (prefix_len > 0 && prev_key)
            memcpy(ent->key, prev_key, (size_t)prefix_len);
        if (k_item->bytes.len > 0)
            memcpy(ent->key + prefix_len, k_item->bytes.data, k_item->bytes.len);
        ent->key_len = full_len;

        /* "v" — value CID */
        wf_cbor_item *v_item = wf_cbor_map_find(entry, "v", 1);
        if (!v_item || v_item->type != WF_CBOR_BYTES || v_item->bytes.len != 36) {
            wf_mst_node_free(out); wf_cbor_free(obj); return WF_ERR_PARSE;
        }
        memcpy(ent->value.bytes, v_item->bytes.data, 36);
        ent->value.len = 36;

        /* "t" — right subtree CID (nullable) */
        wf_cbor_item *t_item = wf_cbor_map_find(entry, "t", 1);
        if (t_item && t_item->type == WF_CBOR_BYTES && t_item->bytes.len == 36) {
            memcpy(ent->subtree.bytes, t_item->bytes.data, 36);
            ent->subtree.len = 36;
        }

        /* Update previous key for next entry */
        free(prev_key);
        prev_key = malloc(full_len);
        if (prev_key) {
            memcpy(prev_key, ent->key, full_len);
            prev_key_len = full_len;
        } else {
            prev_key_len = 0;
        }
    }

    free(prev_key);

    /* Validate sorted keys */
    for (size_t i = 1; i < out->count; i++) {
        if (wf_mst_key_cmp(out->entries[i-1].key, out->entries[i-1].key_len,
                           out->entries[i].key, out->entries[i].key_len) >= 0) {
            wf_mst_node_free(out); wf_cbor_free(obj); return WF_ERR_PARSE;
        }
    }

    wf_cbor_free(obj);
    return WF_OK;
}

void wf_mst_node_free(wf_mst_node *node) {
    if (!node) return;
    for (size_t i = 0; i < node->count; i++)
        free(node->entries[i].key);
    free(node->entries);
    node->entries = NULL;
    node->count = 0;
}

wf_status wf_mst_find(wf_car *car, const wf_cid *root_cid,
                       const unsigned char *key, size_t key_len,
                       wf_cid *out) {
    if (!car || !root_cid || !key || key_len == 0 || !out)
        return WF_ERR_INVALID_ARG;

    wf_cid current = *root_cid;

    while (1) {
        wf_car_block *block = wf_car_find_block(car, &current);
        if (!block) return WF_ERR_PARSE;

        wf_mst_node node;
        wf_status s = wf_mst_node_parse(block->data, block->data_len,
                                         &current, &node);
        if (s != WF_OK) return s;

        size_t idx = 0;
        for (; idx < node.count; idx++) {
            int cmp = wf_mst_key_cmp(key, key_len,
                                      node.entries[idx].key,
                                      node.entries[idx].key_len);
            if (cmp == 0) {
                *out = node.entries[idx].value;
                wf_mst_node_free(&node);
                return WF_OK;
            }
            if (cmp < 0) {
                break;
            }
        }

        wf_cid *subtree = NULL;
        if (idx == 0) {
            if (node.left.len > 0) subtree = &node.left;
        } else {
            if (node.entries[idx - 1].subtree.len > 0)
                subtree = &node.entries[idx - 1].subtree;
        }

        if (!subtree && idx >= node.count && node.count > 0) {
            if (node.entries[node.count - 1].subtree.len > 0)
                subtree = &node.entries[node.count - 1].subtree;
        }

        if (subtree && subtree->len > 0) {
            current = *subtree;
            wf_mst_node_free(&node);
            continue;
        }

        wf_mst_node_free(&node);
        return WF_ERR_NOT_FOUND;
    }
}
