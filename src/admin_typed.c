/*
 * admin_typed.c — typed parsers + agent wrappers for com.atproto.admin.
 * See include/wolfram/admin_typed.h for the public API and ownership rules.
 * Follows the conventions of actor_typed.c / feed_typed.c / graph_typed.c:
 * static strdup/set_string/reset helpers, owned strings, full cleanup on the
 * first error.
 */

#include "wolfram/admin_typed.h"

#include "agent/_internal.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_admin_strdup(const char *s) {
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

static wf_status wf_admin_set_string(char **dst, const char *src) {
    char *copy = wf_admin_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static void wf_admin_account_view_reset(wf_admin_account_view *v) {
    if (!v) {
        return;
    }
    free(v->did);
    free(v->handle);
    free(v->email);
    free(v->indexed_at);
    free(v->deactivated_at);
    if (v->extra) {
        cJSON_Delete(v->extra);
    }
    memset(v, 0, sizeof(*v));
}

static void wf_admin_subject_status_reset(wf_admin_subject_status *s) {
    if (!s) {
        return;
    }
    if (s->subject) {
        cJSON_Delete(s->subject);
    }
    free(s->did);
    free(s->takedown_ref);
    free(s->deactivated_ref);
    memset(s, 0, sizeof(*s));
}

static void wf_admin_invite_code_reset(wf_admin_invite_code *c) {
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

/* Read the core scalar fields of an accountView into `v`. Does not detach
 * anything (the caller owns the detach/extra step). Returns WF_OK / WF_ERR_ALLOC. */
static wf_status wf_admin_read_account_view(cJSON *obj,
                                            wf_admin_account_view *v) {
    wf_status status = WF_OK;
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(obj, "handle");
    cJSON *email = cJSON_GetObjectItemCaseSensitive(obj, "email");
    cJSON *indexed = cJSON_GetObjectItemCaseSensitive(obj, "indexedAt");
    cJSON *inv_dis = cJSON_GetObjectItemCaseSensitive(obj, "invitesDisabled");
    cJSON *deact = cJSON_GetObjectItemCaseSensitive(obj, "deactivatedAt");

    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_admin_set_string(&v->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
        status = wf_admin_set_string(&v->handle, handle->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(email) && email->valuestring) {
        status = wf_admin_set_string(&v->email, email->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(indexed) && indexed->valuestring) {
        status = wf_admin_set_string(&v->indexed_at, indexed->valuestring);
    }
    if (status == WF_OK) {
        if (cJSON_IsBool(inv_dis)) {
            v->has_invites_disabled = true;
            v->invites_disabled = cJSON_IsTrue(inv_dis);
        }
        if (cJSON_IsString(deact) && deact->valuestring) {
            status = wf_admin_set_string(&v->deactivated_at, deact->valuestring);
        }
    }
    return status;
}

/* Remove a known scalar key from `obj` after its value has been copied. */
static void wf_admin_strip_key(cJSON *obj, const char *key) {
    cJSON *item = cJSON_DetachItemFromObject(obj, key);
    if (item) {
        cJSON_Delete(item);
    }
}

wf_status wf_admin_parse_account_view(const char *json, size_t json_len,
                                      wf_admin_account_view *out) {
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

    wf_status status = wf_admin_read_account_view(root, out);
    if (status == WF_OK) {
        /* Detach known keys so `extra` holds only unknown fields. */
        wf_admin_strip_key(root, "did");
        wf_admin_strip_key(root, "handle");
        wf_admin_strip_key(root, "email");
        wf_admin_strip_key(root, "indexedAt");
        wf_admin_strip_key(root, "invitesDisabled");
        wf_admin_strip_key(root, "deactivatedAt");
        out->extra = root; /* take ownership of the rest */
        return WF_OK;
    }

    wf_admin_account_view_reset(out);
    cJSON_Delete(root);
    return status;
}

/* Parse an accountView array held under `key` into an owned account list. */
static wf_status wf_admin_parse_account_view_list(const char *json,
                                                  size_t json_len,
                                                  const char *key,
                                                  wf_admin_account_view_list *out) {
    if (!json || !out || !key) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_admin_account_view *items = NULL;
    cJSON **ptrs = NULL;
    if (count > 0) {
        items = (wf_admin_account_view *)calloc(count, sizeof(*items));
        ptrs = (cJSON **)calloc(count, sizeof(*ptrs));
        if (!items || !ptrs) {
            free(items);
            free(ptrs);
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
        /* Capture item pointers up front: detaching later shifts indices. */
        for (size_t i = 0; i < count; ++i) {
            ptrs[i] = cJSON_GetArrayItem(arr, (int)i);
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        wf_admin_account_view *v = &items[i];
        cJSON *obj = ptrs[i];
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_admin_read_account_view(obj, v);
        if (status == WF_OK) {
            wf_admin_strip_key(obj, "did");
            wf_admin_strip_key(obj, "handle");
            wf_admin_strip_key(obj, "email");
            wf_admin_strip_key(obj, "indexedAt");
            wf_admin_strip_key(obj, "invitesDisabled");
            wf_admin_strip_key(obj, "deactivatedAt");
            /* Take ownership of the item (with known keys removed) as `extra`. */
            v->extra = cJSON_DetachItemViaPointer(arr, obj);
        }
        if (status != WF_OK) {
            wf_admin_account_view_reset(v);
        }
    }
    free(ptrs);

    if (status == WF_OK) {
        out->accounts = items;
        out->account_count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_admin_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_admin_account_view_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

wf_status wf_admin_parse_account_infos(const char *json, size_t json_len,
                                       wf_admin_account_view_list *out) {
    return wf_admin_parse_account_view_list(json, json_len, "infos", out);
}

wf_status wf_admin_parse_search_accounts(const char *json, size_t json_len,
                                         wf_admin_account_view_list *out) {
    return wf_admin_parse_account_view_list(json, json_len, "accounts", out);
}

wf_status wf_admin_parse_subject_status(const char *json, size_t json_len,
                                        wf_admin_subject_status *out) {
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

    cJSON *subject = cJSON_DetachItemFromObject(root, "subject");
    if (!subject) {
        wf_admin_subject_status_reset(out);
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    out->subject = subject;
    if (cJSON_IsObject(subject)) {
        cJSON *sdid = cJSON_GetObjectItemCaseSensitive(subject, "did");
        if (cJSON_IsString(sdid) && sdid->valuestring) {
            status = wf_admin_set_string(&out->did, sdid->valuestring);
        }
    }

    if (status == WF_OK) {
        cJSON *takedown = cJSON_DetachItemFromObject(root, "takedown");
        if (takedown) {
            if (cJSON_IsObject(takedown)) {
                out->has_takedown = true;
                cJSON *applied =
                    cJSON_GetObjectItemCaseSensitive(takedown, "applied");
                if (cJSON_IsBool(applied)) {
                    out->takedown_applied = cJSON_IsTrue(applied);
                }
                cJSON *ref = cJSON_GetObjectItemCaseSensitive(takedown, "ref");
                if (cJSON_IsString(ref) && ref->valuestring) {
                    status = wf_admin_set_string(&out->takedown_ref,
                                                 ref->valuestring);
                }
            }
            cJSON_Delete(takedown);
        }
    }

    if (status == WF_OK) {
        cJSON *deactivated = cJSON_DetachItemFromObject(root, "deactivated");
        if (deactivated) {
            if (cJSON_IsObject(deactivated)) {
                out->has_deactivated = true;
                cJSON *applied =
                    cJSON_GetObjectItemCaseSensitive(deactivated, "applied");
                if (cJSON_IsBool(applied)) {
                    out->deactivated_applied = cJSON_IsTrue(applied);
                }
                cJSON *ref =
                    cJSON_GetObjectItemCaseSensitive(deactivated, "ref");
                if (cJSON_IsString(ref) && ref->valuestring) {
                    status = wf_admin_set_string(&out->deactivated_ref,
                                                 ref->valuestring);
                }
            }
            cJSON_Delete(deactivated);
        }
    }

    if (status != WF_OK) {
        wf_admin_subject_status_reset(out);
    }
    cJSON_Delete(root);
    return status;
}

/* Read the core fields of an inviteCode into `c`. Does not detach anything. */
static wf_status wf_admin_read_invite_code(cJSON *obj,
                                           wf_admin_invite_code *c) {
    wf_status status = WF_OK;
    cJSON *code = cJSON_GetObjectItemCaseSensitive(obj, "code");
    cJSON *avail = cJSON_GetObjectItemCaseSensitive(obj, "available");
    cJSON *dis = cJSON_GetObjectItemCaseSensitive(obj, "disabled");
    cJSON *for_acc = cJSON_GetObjectItemCaseSensitive(obj, "forAccount");
    cJSON *by = cJSON_GetObjectItemCaseSensitive(obj, "createdBy");
    cJSON *at = cJSON_GetObjectItemCaseSensitive(obj, "createdAt");

    if (cJSON_IsString(code) && code->valuestring) {
        status = wf_admin_set_string(&c->code, code->valuestring);
    }
    if (status == WF_OK && cJSON_IsNumber(avail)) {
        c->has_available = true;
        c->available = (int)avail->valuedouble;
    }
    if (status == WF_OK && cJSON_IsBool(dis)) {
        c->has_disabled = true;
        c->disabled = cJSON_IsTrue(dis);
    }
    if (status == WF_OK && cJSON_IsString(for_acc) && for_acc->valuestring) {
        status = wf_admin_set_string(&c->for_account, for_acc->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(by) && by->valuestring) {
        status = wf_admin_set_string(&c->created_by, by->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(at) && at->valuestring) {
        status = wf_admin_set_string(&c->created_at, at->valuestring);
    }
    return status;
}

wf_status wf_admin_parse_invite_codes(const char *json, size_t json_len,
                                      wf_admin_invite_code_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "codes");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_admin_invite_code *items = NULL;
    cJSON **ptrs = NULL;
    if (count > 0) {
        items = (wf_admin_invite_code *)calloc(count, sizeof(*items));
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
        wf_admin_invite_code *c = &items[i];
        cJSON *obj = ptrs[i];
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_admin_read_invite_code(obj, c);
        if (status == WF_OK) {
            wf_admin_strip_key(obj, "code");
            wf_admin_strip_key(obj, "available");
            wf_admin_strip_key(obj, "disabled");
            wf_admin_strip_key(obj, "forAccount");
            wf_admin_strip_key(obj, "createdBy");
            wf_admin_strip_key(obj, "createdAt");
            c->extra = cJSON_DetachItemViaPointer(arr, obj);
        }
        if (status != WF_OK) {
            wf_admin_invite_code_reset(c);
        }
    }
    free(ptrs);

    if (status == WF_OK) {
        out->codes = items;
        out->code_count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_admin_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_admin_invite_code_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_admin_account_view_free(wf_admin_account_view *v) {
    wf_admin_account_view_reset(v);
}

void wf_admin_account_view_list_free(wf_admin_account_view_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->account_count; ++i) {
        wf_admin_account_view_reset(&l->accounts[i]);
    }
    free(l->accounts);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

void wf_admin_subject_status_free(wf_admin_subject_status *s) {
    wf_admin_subject_status_reset(s);
}

void wf_admin_invite_code_free(wf_admin_invite_code *c) {
    wf_admin_invite_code_reset(c);
}

void wf_admin_invite_code_list_free(wf_admin_invite_code_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->code_count; ++i) {
        wf_admin_invite_code_reset(&l->codes[i]);
    }
    free(l->codes);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

/* ---- Agent convenience wrappers ---- */

wf_status wf_agent_admin_get_account_info(wf_agent *agent, const char *did,
                                          wf_admin_account_view *out) {
    if (!agent || !did || !did[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_admin_get_account_info_main_params params = {0};
    params.did = did;
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_com_atproto_admin_get_account_info_main_call(agent->client,
                                                            &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_admin_parse_account_view(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_admin_get_account_infos(wf_agent *agent,
                                           const char *const *dids, size_t n,
                                           wf_admin_account_view_list *out) {
    if (!agent || !dids || n == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < n; ++i) {
        if (!dids[i] || !dids[i][0]) {
            return WF_ERR_INVALID_ARG;
        }
    }
    wf_lex_com_atproto_admin_get_account_infos_main_params params = {0};
    params.dids.items = dids;
    params.dids.count = n;
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_com_atproto_admin_get_account_infos_main_call(agent->client,
                                                             &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_admin_parse_account_infos(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_admin_search_accounts(wf_agent *agent, const char *query,
                                         int limit, const char *cursor,
                                         wf_admin_account_view_list *out) {
    if (!agent || !query || !query[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_admin_search_accounts_main_params params = {0};
    /* searchAccounts currently only supports email filtering in the lexicon. */
    params.has_email = true;
    params.email = query;
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_com_atproto_admin_search_accounts_main_call(agent->client,
                                                           &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_admin_parse_search_accounts(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_admin_get_subject_status(wf_agent *agent, const char *did,
                                            wf_admin_subject_status *out) {
    if (!agent || !did || !did[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_admin_get_subject_status_main_params params = {0};
    params.has_did = true;
    params.did = did;
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_com_atproto_admin_get_subject_status_main_call(agent->client,
                                                              &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_admin_parse_subject_status(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_admin_get_invite_codes(wf_agent *agent, const char *cursor,
                                          wf_admin_invite_code_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_admin_get_invite_codes_main_params params = {0};
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_com_atproto_admin_get_invite_codes_main_call(agent->client,
                                                            &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_admin_parse_invite_codes(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_admin_update_subject_status(wf_agent *agent, const char *did,
                                               int moderated, int deactivated,
                                               const char *reason) {
    if (!agent || !did || !did[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_admin_update_subject_status_main_input input = {0};
    /* subject must be a repoRef for account subjects. */
    size_t need = strlen(did) + 96;
    char *subject_json = (char *)malloc(need);
    if (!subject_json) {
        return WF_ERR_ALLOC;
    }
    snprintf(subject_json, need,
             "{\"$type\":\"com.atproto.admin.defs#repoRef\",\"did\":\"%s\"}", did);
    input.subject.kind = -1;
    input.subject.data = subject_json;
    input.subject.length = strlen(subject_json);

    wf_lex_com_atproto_admin_defs_status_attr takedown = {0};
    takedown.applied = moderated != 0;
    if (reason) {
        takedown.has_ref = true;
        takedown.ref = reason;
    }
    input.has_takedown = true;
    input.takedown = &takedown;

    wf_lex_com_atproto_admin_defs_status_attr deact = {0};
    deact.applied = deactivated != 0;
    if (reason) {
        deact.has_ref = true;
        deact.ref = reason;
    }
    input.has_deactivated = true;
    input.deactivated = &deact;

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_com_atproto_admin_update_subject_status_main_call(agent->client,
                                                                 &input, &res);
    free(subject_json);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_admin_update_account_handle(wf_agent *agent, const char *did,
                                               const char *new_handle) {
    if (!agent || !did || !did[0] || !new_handle || !new_handle[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_admin_update_account_handle_main_input input = {0};
    input.did = did;
    input.handle = new_handle;
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_com_atproto_admin_update_account_handle_main_call(agent->client,
                                                                &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_admin_update_account_email(wf_agent *agent, const char *did,
                                              const char *email) {
    if (!agent || !did || !did[0] || !email || !email[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_admin_update_account_email_main_input input = {0};
    input.account = did;
    input.email = email;
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_com_atproto_admin_update_account_email_main_call(agent->client,
                                                               &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_admin_update_account_password(wf_agent *agent,
                                                 const char *did,
                                                 const char *new_password) {
    if (!agent || !did || !did[0] || !new_password || !new_password[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_admin_update_account_password_main_input input = {0};
    input.did = did;
    input.password = new_password;
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_com_atproto_admin_update_account_password_main_call(agent->client,
                                                                  &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_admin_update_account_signing_key(wf_agent *agent,
                                                    const char *did,
                                                    const char *signing_key_multibase) {
    if (!agent || !did || !did[0] || !signing_key_multibase ||
        !signing_key_multibase[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_admin_update_account_signing_key_main_input input = {0};
    input.did = did;
    input.signing_key = signing_key_multibase;
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_com_atproto_admin_update_account_signing_key_main_call(
            agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_admin_delete_account(wf_agent *agent, const char *did) {
    if (!agent || !did || !did[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_admin_delete_account_main_input input = {0};
    input.did = did;
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_com_atproto_admin_delete_account_main_call(agent->client, &input,
                                                          &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_admin_enable_account_invites(wf_agent *agent) {
    if (!agent) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_admin_enable_account_invites_main_input input = {0};
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_com_atproto_admin_enable_account_invites_main_call(agent->client,
                                                                 &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_admin_disable_account_invites(wf_agent *agent) {
    if (!agent) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_admin_disable_account_invites_main_input input = {0};
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_com_atproto_admin_disable_account_invites_main_call(agent->client,
                                                                  &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_admin_disable_invite_codes(wf_agent *agent,
                                              const char *cursor) {
    if (!agent || cursor) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_admin_disable_invite_codes_typed(agent, NULL, 0, NULL, 0);
}

wf_status wf_agent_admin_disable_invite_codes_typed(
    wf_agent *agent, const char *const *codes, size_t code_count,
    const char *const *accounts, size_t account_count) {
    if (!agent || !agent->client || (code_count && !codes) ||
        (account_count && !accounts)) {
        return WF_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < code_count; ++i)
        if (!codes[i]) return WF_ERR_INVALID_ARG;
    for (size_t i = 0; i < account_count; ++i)
        if (!accounts[i]) return WF_ERR_INVALID_ARG;

    wf_lex_com_atproto_admin_disable_invite_codes_main_input input = {0};
    if (codes) {
        input.has_codes = true;
        input.codes.items = codes;
        input.codes.count = code_count;
    }
    if (accounts) {
        input.has_accounts = true;
        input.accounts.items = accounts;
        input.accounts.count = account_count;
    }
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_com_atproto_admin_disable_invite_codes_main_call(agent->client,
                                                               &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_admin_send_email(wf_agent *agent, const char *recipient_did,
                                    const char *content, const char *subject,
                                    const char *sender_did) {
    if (!agent || !recipient_did || !recipient_did[0] || !content ||
        !content[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_admin_send_email_main_input input = {0};
    input.recipient_did = recipient_did;
    input.content = content;
    if (subject && subject[0]) {
        input.has_subject = true;
        input.subject = subject;
    }
    input.sender_did = sender_did;
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_com_atproto_admin_send_email_main_call(agent->client, &input,
                                                      &res);
    wf_response_free(&res);
    return status;
}
