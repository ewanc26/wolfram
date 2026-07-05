/**
 * repo.c — DAG-CBOR decode, CID computation.
 *
 * DAG-CBOR is a strict canonical subset of CBOR with full constraint
 * validation. CID computation uses OpenSSL for SHA-256 hashing.
 */

#include "wolfram/repo.h"

#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
            if (info == 20) { item->type = WF_CBOR_SIMPLE; item->simple_value = 20; return item; }
            if (info == 21) { item->type = WF_CBOR_SIMPLE; item->simple_value = 21; return item; }
            if (info == 22) { item->type = WF_CBOR_SIMPLE; item->simple_value = 22; return item; }
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

/* ── DAG-CBOR serializer ──────────────────────────────────── */

static size_t wf_cbor_ser_uint_size(uint64_t val) {
    if (val <= 23) return 1;
    if (val <= 0xFF) return 2;
    if (val <= 0xFFFF) return 3;
    if (val <= 0xFFFFFFFFULL) return 5;
    return 9;
}

static size_t wf_cbor_ser_item_size(const wf_cbor_item *item) {
    if (!item) return 0;
    size_t s = 0;

    if (item->type == WF_CBOR_UNSIGNED) {
        s += wf_cbor_ser_uint_size(item->uinteger);
    } else if (item->type == WF_CBOR_NEGATIVE) {
        s += wf_cbor_ser_uint_size(item->neginteger);
    } else if (item->type == WF_CBOR_BYTES) {
        s += wf_cbor_ser_uint_size(item->bytes.len);
        s += item->bytes.len;
    } else if (item->type == WF_CBOR_STRING) {
        s += wf_cbor_ser_uint_size(item->string.len);
        s += item->string.len;
    } else if (item->type == WF_CBOR_ARRAY) {
        s += wf_cbor_ser_uint_size(item->children.count);
        for (size_t i = 0; i < item->children.count; i++)
            s += wf_cbor_ser_item_size(item->children.items[i]);
    } else if (item->type == WF_CBOR_MAP) {
        s += wf_cbor_ser_uint_size(item->map.count);
        for (size_t i = 0; i < item->map.count; i++) {
            s += wf_cbor_ser_item_size(item->map.pairs[i].key);
            s += wf_cbor_ser_item_size(item->map.pairs[i].value);
        }
    } else if (item->type == WF_CBOR_SIMPLE) {
        int sv = item->simple_value;
        if (sv >= 20 && sv <= 23) s += 1;
        else s += 2; /* 0xF8 + value */
    }
    return s;
}

static void wf_cbor_ser_write_uint(unsigned major, uint64_t val,
                                    unsigned char **pos) {
    unsigned char m = (unsigned)(major << 5);
    if (val <= 23) {
        *(*pos)++ = m | (unsigned char)val;
    } else if (val <= 0xFF) {
        *(*pos)++ = m | 24;
        *(*pos)++ = (unsigned char)val;
    } else if (val <= 0xFFFF) {
        *(*pos)++ = m | 25;
        *(*pos)++ = (unsigned char)(val >> 8);
        *(*pos)++ = (unsigned char)val;
    } else if (val <= 0xFFFFFFFFULL) {
        *(*pos)++ = m | 26;
        *(*pos)++ = (unsigned char)(val >> 24);
        *(*pos)++ = (unsigned char)(val >> 16);
        *(*pos)++ = (unsigned char)(val >> 8);
        *(*pos)++ = (unsigned char)val;
    } else {
        *(*pos)++ = m | 27;
        *(*pos)++ = (unsigned char)(val >> 56);
        *(*pos)++ = (unsigned char)(val >> 48);
        *(*pos)++ = (unsigned char)(val >> 40);
        *(*pos)++ = (unsigned char)(val >> 32);
        *(*pos)++ = (unsigned char)(val >> 24);
        *(*pos)++ = (unsigned char)(val >> 16);
        *(*pos)++ = (unsigned char)(val >> 8);
        *(*pos)++ = (unsigned char)val;
    }
}

static void wf_cbor_ser_write_item(const wf_cbor_item *item,
                                    unsigned char **pos) {
    if (!item) return;

    switch (item->type) {
    case WF_CBOR_UNSIGNED:
        wf_cbor_ser_write_uint(0, item->uinteger, pos);
        break;
    case WF_CBOR_NEGATIVE:
        wf_cbor_ser_write_uint(1, item->neginteger, pos);
        break;
    case WF_CBOR_BYTES:
        wf_cbor_ser_write_uint(2, item->bytes.len, pos);
        if (item->bytes.len > 0) {
            memcpy(*pos, item->bytes.data, item->bytes.len);
            *pos += item->bytes.len;
        }
        break;
    case WF_CBOR_STRING:
        wf_cbor_ser_write_uint(3, item->string.len, pos);
        if (item->string.len > 0) {
            memcpy(*pos, item->string.str, item->string.len);
            *pos += item->string.len;
        }
        break;
    case WF_CBOR_ARRAY:
        wf_cbor_ser_write_uint(4, item->children.count, pos);
        for (size_t i = 0; i < item->children.count; i++)
            wf_cbor_ser_write_item(item->children.items[i], pos);
        break;
    case WF_CBOR_MAP:
        wf_cbor_ser_write_uint(5, item->map.count, pos);
        for (size_t i = 0; i < item->map.count; i++) {
            wf_cbor_ser_write_item(item->map.pairs[i].key, pos);
            wf_cbor_ser_write_item(item->map.pairs[i].value, pos);
        }
        break;
    case WF_CBOR_SIMPLE: {
        int sv = item->simple_value;
        if (sv >= 20 && sv <= 23) {
            *(*pos)++ = (unsigned char)(0xE0 | sv);
        } else if (sv < 256) {
            *(*pos)++ = 0xF8;
            *(*pos)++ = (unsigned char)sv;
        }
        break;
    }
    }
}

unsigned char *wf_cbor_serialize(const wf_cbor_item *item, size_t *out_len) {
    if (!item || !out_len) { if (out_len) *out_len = 0; return NULL; }

    size_t len = wf_cbor_ser_item_size(item);
    unsigned char *buf = malloc(len);
    if (!buf) { *out_len = 0; return NULL; }

    unsigned char *pos = buf;
    wf_cbor_ser_write_item(item, &pos);

    size_t written = (size_t)(pos - buf);
    if (written != len) {
        /* Shouldn't happen, but be safe */
        free(buf);
        *out_len = 0;
        return NULL;
    }

    *out_len = len;
    return buf;
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

wf_status wf_car_write(const wf_car *car,
                        unsigned char **out, size_t *out_len) {
    if (!car || !out || !out_len) return WF_ERR_INVALID_ARG;

    *out = NULL;
    *out_len = 0;

    /* ── Calculate header CBOR size ── */
    size_t hdr_cbor = 0;
    /* map(2) */
    hdr_cbor += 1;
    /* "roots": text(5) */
    hdr_cbor += 1 + 5;
    /* array(root_count) */
    if (car->root_count <= 23) {
        hdr_cbor += 1;
    } else {
        hdr_cbor += 2;  /* 0x98 + count */
    }
    /* tag(42) + bytes(36) + payload for each root */
    hdr_cbor += car->root_count * (4 + 36);
    /* "version": text(7) + unsigned(1) */
    hdr_cbor += 1 + 7 + 1;

    /* ── Calculate header varint size ── */
    uint64_t hv = (uint64_t)hdr_cbor;
    size_t hdr_vint = 0;
    do { hdr_vint++; hv >>= 7; } while (hv > 0);

    /* ── Calculate total size ── */
    size_t total = hdr_vint + hdr_cbor;
    for (size_t i = 0; i < car->block_count; i++) {
        size_t blk_total = 36 + car->blocks[i].data_len;
        uint64_t bv = (uint64_t)blk_total;
        size_t blk_vint = 0;
        do { blk_vint++; bv >>= 7; } while (bv > 0);
        total += blk_vint + blk_total;
    }

    *out = malloc(total);
    if (!*out) return WF_ERR_ALLOC;

    unsigned char *pos = *out;

    /* ── Write header varint ── */
    hv = (uint64_t)hdr_cbor;
    do {
        unsigned char byte = (unsigned char)(hv & 0x7f);
        hv >>= 7;
        if (hv > 0) byte |= 0x80;
        *pos++ = byte;
    } while (hv > 0);

    /* ── Write header CBOR ── */
    *pos++ = 0xA2;                               /* map(2) */
    *pos++ = 0x65;                                /* text(5) */
    memcpy(pos, "roots", 5); pos += 5;

    if (car->root_count <= 23) {
        *pos++ = (unsigned char)(0x80 | car->root_count);
    } else {
        *pos++ = 0x98;
        *pos++ = (unsigned char)car->root_count;
    }

    for (size_t i = 0; i < car->root_count; i++) {
        *pos++ = 0xD8; *pos++ = 0x2A;            /* tag(42) */
        *pos++ = 0x58; *pos++ = 0x24;            /* bytes(36) */
        memcpy(pos, car->roots[i].bytes, 36);
        pos += 36;
    }

    *pos++ = 0x67;                                /* text(7) */
    memcpy(pos, "version", 7); pos += 7;
    *pos++ = 0x01;                                /* unsigned(1) */

    /* ── Write blocks ── */
    for (size_t i = 0; i < car->block_count; i++) {
        size_t blk_total = 36 + car->blocks[i].data_len;
        uint64_t bv = (uint64_t)blk_total;
        do {
            unsigned char byte = (unsigned char)(bv & 0x7f);
            bv >>= 7;
            if (bv > 0) byte |= 0x80;
            *pos++ = byte;
        } while (bv > 0);

        memcpy(pos, car->blocks[i].cid.bytes, 36);
        pos += 36;

        if (car->blocks[i].data_len > 0) {
            memcpy(pos, car->blocks[i].data, car->blocks[i].data_len);
            pos += car->blocks[i].data_len;
        }
    }

    *out_len = total;
    return WF_OK;
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
    } else if (prev_item && !(prev_item->type == WF_CBOR_SIMPLE &&
                              prev_item->simple_value == 22)) {
        wf_cbor_free(obj); return WF_ERR_PARSE;
    }

    wf_cbor_item *sig_item = wf_cbor_map_find(obj, "sig", 3);
    if (sig_item) {
        if (sig_item->type != WF_CBOR_BYTES || sig_item->bytes.len != 64) {
            wf_cbor_free(obj); return WF_ERR_PARSE;
        }
        memcpy(out->sig, sig_item->bytes.data, 64);
        out->sig_len = 64;
    }

    wf_cbor_free(obj);
    return WF_OK;
}

/* ── Commit creation ──────────────────────────────────────── */

/* Forward declarations for CBOR item helpers (defined later) */
static wf_cbor_item *cbor_str(const char *s);
static wf_cbor_item *cbor_bytes(const unsigned char *data, size_t len);
static wf_cbor_item *cbor_uint(uint64_t v);
static wf_cbor_item *cbor_null(void);
static wf_cbor_item *cbor_cid(const wf_cid *cid);

