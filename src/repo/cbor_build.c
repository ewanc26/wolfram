#include "cbor_build.h"

#include <stdlib.h>
#include <string.h>

wf_cbor_item *cbor_str(const char *s) {
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

wf_cbor_item *cbor_bytes(const unsigned char *data, size_t len) {
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

wf_cbor_item *cbor_uint(uint64_t v) {
    wf_cbor_item *item = calloc(1, sizeof(*item));
    if (!item) return NULL;
    item->type = WF_CBOR_UNSIGNED;
    item->uinteger = v;
    return item;
}

wf_cbor_item *cbor_null(void) {
    wf_cbor_item *item = calloc(1, sizeof(*item));
    if (!item) return NULL;
    item->type = WF_CBOR_SIMPLE;
    item->simple_value = 22;
    return item;
}

wf_cbor_item *cbor_cid(const wf_cid *cid) {
    wf_cbor_item *item = cbor_bytes(cid->bytes, cid->len);
    if (item) item->type = WF_CBOR_LINK;
    return item;
}