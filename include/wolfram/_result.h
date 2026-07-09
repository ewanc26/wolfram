#ifndef WOLFRAM_RESULT_H
#define WOLFRAM_RESULT_H

#include <stdbool.h>
#include <stddef.h>

#include <wolfram/xrpc.h>

/* `wf_error` is a typedef alias of the canonical `wf_status` enum (xrpc.h) so
 * the two status vocabularies never diverge. */
typedef wf_status wf_error;

#ifdef __cplusplus
extern "C" {
#endif

#define wf_ok(t, v) ((wf_result)(t##_result){.ok = true, .error = {.ok = true}, .value = {.t = v}})
#define wf_err(e)  ((wf_result) {.ok = false, .error = {.ok = false}, .error = e})

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

static inline bool wf_result_is_ok(wf_result r) {
    return r.success && r.detail.ok;
}

static inline bool wf_result_is_err(wf_result r) {
    return !r.success || !r.detail.ok;
}

wf_error wf_result_to_error(wf_result r);

#ifdef __cplusplus
}
#endif

#endif
