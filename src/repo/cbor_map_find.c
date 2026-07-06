#include "cbor_map_find.h"

#include <string.h>

wf_cbor_item *wf_cbor_map_find(wf_cbor_item *item,
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