wf_status wf_commit_create(const char *did_str, const char *rev_str,
                            const wf_cid *data_cid,
                            const wf_cid *prev_cid,
                            const wf_signing_key *key,
                            wf_car *car,
                            wf_commit *out) {
    if (!did_str || !rev_str || !data_cid || !key || !car || !out)
        return WF_ERR_INVALID_ARG;

    memset(out, 0, sizeof(*out));
    size_t dlen = strlen(did_str);
    if (dlen >= sizeof(out->did)) return WF_ERR_INVALID_ARG;
    memcpy(out->did, did_str, dlen + 1);
    size_t rlen = strlen(rev_str);
    if (rlen >= sizeof(out->rev)) return WF_ERR_INVALID_ARG;
    memcpy(out->rev, rev_str, rlen + 1);
    out->data = *data_cid;
    if (prev_cid && prev_cid->len > 0) {
        out->prev = *prev_cid;
        out->has_prev = 1;
    }
    out->version = 3;

    /* ── Build CBOR without sig ────────────────────────────── */
    /* Sorted keys: "did"(0x63) < "rev"(0x63) < "data"(0x64) < "prev"(0x64) < "version"(0x67) */

    wf_cbor_item *map = calloc(1, sizeof(*map));
    if (!map) return WF_ERR_ALLOC;
    map->type = WF_CBOR_MAP;
    map->map.count = 5;
    map->map.pairs = calloc(5, sizeof(wf_cbor_pair));
    if (!map->map.pairs) { free(map); return WF_ERR_ALLOC; }

    map->map.pairs[0].key = cbor_str("did");
    map->map.pairs[0].value = cbor_str(did_str);
    map->map.pairs[1].key = cbor_str("rev");
    map->map.pairs[1].value = cbor_str(rev_str);
    map->map.pairs[2].key = cbor_str("data");
    map->map.pairs[2].value = cbor_cid(data_cid);
    map->map.pairs[3].key = cbor_str("prev");
    map->map.pairs[3].value = (prev_cid && prev_cid->len > 0)
                              ? cbor_cid(prev_cid) : cbor_null();
    map->map.pairs[4].key = cbor_str("version");
    map->map.pairs[4].value = cbor_uint(3);

    size_t cbor_len;
    unsigned char *cbor = wf_cbor_serialize(map, &cbor_len);
    if (!cbor) { wf_cbor_free(map); return WF_ERR_ALLOC; }

    /* AT Protocol signs the canonical unsigned commit bytes. */
    wf_status s = wf_sign(key, cbor, cbor_len,
                out->sig, sizeof(out->sig));
    free(cbor);
    if (s != WF_OK) { wf_cbor_free(map); return s; }
    out->sig_len = 64;

    /* ── Rebuild CBOR with sig ─────────────────────────────── */
    /* Insert sig at index 2: "did"(0) < "rev"(1) < "sig"(2) < "data"(3) < "prev"(4) < "version"(5) */
    map->map.count = 6;
    wf_cbor_pair *np = realloc(map->map.pairs, 6 * sizeof(wf_cbor_pair));
    if (!np) { wf_cbor_free(map); return WF_ERR_ALLOC; }
    map->map.pairs = np;
    memmove(&map->map.pairs[3], &map->map.pairs[2],
            3 * sizeof(wf_cbor_pair));
    map->map.pairs[2].key = cbor_str("sig");
    map->map.pairs[2].value = cbor_bytes(out->sig, out->sig_len);

    unsigned char *final_cbor = wf_cbor_serialize(map, &cbor_len);
    wf_cbor_free(map);
    if (!final_cbor) return WF_ERR_ALLOC;

    wf_cid final_cid;
    s = wf_cid_of_block(final_cbor, cbor_len, &final_cid);
    if (s != WF_OK) { free(final_cbor); return s; }

    wf_car_block *blk = realloc(car->blocks,
        (car->block_count + 1) * sizeof(wf_car_block));
    if (!blk) { free(final_cbor); return WF_ERR_ALLOC; }
    car->blocks = blk;
    car->blocks[car->block_count].cid = final_cid;
    car->blocks[car->block_count].data = final_cbor;
    car->blocks[car->block_count].data_len = cbor_len;
    car->block_count++;

    out->cid = final_cid;
    return WF_OK;
}

static int cid_equal(const wf_cid *a, const wf_cid *b) {
    return a && b && a->len == b->len && a->len > 0 &&
           memcmp(a->bytes, b->bytes, a->len) == 0;
}

static wf_status commit_unsigned_bytes(const wf_commit *commit,
                                       unsigned char **out,
                                       size_t *out_len) {
    wf_cbor_item *map = calloc(1, sizeof(*map));
    if (!map) return WF_ERR_ALLOC;
    map->type = WF_CBOR_MAP;
    map->map.count = 5;
    map->map.pairs = calloc(5, sizeof(wf_cbor_pair));
    if (!map->map.pairs) { free(map); return WF_ERR_ALLOC; }
    map->map.pairs[0].key = cbor_str("did");
    map->map.pairs[0].value = cbor_str(commit->did);
    map->map.pairs[1].key = cbor_str("rev");
    map->map.pairs[1].value = cbor_str(commit->rev);
    map->map.pairs[2].key = cbor_str("data");
    map->map.pairs[2].value = cbor_cid(&commit->data);
    map->map.pairs[3].key = cbor_str("prev");
    map->map.pairs[3].value = commit->has_prev
        ? cbor_cid(&commit->prev) : cbor_null();
    map->map.pairs[4].key = cbor_str("version");
    map->map.pairs[4].value = cbor_uint((uint64_t)commit->version);
    *out = wf_cbor_serialize(map, out_len);
    wf_cbor_free(map);
    return *out ? WF_OK : WF_ERR_ALLOC;
}

static wf_status verify_mst_links(const wf_car *car, const wf_cid *cid,
                                  size_t depth) {
    if (depth > car->block_count) return WF_ERR_PARSE;
    wf_car_block *block = wf_car_find_block((wf_car *)car, cid);
    if (!block) return WF_ERR_NOT_FOUND;
    wf_mst_node node;
    wf_status status = wf_mst_node_parse(block->data, block->data_len,
                                         cid, &node);
    if (status != WF_OK) return status;
    if (node.left.len) {
        status = verify_mst_links(car, &node.left, depth + 1);
    }
    for (size_t i = 0; status == WF_OK && i < node.count; i++) {
        if (!wf_car_find_block((wf_car *)car, &node.entries[i].value)) {
            status = WF_ERR_NOT_FOUND;
            break;
        }
        if (node.entries[i].subtree.len) {
            status = verify_mst_links(car, &node.entries[i].subtree, depth + 1);
        }
    }
    wf_mst_node_free(&node);
    return status;
}

wf_status wf_repo_verify(const wf_car *car,
                         const wf_repo_verify_options *options,
                         wf_commit *out_commit) {
    if (!car || !options || !options->expected_did ||
        options->expected_did[0] == '\0' || !options->signing_key ||
        options->signing_key[0] == '\0' || !out_commit) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out_commit, 0, sizeof(*out_commit));
    if (car->root_count != 1 || car->roots[0].len != 36) return WF_ERR_PARSE;

    for (size_t i = 0; i < car->block_count; i++) {
        wf_cid computed;
        if (!car->blocks[i].data || car->blocks[i].data_len == 0 ||
            wf_cid_of_block(car->blocks[i].data, car->blocks[i].data_len,
                            &computed) != WF_OK ||
            !cid_equal(&computed, &car->blocks[i].cid)) return WF_ERR_PARSE;
        for (size_t j = 0; j < i; j++) {
            if (cid_equal(&car->blocks[i].cid, &car->blocks[j].cid))
                return WF_ERR_PARSE;
        }
    }

    wf_car_block *root = wf_car_find_block((wf_car *)car, &car->roots[0]);
    if (!root) return WF_ERR_NOT_FOUND;
    wf_commit commit;
    wf_status status = wf_commit_parse(root->data, root->data_len, &commit);
    if (status != WF_OK || commit.version != 3 || commit.sig_len != 64)
        return WF_ERR_PARSE;
    /* Commit v3 is a closed six-field object. Reject unsigned/extended forms
     * so signature reconstruction cannot ignore attacker-controlled fields. */
    wf_cbor_item *root_object = wf_cbor_parse(root->data, root->data_len);
    if (!root_object || root_object->type != WF_CBOR_MAP ||
        root_object->map.count != 6 ||
        !wf_cbor_map_find(root_object, "prev", 4)) {
        wf_cbor_free(root_object);
        return WF_ERR_PARSE;
    }
    wf_cbor_free(root_object);
    commit.cid = car->roots[0];
    if (strcmp(commit.did, options->expected_did) != 0) return WF_ERR_PARSE;
    if (options->expected_prev &&
        (!commit.has_prev || !cid_equal(&commit.prev, options->expected_prev)))
        return WF_ERR_PARSE;

    unsigned char *unsigned_commit = NULL;
    size_t unsigned_len = 0;
    status = commit_unsigned_bytes(&commit, &unsigned_commit, &unsigned_len);
    if (status != WF_OK) return status;
    status = wf_verify(options->signing_key, unsigned_commit, unsigned_len,
                       commit.sig, commit.sig_len);
    free(unsigned_commit);
    if (status != WF_OK) return status;

    status = verify_mst_links(car, &commit.data, 0);
    if (status != WF_OK) return status;
    *out_commit = commit;
    return WF_OK;
}

