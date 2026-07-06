#ifndef WOLFRAM_REPO_CBOR_H
#define WOLFRAM_REPO_CBOR_H

#include <stddef.h>
#include <stdint.h>
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum wf_cbor_type {
    WF_CBOR_UNSIGNED = 0,
    WF_CBOR_NEGATIVE = 1,
    WF_CBOR_BYTES    = 2,
    WF_CBOR_STRING   = 3,
    WF_CBOR_ARRAY    = 4,
    WF_CBOR_MAP      = 5,
    WF_CBOR_LINK     = 6,
    WF_CBOR_SIMPLE   = 7,
} wf_cbor_type;

typedef struct wf_cbor_pair {
    struct wf_cbor_item *key;
    struct wf_cbor_item *value;
} wf_cbor_pair;

typedef struct wf_cbor_item {
    wf_cbor_type type;
    union {
        uint64_t uinteger;
        uint64_t neginteger;
        struct { unsigned char *data; size_t len; } bytes;
        struct { char *str; size_t len; } string;
        struct { struct wf_cbor_item **items; size_t count; } children;
        struct { struct wf_cbor_pair *pairs; size_t count; } map;
        int simple_value;
    };
} wf_cbor_item;

wf_cbor_item *wf_cbor_parse(const unsigned char *data, size_t len);
void wf_cbor_free(wf_cbor_item *item);
unsigned char *wf_cbor_serialize(const wf_cbor_item *item, size_t *out_len);

typedef enum wf_record_value_type {
    WF_RECORD_STRING,
    WF_RECORD_INTEGER,
    WF_RECORD_BOOLEAN,
    WF_RECORD_ARRAY,
    WF_RECORD_OBJECT,
} wf_record_value_type;

typedef struct wf_record_schema wf_record_schema;
typedef struct wf_record_property {
    const char *name;
    const wf_record_schema *schema;
    int required;
} wf_record_property;

struct wf_record_schema {
    wf_record_value_type type;
    const wf_record_property *properties;
    size_t property_count;
    const wf_record_schema *items;
};

wf_status wf_record_encode_json(const char *collection,
                                const wf_record_schema *schema,
                                const char *json,
                                unsigned char **out_cbor,
                                size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_REPO_CBOR_H */