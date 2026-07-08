/*
 * contact_typed.c — owned typed parsers + agent convenience wrappers for the
 * app.bsky.contact namespace. See include/wolfram/contact_typed.h for the
 * public API, the authoritative wire format, and ownership rules.
 *
 * Mirrors feed_typed.c / actor_typed.c: static strdup/set_string/reset helpers,
 * owned strings, and full cleanup on the first error. The agent wrappers call
 * the generated lex wrappers directly (`wf_lex_app_bsky_contact_*_main_call`)
 * after syncing auth onto the agent's primary XRPC client, exactly as the
 * other agent_* translation units do via `wf_agent_sync_auth`.
 */

#include "wolfram/contact_typed.h"

#include "agent/_internal.h"
#include "wolfram/atproto_lex.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_contact_strdup(const char *s) {
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

static wf_status wf_contact_set_string(char **dst, const char *src) {
    char *copy = wf_contact_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static void wf_contact_match_reset(wf_contact_match *m) {
    if (!m) {
        return;
    }
    free(m->did);
    free(m->display_name);
    free(m->avatar);
    memset(m, 0, sizeof(*m));
}

/* Parse the three core profileView fields (did/displayName/avatar). */
static wf_status wf_contact_parse_match(wf_contact_match *m, cJSON *obj) {
    wf_status status = WF_OK;
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");
    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_contact_set_string(&m->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_contact_set_string(&m->display_name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        status = wf_contact_set_string(&m->avatar, avatar->valuestring);
    }
    return status;
}

/* ---- getMatches ---- */

wf_status wf_contact_parse_matches(const char *json, size_t json_len,
                                   wf_contact_match_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "matches");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_contact_match *items = NULL;
    if (count > 0) {
        items = (wf_contact_match *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_contact_parse_match(&items[i], obj);
        if (status != WF_OK) {
            wf_contact_match_reset(&items[i]);
        }
    }

    if (status == WF_OK) {
        out->items = items;
        out->count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_contact_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_contact_match_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_contact_match_list_free(wf_contact_match_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; ++i) {
        wf_contact_match_reset(&list->items[i]);
    }
    free(list->items);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

/* ---- importContacts ---- */

wf_status wf_contact_parse_import(const char *json, size_t json_len,
                                  wf_contact_import_result *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr =
        cJSON_GetObjectItemCaseSensitive(root, "matchesAndContactIndexes");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_contact_import_match *items = NULL;
    if (count > 0) {
        items = (wf_contact_import_match *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        wf_contact_import_match *entry = &items[i];
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        cJSON *match = cJSON_GetObjectItemCaseSensitive(obj, "match");
        cJSON *idx = cJSON_GetObjectItemCaseSensitive(obj, "contactIndex");
        if (!cJSON_IsObject(match)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_contact_parse_match(&entry->match, match);
        if (status != WF_OK) {
            wf_contact_match_reset(&entry->match);
            break;
        }
        entry->contact_index =
            (cJSON_IsNumber(idx)) ? (int64_t)idx->valuedouble : 0;
    }

    if (status == WF_OK) {
        out->items = items;
        out->count = count;
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_contact_match_reset(&items[i].match);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_contact_import_result_free(wf_contact_import_result *out) {
    if (!out) {
        return;
    }
    for (size_t i = 0; i < out->count; ++i) {
        wf_contact_match_reset(&out->items[i].match);
    }
    free(out->items);
    memset(out, 0, sizeof(*out));
}

/* ---- getSyncStatus ---- */

wf_status wf_contact_parse_sync_status(const char *json, size_t json_len,
                                       wf_contact_sync_status *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *status_obj = cJSON_GetObjectItemCaseSensitive(root, "syncStatus");
    if (cJSON_IsObject(status_obj)) {
        out->has_status = 1;
        cJSON *synced = cJSON_GetObjectItemCaseSensitive(status_obj, "syncedAt");
        cJSON *matches =
            cJSON_GetObjectItemCaseSensitive(status_obj, "matchesCount");
        if (cJSON_IsString(synced) && synced->valuestring) {
            status = wf_contact_set_string(&out->last_synced_at,
                                           synced->valuestring);
        }
        if (status == WF_OK && cJSON_IsNumber(matches)) {
            out->matches_count = (int64_t)matches->valuedouble;
        }
    } else {
        out->has_status = 0;
    }

    if (status != WF_OK) {
        free(out->last_synced_at);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_contact_sync_status_free(wf_contact_sync_status *out) {
    if (!out) {
        return;
    }
    free(out->last_synced_at);
    memset(out, 0, sizeof(*out));
}

/* ---- Agent convenience wrappers ---- */

wf_status wf_agent_get_contact_matches_typed(wf_agent *agent, int limit,
                                             const char *cursor,
                                             wf_contact_match_list *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_contact_match_list list = {0};
    wf_lex_app_bsky_contact_get_matches_main_params params = {0};
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor) {
        params.has_cursor = true;
        params.cursor = cursor;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status =
        wf_lex_app_bsky_contact_get_matches_main_call(agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_contact_parse_matches(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_import_contacts(wf_agent *agent, const char *token,
                                   const char *const *contacts,
                                   size_t contact_count,
                                   wf_contact_import_result *out) {
    if (!agent || !agent->client || !token || !contacts || contact_count == 0 ||
        !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_contact_import_result result = {0};
    wf_lex_app_bsky_contact_import_contacts_main_input input = {0};
    input.token = token;
    input.contacts.items = contacts;
    input.contacts.count = contact_count;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_contact_import_contacts_main_call(
        agent->client, &input, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_contact_parse_import(res.body, res.body_len, &result);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = result;
    }
    return status;
}

wf_status wf_agent_get_contact_sync_status(wf_agent *agent,
                                           wf_contact_sync_status *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_contact_sync_status status_out = {0};
    wf_lex_app_bsky_contact_get_sync_status_main_params params = {0};

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_contact_get_sync_status_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_contact_parse_sync_status(res.body, res.body_len, &status_out);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = status_out;
    }
    return status;
}

wf_status wf_agent_contact_send_notification(wf_agent *agent, const char *did) {
    if (!agent || !agent->client || !did) {
        return WF_ERR_INVALID_ARG;
    }
    if (!agent->session || !agent->session->data.did) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_app_bsky_contact_send_notification_main_input input = {0};
    input.from_ = agent->session->data.did;
    input.to = did;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_contact_send_notification_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_contact_start_phone_verification(wf_agent *agent,
                                                    const char *phone_number) {
    if (!agent || !agent->client || !phone_number) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_app_bsky_contact_start_phone_verification_main_input input = {0};
    input.phone = phone_number;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status =
        wf_lex_app_bsky_contact_start_phone_verification_main_call(agent->client,
                                                                   &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_contact_verify_phone(wf_agent *agent,
                                        const char *phone_number,
                                        const char *code) {
    if (!agent || !agent->client || !phone_number || !code) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_app_bsky_contact_verify_phone_main_input input = {0};
    input.phone = phone_number;
    input.code = code;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_contact_verify_phone_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_contact_dismiss_match(wf_agent *agent, const char *did) {
    if (!agent || !agent->client || !did) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_app_bsky_contact_dismiss_match_main_input input = {0};
    input.subject = did;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_contact_dismiss_match_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_contact_remove_data(wf_agent *agent) {
    if (!agent || !agent->client) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_app_bsky_contact_remove_data_main_input input = {0};

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_contact_remove_data_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}
