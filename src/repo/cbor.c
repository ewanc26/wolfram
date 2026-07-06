#include "wolfram/repo/cbor.h"
#include <cbor.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

/* ── DAG-CBOR decoder ─────────────────────────────────────── */

#define WF_CBOR_MAX_DEPTH 128

static int wf_cbor_varint(const unsigned char *data, size_t len,
                          uint64_t *value, size_t *used) {
    uint64_t result = 0;
    unsigned shift = 0;
    size_t i;
    if (!data || !value || !used) return 0;
    for (i = 0; i < len && i < 10; i++) {
        unsigned char byte = data[i];
        if (shift == 63 && (byte & 0x7e) != 0) return 0;
        result |= (uint64_t)(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            if (i > 0 && byte == 0) return 0;
            *value = result;
            *used = i + 1;
            return 1;
        }
        shift += 7;
    }
    return 0;
}

static int wf_cbor_cid_valid(const unsigned char *data, size_t len) {
    uint64_t value;
    size_t used, pos = 0;
    if (!data || len == 0) return 0;
    if (len == 34 && data[0] == 0x12 && data[1] == 0x20) return 1;
    if (!wf_cbor_varint(data + pos, len - pos, &value, &used) || value != 1)
        return 0;
    pos += used;
    if (!wf_cbor_varint(data + pos, len - pos, &value, &used) || value == 0)
        return 0;
    pos += used;
    if (!wf_cbor_varint(data + pos, len - pos, &value, &used) || value == 0)
        return 0;
    pos += used;
    if (!wf_cbor_varint(data + pos, len - pos, &value, &used)) return 0;
    pos += used;
    return value == len - pos;
}

static int wf_cbor_key_before(const cbor_item_t *left,
                               const cbor_item_t *right) {
    unsigned char *left_bytes = NULL, *right_bytes = NULL;
    size_t left_len = 0, right_len = 0, common;
    int cmp, result = 0;
    if (!cbor_isa_string(left) || !cbor_isa_string(right)) return 0;
    cbor_serialize_alloc(left, &left_bytes, &left_len);
    cbor_serialize_alloc(right, &right_bytes, &right_len);
    if (!left_bytes || !right_bytes) goto done;
    common = left_len < right_len ? left_len : right_len;
    cmp = memcmp(left_bytes, right_bytes, common);
    result = cmp < 0 || (cmp == 0 && left_len < right_len);
done:
    free(left_bytes);
    free(right_bytes);
    return result;
}

static int wf_cbor_int_is_canonical(const cbor_item_t *item) {
    uint64_t value = cbor_get_int(item);
    switch (cbor_int_get_width(item)) {
    case CBOR_INT_8:
        return 1;
    case CBOR_INT_16:
        return value > UINT8_MAX;
    case CBOR_INT_32:
        return value > UINT16_MAX;
    case CBOR_INT_64:
        return value > UINT32_MAX;
    }
    return 0;
}