wf_status wf_repo_import(const unsigned char *bytes, size_t len,
                         const wf_repo_verify_options *options,
                         wf_car *out_car, wf_commit *out_commit) {
    if (!bytes || len == 0 || !options || !out_car || !out_commit)
        return WF_ERR_INVALID_ARG;
    memset(out_car, 0, sizeof(*out_car));
    memset(out_commit, 0, sizeof(*out_commit));
    wf_status status = wf_car_parse(bytes, len, out_car);
    if (status != WF_OK) return status;
    status = wf_repo_verify(out_car, options, out_commit);
    if (status != WF_OK) {
        wf_car_free(out_car);
        memset(out_commit, 0, sizeof(*out_commit));
    }
    return status;
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

    /* Compute layer from the first leaf key's depth */
    out->layer = (out->count > 0)
        ? wf_mst_key_layer(out->entries[0].key, out->entries[0].key_len)
        : 0;

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

/* ── CBOR item helpers ─────────────────────────────────────── */

static wf_cbor_item *cbor_str(const char *s) {
    wf_cbor_item *item = calloc(1, sizeof(*item));
    if (!item) return NULL;
    item->type = WF_CBOR_STRING;
    if (s) {
        item->string.len = strlen(s);
        item->string.str = malloc(item->string.len);
        if (item->string.str)
            memcpy(item->string.str, s, item->string.len);
    }
    return item;
}

static wf_cbor_item *cbor_bytes(const unsigned char *data, size_t len) {
    wf_cbor_item *item = calloc(1, sizeof(*item));
    if (!item) return NULL;
    item->type = WF_CBOR_BYTES;
    item->bytes.len = len;
    if (len > 0) {
        item->bytes.data = malloc(len);
        if (item->bytes.data)
            memcpy(item->bytes.data, data, len);
    }
    return item;
}

static wf_cbor_item *cbor_uint(uint64_t v) {
    wf_cbor_item *item = calloc(1, sizeof(*item));
    if (!item) return NULL;
    item->type = WF_CBOR_UNSIGNED;
    item->uinteger = v;
    return item;
}

static wf_cbor_item *cbor_null(void) {
    wf_cbor_item *item = calloc(1, sizeof(*item));
    if (!item) return NULL;
    item->type = WF_CBOR_SIMPLE;
    item->simple_value = 22;
    return item;
}

static wf_cbor_item *cbor_cid(const wf_cid *cid) {
    return cbor_bytes(cid->bytes, 36);
}

/* ── MST node build / finalize ─────────────────────────────── */

wf_status wf_mst_node_build(unsigned layer, const wf_cid *left,
                             wf_mst_entry *entries, size_t count,
                             wf_mst_node *out) {
    if (!out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->layer = layer;
    if (left) out->left = *left;
    out->count = count;
    if (count > 0) {
        out->entries = calloc(count, sizeof(wf_mst_entry));
        if (!out->entries) return WF_ERR_ALLOC;
        for (size_t i = 0; i < count; i++) {
            out->entries[i].key = entries[i].key;
            out->entries[i].key_len = entries[i].key_len;
            out->entries[i].value = entries[i].value;
            out->entries[i].subtree = entries[i].subtree;
            entries[i].key = NULL;
        }
    }
    return WF_OK;
}

wf_status wf_mst_node_finalize(wf_mst_node *node, wf_car *car) {
    if (!node || !car) return WF_ERR_INVALID_ARG;
    if (node->cid.len > 0) return WF_OK; /* already finalized */

    /* Build CBOR: {"e": [entries...], "l": <cid|null>}
     * Map keys sorted: "e"(0x65) < "l"(0x6C) */

    unsigned char *prev_key = NULL;
    size_t prev_key_len = 0;
    int err = 0;

    wf_cbor_item *arr = calloc(1, sizeof(*arr));
    if (!arr) return WF_ERR_ALLOC;
    arr->type = WF_CBOR_ARRAY;
    arr->children.count = node->count;
    if (node->count > 0) {
        arr->children.items = calloc(node->count, sizeof(wf_cbor_item *));
        if (!arr->children.items) { free(arr); return WF_ERR_ALLOC; }
    }

    for (size_t i = 0; i < node->count; i++) {
        wf_mst_entry *src = &node->entries[i];
        uint64_t prefix = 0;
        if (prev_key) {
            size_t min = prev_key_len < src->key_len ? prev_key_len : src->key_len;
            while (prefix < min && prev_key[prefix] == src->key[prefix])
                prefix++;
        }

        wf_cbor_item *entry = calloc(1, sizeof(*entry));
        if (!entry) { err = 1; break; }
        entry->type = WF_CBOR_MAP;
        entry->map.count = 4;
        entry->map.pairs = calloc(4, sizeof(wf_cbor_pair));
        if (!entry->map.pairs) { free(entry); err = 1; break; }

        /* "k" — key suffix */
        entry->map.pairs[0].key = cbor_str("k");
        entry->map.pairs[0].value = cbor_bytes(src->key + prefix, src->key_len - (size_t)prefix);
        /* "p" — prefix length */
        entry->map.pairs[1].key = cbor_str("p");
        entry->map.pairs[1].value = cbor_uint(prefix);
        /* "t" — right subtree or null */
        entry->map.pairs[2].key = cbor_str("t");
        entry->map.pairs[2].value = (src->subtree.len > 0)
            ? cbor_cid(&src->subtree) : cbor_null();
        /* "v" — value CID */
        entry->map.pairs[3].key = cbor_str("v");
        entry->map.pairs[3].value = cbor_cid(&src->value);

        free(prev_key);
        prev_key = malloc(src->key_len);
        if (prev_key) {
            memcpy(prev_key, src->key, src->key_len);
            prev_key_len = src->key_len;
        }
        arr->children.items[i] = entry;
    }
    free(prev_key);
    if (err) {
        wf_cbor_free(arr);
        return WF_ERR_ALLOC;
    }

    /* Outer map */
    wf_cbor_item *map = calloc(1, sizeof(*map));
    if (!map) { wf_cbor_free(arr); return WF_ERR_ALLOC; }
    map->type = WF_CBOR_MAP;
    map->map.count = 2;
    map->map.pairs = calloc(2, sizeof(wf_cbor_pair));
    if (!map->map.pairs) { free(map); wf_cbor_free(arr); return WF_ERR_ALLOC; }

    map->map.pairs[0].key = cbor_str("e");
    map->map.pairs[0].value = arr;
    map->map.pairs[1].key = cbor_str("l");
    map->map.pairs[1].value = (node->left.len > 0)
        ? cbor_cid(&node->left) : cbor_null();

    size_t cbor_len;
    unsigned char *cbor = wf_cbor_serialize(map, &cbor_len);
    wf_cbor_free(map);
    if (!cbor) return WF_ERR_ALLOC;

    wf_cid cid;
    wf_status s = wf_cid_of_block(cbor, cbor_len, &cid);
    if (s != WF_OK) { free(cbor); return s; }

    wf_car_block *new_blocks = realloc(car->blocks,
        (car->block_count + 1) * sizeof(wf_car_block));
    if (!new_blocks) { free(cbor); return WF_ERR_ALLOC; }
    car->blocks = new_blocks;
    car->blocks[car->block_count].cid = cid;
    car->blocks[car->block_count].data = cbor;
    car->blocks[car->block_count].data_len = cbor_len;
    car->block_count++;

    node->cid = cid;
    return WF_OK;
}

/* ── MST mutation helpers ──────────────────────────────────── */

/* Find leaf index: first entry where key >= search key, or count */
static size_t mst_find_ge(const wf_mst_node *node,
                           const unsigned char *key, size_t key_len) {
    for (size_t i = 0; i < node->count; i++) {
        if (wf_mst_key_cmp(key, key_len,
                           node->entries[i].key,
                           node->entries[i].key_len) <= 0)
            return i;
    }
    return node->count;
}

/* Load a node from a CAR by CID.  Structural nodes contain only a child
 * pointer, so their layer must be inferred from that child's layer. */
static wf_status mst_load_node_depth(wf_car *car, const wf_cid *cid,
                                      wf_mst_node *out, size_t depth) {
    if (depth > car->block_count) return WF_ERR_PARSE;
    wf_car_block *block = wf_car_find_block(car, cid);
    if (!block) return WF_ERR_PARSE;
    wf_status s = wf_mst_node_parse(block->data, block->data_len, cid, out);
    if (s != WF_OK || out->count > 0 || out->left.len == 0) return s;

    wf_mst_node child = {0};
    s = mst_load_node_depth(car, &out->left, &child, depth + 1);
    if (s == WF_OK) out->layer = child.layer + 1;
    wf_mst_node_free(&child);
    if (s != WF_OK) wf_mst_node_free(out);
    return s;
}

static wf_status mst_load_node(wf_car *car, const wf_cid *cid,
                                wf_mst_node *out) {
    return mst_load_node_depth(car, cid, out, 0);
}

/* Free entries array (keys only, not the array itself) */
static void free_entries(wf_mst_entry *entries, size_t count) {
    for (size_t i = 0; i < count; i++)
        free(entries[i].key);
}

/* Add leaf at current layer (keyZeros == node->layer).
 * Creates a new finalized node and returns its CID. */
static wf_status mst_add_at_layer(wf_car *car, const wf_mst_node *node,
                                   const unsigned char *key, size_t key_len,
                                   const wf_cid *value, wf_cid *new_cid) {
    size_t idx = mst_find_ge(node, key, key_len);

    /* Check for duplicate */
    if (idx < node->count) {
        int cmp = wf_mst_key_cmp(key, key_len,
                                  node->entries[idx].key,
                                  node->entries[idx].key_len);
        if (cmp == 0) {
            *new_cid = node->cid;
            return WF_OK;
        }
    }

    size_t new_count = node->count + 1;
    wf_mst_entry *new_entries = calloc(new_count, sizeof(wf_mst_entry));
    if (!new_entries) return WF_ERR_ALLOC;

    for (size_t i = 0, j = 0; i < node->count; i++, j++) {
        if (j == idx) {
            /* Insert the new entry */
            new_entries[j].key = malloc(key_len);
            if (!new_entries[j].key) { free_entries(new_entries, j); return WF_ERR_ALLOC; }
            memcpy(new_entries[j].key, key, key_len);
            new_entries[j].key_len = key_len;
            new_entries[j].value = *value;
            j++;
        }
        new_entries[j].key = malloc(node->entries[i].key_len);
        if (!new_entries[j].key) { free_entries(new_entries, j); return WF_ERR_ALLOC; }
        memcpy(new_entries[j].key, node->entries[i].key, node->entries[i].key_len);
        new_entries[j].key_len = node->entries[i].key_len;
        new_entries[j].value = node->entries[i].value;
        new_entries[j].subtree = node->entries[i].subtree;
    }
    /* Handle insert at end */
    if (idx == node->count) {
        new_entries[idx].key = malloc(key_len);
        if (!new_entries[idx].key) { free_entries(new_entries, idx); return WF_ERR_ALLOC; }
        memcpy(new_entries[idx].key, key, key_len);
        new_entries[idx].key_len = key_len;
        new_entries[idx].value = *value;
    }

    wf_mst_node new_node;
    wf_status s = wf_mst_node_build(node->layer, &node->left,
                                     new_entries, new_count, &new_node);
    free_entries(new_entries, new_count);
    if (s != WF_OK) return s;

    s = wf_mst_node_finalize(&new_node, car);
    if (s != WF_OK) { wf_mst_node_free(&new_node); return s; }

    *new_cid = new_node.cid;
    wf_mst_node_free(&new_node);
    return WF_OK;
}

static wf_status mst_raise_subtree(wf_car *car, wf_cid *cid,
                                    unsigned child_layer,
                                    unsigned parent_layer);

/* Add key at a lower layer (keyZeros < node->layer).
 * Recurse into the appropriate subtree, creating one if needed. */
static wf_status mst_add_at_lower_layer(wf_car *car, const wf_mst_node *node,
                                         const unsigned char *key,
                                         size_t key_len, const wf_cid *value,
                                         unsigned key_layer, wf_cid *new_cid) {
    (void)key_layer;
    size_t idx = mst_find_ge(node, key, key_len);

    wf_cid new_subtree;
    wf_status s;

    /* Determine which subtree to recurse into:
     *   idx == 0           → left subtree  (node->left)
     *   idx < node->count  → entries[idx-1].subtree
     *   idx == node->count → last entry's subtree (entries[count-1].subtree) */
    int use_left = (idx == 0);
    size_t use_prev = (size_t)-1;
    if (idx > 0) {
        use_left = 0;
        use_prev = (idx > node->count) ? node->count - 1 : idx - 1;
    } else if (idx >= node->count && node->count > 0) {
        use_left = 0;
        use_prev = node->count - 1;
    }

    const wf_cid *target_cid = NULL;
    if (use_left) {
        target_cid = &node->left;
    } else {
        target_cid = &node->entries[use_prev].subtree;
    }

    if (target_cid->len > 0) {
        if (use_left) {
            wf_mst_node child;
            s = mst_load_node(car, target_cid, &child);
            if (s != WF_OK) return s;
            s = wf_mst_add(car, target_cid, key, key_len, value, &new_subtree);
            wf_mst_node_free(&child);
        } else {
            s = wf_mst_add(car, target_cid, key, key_len, value, &new_subtree);
        }
    } else {
        /* Build the leaf tree, then retain every structural layer between it
         * and this node. */
        wf_cid zero = {{0}, 0};
        s = wf_mst_add(car, &zero, key, key_len, value, &new_subtree);
        if (s == WF_OK)
            s = mst_raise_subtree(car, &new_subtree, key_layer, node->layer);
    }
    if (s != WF_OK) return s;

    /* Build new node with updated subtree reference */
    size_t new_count = node->count;
    wf_mst_entry *new_entries = calloc(new_count, sizeof(wf_mst_entry));
    if (!new_entries) return WF_ERR_ALLOC;

    for (size_t i = 0; i < new_count; i++) {
        new_entries[i].key = malloc(node->entries[i].key_len);
        if (!new_entries[i].key) { free_entries(new_entries, i); return WF_ERR_ALLOC; }
        memcpy(new_entries[i].key, node->entries[i].key, node->entries[i].key_len);
        new_entries[i].key_len = node->entries[i].key_len;
        new_entries[i].value = node->entries[i].value;
        new_entries[i].subtree = node->entries[i].subtree;
    }

    wf_mst_node new_node;
    if (use_left) {
        s = wf_mst_node_build(node->layer, &new_subtree,
                               new_entries, new_count, &new_node);
    } else {
        new_entries[use_prev].subtree = new_subtree;
        s = wf_mst_node_build(node->layer, &node->left,
                               new_entries, new_count, &new_node);
    }
    free_entries(new_entries, new_count); free(new_entries);
    if (s != WF_OK) return s;

    s = wf_mst_node_finalize(&new_node, car);
    if (s != WF_OK) { wf_mst_node_free(&new_node); return s; }
    *new_cid = new_node.cid;
    wf_mst_node_free(&new_node);
    return WF_OK;
}

/* Add key at a higher layer (keyZeros > node->layer).
 * Create a new root at key_layer with structural parents. */
static wf_status mst_raise_subtree(wf_car *car, wf_cid *cid,
                                    unsigned child_layer,
                                    unsigned parent_layer) {
    if (cid->len == 0) return WF_OK;
    for (unsigned layer = child_layer + 1; layer < parent_layer; layer++) {
        wf_mst_node structural;
        wf_status s = wf_mst_node_build(layer, cid, NULL, 0, &structural);
        if (s != WF_OK) return s;
        s = wf_mst_node_finalize(&structural, car);
        if (s == WF_OK) *cid = structural.cid;
        wf_mst_node_free(&structural);
        if (s != WF_OK) return s;
    }
    return WF_OK;
}

static wf_status mst_add_at_higher_layer(wf_car *car,
                                          const wf_mst_node *node,
                                          const unsigned char *key,
                                          size_t key_len, const wf_cid *value,
                                          unsigned key_layer,
                                          wf_cid *new_cid) {
    size_t idx = mst_find_ge(node, key, key_len);
    size_t left_count = idx;
    size_t right_count = node->count - idx;

    wf_mst_entry *left_entries = NULL;
    wf_mst_entry *right_entries = NULL;

    /* Deep-copy keys for left entries */
    if (left_count > 0) {
        left_entries = calloc(left_count, sizeof(wf_mst_entry));
        if (!left_entries) return WF_ERR_ALLOC;
        for (size_t i = 0; i < left_count; i++) {
            left_entries[i].key_len = node->entries[i].key_len;
            left_entries[i].key = malloc(left_entries[i].key_len);
            if (!left_entries[i].key) {
                for (size_t j = 0; j < i; j++) free(left_entries[j].key);
                free(left_entries); free(right_entries);
                return WF_ERR_ALLOC;
            }
            memcpy(left_entries[i].key, node->entries[i].key, left_entries[i].key_len);
            left_entries[i].value = node->entries[i].value;
            left_entries[i].subtree = node->entries[i].subtree;
        }
    }
    /* Deep-copy keys for right entries */
    if (right_count > 0) {
        right_entries = calloc(right_count, sizeof(wf_mst_entry));
        if (!right_entries) {
            for (size_t i = 0; i < left_count; i++) free(left_entries[i].key);
            free(left_entries); return WF_ERR_ALLOC;
        }
        for (size_t i = 0; i < right_count; i++) {
            right_entries[i].key_len = node->entries[idx + i].key_len;
            right_entries[i].key = malloc(right_entries[i].key_len);
            if (!right_entries[i].key) {
                for (size_t j = 0; j < i; j++) free(right_entries[j].key);
                for (size_t j = 0; j < left_count; j++) free(left_entries[j].key);
                free(right_entries); free(left_entries);
                return WF_ERR_ALLOC;
            }
            memcpy(right_entries[i].key, node->entries[idx + i].key, right_entries[i].key_len);
            right_entries[i].value = node->entries[idx + i].value;
            right_entries[i].subtree = node->entries[idx + i].subtree;
        }
    }

    /* Create left subtree node at old layer */
    wf_cid left_cid = {{0}, 0};
    if (left_count > 0) {
        wf_mst_node left_node;
        wf_status st = wf_mst_node_build(node->layer, &node->left,
                                          left_entries, left_count, &left_node);
        free(left_entries); left_entries = NULL;
        if (st != WF_OK) {
            if (right_entries) {
                for (size_t i = 0; i < right_count; i++) free(right_entries[i].key);
                free(right_entries);
            }
            return st;
        }
        st = wf_mst_node_finalize(&left_node, car);
        if (st != WF_OK) {
            wf_mst_node_free(&left_node);
            if (right_entries) {
                for (size_t i = 0; i < right_count; i++) free(right_entries[i].key);
                free(right_entries);
            }
            return st;
        }
        left_cid = left_node.cid;
        wf_mst_node_free(&left_node);
        st = mst_raise_subtree(car, &left_cid, node->layer, key_layer);
        if (st != WF_OK) {
            if (right_entries) {
                for (size_t i = 0; i < right_count; i++) free(right_entries[i].key);
                free(right_entries);
            }
            return st;
        }
    }

    /* Create right subtree node at old layer */
    wf_cid right_cid = {{0}, 0};
    if (right_count > 0) {
        wf_mst_node right_node;
        wf_cid right_left = {{0}, 0};
        wf_status st = wf_mst_node_build(node->layer, &right_left,
                                          right_entries, right_count, &right_node);
        free(right_entries); right_entries = NULL;
        if (st != WF_OK) return st;
        st = wf_mst_node_finalize(&right_node, car);
        if (st != WF_OK) { wf_mst_node_free(&right_node); return st; }
        right_cid = right_node.cid;
        wf_mst_node_free(&right_node);
        st = mst_raise_subtree(car, &right_cid, node->layer, key_layer);
        if (st != WF_OK) return st;
    }

    /* Create the new leaf entry for the inserted key */
    wf_mst_entry leaf_entry;
    leaf_entry.key_len = key_len;
    leaf_entry.value = *value;
    leaf_entry.subtree = right_cid;
    leaf_entry.key = malloc(key_len);
    if (!leaf_entry.key) return WF_ERR_ALLOC;
    memcpy(leaf_entry.key, key, key_len);

    /* Build new root at key_layer: one entry with left=left_cid, subtree=right_cid */
    wf_mst_entry mid_entries[1];
    mid_entries[0].key = leaf_entry.key;
    mid_entries[0].key_len = leaf_entry.key_len;
    mid_entries[0].value = leaf_entry.value;
    mid_entries[0].subtree = leaf_entry.subtree;

    wf_mst_node root_node;
    wf_status st = wf_mst_node_build(key_layer, &left_cid,
                                      mid_entries, 1, &root_node);
    if (st != WF_OK) { free(leaf_entry.key); return st; }
    st = wf_mst_node_finalize(&root_node, car);
    if (st != WF_OK) { wf_mst_node_free(&root_node); free(leaf_entry.key); return st; }
    *new_cid = root_node.cid;
    wf_mst_node_free(&root_node);
    return WF_OK;
}

/* ── MST public API ────────────────────────────────────────── */

wf_status wf_mst_add(wf_car *car, const wf_cid *root_cid,
                      const unsigned char *key, size_t key_len,
                      const wf_cid *value, wf_cid *new_root) {
    if (!car || !root_cid || !key || key_len == 0 || !value || !new_root)
        return WF_ERR_INVALID_ARG;

    unsigned key_layer = wf_mst_key_layer(key, key_len);

    /* Handle empty tree */
    if (root_cid->len == 0) {
        wf_mst_entry entry;
        memset(&entry, 0, sizeof(entry));
        entry.key = malloc(key_len);
        if (!entry.key) return WF_ERR_ALLOC;
        memcpy(entry.key, key, key_len);
        entry.key_len = key_len;
        entry.value = *value;

        wf_cid empty_left = {{0}, 0};
        wf_mst_node node;
        wf_status s = wf_mst_node_build(key_layer, &empty_left,
                                         &entry, 1, &node);
        free(entry.key);
        entry.key = NULL;
        if (s != WF_OK) return s;
        s = wf_mst_node_finalize(&node, car);
        if (s != WF_OK) { wf_mst_node_free(&node); return s; }
        *new_root = node.cid;
        wf_mst_node_free(&node);
        return WF_OK;
    }

    wf_mst_node node;
    wf_status s = mst_load_node(car, root_cid, &node);
    if (s != WF_OK) return s;

    if (key_layer == node.layer) {
        s = mst_add_at_layer(car, &node, key, key_len, value, new_root);
    } else if (key_layer > node.layer) {
        s = mst_add_at_higher_layer(car, &node, key, key_len,
                                     value, key_layer, new_root);
    } else {
        s = mst_add_at_lower_layer(car, &node, key, key_len,
                                    value, key_layer, new_root);
    }

    wf_mst_node_free(&node);
    return s;
}

/* ── MST delete (internal helpers) ─────────────────────────── */

/* Merge two adjacent subtrees at the same layer.  In the in-memory model this
 * is appendMerge: concatenate the two nodes and recursively coalesce the
 * neighboring right/left tree pointers at their boundary. */
static wf_status mst_merge_subtrees(wf_car *car, const wf_cid *a,
                                     const wf_cid *b, wf_cid *out) {
    if (a->len == 0) { *out = *b; return WF_OK; }
    if (b->len == 0) { *out = *a; return WF_OK; }

    wf_mst_node left_node, right_node;
    wf_status s = mst_load_node(car, a, &left_node);
    if (s != WF_OK) return s;
    s = mst_load_node(car, b, &right_node);
    if (s != WF_OK) {
        wf_mst_node_free(&left_node);
        return s;
    }
    if (left_node.layer != right_node.layer ||
        (left_node.count > 0 && right_node.count > 0 &&
         wf_mst_key_cmp(left_node.entries[left_node.count - 1].key,
                        left_node.entries[left_node.count - 1].key_len,
                        right_node.entries[0].key,
                        right_node.entries[0].key_len) >= 0)) {
        wf_mst_node_free(&left_node);
        wf_mst_node_free(&right_node);
        return WF_ERR_PARSE;
    }

    size_t count = left_node.count + right_node.count;
    wf_mst_entry *entries = count ? calloc(count, sizeof(*entries)) : NULL;
    if (count && !entries) {
        wf_mst_node_free(&left_node);
        wf_mst_node_free(&right_node);
        return WF_ERR_ALLOC;
    }
    size_t copied = 0;
    for (size_t side = 0; side < 2; side++) {
        const wf_mst_node *node = side == 0 ? &left_node : &right_node;
        for (size_t i = 0; i < node->count; i++, copied++) {
            entries[copied].key = malloc(node->entries[i].key_len);
            if (!entries[copied].key) {
                free_entries(entries, copied);
                free(entries);
                wf_mst_node_free(&left_node);
                wf_mst_node_free(&right_node);
                return WF_ERR_ALLOC;
            }
            memcpy(entries[copied].key, node->entries[i].key,
                   node->entries[i].key_len);
            entries[copied].key_len = node->entries[i].key_len;
            entries[copied].value = node->entries[i].value;
            entries[copied].subtree = node->entries[i].subtree;
        }
    }

    wf_cid new_left = left_node.left;
    wf_cid boundary;
    if (left_node.count > 0) {
        s = mst_merge_subtrees(car,
                               &left_node.entries[left_node.count - 1].subtree,
                               &right_node.left, &boundary);
        if (s == WF_OK) entries[left_node.count - 1].subtree = boundary;
    } else {
        s = mst_merge_subtrees(car, &left_node.left, &right_node.left,
                               &new_left);
    }

    wf_mst_node merged_node;
    if (s == WF_OK)
        s = wf_mst_node_build(left_node.layer, &new_left, entries, count,
                              &merged_node);
    free_entries(entries, count);
    free(entries);
    wf_mst_node_free(&left_node);
    wf_mst_node_free(&right_node);
    if (s != WF_OK) return s;

    s = wf_mst_node_finalize(&merged_node, car);
    if (s == WF_OK) *out = merged_node.cid;
    wf_mst_node_free(&merged_node);
    return s;
}

/* Delete key from a node at the same layer.
 * Returns the new node CID via new_cid (zeroed if node becomes empty). */
static wf_status mst_delete_at_layer(wf_car *car, const wf_mst_node *node,
                                      const unsigned char *key, size_t key_len,
                                      wf_cid *new_cid) {
    size_t idx = mst_find_ge(node, key, key_len);
    if (idx >= node->count) {
        *new_cid = node->cid;
        return WF_OK; /* key not in this node */
    }
    int cmp = wf_mst_key_cmp(key, key_len,
                              node->entries[idx].key,
                              node->entries[idx].key_len);
    if (cmp != 0) {
        *new_cid = node->cid;
        return WF_OK; /* key not in this node */
    }

    size_t new_count = node->count - 1;

    /* Determine which adjacent subtrees need merging */
    wf_cid left_subtree, right_subtree;
    if (idx == 0) {
        left_subtree = node->left;
        right_subtree = node->entries[0].subtree;
    } else {
        left_subtree = node->entries[idx - 1].subtree;
        right_subtree = node->entries[idx].subtree;
    }

    wf_cid merged;
    wf_status s = mst_merge_subtrees(car, &left_subtree, &right_subtree,
                                     &merged);
    if (s != WF_OK) return s;

    if (new_count == 0) {
        *new_cid = merged;
        return WF_OK;
    }

    /* Build new entry list */
    wf_mst_entry *new_entries = calloc(new_count, sizeof(wf_mst_entry));
    if (!new_entries) return WF_ERR_ALLOC;

    for (size_t i = 0, j = 0; i < node->count; i++) {
        if (i == idx) continue;
        new_entries[j].key_len = node->entries[i].key_len;
        new_entries[j].key = malloc(new_entries[j].key_len);
        if (!new_entries[j].key) {
            free_entries(new_entries, j);
            return WF_ERR_ALLOC;
        }
        memcpy(new_entries[j].key, node->entries[i].key, new_entries[j].key_len);
        new_entries[j].value = node->entries[i].value;
        new_entries[j].subtree = node->entries[i].subtree;
        j++;
    }

    /* Place the merged subtree */
    wf_cid new_left = node->left;
    if (idx == 0) {
        new_left = merged;
    } else {
        new_entries[idx - 1].subtree = merged;
    }

    wf_mst_node new_node;
    s = wf_mst_node_build(node->layer, &new_left,
                           new_entries, new_count, &new_node);
    free_entries(new_entries, new_count); free(new_entries);
    if (s != WF_OK) return s;

    s = wf_mst_node_finalize(&new_node, car);
    if (s != WF_OK) { wf_mst_node_free(&new_node); return s; }
    *new_cid = new_node.cid;
    wf_mst_node_free(&new_node);
    return WF_OK;
}

/* Delete key recursively: handles both same-layer and lower-layer cases. */
static wf_status mst_delete_recursive(wf_car *car, const wf_mst_node *node,
                                       const unsigned char *key, size_t key_len,
                                       unsigned key_layer, wf_cid *new_cid) {
    wf_status s;

    if (key_layer == node->layer) {
        /* Try to delete from this node */
        size_t idx = mst_find_ge(node, key, key_len);
        int found_here = 0;
        if (idx < node->count) {
            int cmp = wf_mst_key_cmp(key, key_len,
                                      node->entries[idx].key,
                                      node->entries[idx].key_len);
            found_here = (cmp == 0);
        }

        if (found_here) {
            return mst_delete_at_layer(car, node, key, key_len, new_cid);
        }

        /* Key not in this node — recurse into appropriate subtree */
        wf_cid subtree;
        int use_left = 0;
        size_t sub_idx = 0;

        if (idx == 0) {
            if (node->left.len > 0) {
                use_left = 1;
            } else {
                memset(new_cid, 0, sizeof(*new_cid));
                return WF_ERR_NOT_FOUND;
            }
        } else {
            sub_idx = (idx >= node->count) ? node->count - 1 : idx - 1;
            if (node->entries[sub_idx].subtree.len == 0) {
                memset(new_cid, 0, sizeof(*new_cid));
                return WF_ERR_NOT_FOUND;
            }
        }

        if (use_left) {
            wf_mst_node child;
            s = mst_load_node(car, &node->left, &child);
            if (s != WF_OK) return s;
            s = mst_delete_recursive(car, &child, key, key_len, key_layer, &subtree);
            wf_mst_node_free(&child);
        } else {
            wf_mst_node sub_node;
            s = mst_load_node(car, &node->entries[sub_idx].subtree, &sub_node);
            if (s != WF_OK) return s;
            s = mst_delete_recursive(car, &sub_node, key, key_len, key_layer, &subtree);
            wf_mst_node_free(&sub_node);
        }
        if (s != WF_OK) return s;

        /* Re-build node with updated subtree reference */
        size_t new_count = node->count;
        wf_mst_entry *new_entries = calloc(new_count, sizeof(wf_mst_entry));
        if (!new_entries) return WF_ERR_ALLOC;

        for (size_t i = 0; i < new_count; i++) {
            new_entries[i].key_len = node->entries[i].key_len;
            new_entries[i].key = malloc(new_entries[i].key_len);
            if (!new_entries[i].key) {
                free_entries(new_entries, i);
                return WF_ERR_ALLOC;
            }
            memcpy(new_entries[i].key, node->entries[i].key, new_entries[i].key_len);
            new_entries[i].value = node->entries[i].value;
            new_entries[i].subtree = node->entries[i].subtree;
        }

        wf_mst_node new_node;
        if (use_left) {
            s = wf_mst_node_build(node->layer, &subtree,
                                   new_entries, new_count, &new_node);
        } else {
            new_entries[sub_idx].subtree = subtree;
            s = wf_mst_node_build(node->layer, &node->left,
                                   new_entries, new_count, &new_node);
        }
        free_entries(new_entries, new_count); free(new_entries);
        if (s != WF_OK) return s;

        s = wf_mst_node_finalize(&new_node, car);
        if (s != WF_OK) { wf_mst_node_free(&new_node); return s; }
        *new_cid = new_node.cid;
        wf_mst_node_free(&new_node);
        return WF_OK;
    }

    if (key_layer > node->layer) {
        memset(new_cid, 0, sizeof(*new_cid));
        return WF_ERR_NOT_FOUND;
    }

    /* key_layer < node.layer — recurse into subtrees */
    size_t idx = mst_find_ge(node, key, key_len);
    int use_left = (idx == 0);
    size_t use_prev = (size_t)-1;
    if (idx > 0) {
        use_prev = (idx > node->count) ? node->count - 1 : idx - 1;
        use_left = 0;
    } else if (idx >= node->count && node->count > 0) {
        use_prev = node->count - 1;
        use_left = 0;
    }

    const wf_cid *target = use_left ? &node->left
                                    : &node->entries[use_prev].subtree;
    if (target->len == 0) {
        memset(new_cid, 0, sizeof(*new_cid));
        return WF_ERR_NOT_FOUND;
    }

    {
        wf_mst_node sub_node;
        s = mst_load_node(car, target, &sub_node);
        if (s != WF_OK) return s;
        s = mst_delete_recursive(car, &sub_node, key, key_len, key_layer, new_cid);
        wf_mst_node_free(&sub_node);
    }
    if (s != WF_OK) return s;

    /* Build updated node */
    size_t new_count = node->count;
    wf_mst_entry *new_entries = calloc(new_count, sizeof(wf_mst_entry));
    if (!new_entries) return WF_ERR_ALLOC;

    for (size_t i = 0; i < new_count; i++) {
        new_entries[i].key_len = node->entries[i].key_len;
        new_entries[i].key = malloc(new_entries[i].key_len);
        if (!new_entries[i].key) {
            free_entries(new_entries, i);
            return WF_ERR_ALLOC;
        }
        memcpy(new_entries[i].key, node->entries[i].key, new_entries[i].key_len);
        new_entries[i].value = node->entries[i].value;
        new_entries[i].subtree = node->entries[i].subtree;
    }

    wf_mst_node new_node;
    if (use_left) {
        s = wf_mst_node_build(node->layer, new_cid,
                               new_entries, new_count, &new_node);
    } else {
        new_entries[use_prev].subtree = *new_cid;
        s = wf_mst_node_build(node->layer, &node->left,
                               new_entries, new_count, &new_node);
    }
    free_entries(new_entries, new_count); free(new_entries);
    if (s != WF_OK) return s;

    s = wf_mst_node_finalize(&new_node, car);
    if (s != WF_OK) { wf_mst_node_free(&new_node); return s; }
    *new_cid = new_node.cid;
    wf_mst_node_free(&new_node);
    return WF_OK;
}

wf_status wf_mst_delete(wf_car *car, const wf_cid *root_cid,
                         const unsigned char *key, size_t key_len,
                         wf_cid *new_root) {
    if (!car || !root_cid || !key || key_len == 0 || !new_root)
        return WF_ERR_INVALID_ARG;

    memset(new_root, 0, sizeof(*new_root));

    if (root_cid->len == 0)
        return WF_ERR_NOT_FOUND;

    unsigned key_layer = wf_mst_key_layer(key, key_len);

    wf_mst_node node;
    wf_status s = mst_load_node(car, root_cid, &node);
    if (s != WF_OK) return s;

    s = mst_delete_recursive(car, &node, key, key_len, key_layer, new_root);
    wf_mst_node_free(&node);
    if (s != WF_OK) return s;

    /* A deletion may leave one or more structural nodes above the highest
     * surviving leaf.  They are top-only scaffolding and must be trimmed. */
    while (new_root->len > 0) {
        wf_mst_node top;
        s = mst_load_node(car, new_root, &top);
        if (s != WF_OK) return s;
        if (top.count != 0) {
            wf_mst_node_free(&top);
            break;
        }
        *new_root = top.left;
        wf_mst_node_free(&top);
    }
    return s;
}

/* ── Record operations ────────────────────────────────────── */

/* Internal ordered snapshots used to compare two verified MST roots. */
typedef struct repo_leaf {
    unsigned char *key;
    size_t key_len;
    wf_cid cid;
} repo_leaf;

typedef struct repo_leaf_list {
    repo_leaf *items;
    size_t count;
} repo_leaf_list;

typedef struct repo_cid_list {
    wf_cid *items;
    size_t count;
} repo_cid_list;

static void repo_leaf_list_free(repo_leaf_list *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) free(list->items[i].key);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

static int repo_cid_list_has(const repo_cid_list *list, const wf_cid *cid) {
    for (size_t i = 0; list && i < list->count; i++)
        if (cid_equal(&list->items[i], cid)) return 1;
    return 0;
}

static wf_status repo_cid_list_add(repo_cid_list *list, const wf_cid *cid) {
    if (repo_cid_list_has(list, cid)) return WF_OK;
    wf_cid *items = realloc(list->items, (list->count + 1) * sizeof(*items));
    if (!items) return WF_ERR_ALLOC;
    list->items = items;
    list->items[list->count++] = *cid;
    return WF_OK;
}

static wf_status repo_leaf_add(repo_leaf_list *list,
                               const unsigned char *key, size_t key_len,
                               const wf_cid *cid) {
    if (key_len == 0 || !cid || cid->len != 36) return WF_ERR_PARSE;
    if (list->count > 0) {
        repo_leaf *previous = &list->items[list->count - 1];
        if (wf_mst_key_cmp(previous->key, previous->key_len,
                           key, key_len) >= 0) return WF_ERR_PARSE;
    }
    repo_leaf *items = realloc(list->items,
                              (list->count + 1) * sizeof(*items));
    if (!items) return WF_ERR_ALLOC;
    list->items = items;
    repo_leaf *leaf = &list->items[list->count];
    memset(leaf, 0, sizeof(*leaf));
    leaf->key = malloc(key_len);
    if (!leaf->key) return WF_ERR_ALLOC;
    memcpy(leaf->key, key, key_len);
    leaf->key_len = key_len;
    leaf->cid = *cid;
    list->count++;
    return WF_OK;
}

static wf_status repo_collect_mst(const wf_car *car, const wf_cid *root,
                                  repo_leaf_list *leaves,
                                  repo_cid_list *nodes, size_t depth) {
    if (!car || !root || root->len != 36 || depth > car->block_count)
        return WF_ERR_PARSE;
    if (repo_cid_list_has(nodes, root)) return WF_ERR_PARSE;
    wf_status status = repo_cid_list_add(nodes, root);
    if (status != WF_OK) return status;
    wf_car_block *block = wf_car_find_block((wf_car *)car, root);
    if (!block) return WF_ERR_NOT_FOUND;
    wf_mst_node node = {0};
    status = wf_mst_node_parse(block->data, block->data_len, root, &node);
    if (status != WF_OK) return status;
    if (node.left.len)
        status = repo_collect_mst(car, &node.left, leaves, nodes, depth + 1);
    for (size_t i = 0; status == WF_OK && i < node.count; i++) {
        if (!wf_car_find_block((wf_car *)car, &node.entries[i].value)) {
            status = WF_ERR_NOT_FOUND;
            break;
        }
        status = repo_leaf_add(leaves, node.entries[i].key,
                               node.entries[i].key_len,
                               &node.entries[i].value);
        if (status == WF_OK && node.entries[i].subtree.len)
            status = repo_collect_mst(car, &node.entries[i].subtree,
                                      leaves, nodes, depth + 1);
    }
    wf_mst_node_free(&node);
    return status;
}

static wf_status repo_car_validate_blocks(const wf_car *car) {
    if (!car) return WF_ERR_INVALID_ARG;
    for (size_t i = 0; i < car->block_count; i++) {
        wf_cid computed = {0};
        if (!car->blocks[i].data || car->blocks[i].data_len == 0 ||
            wf_cid_of_block(car->blocks[i].data, car->blocks[i].data_len,
                            &computed) != WF_OK ||
            !cid_equal(&computed, &car->blocks[i].cid)) return WF_ERR_PARSE;
        for (size_t j = 0; j < i; j++)
            if (cid_equal(&car->blocks[i].cid, &car->blocks[j].cid))
                return WF_ERR_PARSE;
    }
    return WF_OK;
}

static wf_status repo_car_add_copy(wf_car *car, const wf_car_block *block) {
    wf_car_block *existing = wf_car_find_block(car, &block->cid);
    if (existing) {
        return existing->data_len == block->data_len &&
               memcmp(existing->data, block->data, block->data_len) == 0
            ? WF_OK : WF_ERR_PARSE;
    }
    wf_car_block *blocks = realloc(car->blocks,
        (car->block_count + 1) * sizeof(*blocks));
    if (!blocks) return WF_ERR_ALLOC;
    car->blocks = blocks;
    wf_car_block *copy = &car->blocks[car->block_count];
    memset(copy, 0, sizeof(*copy));
    copy->data = malloc(block->data_len);
    if (!copy->data) return WF_ERR_ALLOC;
    memcpy(copy->data, block->data, block->data_len);
    copy->data_len = block->data_len;
    copy->cid = block->cid;
    car->block_count++;
    return WF_OK;
}

static wf_status repo_car_add_cid_copy(wf_car *destination,
                                       const wf_car *source,
                                       const wf_cid *cid) {
    wf_car_block *block = wf_car_find_block((wf_car *)source, cid);
    return block ? repo_car_add_copy(destination, block) : WF_ERR_NOT_FOUND;
}

static wf_status repo_composite_car(const wf_car *base, const wf_car *update,
                                    wf_car *out) {
    memset(out, 0, sizeof(*out));
    out->roots = update->roots;
    out->root_count = update->root_count;
    size_t capacity = base->block_count + update->block_count;
    out->blocks = capacity ? calloc(capacity, sizeof(*out->blocks)) : NULL;
    if (capacity && !out->blocks) return WF_ERR_ALLOC;
    for (size_t pass = 0; pass < 2; pass++) {
        const wf_car *source = pass == 0 ? update : base;
        for (size_t i = 0; i < source->block_count; i++) {
            wf_car_block *present = wf_car_find_block(out,
                                                       &source->blocks[i].cid);
            if (present) {
                if (present->data_len != source->blocks[i].data_len ||
                    memcmp(present->data, source->blocks[i].data,
                           present->data_len) != 0) {
                    free(out->blocks);
                    memset(out, 0, sizeof(*out));
                    return WF_ERR_PARSE;
                }
                continue;
            }
            out->blocks[out->block_count++] = source->blocks[i];
        }
    }
    return WF_OK;
}

void wf_repo_operations_free(wf_repo_operation *operations, size_t count) {
    if (!operations) return;
    for (size_t i = 0; i < count; i++) {
        free(operations[i].collection);
        free(operations[i].rkey);
    }
    free(operations);
}

static char *repo_string_copy(const char *value) {
    size_t len = strlen(value);
    char *copy = malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, value, len + 1);
    return copy;
}

wf_status wf_repo_operations_invert(const wf_repo_operation *operations,
                                    size_t count,
                                    wf_repo_operation **out_operations) {
    if ((!operations && count > 0) || !out_operations)
        return WF_ERR_INVALID_ARG;
    *out_operations = NULL;
    if (count == 0) return WF_OK;
    wf_repo_operation *inverted = calloc(count, sizeof(*inverted));
    if (!inverted) return WF_ERR_ALLOC;
    for (size_t i = 0; i < count; i++) {
        const wf_repo_operation *source = &operations[count - i - 1];
        wf_repo_operation *target = &inverted[i];
        if (!source->collection || !source->collection[0] ||
            !source->rkey || !source->rkey[0] || source->cid.len != 36 ||
            (source->action == WF_REPO_UPDATE && source->prev.len != 36) ||
            source->action < WF_REPO_CREATE ||
            source->action > WF_REPO_DELETE) {
            wf_repo_operations_free(inverted, count);
            return WF_ERR_INVALID_ARG;
        }
        target->collection = repo_string_copy(source->collection);
        target->rkey = repo_string_copy(source->rkey);
        if (!target->collection || !target->rkey) {
            wf_repo_operations_free(inverted, count);
            return WF_ERR_ALLOC;
        }
        if (source->action == WF_REPO_CREATE) {
            target->action = WF_REPO_DELETE;
            target->cid = source->cid;
        } else if (source->action == WF_REPO_DELETE) {
            target->action = WF_REPO_CREATE;
            target->cid = source->cid;
        } else {
            target->action = WF_REPO_UPDATE;
            target->cid = source->prev;
            target->prev = source->cid;
        }
    }
    *out_operations = inverted;
    return WF_OK;
}

void wf_repo_diff_free(wf_repo_diff *diff) {
    if (!diff) return;
    wf_repo_operations_free(diff->operations, diff->operation_count);
    wf_car_free(&diff->new_blocks);
    free(diff->removed_cids);
    memset(diff, 0, sizeof(*diff));
}

static wf_status repo_operation_from_leaf(wf_repo_operation *operation,
                                          wf_repo_operation_action action,
                                          const repo_leaf *leaf) {
    const unsigned char *slash = memchr(leaf->key, '/', leaf->key_len);
    if (!slash || slash == leaf->key || slash == leaf->key + leaf->key_len - 1)
        return WF_ERR_PARSE;
    size_t collection_len = (size_t)(slash - leaf->key);
    size_t rkey_len = leaf->key_len - collection_len - 1;
    if (memchr(slash + 1, '/', rkey_len) ||
        memchr(leaf->key, '\0', leaf->key_len)) return WF_ERR_PARSE;
    operation->collection = malloc(collection_len + 1);
    operation->rkey = malloc(rkey_len + 1);
    if (!operation->collection || !operation->rkey) {
        free(operation->collection);
        free(operation->rkey);
        operation->collection = NULL;
        operation->rkey = NULL;
        return WF_ERR_ALLOC;
    }
    memcpy(operation->collection, leaf->key, collection_len);
    operation->collection[collection_len] = '\0';
    memcpy(operation->rkey, slash + 1, rkey_len);
    operation->rkey[rkey_len] = '\0';
    operation->action = action;
    operation->cid = leaf->cid;
    return WF_OK;
}

static wf_status repo_diff_operations(const repo_leaf_list *previous,
                                      const repo_leaf_list *current,
                                      wf_repo_diff *out) {
    size_t capacity = previous->count + current->count;
    out->operations = capacity ? calloc(capacity, sizeof(*out->operations))
                               : NULL;
    if (capacity && !out->operations) return WF_ERR_ALLOC;
    size_t old_index = 0, new_index = 0;
    while (old_index < previous->count || new_index < current->count) {
        int comparison;
        if (old_index == previous->count) comparison = 1;
        else if (new_index == current->count) comparison = -1;
        else comparison = wf_mst_key_cmp(previous->items[old_index].key,
                                          previous->items[old_index].key_len,
                                          current->items[new_index].key,
                                          current->items[new_index].key_len);
        wf_repo_operation *operation = &out->operations[out->operation_count];
        wf_status status;
        if (comparison < 0) {
            status = repo_operation_from_leaf(operation, WF_REPO_DELETE,
                                              &previous->items[old_index++]);
        } else if (comparison > 0) {
            status = repo_operation_from_leaf(operation, WF_REPO_CREATE,
                                              &current->items[new_index++]);
        } else {
            const repo_leaf *old_leaf = &previous->items[old_index++];
            const repo_leaf *new_leaf = &current->items[new_index++];
            if (cid_equal(&old_leaf->cid, &new_leaf->cid)) continue;
            status = repo_operation_from_leaf(operation, WF_REPO_UPDATE,
                                              new_leaf);
            if (status == WF_OK) operation->prev = old_leaf->cid;
        }
        if (status != WF_OK) return status;
        out->operation_count++;
    }
    return WF_OK;
}

static int repo_leaves_have_cid(const repo_leaf_list *leaves,
                                const wf_cid *cid) {
    for (size_t i = 0; i < leaves->count; i++)
        if (cid_equal(&leaves->items[i].cid, cid)) return 1;
    return 0;
}

static wf_status repo_diff_add_removed(wf_repo_diff *diff,
                                       const wf_cid *cid) {
    repo_cid_list view = {diff->removed_cids, diff->removed_count};
    if (repo_cid_list_has(&view, cid)) return WF_OK;
    wf_cid *items = realloc(diff->removed_cids,
                            (diff->removed_count + 1) * sizeof(*items));
    if (!items) return WF_ERR_ALLOC;
    diff->removed_cids = items;
    diff->removed_cids[diff->removed_count++] = *cid;
    return WF_OK;
}

wf_status wf_repo_diff_verify(const wf_car *base,
                              const wf_cid *base_commit,
                              const wf_car *update,
                              const wf_repo_verify_options *options,
                              wf_repo_diff *out) {
    if (!base || !base_commit || base_commit->len != 36 || !update ||
        !options || !out || !options->expected_did ||
        !options->signing_key) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (base->root_count != 1 || !cid_equal(&base->roots[0], base_commit) ||
        update->root_count != 1 || update->roots[0].len != 36)
        return WF_ERR_PARSE;

    wf_repo_verify_options base_options = *options;
    base_options.expected_prev = NULL;
    wf_commit previous_commit = {0};
    wf_status status = wf_repo_verify(base, &base_options, &previous_commit);
    if (status != WF_OK) return status;
    status = repo_car_validate_blocks(update);
    if (status != WF_OK) return status;

    wf_car composite = {0};
    status = repo_composite_car(base, update, &composite);
    if (status != WF_OK) return status;
    wf_commit current_commit = {0};
    status = wf_repo_verify(&composite, options, &current_commit);
    if (status != WF_OK) {
        free(composite.blocks);
        return status;
    }

    repo_leaf_list previous_leaves = {0}, current_leaves = {0};
    repo_cid_list previous_nodes = {0}, current_nodes = {0};
    status = repo_collect_mst(&composite, &previous_commit.data,
                              &previous_leaves, &previous_nodes, 0);
    if (status == WF_OK)
        status = repo_collect_mst(&composite, &current_commit.data,
                                  &current_leaves, &current_nodes, 0);
    if (status == WF_OK)
        status = repo_diff_operations(&previous_leaves, &current_leaves, out);
    if (status != WF_OK) goto cleanup_diff;

    out->commit = current_commit;
    out->previous_commit = *base_commit;
    memcpy(out->since, previous_commit.rev, sizeof(out->since));
    out->new_blocks.roots = malloc(sizeof(wf_cid));
    if (!out->new_blocks.roots) { status = WF_ERR_ALLOC; goto cleanup_diff; }
    out->new_blocks.roots[0] = current_commit.cid;
    out->new_blocks.root_count = 1;

    for (size_t i = 0; i < current_nodes.count; i++) {
        if (!repo_cid_list_has(&previous_nodes, &current_nodes.items[i])) {
            status = repo_car_add_cid_copy(&out->new_blocks, &composite,
                                           &current_nodes.items[i]);
            if (status != WF_OK) goto cleanup_diff;
        }
    }
    for (size_t i = 0; i < out->operation_count; i++) {
        wf_repo_operation *operation = &out->operations[i];
        if (operation->action == WF_REPO_DELETE) continue;
        wf_car_block *leaf = wf_car_find_block((wf_car *)update,
                                                &operation->cid);
        if (!leaf) { status = WF_ERR_NOT_FOUND; goto cleanup_diff; }
        status = repo_car_add_copy(&out->new_blocks, leaf);
        if (status != WF_OK) goto cleanup_diff;
    }
    if (!cid_equal(&current_commit.cid, base_commit)) {
        status = repo_car_add_cid_copy(&out->new_blocks, &composite,
                                       &current_commit.cid);
        if (status != WF_OK) goto cleanup_diff;
    }
    for (size_t i = 0; i < previous_nodes.count; i++) {
        if (!repo_cid_list_has(&current_nodes, &previous_nodes.items[i])) {
            status = repo_diff_add_removed(out, &previous_nodes.items[i]);
            if (status != WF_OK) goto cleanup_diff;
        }
    }
    for (size_t i = 0; i < out->operation_count; i++) {
        wf_repo_operation *operation = &out->operations[i];
        const wf_cid *old_cid = operation->action == WF_REPO_UPDATE
            ? &operation->prev : &operation->cid;
        if (operation->action != WF_REPO_CREATE &&
            !repo_leaves_have_cid(&current_leaves, old_cid)) {
            status = repo_diff_add_removed(out, old_cid);
            if (status != WF_OK) goto cleanup_diff;
        }
    }
    if (!cid_equal(base_commit, &current_commit.cid))
        status = repo_diff_add_removed(out, base_commit);

cleanup_diff:
    free(composite.blocks);
    repo_leaf_list_free(&previous_leaves);
    repo_leaf_list_free(&current_leaves);
    free(previous_nodes.items);
    free(current_nodes.items);
    if (status != WF_OK) wf_repo_diff_free(out);
    return status;
}

static int repo_removed_has(const wf_repo_diff *diff, const wf_cid *cid) {
    for (size_t i = 0; i < diff->removed_count; i++)
        if (cid_equal(&diff->removed_cids[i], cid)) return 1;
    return 0;
}

wf_status wf_repo_diff_apply(wf_car *repo, const wf_repo_diff *diff) {
    if (!repo || !diff || (diff->operation_count && !diff->operations) ||
        (diff->removed_count && !diff->removed_cids) ||
        diff->previous_commit.len != 36 || diff->commit.cid.len != 36 ||
        repo->root_count != 1 ||
        !cid_equal(&repo->roots[0], &diff->previous_commit) ||
        diff->new_blocks.root_count != 1 ||
        !cid_equal(&diff->new_blocks.roots[0], &diff->commit.cid))
        return WF_ERR_INVALID_ARG;

    wf_status status = repo_car_validate_blocks(&diff->new_blocks);
    if (status != WF_OK) return status;
    wf_car staged = {0};
    staged.roots = malloc(sizeof(wf_cid));
    if (!staged.roots) return WF_ERR_ALLOC;
    staged.roots[0] = diff->commit.cid;
    staged.root_count = 1;
    for (size_t i = 0; i < repo->block_count; i++) {
        if (repo_removed_has(diff, &repo->blocks[i].cid)) continue;
        status = repo_car_add_copy(&staged, &repo->blocks[i]);
        if (status != WF_OK) goto apply_fail;
    }
    for (size_t i = 0; i < diff->new_blocks.block_count; i++) {
        status = repo_car_add_copy(&staged, &diff->new_blocks.blocks[i]);
        if (status != WF_OK) goto apply_fail;
    }
    wf_car_block *commit_block = wf_car_find_block(&staged, &diff->commit.cid);
    wf_commit parsed = {0};
    if (!commit_block) { status = WF_ERR_NOT_FOUND; goto apply_fail; }
    status = wf_commit_parse(commit_block->data, commit_block->data_len,
                             &parsed);
    if (status != WF_OK || strcmp(parsed.did, diff->commit.did) != 0 ||
        strcmp(parsed.rev, diff->commit.rev) != 0 ||
        parsed.version != diff->commit.version ||
        parsed.has_prev != diff->commit.has_prev ||
        (parsed.has_prev && !cid_equal(&parsed.prev, &diff->commit.prev)) ||
        parsed.sig_len != diff->commit.sig_len ||
        memcmp(parsed.sig, diff->commit.sig, parsed.sig_len) != 0 ||
        !cid_equal(&parsed.data, &diff->commit.data)) {
        status = WF_ERR_PARSE;
        goto apply_fail;
    }
    status = verify_mst_links(&staged, &parsed.data, 0);
    if (status != WF_OK) goto apply_fail;
    wf_car_free(repo);
    *repo = staged;
    return WF_OK;

apply_fail:
    wf_car_free(&staged);
    return status;
}

wf_status wf_repo_create_record(wf_car *car,
                                 const wf_cid *prev_commit,
                                 const char *did,
                                 const char *collection,
                                 const char *rkey,
                                 const unsigned char *record_cbor,
                                 size_t record_cbor_len,
                                 const wf_signing_key *key,
                                 wf_cid *out_commit,
                                 wf_cid *out_record) {
    if (!car || !did || !collection || !rkey || !record_cbor ||
        record_cbor_len == 0 || !key || !out_commit || !out_record) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out_commit, 0, sizeof(*out_commit));
    memset(out_record, 0, sizeof(*out_record));

    /* Compute record CID */
    wf_status s = wf_cid_of_block(record_cbor, record_cbor_len, out_record);
    if (s != WF_OK) return s;

    /* Add record block to CAR */
    {
        wf_car_block *new_blocks = realloc(car->blocks,
            (car->block_count + 1) * sizeof(wf_car_block));
        if (!new_blocks) return WF_ERR_ALLOC;
        car->blocks = new_blocks;
        wf_car_block *blk = &car->blocks[car->block_count];
        blk->cid = *out_record;
        blk->data_len = record_cbor_len;
        blk->data = malloc(record_cbor_len);
        if (!blk->data) return WF_ERR_ALLOC;
        memcpy(blk->data, record_cbor, record_cbor_len);
        car->block_count++;
    }

    /* Build MST root CID from previous commit if available */
    wf_cid mst_root = {{0}, 0};
    if (prev_commit && prev_commit->len > 0) {
        wf_car_block *block = wf_car_find_block(car, prev_commit);
        if (!block) return WF_ERR_PARSE;
        wf_commit commit;
        memset(&commit, 0, sizeof(commit));
        s = wf_commit_parse(block->data, block->data_len, &commit);
        if (s != WF_OK) return s;
        mst_root = commit.data;
    }

    /* Build MST key: "collection/rkey" */
    size_t col_len = strlen(collection);
    size_t rkey_len = strlen(rkey);
    size_t key_len = col_len + 1 + rkey_len;
    unsigned char *mst_key = malloc(key_len);
    if (!mst_key) return WF_ERR_ALLOC;
    memcpy(mst_key, collection, col_len);
    mst_key[col_len] = '/';
    memcpy(mst_key + col_len + 1, rkey, rkey_len);

    /* Add record to MST */
    wf_cid new_mst_root = {{0}, 0};
    s = wf_mst_add(car, &mst_root, mst_key, key_len, out_record, &new_mst_root);
    free(mst_key);
    if (s != WF_OK) return s;

    /* Create signed commit */
    wf_commit commit;
    memset(&commit, 0, sizeof(commit));
    char rev[64];
    snprintf(rev, sizeof(rev), "3z%08x", (unsigned)time(NULL));
    s = wf_commit_create(did, rev, &new_mst_root,
                          (prev_commit && prev_commit->len > 0) ? prev_commit : NULL,
                          key, car, &commit);
    if (s != WF_OK) return s;

    *out_commit = commit.cid;
    return WF_OK;
}

