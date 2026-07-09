/*
 * server_typed.c — owned typed parsers + agent convenience wrappers for the
 * com.atproto.server session/account endpoints. See
 * include/wolfram/server_typed.h for the public API, the authoritative wire
 * format, and ownership rules.
 *
 * Mirrors labeler_typed.c / ozone_typed.c: static strdup/set_string/reset
 * helpers, owned strings, detached `extra` / `did_doc` cJSON subtrees for
 * open/unbounded fields, and full cleanup on the first error. The agent
 * wrappers call the generated lex wrappers directly after syncing auth via
 * wf_agent_sync_auth.
 */

#include "wolfram/server_typed.h"

#include "agent/_internal.h"
#include "wolfram/atproto_lex.h"
#include "wolfram/server.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* ---- local string/reset helpers ---- */

static char *wf_server_strdup(const char *s) {
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

static wf_status wf_server_set_string(char **dst, const char *src) {
    char *copy = wf_server_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

/* ---- session_info (getSession) ---- */

static void wf_server_session_info_reset(wf_server_session_info *s) {
    if (!s) {
        return;
    }
    free(s->did);
    free(s->handle);
    free(s->email);
    free(s->status);
    if (s->did_doc) {
        cJSON_Delete(s->did_doc);
    }
    if (s->extra) {
        cJSON_Delete(s->extra);
    }
    memset(s, 0, sizeof(*s));
}

wf_status wf_server_parse_session_info(const char *json, size_t json_len,
                                       wf_server_session_info *out) {
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
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(root, "handle");
    cJSON *email = cJSON_GetObjectItemCaseSensitive(root, "email");
    cJSON *ec = cJSON_GetObjectItemCaseSensitive(root, "emailConfirmed");
    cJSON *active = cJSON_GetObjectItemCaseSensitive(root, "active");
    cJSON *status_field = cJSON_GetObjectItemCaseSensitive(root, "status");
    cJSON *did_doc = cJSON_GetObjectItemCaseSensitive(root, "didDoc");

    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_server_set_string(&out->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
        status = wf_server_set_string(&out->handle, handle->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(email) && email->valuestring) {
        out->has_email = true;
        status = wf_server_set_string(&out->email, email->valuestring);
    }
    if (status == WF_OK && cJSON_IsBool(ec)) {
        out->has_email_confirmed = true;
        out->email_confirmed = cJSON_IsTrue(ec);
    }
    if (status == WF_OK && cJSON_IsBool(active)) {
        out->has_active = true;
        out->active = cJSON_IsTrue(active);
    }
    if (status == WF_OK && cJSON_IsString(status_field) &&
        status_field->valuestring) {
        out->has_status = true;
        status = wf_server_set_string(&out->status, status_field->valuestring);
    }
    if (status == WF_OK && did_doc != NULL) {
        out->did_doc = cJSON_DetachItemFromObject(root, "didDoc");
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(root, "did");
        cJSON_DetachItemFromObject(root, "handle");
        cJSON_DetachItemFromObject(root, "email");
        cJSON_DetachItemFromObject(root, "emailConfirmed");
        cJSON_DetachItemFromObject(root, "active");
        cJSON_DetachItemFromObject(root, "status");
        out->extra = root;
    } else {
        wf_server_session_info_reset(out);
        cJSON_Delete(root);
    }
    return status;
}

void wf_server_session_info_free(wf_server_session_info *s) {
    if (!s) {
        return;
    }
    wf_server_session_info_reset(s);
}

/* ---- account_status (checkAccountStatus) ---- */

static void wf_server_account_status_reset(wf_server_account_status *s) {
    if (!s) {
        return;
    }
    free(s->repo_commit);
    free(s->repo_rev);
    memset(s, 0, sizeof(*s));
}

wf_status wf_server_parse_account_status(const char *json, size_t json_len,
                                         wf_server_account_status *out) {
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
    cJSON *activated = cJSON_GetObjectItemCaseSensitive(root, "activated");
    cJSON *valid_did = cJSON_GetObjectItemCaseSensitive(root, "validDid");
    cJSON *repo_commit = cJSON_GetObjectItemCaseSensitive(root, "repoCommit");
    cJSON *repo_rev = cJSON_GetObjectItemCaseSensitive(root, "repoRev");
    cJSON *repo_blocks = cJSON_GetObjectItemCaseSensitive(root, "repoBlocks");
    cJSON *indexed = cJSON_GetObjectItemCaseSensitive(root, "indexedRecords");
    cJSON *pstate = cJSON_GetObjectItemCaseSensitive(root, "privateStateValues");
    cJSON *expected = cJSON_GetObjectItemCaseSensitive(root, "expectedBlobs");
    cJSON *imported = cJSON_GetObjectItemCaseSensitive(root, "importedBlobs");

    if (cJSON_IsBool(activated)) {
        out->has_activated = true;
        out->activated = cJSON_IsTrue(activated);
    }
    if (cJSON_IsBool(valid_did)) {
        out->has_valid_did = true;
        out->valid_did = cJSON_IsTrue(valid_did);
    }
    if (cJSON_IsString(repo_commit) && repo_commit->valuestring) {
        status = wf_server_set_string(&out->repo_commit, repo_commit->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(repo_rev) && repo_rev->valuestring) {
        status = wf_server_set_string(&out->repo_rev, repo_rev->valuestring);
    }
    if (status == WF_OK && cJSON_IsNumber(repo_blocks)) {
        out->has_repo_blocks = true;
        out->repo_blocks = (int64_t)repo_blocks->valuedouble;
    }
    if (status == WF_OK && cJSON_IsNumber(indexed)) {
        out->has_indexed_records = true;
        out->indexed_records = (int64_t)indexed->valuedouble;
    }
    if (status == WF_OK && cJSON_IsNumber(pstate)) {
        out->has_private_state_values = true;
        out->private_state_values = (int64_t)pstate->valuedouble;
    }
    if (status == WF_OK && cJSON_IsNumber(expected)) {
        out->has_expected_blobs = true;
        out->expected_blobs = (int64_t)expected->valuedouble;
    }
    if (status == WF_OK && cJSON_IsNumber(imported)) {
        out->has_imported_blobs = true;
        out->imported_blobs = (int64_t)imported->valuedouble;
    }

    if (status != WF_OK) {
        wf_server_account_status_reset(out);
    }
    cJSON_Delete(root);
    return status;
}

void wf_server_account_status_free(wf_server_account_status *s) {
    if (!s) {
        return;
    }
    wf_server_account_status_reset(s);
}

/* ---- invite codes (getAccountInviteCodes) ---- */

static void wf_server_invite_code_reset(wf_server_invite_code *c) {
    if (!c) {
        return;
    }
    free(c->code);
    free(c->for_account);
    free(c->created_by);
    free(c->created_at);
    if (c->extra) {
        cJSON_Delete(c->extra);
    }
    memset(c, 0, sizeof(*c));
}

static wf_status wf_server_parse_invite_code(cJSON *obj,
                                             wf_server_invite_code *c) {
    wf_status status = WF_OK;
    cJSON *code = cJSON_GetObjectItemCaseSensitive(obj, "code");
    cJSON *available = cJSON_GetObjectItemCaseSensitive(obj, "available");
    cJSON *disabled = cJSON_GetObjectItemCaseSensitive(obj, "disabled");
    cJSON *for_account = cJSON_GetObjectItemCaseSensitive(obj, "forAccount");
    cJSON *created_by = cJSON_GetObjectItemCaseSensitive(obj, "createdBy");
    cJSON *created_at = cJSON_GetObjectItemCaseSensitive(obj, "createdAt");

    if (cJSON_IsString(code) && code->valuestring) {
        status = wf_server_set_string(&c->code, code->valuestring);
    }
    if (status == WF_OK && cJSON_IsNumber(available)) {
        c->has_available = true;
        c->available = (int64_t)available->valuedouble;
    }
    if (status == WF_OK && cJSON_IsBool(disabled)) {
        c->has_disabled = true;
        c->disabled = cJSON_IsTrue(disabled);
    }
    if (status == WF_OK && cJSON_IsString(for_account) && for_account->valuestring) {
        status = wf_server_set_string(&c->for_account, for_account->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(created_by) && created_by->valuestring) {
        status = wf_server_set_string(&c->created_by, created_by->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(created_at) && created_at->valuestring) {
        status = wf_server_set_string(&c->created_at, created_at->valuestring);
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "code");
        cJSON_DetachItemFromObject(obj, "available");
        cJSON_DetachItemFromObject(obj, "disabled");
        cJSON_DetachItemFromObject(obj, "forAccount");
        cJSON_DetachItemFromObject(obj, "createdBy");
        cJSON_DetachItemFromObject(obj, "createdAt");
        c->extra = obj;
    } else {
        wf_server_invite_code_reset(c);
    }
    return status;
}

wf_status wf_server_parse_invite_codes(const char *json, size_t json_len,
                                       wf_server_invite_code_list *out) {
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
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "codes");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_server_invite_code *items = NULL;
    cJSON **ptrs = NULL;
    if (count > 0) {
        items = (wf_server_invite_code *)calloc(count, sizeof(*items));
        ptrs = (cJSON **)calloc(count, sizeof(*ptrs));
        if (!items || !ptrs) {
            free(items);
            free(ptrs);
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
        for (size_t i = 0; i < count; ++i) {
            ptrs[i] = cJSON_GetArrayItem(arr, (int)i);
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *obj = ptrs[i];
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_server_parse_invite_code(obj, &items[i]);
        if (status == WF_OK) {
            items[i].extra = cJSON_DetachItemViaPointer(arr, obj);
        }
        if (status != WF_OK) {
            wf_server_invite_code_reset(&items[i]);
        }
    }
    free(ptrs);

    if (status == WF_OK) {
        out->codes = items;
        out->code_count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_server_invite_code_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

void wf_server_invite_code_list_free(wf_server_invite_code_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->code_count; ++i) {
        wf_server_invite_code_reset(&l->codes[i]);
    }
    free(l->codes);
    memset(l, 0, sizeof(*l));
}

/* ---- auth token (getServiceAuth / reserveSigningKey) ---- */

static void wf_server_auth_token_reset(wf_server_auth_token *t) {
    if (!t) {
        return;
    }
    free(t->token);
    memset(t, 0, sizeof(*t));
}

wf_status wf_server_parse_auth_token(const char *json, size_t json_len,
                                     wf_server_auth_token *out) {
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
    cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "token");
    /* reserveSigningKey returns "signingKey" instead of "token". */
    if (token == NULL) {
        token = cJSON_GetObjectItemCaseSensitive(root, "signingKey");
    }
    if (cJSON_IsString(token) && token->valuestring) {
        status = wf_server_set_string(&out->token, token->valuestring);
    }
    if (status != WF_OK) {
        wf_server_auth_token_reset(out);
    }
    cJSON_Delete(root);
    return status;
}

void wf_server_auth_token_free(wf_server_auth_token *t) {
    if (!t) {
        return;
    }
    wf_server_auth_token_reset(t);
}

/* ---- session tokens (createSession / refreshSession) ---- */

static void wf_server_session_tokens_reset(wf_server_session_tokens *s) {
    if (!s) {
        return;
    }
    free(s->access_jwt);
    free(s->refresh_jwt);
    free(s->handle);
    free(s->did);
    free(s->email);
    free(s->status);
    if (s->did_doc) {
        cJSON_Delete(s->did_doc);
    }
    if (s->extra) {
        cJSON_Delete(s->extra);
    }
    memset(s, 0, sizeof(*s));
}

wf_status wf_server_parse_session_tokens(const char *json, size_t json_len,
                                         wf_server_session_tokens *out) {
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
    cJSON *access = cJSON_GetObjectItemCaseSensitive(root, "accessJwt");
    cJSON *refresh = cJSON_GetObjectItemCaseSensitive(root, "refreshJwt");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(root, "handle");
    cJSON *did = cJSON_GetObjectItemCaseSensitive(root, "did");
    cJSON *email = cJSON_GetObjectItemCaseSensitive(root, "email");
    cJSON *ec = cJSON_GetObjectItemCaseSensitive(root, "emailConfirmed");
    cJSON *active = cJSON_GetObjectItemCaseSensitive(root, "active");
    cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "status");
    cJSON *did_doc = cJSON_GetObjectItemCaseSensitive(root, "didDoc");

    if (cJSON_IsString(access) && access->valuestring) {
        status = wf_server_set_string(&out->access_jwt, access->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(refresh) && refresh->valuestring) {
        status = wf_server_set_string(&out->refresh_jwt, refresh->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
        status = wf_server_set_string(&out->handle, handle->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(did) && did->valuestring) {
        status = wf_server_set_string(&out->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(email) && email->valuestring) {
        out->has_email = true;
        status = wf_server_set_string(&out->email, email->valuestring);
    }
    if (status == WF_OK && cJSON_IsBool(ec)) {
        out->has_email_confirmed = true;
        out->email_confirmed = cJSON_IsTrue(ec);
    }
    if (status == WF_OK && cJSON_IsBool(active)) {
        out->has_active = true;
        out->active = cJSON_IsTrue(active);
    }
    if (status == WF_OK && cJSON_IsString(st) && st->valuestring) {
        out->has_status = true;
        status = wf_server_set_string(&out->status, st->valuestring);
    }
    if (status == WF_OK && did_doc != NULL) {
        out->did_doc = cJSON_DetachItemFromObject(root, "didDoc");
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(root, "accessJwt");
        cJSON_DetachItemFromObject(root, "refreshJwt");
        cJSON_DetachItemFromObject(root, "handle");
        cJSON_DetachItemFromObject(root, "did");
        cJSON_DetachItemFromObject(root, "email");
        cJSON_DetachItemFromObject(root, "emailConfirmed");
        cJSON_DetachItemFromObject(root, "active");
        cJSON_DetachItemFromObject(root, "status");
        out->extra = root;
    } else {
        wf_server_session_tokens_reset(out);
        cJSON_Delete(root);
    }
    return status;
}

void wf_server_session_tokens_free(wf_server_session_tokens *s) {
    if (!s) {
        return;
    }
    wf_server_session_tokens_reset(s);
}

/* ---- email update request (requestEmailUpdate) ---- */

static void wf_server_email_update_request_reset(
    wf_server_email_update_request *r) {
    if (!r) {
        return;
    }
    memset(r, 0, sizeof(*r));
}

wf_status wf_server_parse_email_update(const char *json, size_t json_len,
                                       wf_server_email_update_request *out) {
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

    cJSON *required = cJSON_GetObjectItemCaseSensitive(root, "tokenRequired");
    if (cJSON_IsBool(required)) {
        out->token_required = cJSON_IsTrue(required);
    }
    cJSON_Delete(root);
    return WF_OK;
}

void wf_server_email_update_request_free(wf_server_email_update_request *r) {
    if (!r) {
        return;
    }
    wf_server_email_update_request_reset(r);
}

/* ---- Agent convenience wrappers ---- */

wf_status wf_agent_get_session_typed(wf_agent *agent,
                                    wf_server_session_info *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_server_session_info info = {0};
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_server_get_session_main_call(
        agent->client, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_server_parse_session_info(res.body, res.body_len, &info);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = info;
    }
    return status;
}

wf_status wf_agent_get_service_auth_typed(wf_agent *agent, const char *aud,
                                         int64_t exp_or_0,
                                         const char *lxm_or_null,
                                         wf_server_auth_token *out) {
    if (!agent || !aud || !aud[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_server_auth_token tok = {0};
    wf_lex_com_atproto_server_get_service_auth_main_params params = {0};
    params.aud = aud;
    if (exp_or_0 > 0) {
        params.has_exp = true;
        params.exp = exp_or_0;
    }
    if (lxm_or_null && lxm_or_null[0]) {
        params.has_lxm = true;
        params.lxm = lxm_or_null;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_server_get_service_auth_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_server_parse_auth_token(res.body, res.body_len, &tok);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = tok;
    }
    return status;
}

wf_status wf_agent_check_account_status_typed(wf_agent *agent,
                                             wf_server_account_status *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_server_account_status st = {0};
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_server_check_account_status_main_call(
        agent->client, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_server_parse_account_status(res.body, res.body_len, &st);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = st;
    }
    return status;
}

wf_status wf_agent_get_account_invite_codes_typed(wf_agent *agent,
                                                 int include_used,
                                                 int create_available,
                                                 wf_server_invite_code_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_server_invite_code_list list = {0};
    wf_lex_com_atproto_server_get_account_invite_codes_main_params params = {0};
    if (include_used) {
        params.has_include_used = true;
        params.include_used = true;
    }
    if (create_available) {
        params.has_create_available = true;
        params.create_available = true;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status =
        wf_lex_com_atproto_server_get_account_invite_codes_main_call(
            agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_server_parse_invite_codes(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_reserve_signing_key_typed(wf_agent *agent,
                                            const char *did_or_null,
                                            wf_server_auth_token *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_server_auth_token tok = {0};
    wf_lex_com_atproto_server_reserve_signing_key_main_input input = {0};
    if (did_or_null && did_or_null[0]) {
        input.has_did = true;
        input.did = did_or_null;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_server_reserve_signing_key_main_call(
        agent->client, &input, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_server_parse_auth_token(res.body, res.body_len, &tok);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = tok;
    }
    return status;
}

/* requestAccountDelete has no input or output; it just fires the procedure. */
wf_status wf_agent_request_account_delete_typed(wf_agent *agent) {
    if (!agent) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_server_request_account_delete_main_call(
        agent->client, &res);
    wf_response_free(&res);
    return status;
}

/* requestEmailUpdate returns { tokenRequired: boolean }; parse into `out`. */
wf_status wf_agent_request_email_update_typed(
    wf_agent *agent, wf_server_email_update_request *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_server_email_update_request req = {0};
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_server_request_email_update_main_call(
        agent->client, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_server_parse_email_update(res.body, res.body_len, &req);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = req;
    }
    return status;
}

/* requestEmailConfirmation has no input or output; it just fires the procedure. */
wf_status wf_agent_request_email_confirmation_typed(wf_agent *agent) {
    if (!agent) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status =
        wf_lex_com_atproto_server_request_email_confirmation_main_call(
            agent->client, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_request_password_reset_typed(wf_agent *agent,
                                               const char *email) {
    if (!agent || !email || !email[0]) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_com_atproto_server_request_password_reset_main_input input = {0};
    input.email = email;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_server_request_password_reset_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_create_session_typed(wf_agent *agent, const char *identifier,
                                       const char *password,
                                       const char *auth_factor_token_or_null,
                                       wf_server_session_tokens *out) {
    if (!agent || !identifier || !identifier[0] ||
        !password || !password[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_server_session_tokens tok = {0};
    wf_lex_com_atproto_server_create_session_main_input input = {0};
    input.identifier = identifier;
    input.password = password;
    if (auth_factor_token_or_null && auth_factor_token_or_null[0]) {
        input.has_auth_factor_token = true;
        input.auth_factor_token = auth_factor_token_or_null;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_server_create_session_main_call(
        agent->client, &input, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_server_parse_session_tokens(res.body, res.body_len, &tok);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = tok;
    }
    return status;
}

/* refreshSession returns a fresh set of session tokens; parse into `out`. */
wf_status wf_agent_refresh_session_typed(wf_agent *agent,
                                         wf_server_session_tokens *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_server_session_tokens tok = {0};
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_server_refresh_session_main_call(
        agent->client, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_server_parse_session_tokens(res.body, res.body_len, &tok);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = tok;
    }
    return status;
}

/* ------------------------------------------------------------------ */
/* Additional com.atproto.server typed agent wrappers.                */
/* These layer on the lower-level, already-owned server.h calls (which */
/* parse into wf_server_* owned structs and free with wf_server_*_free) */
/* so no second owned parser is introduced and no type is redefined.   */
/* ------------------------------------------------------------------ */

/* com.atproto.server.describeServer */
wf_status wf_agent_describe_server_typed(wf_agent *agent,
                                        wf_server_description *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_agent_sync_auth(agent);
    return wf_server_describe(agent->client, out);
}

/* com.atproto.server.createAccount */
wf_status wf_agent_create_account_typed(
    wf_agent *agent, const wf_server_create_account_input *input,
    wf_server_create_account_result *out) {
    if (!agent || !agent->client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_agent_sync_auth(agent);
    return wf_server_create_account(agent->client, input, out);
}

/* com.atproto.server.createAppPassword */
wf_status wf_agent_create_app_password_typed(wf_agent *agent, const char *name,
                                             int privileged,
                                             wf_server_app_password *out) {
    if (!agent || !agent->client || !name || !name[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_server_create_app_password_input input = {
        .name = name,
        .privileged = privileged,
    };
    wf_agent_sync_auth(agent);
    return wf_server_create_app_password(agent->client, &input, out);
}

/* com.atproto.server.listAppPasswords */
wf_status wf_agent_list_app_passwords_typed(wf_agent *agent,
                                            wf_server_app_password_list *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_agent_sync_auth(agent);
    return wf_server_list_app_passwords(agent->client, out);
}

/* com.atproto.server.revokeAppPassword */
wf_status wf_agent_revoke_app_password_typed(wf_agent *agent, const char *name) {
    if (!agent || !agent->client || !name || !name[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_server_revoke_app_password_input input = { .name = name };
    wf_agent_sync_auth(agent);
    return wf_server_revoke_app_password(agent->client, &input);
}

/* com.atproto.server.deleteSession (procedure, no output) */
wf_status wf_agent_delete_session_typed(wf_agent *agent) {
    if (!agent || !agent->client) {
        return WF_ERR_INVALID_ARG;
    }
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_server_delete_session_main_call(
        agent->client, &res);
    wf_response_free(&res);
    return status;
}

/* com.atproto.server.activateAccount (procedure, no output) */
wf_status wf_agent_activate_account_typed(wf_agent *agent) {
    if (!agent || !agent->client) {
        return WF_ERR_INVALID_ARG;
    }
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_server_activate_account_main_call(
        agent->client, &res);
    wf_response_free(&res);
    return status;
}

/* com.atproto.server.deactivateAccount (procedure, no output) */
wf_status wf_agent_deactivate_account_typed(wf_agent *agent,
                                            const char *delete_after_or_null) {
    if (!agent || !agent->client) {
        return WF_ERR_INVALID_ARG;
    }
    wf_server_deactivate_account_input input = {0};
    if (delete_after_or_null && delete_after_or_null[0]) {
        input.delete_after = delete_after_or_null;
    }
    wf_agent_sync_auth(agent);
    return wf_server_deactivate_account(agent->client, &input);
}

/* com.atproto.server.confirmEmail (procedure, no output) */
wf_status wf_agent_confirm_email_typed(wf_agent *agent, const char *email,
                                       const char *token) {
    if (!agent || !agent->client || !email || !email[0] || !token ||
        !token[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_server_confirm_email_input input = { .email = email, .token = token };
    wf_agent_sync_auth(agent);
    return wf_server_confirm_email(agent->client, &input);
}

/* com.atproto.server.resetPassword (procedure, no output) */
wf_status wf_agent_reset_password_typed(wf_agent *agent, const char *reset_token,
                                        const char *new_password) {
    if (!agent || !agent->client || !reset_token || !reset_token[0] ||
        !new_password || !new_password[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_server_reset_password_input input = {
        .reset_token = reset_token,
        .new_password = new_password,
    };
    wf_agent_sync_auth(agent);
    return wf_server_reset_password(agent->client, &input);
}

/* com.atproto.server.updateEmail (procedure, no output) */
wf_status wf_agent_update_email_typed(wf_agent *agent, const char *email,
                                      const char *token, int email_auth_factor) {
    if (!agent || !agent->client || !email || !email[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_server_update_email_input input = {0};
    input.email = email;
    if (email_auth_factor) {
        input.has_email_auth_factor = 1;
        input.email_auth_factor = 1;
    }
    if (token && token[0]) {
        input.has_token = 1;
        input.token = token;
    }
    wf_agent_sync_auth(agent);
    return wf_server_update_email(agent->client, &input);
}

/* com.atproto.server.createInviteCode */
wf_status wf_agent_create_invite_code_typed(
    wf_agent *agent, int64_t use_count, const char *for_account_or_null,
    wf_server_create_invite_code_result *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_server_create_invite_code_input input = { .use_count = use_count };
    if (for_account_or_null && for_account_or_null[0]) {
        input.for_account = for_account_or_null;
    }
    wf_agent_sync_auth(agent);
    return wf_server_create_invite_code(agent->client, &input, out);
}

/* com.atproto.server.createInviteCodes */
wf_status wf_agent_create_invite_codes_typed(
    wf_agent *agent, int64_t code_count, int64_t use_count,
    const char *const *for_accounts, size_t for_accounts_count,
    wf_server_create_invite_codes_result *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_server_create_invite_codes_input input = {0};
    input.code_count = code_count;
    input.use_count = use_count;
    input.for_accounts = for_accounts;
    input.for_accounts_count = for_accounts_count;
    wf_agent_sync_auth(agent);
    return wf_server_create_invite_codes(agent->client, &input, out);
}
