#ifndef WOLFRAM_OAUTH_CALLBACK_H
#define WOLFRAM_OAUTH_CALLBACK_H

#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Decoded parameters received at the client's redirect URI. */
typedef struct wf_oauth_callback_params {
    const char *response;          /* JARM response; unsupported when present */
    const char *state;
    const char *code;
    const char *issuer;            /* `iss` */
    const char *error;
    const char *error_description;
} wf_oauth_callback_params;

/** Owned validated callback outcome. Exactly one of code/error is populated. */
typedef struct wf_oauth_callback_result {
    char *state;
    char *code;
    char *issuer;
    char *error;
    char *error_description;
} wf_oauth_callback_result;

/**
 * Validate an authorization callback against its one-time state and issuer.
 * Callers must atomically consume expected_state from persistent storage before
 * using a successful result, preventing callback replay.
 */
wf_status wf_oauth_callback_validate(const wf_oauth_callback_params *params,
                                     const char *expected_state,
                                     const char *expected_issuer,
                                     int issuer_parameter_required,
                                     wf_oauth_callback_result *out);
void wf_oauth_callback_result_free(wf_oauth_callback_result *result);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_OAUTH_CALLBACK_H */