wf_status wf_repo_get_record(wf_car *car,
                              const wf_cid *commit_cid,
                              const char *collection,
                              const char *rkey,
                              unsigned char **out_data,
                              size_t *out_len,
                              wf_cid *out_record_cid) {
    if (!car || !commit_cid || !collection || !rkey ||
        !out_data || !out_len || !out_record_cid) {
        return WF_ERR_INVALID_ARG;
    }

    *out_data = NULL;
    *out_len = 0;
    memset(out_record_cid, 0, sizeof(*out_record_cid));

    /* Parse commit to get MST root */
    wf_car_block *block = wf_car_find_block(car, commit_cid);
    if (!block) return WF_ERR_PARSE;

    wf_commit commit;
    memset(&commit, 0, sizeof(commit));
    wf_status s = wf_commit_parse(block->data, block->data_len, &commit);
    if (s != WF_OK) return s;

    /* Build MST key */
    size_t col_len = strlen(collection);
    size_t rkey_len = strlen(rkey);
    size_t key_len = col_len + 1 + rkey_len;
    unsigned char *mst_key = malloc(key_len);
    if (!mst_key) return WF_ERR_ALLOC;
    memcpy(mst_key, collection, col_len);
    mst_key[col_len] = '/';
    memcpy(mst_key + col_len + 1, rkey, rkey_len);

    /* Find in MST */
    wf_cid found;
    memset(&found, 0, sizeof(found));
    s = wf_mst_find(car, &commit.data, mst_key, key_len, &found);
    free(mst_key);
    if (s != WF_OK) return s;

    /* Get block data */
    wf_car_block *rec_block = wf_car_find_block(car, &found);
    if (!rec_block) return WF_ERR_PARSE;

    *out_data = malloc(rec_block->data_len);
    if (!*out_data && rec_block->data_len > 0) return WF_ERR_ALLOC;
    if (rec_block->data_len > 0)
        memcpy(*out_data, rec_block->data, rec_block->data_len);
    *out_len = rec_block->data_len;
    *out_record_cid = found;

    return WF_OK;
}

