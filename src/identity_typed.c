/*
 * identity_typed.c — typed parsers + agent wrappers for com.atproto.identity.
 * See include/wolfram/identity_typed.h for the public API and ownership rules.
 * Follows the conventions of contact_typed.c / admin_typed.c: static
 * strdup/set_string/reset helpers, owned strings, full cleanup on the first
 * error, and generated lex wrappers invoked after wf_agent_sync_auth.
 */

#include "wolfram/identity_typed.h"

#include "agent/_internal.h"
#include "wolfram/atproto_lex.h"
#include "wolfram/plc.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_identity_strdup(const char *s) {
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

static wf_status wf_identity_set_string(char **dst, const char *src) {
    char *copy = wf_identity_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

/* Parse a JSON array of strings into an owned `char **` + count. */
static wf_status wf_identity_parse_string_array(cJSON *arr, char ***out_items,
                                                size_t *out_count) {
    size_t count = (size_t)cJSON_GetArraySize(arr);
    char **items = NULL;
    if (count > 0) {
        items = (char **)calloc(count, sizeof(*items));
        if (!items) {
            return WF_ERR_ALLOC;
        }
    }
    wf_status status = WF_OK;
    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *el = cJSON_GetArrayItem(arr, (int)i);
        if (cJSON_IsString(el) && el->valuestring) {
            status = wf_identity_set_string(&items[i], el->valuestring);
        } else {
            status = WF_ERR_PARSE;
        }
    }
    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            free(items[i]);
        }
        free(items);
        return status;
    }
    *out_items = items;
    *out_count = count;
    return WF_OK;
}

/* ---- resolveHandle ---- */

static void wf_identity_resolve_handle_reset(wf_identity_resolve_handle *v) {
    if (!v) {
        return;
    }
    free(v->did);
    memset(v, 0, sizeof(*v));
}

wf_status wf_identity_parse_resolve_handle(const char *json, size_t json_len,
                                           wf_identity_resolve_handle *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *did = cJSON_GetObjectItemCaseSensitive(root, "did");
    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_identity_set_string(&out->did, did->valuestring);
    } else {
        status = WF_ERR_PARSE;
    }

    if (status != WF_OK) {
        wf_identity_resolve_handle_reset(out);
    }
    cJSON_Delete(root);
    return status;
}

void wf_identity_resolve_handle_free(wf_identity_resolve_handle *v) {
    wf_identity_resolve_handle_reset(v);
}

/* ---- resolveDid ---- */

static void wf_identity_verification_method_reset(
    wf_identity_verification_method *m) {
    if (!m) {
        return;
    }
    free(m->id);
    free(m->type);
    free(m->controller);
    free(m->public_key_multibase);
    memset(m, 0, sizeof(*m));
}

static void wf_identity_service_reset(wf_identity_service *s) {
    if (!s) {
        return;
    }
    free(s->id);
    free(s->type);
    free(s->service_endpoint_json);
    memset(s, 0, sizeof(*s));
}