static wf_cbor_item *wf_cbor_from_libcbor(const cbor_item_t *source,
                                           unsigned depth) {
    wf_cbor_item *item;
    size_t count, i;
    if (!source || depth > WF_CBOR_MAX_DEPTH) return NULL;
    item = calloc(1, sizeof(*item));
    if (!item) return NULL;
    switch (cbor_typeof(source)) {
    case CBOR_TYPE_UINT:
        if (!wf_cbor_int_is_canonical(source)) break;
        item->type = WF_CBOR_UNSIGNED;
        item->uinteger = cbor_get_int(source);
        return item;
    case CBOR_TYPE_NEGINT:
        if (!wf_cbor_int_is_canonical(source)) break;
        item->type = WF_CBOR_NEGATIVE;
        item->neginteger = cbor_get_int(source);
        return item;
    case CBOR_TYPE_BYTESTRING:
        if (!cbor_bytestring_is_definite(source)) break;
        item->type = WF_CBOR_BYTES;
        item->bytes.len = cbor_bytestring_length(source);
        if (item->bytes.len > 0) {
            item->bytes.data = malloc(item->bytes.len);
            if (!item->bytes.data) break;
            memcpy(item->bytes.data, cbor_bytestring_handle(source),
                   item->bytes.len);
        }
        return item;
    case CBOR_TYPE_STRING:
        if (!cbor_string_is_definite(source) ||
            (cbor_string_length(source) > 0 &&
             cbor_string_codepoint_count(source) == 0)) break;
        item->type = WF_CBOR_STRING;
        item->string.len = cbor_string_length(source);
        item->string.str = malloc(item->string.len + 1);
        if (!item->string.str) break;
        if (item->string.len > 0)
            memcpy(item->string.str, cbor_string_handle(source), item->string.len);
        item->string.str[item->string.len] = '\0';
        return item;
    case CBOR_TYPE_ARRAY:
        if (!cbor_array_is_definite(source)) break;
        item->type = WF_CBOR_ARRAY;
        count = cbor_array_size(source);
        item->children.count = count;
        if (count > 0) {
            cbor_item_t **children = cbor_array_handle(source);
            item->children.items = calloc(count, sizeof(*item->children.items));
            if (!item->children.items) break;
            for (i = 0; i < count; i++) {
                item->children.items[i] = wf_cbor_from_libcbor(
                    children[i], depth + 1);
                if (!item->children.items[i]) break;
            }
            if (i != count) break;
        }
        return item;
    case CBOR_TYPE_MAP: {
        struct cbor_pair *pairs;
        if (!cbor_map_is_definite(source)) break;
        item->type = WF_CBOR_MAP;
        count = cbor_map_size(source);
        item->map.count = count;
        pairs = cbor_map_handle(source);
        if (count > 0) {
            item->map.pairs = calloc(count, sizeof(*item->map.pairs));
            if (!item->map.pairs) break;
            for (i = 0; i < count; i++) {
                if (!cbor_isa_string(pairs[i].key) ||
                    (i > 0 && !wf_cbor_key_before(pairs[i - 1].key,
                                                  pairs[i].key))) break;
                item->map.pairs[i].key = wf_cbor_from_libcbor(pairs[i].key,
                                                               depth + 1);
                item->map.pairs[i].value = wf_cbor_from_libcbor(pairs[i].value,
                                                                 depth + 1);
                if (!item->map.pairs[i].key || !item->map.pairs[i].value) break;
            }
            if (i != count) break;
        }
        return item;
    }
    case CBOR_TYPE_TAG: {
        cbor_item_t *tagged = cbor_tag_item(source);
        const unsigned char *bytes;
        size_t bytes_len;
        if (cbor_tag_value(source) != 42 || !cbor_isa_bytestring(tagged) ||
            !cbor_bytestring_is_definite(tagged)) {
            cbor_decref(&tagged);
            break;
        }
        bytes = cbor_bytestring_handle(tagged);
        bytes_len = cbor_bytestring_length(tagged);
        if (bytes_len < 2 || bytes[0] != 0 ||
            !wf_cbor_cid_valid(bytes + 1, bytes_len - 1)) {
            cbor_decref(&tagged);
            break;
        }
        item->type = WF_CBOR_LINK;
        item->bytes.len = bytes_len - 1;
        item->bytes.data = malloc(item->bytes.len);
        if (!item->bytes.data) {
            cbor_decref(&tagged);
            break;
        }
        memcpy(item->bytes.data, bytes + 1, item->bytes.len);
        cbor_decref(&tagged);
        return item;
    }
    case CBOR_TYPE_FLOAT_CTRL:
        if (cbor_is_bool(source)) {
            item->type = WF_CBOR_SIMPLE;
            item->simple_value = cbor_get_bool(source) ? 21 : 20;
            return item;
        }
        if (cbor_is_null(source) || cbor_is_undef(source)) {
            item->type = WF_CBOR_SIMPLE;
            item->simple_value = 22;
            return item;
        }
        break;
    }
    wf_cbor_free(item);
    return NULL;
}

wf_cbor_item *wf_cbor_parse(const unsigned char *data, size_t len) {
    struct cbor_load_result result = {0};
    cbor_item_t *decoded = NULL;
    unsigned char *encoded = NULL;
    size_t encoded_len = 0;
    wf_cbor_item *item = NULL;
    if (!data || len == 0) return NULL;
    decoded = cbor_load(data, len, &result);
    if (!decoded || result.error.code != CBOR_ERR_NONE || result.read != len)
        goto done;
    cbor_serialize_alloc(decoded, &encoded, &encoded_len);
    if (!encoded || encoded_len != len || memcmp(encoded, data, len) != 0)
        goto done;
    item = wf_cbor_from_libcbor(decoded, 0);
done:
    free(encoded);
    if (decoded) cbor_decref(&decoded);
    return item;
}

void wf_cbor_free(wf_cbor_item *item) {
    if (!item) return;

    switch (item->type) {
    case WF_CBOR_BYTES:
    case WF_CBOR_LINK:
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
    } else if (item->type == WF_CBOR_LINK) {
        s += 2; /* tag 42 */
        s += wf_cbor_ser_uint_size(item->bytes.len + 1);
        s += item->bytes.len + 1; /* historical 0x00 CID prefix */
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
    case WF_CBOR_LINK:
        *(*pos)++ = 0xD8;
        *(*pos)++ = 0x2A;
        wf_cbor_ser_write_uint(2, item->bytes.len + 1, pos);
        *(*pos)++ = 0x00;
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

