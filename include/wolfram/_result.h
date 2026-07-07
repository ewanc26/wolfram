#ifndef WOLFRAM_RESULT_H
#define WOLFRAM_RESULT_H

#include <stdbool.h>
#include <stddef.h>

typedef enum wf_error {
    WF_OK = 0,
    WF_ERR_INVALID_ARG = 1,
    WF_ERR_ALLOC = 2,
    WF_ERR_NETWORK = 3,
    WF_ERR_HTTP = 4,
    WF_ERR_PARSE = 5,
    WF_ERR_NOT_FOUND = 6,
    WF_ERR_WOULD_BLOCK = 7,
    WF_ERR_DID_RESOLVE = 8,
    WF_ERR_DID_DOCUMENT_NOT_FOUND = 9,
    WF_ERR_HANDLE_RESOLVE = 10,
    WF_ERR_HANDLE_DOCUMENT_NOT_FOUND = 11,
    WF_ERR_HANDLE_TTL_EXPIRED = 12,
    WF_ERR_HANDLE_CACHE_KEY = 13,
    WF_ERR_CRYPTO = 14,
    WF_ERR_VALIDATION = 15,
    WF_ERR_STATE = 16,
    WF_ERR_CONFIG = 17,
    WF_ERR_TIMEOUT = 18,
    WF_ERR_UNSUPPORTED = 19,
    WF_ERR_PERMISSION = 20,
    WF_ERR_RATE_LIMIT = 21,
    WF_ERR_DUPLICATE = 22,
    WF_ERR_CONFLICT = 23,
    WF_ERR_NOT_IMPLEMENTED = 24,
    WF_ERR_INTERNAL = 25,
    WF_ERR_UNKNOWN = 26
} wf_error;

#define wf_ok(t, v) ((wf_result)(t##_result){.ok = true, .error = {.ok = true}, .value = {.t = v}})
#define wf_err(e)  ((wf_result) {.ok = false, .error = {.ok = false, .error = e}})

typedef union wf_result_detail {
    bool ok;
    struct wf_result_error {
        bool ok;
        wf_error error;
    } error;
    struct wf_result_ok {
        bool ok;
    } ok_detail;
    struct wf_result_int {
        bool ok;
        wf_error error;
    } int_value;
    struct wf_result_long {
        bool ok;
        wf_error error;
    } long_value;
    struct wf_result_char {
        bool ok;
        wf_error error;
        char *data;
        size_t length;
    } char_value;
    struct wf_result_ptr {
        bool ok;
        wf_error error;
        void *ptr;
    } ptr_value;
    struct wf_result_blob {
        bool ok;
        wf_error error;
        const void *data;
        size_t length;
    } blob_value;
} wf_result_detail;

typedef struct wf_result {
    bool success;
    wf_result_detail detail;
} wf_result;

type static inline bool wf_result_is_ok(wf_result r) {
    return r.success && r.detail.ok;
}

type static inline bool wf_result_is_err(wf_result r) {
    return !r.success || !r.detail.ok;
}

wf_error wf_result_to_error(wf_result r);

#endif
