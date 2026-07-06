#include "internal.h"

#include <stdlib.h>
#include <string.h>

void wf_oauth_callback_result_free(wf_oauth_callback_result *result) {
    if (!result) return;
    free(result->state);
    free(result->code);
    free(result->issuer);
    free(result->error);
    free(result->error_description);
    memset(result, 0, sizeof(*result));
}

wf_status wf_oauth_callback_validate(const wf_oauth_callback_params *params,
                                     const char *expected_state,
                                     const char *expected_issuer,
                                     int issuer_parameter_required,
                                     wf_oauth_callback_result *out) {
    if (!params || !expected_state || !expected_issuer || !out ||
        expected_state[0] == '\0' || expected_issuer[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (params->response || !params->state || params->state[0] == '\0' ||
        strcmp(params->state, expected_state) != 0) {
        return WF_ERR_PARSE;
    }
    if ((params->code && params->error) || (!params->code && !params->error) ||
        (params->code && params->code[0] == '\0') ||
        (params->error && params->error[0] == '\0')) {
        return WF_ERR_PARSE;
    }
    if (params->issuer) {
        if (strcmp(params->issuer, expected_issuer) != 0) return WF_ERR_PARSE;
    } else if (issuer_parameter_required) {
        return WF_ERR_PARSE;
    }
    out->state = wf_oauth_strdup(params->state);
    if (params->code) out->code = wf_oauth_strdup(params->code);
    if (params->issuer) out->issuer = wf_oauth_strdup(params->issuer);
    if (params->error) out->error = wf_oauth_strdup(params->error);
    if (params->error_description) {
        if (!params->error) {
            wf_oauth_callback_result_free(out);
            return WF_ERR_PARSE;
        }
        out->error_description = wf_oauth_strdup(params->error_description);
    }
    if (!out->state || (params->code && !out->code) ||
        (params->issuer && !out->issuer) || (params->error && !out->error) ||
        (params->error_description && !out->error_description)) {
        wf_oauth_callback_result_free(out);
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}