/* Replace an existing leaf value while preserving the MST shape. */
static wf_status mst_update_value(wf_car *car, const wf_cid *node_cid,
                                   const unsigned char *key, size_t key_len,
                                   const wf_cid *value, wf_cid *new_cid) {
    wf_mst_node node;
    wf_status s = mst_load_node(car, node_cid, &node);
    if (s != WF_OK) return s;

    size_t idx = mst_find_ge(&node, key, key_len);
    int found = idx < node.count &&
        wf_mst_key_cmp(key, key_len, node.entries[idx].key,
                       node.entries[idx].key_len) == 0;
    int use_left = idx == 0;
    size_t parent_idx = idx > 0 ? idx - 1 : 0;
    wf_cid updated_child = {{0}, 0};

    if (!found) {
        const wf_cid *child = use_left ? &node.left
                                       : &node.entries[parent_idx].subtree;
        if (child->len == 0) {
            wf_mst_node_free(&node);
            return WF_ERR_NOT_FOUND;
        }
        s = mst_update_value(car, child, key, key_len, value, &updated_child);
        if (s != WF_OK) {
            wf_mst_node_free(&node);
            return s;
        }
    }

    wf_mst_entry *entries = calloc(node.count, sizeof(*entries));
    if (!entries) {
        wf_mst_node_free(&node);
        return WF_ERR_ALLOC;
    }
    for (size_t i = 0; i < node.count; i++) {
        entries[i].key = malloc(node.entries[i].key_len);
        if (!entries[i].key) {
            free_entries(entries, i);
            free(entries);
            wf_mst_node_free(&node);
            return WF_ERR_ALLOC;
        }
        memcpy(entries[i].key, node.entries[i].key, node.entries[i].key_len);
        entries[i].key_len = node.entries[i].key_len;
        entries[i].value = node.entries[i].value;
        entries[i].subtree = node.entries[i].subtree;
    }

    wf_cid left = node.left;
    if (found)
        entries[idx].value = *value;
    else if (use_left)
        left = updated_child;
    else
        entries[parent_idx].subtree = updated_child;

    wf_mst_node replacement;
    s = wf_mst_node_build(node.layer, &left, entries, node.count,
                          &replacement);
    free_entries(entries, node.count);
    free(entries);
    wf_mst_node_free(&node);
    if (s != WF_OK) return s;

    s = wf_mst_node_finalize(&replacement, car);
    if (s == WF_OK) *new_cid = replacement.cid;
    wf_mst_node_free(&replacement);
    return s;
}

