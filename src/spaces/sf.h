/*
 * The part of RFC 9651 Structured Fields that Spaces headers need:
 * dictionaries whose members are items or inner lists, with parameters. Dates
 * and display strings are refused. Not part of the public API.
 */
#ifndef WOLFRAM_SPACES_SF_H
#define WOLFRAM_SPACES_SF_H

#include <stddef.h>

#include "wolfram/xrpc.h"

/* Longest header value accepted. */
#define WF_SF_MAX_INPUT 32768
/* Most members, parameters or inner list items in any one container. */
#define WF_SF_MAX_COUNT 64

typedef enum wf_sf_type {
    WF_SF_INTEGER,
    WF_SF_DECIMAL, /* data holds the canonical text */
    WF_SF_STRING,
    WF_SF_TOKEN,
    WF_SF_BYTES,
    WF_SF_BOOLEAN, /* integer is 0 or 1 */
    WF_SF_INNER_LIST
} wf_sf_type;

typedef struct wf_sf_bare {
    wf_sf_type type;
    long long integer;
    char *data; /* NUL-terminated; len bytes before the NUL */
    size_t len;
} wf_sf_bare;

typedef struct wf_sf_param {
    char *key;
    wf_sf_bare value;
} wf_sf_param;

typedef struct wf_sf_item {
    wf_sf_bare bare;
    wf_sf_param *params;
    size_t param_count;
    struct wf_sf_item *items; /* inner list members */
    size_t item_count;
} wf_sf_item;

typedef struct wf_sf_member {
    char *key;
    wf_sf_item item;
} wf_sf_member;

typedef struct wf_sf_dict {
    wf_sf_member *members;
    size_t count;
} wf_sf_dict;

/* Growable string. `failed` latches an allocation failure. */
typedef struct wf_sf_out {
    char *data;
    size_t len;
    size_t cap;
    int failed;
} wf_sf_out;

void wf_sf_put(wf_sf_out *out, const char *data, size_t len);
void wf_sf_puts(wf_sf_out *out, const char *text);
/* The accumulated string (caller frees), or NULL if anything failed. */
char *wf_sf_take(wf_sf_out *out);

/* WF_OK, WF_ERR_PARSE or WF_ERR_ALLOC. `out` is empty unless WF_OK. */
wf_status wf_sf_parse_dict(const char *text, wf_sf_dict *out);
void wf_sf_dict_free(wf_sf_dict *dict);

const wf_sf_member *wf_sf_dict_get(const wf_sf_dict *dict, const char *key);
const wf_sf_bare *wf_sf_param_get(const wf_sf_item *item, const char *key);

/* Canonical serialisations. */
void wf_sf_write_item(wf_sf_out *out, const wf_sf_item *item);
void wf_sf_write_dict(wf_sf_out *out, const wf_sf_dict *dict);
/* A string or byte sequence built from a value; returns 0 if a string holds
 * anything but printable ASCII. */
int wf_sf_write_string(wf_sf_out *out, const char *text);
void wf_sf_write_bytes(wf_sf_out *out, const unsigned char *data, size_t len);

#endif /* WOLFRAM_SPACES_SF_H */
