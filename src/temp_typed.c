/*
 * temp_typed.c — owned typed parsers + agent convenience wrappers for the
 * com.atproto.temp namespace. See temp_typed.h for the endpoint list and the
 * lexicon-authoritative wire formats.
 *
 * Mirrors feed_typed.c / actor_typed.c: static strdup/set_string/reset helpers,
 * ownership via cJSON_DetachItemFromObject, and full cleanup on the first error.
 */

#include "wolfram/temp_typed.h"

#include "wolfram/atproto_lex.h"
#include "agent/_internal.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_temp_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

static wf_status wf_temp_set_string(char **dst, const char *src) {
    char *copy = wf_temp_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

/* ---- checkHandleAvailability -------------------------------------------- */

static void wf_temp_suggestion_reset(wf_temp_handle_suggestion *s) {
    if (!s) {
        return;
    }
    free(s->handle);
    free(s->method);
    memset(s, 0, sizeof(*s));
}

static void wf_temp_check_handle_availability_reset(
    wf_temp_check_handle_availability *v) {
    if (!v) {
        return;
    }
    free(v->handle);
    for (size_t i = 0; i < v->suggestion_count; ++i) {
        wf_temp_suggestion_reset(&v->suggestions[i]);
    }
    free(v->suggestions);
    if (v->raw_result) {
        cJSON_Delete(v->raw_result);
    }
    memset(v, 0, sizeof(*v));
}

wf_status wf_temp_check_handle_availability_parse(
    const char *json, size_t json_len,
    wf_temp_check_handle_availability *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;

    cJSON *handle = cJSON_GetObjectItemCaseSensitive(root, "handle");
    if (cJSON_IsString(handle) && handle->valuestring) {
        status = wf_temp_set_string(&out->handle, handle->valuestring);
    }

    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (status == WF_OK) {
        if (!cJSON_IsObject(result)) {
            status = WF_ERR_PARSE;
        } else {
            /* resultUnavailable carries "suggestions"; resultAvailable does not. */
            cJSON *suggestions =
                cJSON_GetObjectItemCaseSensitive(result, "suggestions");
            if (cJSON_IsArray(suggestions)) {
                out->available = 0;
                size_t n = (size_t)cJSON_GetArraySize(suggestions);
                if (n > 0) {
                    out->suggestions =
                        (wf_temp_handle_suggestion *)calloc(n, sizeof(*out->suggestions));
                    if (!out->suggestions) {
                        status = WF_ERR_ALLOC;
                    }
                }
                for (size_t i = 0; i < n && status == WF_OK; ++i) {
                    cJSON *s = cJSON_GetArrayItem(suggestions, (int)i);
                    if (!cJSON_IsObject(s)) {
                        status = WF_ERR_PARSE;
                        break;
                    }
                    cJSON *sh = cJSON_GetObjectItemCaseSensitive(s, "handle");
                    cJSON *sm = cJSON_GetObjectItemCaseSensitive(s, "method");
                    if (cJSON_IsString(sh) && sh->valuestring) {
                        status = wf_temp_set_string(&out->suggestions[i].handle,
                                                    sh->valuestring);
                    }
                    if (status == WF_OK && cJSON_IsString(sm) && sm->valuestring) {
                        status = wf_temp_set_string(&out->suggestions[i].method,
                                                    sm->valuestring);
                    }
                    if (status != WF_OK) {
                        wf_temp_suggestion_reset(&out->suggestions[i]);
                    } else {
                        out->suggestion_count = i + 1;
                    }
                }
            } else {
                out->available = 1;
            }

            /* Keep the full union subtree as owned cJSON. */
            if (status == WF_OK) {
                cJSON *raw = cJSON_DetachItemFromObject(root, "result");
                out->raw_result = raw;
            }
        }
    }

    if (status != WF_OK) {
        wf_temp_check_handle_availability_reset(out);
    }
    cJSON_Delete(root);
    return status;
}

void wf_temp_check_handle_availability_free(
    wf_temp_check_handle_availability *v) {
    wf_temp_check_handle_availability_reset(v);
}

/* ---- checkSignupQueue --------------------------------------------------- */

wf_status wf_temp_check_signup_queue_parse(
    const char *json, size_t json_len,
    wf_temp_check_signup_queue *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    cJSON *activated = cJSON_GetObjectItemCaseSensitive(root, "activated");
    if (!cJSON_IsBool(activated)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    out->activated = cJSON_IsTrue(activated) ? 1 : 0;

    cJSON *place = cJSON_GetObjectItemCaseSensitive(root, "placeInQueue");
    if (cJSON_IsNumber(place)) {
        out->has_place_in_queue = 1;
        out->place_in_queue = (int64_t)place->valuedouble;
    }

    cJSON *est = cJSON_GetObjectItemCaseSensitive(root, "estimatedTimeMs");
    if (cJSON_IsNumber(est)) {
        out->has_estimated_time_ms = 1;
        out->estimated_time_ms = (int64_t)est->valuedouble;
    }

    cJSON_Delete(root);
    return WF_OK;
}

void wf_temp_check_signup_queue_free(wf_temp_check_signup_queue *v) {
    if (v) {
        memset(v, 0, sizeof(*v));
    }
}

/* ---- fetchLabels -------------------------------------------------------- */

wf_status wf_temp_fetch_labels_parse(
    const char *json, size_t json_len,
    wf_temp_fetch_labels *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    cJSON *labels = cJSON_GetObjectItemCaseSensitive(root, "labels");
    if (!cJSON_IsArray(labels)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    /* Take ownership of the whole labels array. */
    out->labels = cJSON_DetachItemFromObject(root, "labels");
    cJSON_Delete(root);
    return WF_OK;
}

void wf_temp_fetch_labels_free(wf_temp_fetch_labels *v) {
    if (!v) {
        return;
    }
    if (v->labels) {
        cJSON_Delete(v->labels);
    }
    memset(v, 0, sizeof(*v));
}

/* ---- dereferenceScope --------------------------------------------------- */

wf_status wf_temp_dereference_scope_parse(
    const char *json, size_t json_len,
    wf_temp_dereference_scope *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *scope = cJSON_GetObjectItemCaseSensitive(root, "scope");
    if (cJSON_IsString(scope) && scope->valuestring) {
        status = wf_temp_set_string(&out->scope, scope->valuestring);
    } else {
        status = WF_ERR_PARSE;
    }

    if (status != WF_OK) {
        free(out->scope);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

void wf_temp_dereference_scope_free(wf_temp_dereference_scope *v) {
    if (!v) {
        return;
    }
    free(v->scope);
    memset(v, 0, sizeof(*v));
}

/* ---- Write-side parsers (com.atproto.temp procedures) ------------------- */

wf_status wf_temp_add_reserved_handle_parse(
    const char *json, size_t json_len,
    wf_temp_add_reserved_handle_result *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    if (!cJSON_IsObject(root)) {
        status = WF_ERR_PARSE;
    } else {
        /* The lexicon output is empty; `ok` is set unconditionally. An optional
         * echoed handle is captured when the server includes one. */
        out->ok = 1;
        cJSON *handle = cJSON_GetObjectItemCaseSensitive(root, "handle");
        if (cJSON_IsString(handle) && handle->valuestring) {
            status = wf_temp_set_string(&out->handle, handle->valuestring);
        }
    }

    if (status != WF_OK) {
        free(out->handle);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

void wf_temp_add_reserved_handle_result_free(
    wf_temp_add_reserved_handle_result *v) {
    if (!v) {
        return;
    }
    free(v->handle);
    memset(v, 0, sizeof(*v));
}

wf_status wf_temp_request_phone_verification_parse(
    const char *json, size_t json_len,
    wf_temp_request_phone_verification_result *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    /* The lexicon declares no output; an empty JSON object is the success
     * signal. Tolerate an absent body here too (callers may pass "" for an
     * empty response) and treat it as success. */
    if (json_len == 0 || (json_len == 1 && json[0] == ' ')) {
        out->ok = 1;
        return WF_OK;
    }

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = cJSON_IsObject(root) ? WF_OK : WF_ERR_PARSE;
    if (status == WF_OK) {
        out->ok = 1;
    }
    cJSON_Delete(root);
    return status;
}

void wf_temp_request_phone_verification_result_free(
    wf_temp_request_phone_verification_result *v) {
    if (v) {
        memset(v, 0, sizeof(*v));
    }
}

wf_status wf_temp_revoke_account_credentials_parse(
    const char *json, size_t json_len,
    wf_temp_revoke_account_credentials_result *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    /* The lexicon declares no output; an empty JSON object is the success
     * signal. Tolerate an absent body here too and treat it as success. */
    if (json_len == 0 || (json_len == 1 && json[0] == ' ')) {
        out->ok = 1;
        return WF_OK;
    }

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = cJSON_IsObject(root) ? WF_OK : WF_ERR_PARSE;
    if (status == WF_OK) {
        out->ok = 1;
    }
    cJSON_Delete(root);
    return status;
}

void wf_temp_revoke_account_credentials_result_free(
    wf_temp_revoke_account_credentials_result *v) {
    if (v) {
        memset(v, 0, sizeof(*v));
    }
}

/* ---- Agent convenience wrappers ----------------------------------------- */

wf_status wf_agent_check_handle_availability(wf_agent *agent,
                                             const char *handle,
                                             int *out_available) {
    if (!agent || !agent->client || !handle || !handle[0] || !out_available) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_com_atproto_temp_check_handle_availability_main_params params = {0};
    params.handle = handle;

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_com_atproto_temp_check_handle_availability_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    wf_temp_check_handle_availability out = {0};
    status = wf_temp_check_handle_availability_parse(res.body, res.body_len, &out);
    if (status == WF_OK) {
        *out_available = out.available;
    }
    wf_temp_check_handle_availability_free(&out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_check_signup_queue(wf_agent *agent,
                                      int *out_activated,
                                      int *out_closed,
                                      char **out_place) {
    if (!agent || !agent->client || !out_activated || !out_closed || !out_place) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_com_atproto_temp_check_signup_queue_main_call(
        agent->client, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    wf_temp_check_signup_queue out = {0};
    status = wf_temp_check_signup_queue_parse(res.body, res.body_len, &out);
    if (status == WF_OK) {
        *out_activated = out.activated;
        /* The current lexicon exposes no queue-closed flag; reserved. */
        *out_closed = 0;
        *out_place = NULL;
        if (out.has_place_in_queue) {
            char buf[32];
            int n = snprintf(buf, sizeof(buf), "%lld",
                             (long long)out.place_in_queue);
            if (n > 0 && (size_t)n < sizeof(buf)) {
                *out_place = wf_temp_strdup(buf);
                if (!*out_place) {
                    status = WF_ERR_ALLOC;
                }
            }
        }
    }
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_fetch_labels(wf_agent *agent,
                                const char *const *did_pointers,
                                size_t count,
                                cJSON **out_labels) {
    if (did_pointers || count != 0) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_fetch_labels_query(agent, 0, 0, 0, out_labels);
}

wf_status wf_agent_fetch_labels_query(wf_agent *agent,
                                      int has_since, int64_t since,
                                      int limit, cJSON **out_labels) {
    if (!agent || !agent->client || !out_labels) {
        return WF_ERR_INVALID_ARG;
    }
    *out_labels = NULL;
    if (limit < 0 || limit > 250) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_com_atproto_temp_fetch_labels_main_params params = {0};
    params.has_since = (has_since != 0);
    params.since = since;
    if (limit != 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_com_atproto_temp_fetch_labels_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    wf_temp_fetch_labels out = {0};
    status = wf_temp_fetch_labels_parse(res.body, res.body_len, &out);
    if (status == WF_OK) {
        *out_labels = out.labels;   /* ownership transferred to caller */
        out.labels = NULL;
    }
    wf_temp_fetch_labels_free(&out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_request_phone_verification(wf_agent *agent,
                                             const char *phone_number,
                                             const char *code) {
    if (code) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_request_phone_verification_typed(agent, phone_number);
}

wf_status wf_agent_request_phone_verification_typed(wf_agent *agent,
                                                     const char *phone_number) {
    if (!agent || !agent->client || !phone_number || !phone_number[0]) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_com_atproto_temp_request_phone_verification_main_input input = {0};
    input.phone_number = phone_number;

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_com_atproto_temp_request_phone_verification_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_revoke_account_credentials(wf_agent *agent,
                                              const char *code,
                                              const char *name,
                                              const char *description) {
    (void)code;
    (void)name;
    (void)description;
    /* TODO: the atproto lexicon input for com.atproto.temp.revokeAccountCredentials
     * is `account` (an at-identifier), which this helper signature does not carry.
     * Implement once the wrapper accepts the account identifier rather than fabricating
     * a request. Until then, fail honestly per AGENTS.md principle 3. */
    if (!agent) {
        return WF_ERR_INVALID_ARG;
    }
    return WF_ERR_INVALID_ARG;
}

wf_status wf_agent_add_reserved_handle(wf_agent *agent, const char *handle) {
    if (!agent || !agent->client || !handle || !handle[0]) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_com_atproto_temp_add_reserved_handle_main_input input = {0};
    input.handle = handle;

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_com_atproto_temp_add_reserved_handle_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_dereference_scope(wf_agent *agent,
                                     const char *scope,
                                     char **out_did) {
    if (!agent || !agent->client || !scope || !scope[0] || !out_did) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_com_atproto_temp_dereference_scope_main_params params = {0};
    params.scope = scope;

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_com_atproto_temp_dereference_scope_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    wf_temp_dereference_scope out = {0};
    status = wf_temp_dereference_scope_parse(res.body, res.body_len, &out);
    if (status == WF_OK) {
        *out_did = out.scope;   /* ownership transferred to caller */
        out.scope = NULL;
    }
    wf_temp_dereference_scope_free(&out);
    wf_response_free(&res);
    return status;
}

/* revokeAccountCredentials (procedure, input: account). Carries the required
 * `account` (at-identifier) input, unlike the legacy honest-stub
 * wf_agent_revoke_account_credentials. */
wf_status wf_agent_revoke_account_credentials_typed(wf_agent *agent,
                                                    const char *account) {
    if (!agent || !agent->client || !account || !account[0]) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_com_atproto_temp_revoke_account_credentials_main_input input = {0};
    input.account = account;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status =
        wf_lex_com_atproto_temp_revoke_account_credentials_main_call(
            agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

/* addReservedHandle (procedure, input: handle; empty output, optional echoed
 * handle). Mirrors wf_agent_get_timeline_typed: sync auth, issue the generated
 * call, then parse the body into the owned result. */
wf_status wf_agent_temp_add_reserved_handle_typed(
    wf_agent *agent, const char *handle,
    wf_temp_add_reserved_handle_result *out) {
    if (!agent || !agent->client || !handle || !handle[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_com_atproto_temp_add_reserved_handle_main_input input = {0};
    input.handle = handle;

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_com_atproto_temp_add_reserved_handle_main_call(
        agent->client, &input, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_temp_add_reserved_handle_parse(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

/* requestPhoneVerification (procedure, input: phoneNumber; no output). */
wf_status wf_agent_temp_request_phone_verification_typed(
    wf_agent *agent, const char *phone_number,
    wf_temp_request_phone_verification_result *out) {
    if (!agent || !agent->client || !phone_number || !phone_number[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_com_atproto_temp_request_phone_verification_main_input input = {0};
    input.phone_number = phone_number;

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_com_atproto_temp_request_phone_verification_main_call(
            agent->client, &input, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    /* An empty/whitespace body is the expected (empty) output for this
     * procedure; treat it as success rather than a parse error. */
    if (res.body_len == 0) {
        out->ok = 1;
    } else {
        status = wf_temp_request_phone_verification_parse(res.body, res.body_len,
                                                          out);
    }
    wf_response_free(&res);
    return status;
}

/* revokeAccountCredentials (procedure, input: account at-identifier; no
 * output). */
wf_status wf_agent_temp_revoke_account_credentials_typed(
    wf_agent *agent, const char *account,
    wf_temp_revoke_account_credentials_result *out) {
    if (!agent || !agent->client || !account || !account[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_com_atproto_temp_revoke_account_credentials_main_input input = {0};
    input.account = account;

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_com_atproto_temp_revoke_account_credentials_main_call(
            agent->client, &input, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    /* An empty/whitespace body is the expected (empty) output for this
     * procedure; treat it as success rather than a parse error. */
    if (res.body_len == 0) {
        out->ok = 1;
    } else {
        status = wf_temp_revoke_account_credentials_parse(res.body, res.body_len,
                                                          out);
    }
    wf_response_free(&res);
    return status;
}