wf_status wf_repo_update_record(wf_car *car,
                                 const wf_cid *prev_commit,
                                 const char *did,
                                 const char *collection,
                                 const char *rkey,
                                 const unsigned char *record_cbor,
                                 size_t record_cbor_len,
                                 const wf_signing_key *key,
                                 wf_cid *out_commit,
                                 wf_cid *out_record) {
    if (!car || !prev_commit || prev_commit->len == 0 || !did ||
        !collection || !rkey || !record_cbor || record_cbor_len == 0 ||
        !key || !out_commit || !out_record) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out_commit, 0, sizeof(*out_commit));
    memset(out_record, 0, sizeof(*out_record));

    wf_car_block *commit_block = wf_car_find_block(car, prev_commit);
    if (!commit_block) return WF_ERR_PARSE;
    wf_commit previous;
    memset(&previous, 0, sizeof(previous));
    wf_status s = wf_commit_parse(commit_block->data, commit_block->data_len,
                                  &previous);
    if (s != WF_OK) return s;

    size_t col_len = strlen(collection), rkey_len = strlen(rkey);
    size_t mst_key_len = col_len + 1 + rkey_len;
    unsigned char *mst_key = malloc(mst_key_len);
    if (!mst_key) return WF_ERR_ALLOC;
    memcpy(mst_key, collection, col_len);
    mst_key[col_len] = '/';
    memcpy(mst_key + col_len + 1, rkey, rkey_len);

    s = wf_cid_of_block(record_cbor, record_cbor_len, out_record);
    if (s != WF_OK) { free(mst_key); return s; }

    wf_cid new_mst_root = {{0}, 0};
    s = mst_update_value(car, &previous.data, mst_key, mst_key_len,
                         out_record, &new_mst_root);
    free(mst_key);
    if (s != WF_OK) return s;

    wf_car_block *blocks = realloc(car->blocks,
        (car->block_count + 1) * sizeof(*blocks));
    if (!blocks) return WF_ERR_ALLOC;
    car->blocks = blocks;
    wf_car_block *record_block = &car->blocks[car->block_count];
    record_block->cid = *out_record;
    record_block->data = malloc(record_cbor_len);
    if (!record_block->data) return WF_ERR_ALLOC;
    memcpy(record_block->data, record_cbor, record_cbor_len);
    record_block->data_len = record_cbor_len;
    car->block_count++;

    wf_commit commit;
    memset(&commit, 0, sizeof(commit));
    char rev[64];
    snprintf(rev, sizeof(rev), "3z%08x", (unsigned)time(NULL));
    s = wf_commit_create(did, rev, &new_mst_root, prev_commit, key, car,
                         &commit);
    if (s != WF_OK) return s;
    *out_commit = commit.cid;
    return WF_OK;
}