static wf_status wf_identity_read_verification_method(
    cJSON *obj, wf_identity_verification_method *m) {
    wf_status status = WF_OK;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "id");
    cJSON *type = cJSON_GetObjectItemCaseSensitive(obj, "type");
    cJSON *controller = cJSON_GetObjectItemCaseSensitive(obj, "controller");
    cJSON *pk = cJSON_GetObjectItemCaseSensitive(obj, "publicKeyMultibase");
    if (cJSON_IsString(id) && id->valuestring) {
        status = wf_identity_set_string(&m->id, id->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(type) && type->valuestring) {
        status = wf_identity_set_string(&m->type, type->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(controller) && controller->valuestring) {
        status = wf_identity_set_string(&m->controller, controller->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(pk) && pk->valuestring) {
        status =
            wf_identity_set_string(&m->public_key_multibase, pk->valuestring);
    }
    return status;
}

static wf_status wf_identity_read_service(cJSON *obj, wf_identity_service *s) {
    wf_status status = WF_OK;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "id");
    cJSON *type = cJSON_GetObjectItemCaseSensitive(obj, "type");
    cJSON *endpoint = cJSON_GetObjectItemCaseSensitive(obj, "serviceEndpoint");
    if (cJSON_IsString(id) && id->valuestring) {
        status = wf_identity_set_string(&s->id, id->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(type) && type->valuestring) {
        status = wf_identity_set_string(&s->type, type->valuestring);
    }
    if (status == WF_OK && endpoint) {
        char *ep = cJSON_PrintUnformatted(endpoint);
        if (!ep) {
            status = WF_ERR_ALLOC;
        } else {
            free(s->service_endpoint_json);
            s->service_endpoint_json = ep;
        }
    }
    return status;
}

static void wf_identity_resolve_did_reset(wf_identity_resolve_did *v) {
    if (!v) {
        return;
    }
    free(v->handle);
    for (size_t i = 0; i < v->verification_method_count; ++i) {
        wf_identity_verification_method_reset(&v->verification_methods[i]);
    }
    free(v->verification_methods);
    for (size_t i = 0; i < v->service_count; ++i) {
        wf_identity_service_reset(&v->services[i]);
    }
    free(v->services);
    memset(v, 0, sizeof(*v));
}

wf_status wf_identity_parse_resolve_did(const char *json, size_t json_len,
                                        wf_identity_resolve_did *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *did_doc = cJSON_GetObjectItemCaseSensitive(root, "didDoc");
    if (!cJSON_IsObject(did_doc)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    cJSON *aka = cJSON_GetObjectItemCaseSensitive(did_doc, "alsoKnownAs");
    if (cJSON_IsArray(aka) && cJSON_GetArraySize(aka) > 0) {
        cJSON *first = cJSON_GetArrayItem(aka, 0);
        if (cJSON_IsString(first) && first->valuestring) {
            const char *raw = first->valuestring;
            const char *prefix = "at://";
            size_t plen = strlen(prefix);
            const char *handle = (strncmp(raw, prefix, plen) == 0)
                                     ? raw + plen
                                     : raw;
            status = wf_identity_set_string(&out->handle, handle);
        }
    }

    if (status == WF_OK) {
        cJSON *vms = cJSON_GetObjectItemCaseSensitive(did_doc,
                                                      "verificationMethod");
        if (cJSON_IsArray(vms)) {
            size_t count = (size_t)cJSON_GetArraySize(vms);
            wf_identity_verification_method *items = NULL;
            if (count > 0) {
                items = (wf_identity_verification_method *)calloc(
                    count, sizeof(*items));
                if (!items) {
                    status = WF_ERR_ALLOC;
                }
            }
            for (size_t i = 0; i < count && status == WF_OK; ++i) {
                cJSON *obj = cJSON_GetArrayItem(vms, (int)i);
                if (!cJSON_IsObject(obj)) {
                    status = WF_ERR_PARSE;
                    break;
                }
                status = wf_identity_read_verification_method(obj, &items[i]);
                if (status != WF_OK) {
                    wf_identity_verification_method_reset(&items[i]);
                }
            }
            if (status == WF_OK) {
                out->verification_methods = items;
                out->verification_method_count = count;
            } else {
                for (size_t i = 0; i < count; ++i) {
                    wf_identity_verification_method_reset(&items[i]);
                }
                free(items);
            }
        }
    }

    if (status == WF_OK) {
        cJSON *svcs = cJSON_GetObjectItemCaseSensitive(did_doc, "service");
        if (cJSON_IsArray(svcs)) {
            size_t count = (size_t)cJSON_GetArraySize(svcs);
            wf_identity_service *items = NULL;
            if (count > 0) {
                items = (wf_identity_service *)calloc(count, sizeof(*items));
                if (!items) {
                    status = WF_ERR_ALLOC;
                }
            }
            for (size_t i = 0; i < count && status == WF_OK; ++i) {
                cJSON *obj = cJSON_GetArrayItem(svcs, (int)i);
                if (!cJSON_IsObject(obj)) {
                    status = WF_ERR_PARSE;
                    break;
                }
                status = wf_identity_read_service(obj, &items[i]);
                if (status != WF_OK) {
                    wf_identity_service_reset(&items[i]);
                }
            }
            if (status == WF_OK) {
                out->services = items;
                out->service_count = count;
            } else {
                for (size_t i = 0; i < count; ++i) {
                    wf_identity_service_reset(&items[i]);
                }
                free(items);
            }
        }
    }

    if (status != WF_OK) {
        wf_identity_resolve_did_reset(out);
    }
    cJSON_Delete(root);
    return status;
}

void wf_identity_resolve_did_free(wf_identity_resolve_did *v) {
    wf_identity_resolve_did_reset(v);
}

/* ---- getRecommendedDidCredentials ---- */

static void wf_identity_recommended_credentials_reset(
    wf_identity_recommended_credentials *v) {
    if (!v) {
        return;
    }
    for (size_t i = 0; i < v->rotation_key_count; ++i) {
        free(v->rotation_keys[i]);
    }
    free(v->rotation_keys);
    for (size_t i = 0; i < v->also_known_as_count; ++i) {
        free(v->also_known_as[i]);
    }
    free(v->also_known_as);
    free(v->verification_methods_json);
    free(v->services_json);
    memset(v, 0, sizeof(*v));
}

wf_status wf_identity_parse_get_recommended_did_credentials(
    const char *json, size_t json_len,
    wf_identity_recommended_credentials *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;

    cJSON *rk = cJSON_GetObjectItemCaseSensitive(root, "rotationKeys");
    if (rk) {
        if (!cJSON_IsArray(rk)) {
            status = WF_ERR_PARSE;
        } else {
            status = wf_identity_parse_string_array(rk, &out->rotation_keys,
                                                    &out->rotation_key_count);
        }
    }

    if (status == WF_OK) {
        cJSON *aka = cJSON_GetObjectItemCaseSensitive(root, "alsoKnownAs");
        if (aka) {
            if (!cJSON_IsArray(aka)) {
                status = WF_ERR_PARSE;
            } else {
                status = wf_identity_parse_string_array(
                    aka, &out->also_known_as, &out->also_known_as_count);
            }
        }
    }

    if (status == WF_OK) {
        cJSON *vm = cJSON_GetObjectItemCaseSensitive(root,
                                                     "verificationMethods");
        if (vm) {
            char *vm_json = cJSON_PrintUnformatted(vm);
            if (!vm_json) {
                status = WF_ERR_ALLOC;
            } else {
                out->verification_methods_json = vm_json;
            }
        }
    }

    if (status == WF_OK) {
        cJSON *svcs = cJSON_GetObjectItemCaseSensitive(root, "services");
        if (svcs) {
            char *svcs_json = cJSON_PrintUnformatted(svcs);
            if (!svcs_json) {
                status = WF_ERR_ALLOC;
            } else {
                out->services_json = svcs_json;
            }
        }
    }

    if (status != WF_OK) {
        wf_identity_recommended_credentials_reset(out);
    }
    cJSON_Delete(root);
    return status;
}

void wf_identity_recommended_credentials_free(
    wf_identity_recommended_credentials *v) {
    wf_identity_recommended_credentials_reset(v);
}

/* ---- signPlcOperation ---- */

static void wf_identity_signed_operation_reset(
    wf_identity_signed_operation *v) {
    if (!v) {
        return;
    }
    free(v->operation_json);
    memset(v, 0, sizeof(*v));
}

wf_status wf_identity_parse_sign_plc_operation(
    const char *json, size_t json_len, wf_identity_signed_operation *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *op = cJSON_GetObjectItemCaseSensitive(root, "operation");
    if (op) {
        char *op_json = cJSON_PrintUnformatted(op);
        if (!op_json) {
            status = WF_ERR_ALLOC;
        } else {
            out->operation_json = op_json;
        }
    } else {
        status = WF_ERR_PARSE;
    }

    if (status != WF_OK) {
        wf_identity_signed_operation_reset(out);
    }
    cJSON_Delete(root);
    return status;
}

void wf_identity_signed_operation_free(wf_identity_signed_operation *v) {
    wf_identity_signed_operation_reset(v);
}

/* ---- resolveIdentity ---- */

static void wf_identity_resolve_identity_reset(wf_identity_resolve_identity *v) {
    if (!v) {
        return;
    }
    free(v->did);
    free(v->handle);
    free(v->did_doc_json);
    memset(v, 0, sizeof(*v));
}

wf_status wf_identity_parse_resolve_identity(const char *json, size_t json_len,
                                             wf_identity_resolve_identity *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *did = cJSON_GetObjectItemCaseSensitive(root, "did");
    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_identity_set_string(&out->did, did->valuestring);
    } else {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK) {
        cJSON *handle = cJSON_GetObjectItemCaseSensitive(root, "handle");
        if (cJSON_IsString(handle) && handle->valuestring) {
            status = wf_identity_set_string(&out->handle, handle->valuestring);
        }
    }
    if (status == WF_OK) {
        cJSON *doc = cJSON_GetObjectItemCaseSensitive(root, "didDoc");
        if (cJSON_IsObject(doc)) {
            char *doc_json = cJSON_PrintUnformatted(doc);
            if (!doc_json) {
                status = WF_ERR_ALLOC;
            } else {
                out->did_doc_json = doc_json;
            }
        }
    }

    if (status != WF_OK) {
        wf_identity_resolve_identity_reset(out);
    }
    cJSON_Delete(root);
    return status;
}

void wf_identity_resolve_identity_free(wf_identity_resolve_identity *v) {
    wf_identity_resolve_identity_reset(v);
}

/* ---- Agent convenience wrappers ---- */

wf_status wf_agent_resolve_handle_typed(wf_agent *agent, const char *handle,
                                        wf_identity_resolve_handle *out) {
    if (!agent || !agent->client || !handle || !handle[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_identity_resolve_handle_main_params params = {0};
    params.handle = handle;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_identity_resolve_handle_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_identity_parse_resolve_handle(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_resolve_did_typed(wf_agent *agent, const char *did,
                                      wf_identity_resolve_did *out) {
    if (!agent || !agent->client || !did || !did[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_identity_resolve_did_main_params params = {0};
    params.did = did;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_identity_resolve_did_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_identity_parse_resolve_did(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_update_handle_typed(wf_agent *agent, const char *new_handle) {
    if (!agent || !agent->client || !new_handle || !new_handle[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_identity_update_handle_main_input input = {0};
    input.handle = new_handle;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_identity_update_handle_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_recommended_did_credentials_typed(
    wf_agent *agent, wf_identity_recommended_credentials *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status =
        wf_lex_com_atproto_identity_get_recommended_did_credentials_main_call(
            agent->client, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_identity_parse_get_recommended_did_credentials(
        res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_request_plc_operation_signature_typed(wf_agent *agent,
                                                          const char *did) {
    if (!agent || !agent->client || !did || !did[0]) {
        return WF_ERR_INVALID_ARG;
    }
    /* The generated lex wrapper for this procedure is not emitted (the
     * lexicon has no input/output schema), so delegate to the identity module
     * helper, which performs the same POST. */
    wf_agent_sync_auth(agent);
    return wf_identity_request_plc_operation_signature(agent->client, did);
}

wf_status wf_agent_sign_plc_operation_typed(
    wf_agent *agent, const char *token,
    const char *const *rotation_keys, size_t rotation_keys_count,
    const char *const *also_known_as, size_t also_known_as_count,
    const char *verification_methods_json, const char *services_json,
    wf_identity_signed_operation *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_identity_sign_plc_operation_main_input input = {0};
    if (token && token[0]) {
        input.has_token = true;
        input.token = token;
    }
    if (rotation_keys && rotation_keys_count > 0) {
        input.has_rotation_keys = true;
        input.rotation_keys.items = rotation_keys;
        input.rotation_keys.count = rotation_keys_count;
    }
    if (also_known_as && also_known_as_count > 0) {
        input.has_also_known_as = true;
        input.also_known_as.items = also_known_as;
        input.also_known_as.count = also_known_as_count;
    }
    if (verification_methods_json) {
        input.has_verification_methods = true;
        input.verification_methods.data = verification_methods_json;
        input.verification_methods.length = strlen(verification_methods_json);
    }
    if (services_json) {
        input.has_services = true;
        input.services.data = services_json;
        input.services.length = strlen(services_json);
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_identity_sign_plc_operation_main_call(
        agent->client, &input, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status =
        wf_identity_parse_sign_plc_operation(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_submit_plc_operation_typed(wf_agent *agent,
                                               const char *operation_json) {
    if (!agent || !agent->client || !operation_json || !operation_json[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_identity_submit_plc_operation_main_input input = {0};
    input.operation.data = operation_json;
    input.operation.length = strlen(operation_json);

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_identity_submit_plc_operation_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_resolve_identity(wf_agent *agent, const char *identifier,
                                    wf_identity_resolve_identity *out) {
    if (!agent || !agent->client || !identifier || !identifier[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_identity_resolve_identity_main_params params = {0};
    params.identifier = identifier;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_identity_resolve_identity_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_identity_parse_resolve_identity(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_refresh_identity(wf_agent *agent, const char *identifier) {
    if (!agent || !agent->client || !identifier || !identifier[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_identity_refresh_identity_main_input input = {0};
    input.identifier = identifier;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_identity_refresh_identity_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_identity_rotate_handle(wf_agent *agent,
                                          const char *new_handle) {
    if (!agent || !agent->client || !new_handle || !new_handle[0]) {
        return WF_ERR_INVALID_ARG;
    }

    /* Step 1: fetch recommended DID credentials for the destination service so
     * we can carry rotation keys / verification methods / services over. */
    wf_identity_recommended_credentials creds = {0};
    wf_status status =
        wf_agent_get_recommended_did_credentials_typed(agent, &creds);
    if (status != WF_OK) {
        return status;
    }

    /* Step 2: build the handle-rotation PLC operation. The new alsoKnownAs is
     * the at:// form of the new handle; the rest is copied from the
     * recommended credentials. `prev` is left NULL because the current
     * operation CID must be resolved from the PLC directory first (it is not
     * available on the agent). */
    char *aka = (char *)malloc(strlen(new_handle) + 6);
    if (!aka) {
        wf_identity_recommended_credentials_free(&creds);
        return WF_ERR_ALLOC;
    }
    snprintf(aka, strlen(new_handle) + 6, "at://%s", new_handle);

    const char *aka_items[1] = {aka};
    wf_plc_operation_update update = {0};
    update.rotation_keys = (const char *const *)creds.rotation_keys;
    update.rotation_keys_count = creds.rotation_key_count;
    update.verification_methods_json = creds.verification_methods_json;
    update.services_json = creds.services_json;
    update.also_known_as = aka_items;
    update.also_known_as_count = 1;

    char *op_json = NULL;
    status = wf_plc_operation_build(&update, &op_json);
    free(aka);
    wf_identity_recommended_credentials_free(&creds);
    if (status == WF_OK) {
        wf_plc_operation_free(op_json);
    }

    /* Step 3: requestPlcOperationSignature emails a token to the account's
     * address. The token is delivered out-of-band and cannot be read here.
     * Step 4: wf_plc_operation_sign requires the account's private signing key,
     * which the agent does not hold. Step 5: submitPlcOperation needs the
     * signed operation plus the current PLC operation CID as `prev`.
     * TODO: completing the rotation requires the out-of-band signature token
     * and the account's private signing key, neither of which is available
     * here, so we cannot finish the flow honestly. */
    return WF_ERR_INVALID_ARG;
}
