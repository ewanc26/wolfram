#include "wolfram/repo/car.h"
#include "wolfram/repo/cbor.h"
#include <string.h>
#include <stdlib.h>

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
            if (v->type != WF_CBOR_ARRAY) {
                wf_cbor_free(hdr);
                return WF_ERR_INVALID_ARG;
            }
            roots_arr = v;
            found_roots = 1;
        }
    }

    if (!found_roots) { wf_cbor_free(hdr); return WF_ERR_INVALID_ARG; }

    out->root_count = roots_arr->children.count;
    if (out->root_count > 0) {
        out->roots = calloc(out->root_count, sizeof(wf_cid));
        if (!out->roots) { wf_cbor_free(hdr); return WF_ERR_ALLOC; }
    }

    for (size_t i = 0; i < out->root_count; i++) {
        wf_cbor_item *root_item = roots_arr->children.items[i];
        if (root_item->type != WF_CBOR_LINK || root_item->bytes.len != 36) {
            wf_car_free(out);
            out->roots = NULL;
            wf_cbor_free(hdr);
            return WF_ERR_INVALID_ARG;
        }
        memcpy(out->roots[i].bytes, root_item->bytes.data, 36);
        out->roots[i].len = 36;
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
    /* tag(42) + bytes(37) + historical 0x00 prefix + CID */
    hdr_cbor += car->root_count * (4 + 1 + 36);
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
        *pos++ = 0x58; *pos++ = 0x25;            /* bytes(37) */
        *pos++ = 0x00;
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