wf_status wf_repo_delete_record(wf_car *car,
                                  const wf_cid *prev_commit,
                                  const char *did,
                                  const char *collection,
                                  const char *rkey,
                                  const wf_signing_key *key,
                                  wf_cid *out_commit) {
    if (!car || !did || !collection || !rkey || !key || !out_commit) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out_commit, 0, sizeof(*out_commit));

    /* Build MST root from previous commit if available */
    wf_cid mst_root = {{0}, 0};
    if (prev_commit && prev_commit->len > 0) {
        wf_car_block *block = wf_car_find_block(car, prev_commit);
        if (!block) return WF_ERR_PARSE;
        wf_commit commit;
        memset(&commit, 0, sizeof(commit));
        wf_status s = wf_commit_parse(block->data, block->data_len, &commit);
        if (s != WF_OK) return s;
        mst_root = commit.data;
    }

    /* Build MST key: "collection/rkey" */
    size_t col_len = strlen(collection);
    size_t rkey_len = strlen(rkey);
    size_t key_len = col_len + 1 + rkey_len;
    unsigned char *mst_key = malloc(key_len);
    if (!mst_key) return WF_ERR_ALLOC;
    memcpy(mst_key, collection, col_len);
    mst_key[col_len] = '/';
    memcpy(mst_key + col_len + 1, rkey, rkey_len);

    /* Delete from MST */
    wf_cid new_mst_root = {{0}, 0};
    wf_status s = wf_mst_delete(car, &mst_root, mst_key, key_len, &new_mst_root);
    free(mst_key);
    if (s != WF_OK) return s;

    if (new_mst_root.len == 0) {
        /* A commit links to the canonical empty MST block, not a zero CID. */
        wf_cid empty = {{0}, 0};
        wf_mst_node empty_node;
        s = wf_mst_node_build(0, &empty, NULL, 0, &empty_node);
        if (s != WF_OK) return s;
        s = wf_mst_node_finalize(&empty_node, car);
        if (s == WF_OK) new_mst_root = empty_node.cid;
        wf_mst_node_free(&empty_node);
        if (s != WF_OK) return s;
    }

    /* Create signed commit */
    wf_commit commit;
    memset(&commit, 0, sizeof(commit));
    char rev[64];
    snprintf(rev, sizeof(rev), "3z%08x", (unsigned)time(NULL));
    s = wf_commit_create(did, rev, &new_mst_root,
                          (prev_commit && prev_commit->len > 0) ? prev_commit : NULL,
                          key, car, &commit);
    if (s != WF_OK) return s;

    *out_commit = commit.cid;
    return WF_OK;
}
