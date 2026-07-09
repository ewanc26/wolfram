/*
 * notification_typed.c — owned typed parsers + agent wrappers for the
 * app.bsky.notification CORE endpoints. See include/wolfram/notification_typed.h
 * for the public API, the authoritative wire format, and ownership rules.
 *
 * Mirrors actor_typed.c / labeler_typed.c: static strdup/set_string/reset
 * helpers, owned strings, detached `extra`/`record`/`labels` cJSON subtrees,
 * and full cleanup on the first error. The agent wrappers call the generated
 * lex wrappers directly after syncing auth via wf_agent_sync_auth.
 */

#include "wolfram/notification_typed.h"

#include "agent/_internal.h"
#include "wolfram/atproto_lex.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_notif_strdup(const char *s) {
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

static wf_status wf_notif_set_string(char **dst, const char *src) {
    char *copy = wf_notif_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

/* ---- notification view ---- */

static void wf_notification_view_reset(wf_notification_view *v) {
    if (!v) {
        return;
    }
    free(v->uri);
    free(v->cid);
    free(v->author.did);
    free(v->author.handle);
    free(v->author.display_name);
    free(v->author.avatar);
    free(v->reason);
    free(v->reason_subject);
    free(v->indexed_at);
    if (v->record) {
        cJSON_Delete(v->record);
    }
    if (v->labels) {
        cJSON_Delete(v->labels);
    }
    if (v->extra) {
        cJSON_Delete(v->extra);
    }
    memset(v, 0, sizeof(*v));
}

/* Parse a single notification object (in place) into `v`. Known fields are
 * detached so the remaining `obj` becomes `v->extra`. Returns
 * WF_OK / WF_ERR_PARSE / WF_ERR_ALLOC. On error `v` is reset. */
static wf_status wf_notification_parse_view(cJSON *obj, wf_notification_view *v) {
    wf_status status = WF_OK;
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(obj, "cid");
    cJSON *author = cJSON_GetObjectItemCaseSensitive(obj, "author");
    cJSON *reason = cJSON_GetObjectItemCaseSensitive(obj, "reason");
    cJSON *rs = cJSON_GetObjectItemCaseSensitive(obj, "reasonSubject");
    cJSON *record = cJSON_GetObjectItemCaseSensitive(obj, "record");
    cJSON *is_read = cJSON_GetObjectItemCaseSensitive(obj, "isRead");
    cJSON *indexed = cJSON_GetObjectItemCaseSensitive(obj, "indexedAt");
    cJSON *labels = cJSON_GetObjectItemCaseSensitive(obj, "labels");

    if (cJSON_IsString(uri) && uri->valuestring) {
        status = wf_notif_set_string(&v->uri, uri->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring) {
        status = wf_notif_set_string(&v->cid, cid->valuestring);
    }
    if (status == WF_OK && cJSON_IsObject(author)) {
        cJSON *did = cJSON_GetObjectItemCaseSensitive(author, "did");
        cJSON *handle = cJSON_GetObjectItemCaseSensitive(author, "handle");
        cJSON *name = cJSON_GetObjectItemCaseSensitive(author, "displayName");
        cJSON *avatar = cJSON_GetObjectItemCaseSensitive(author, "avatar");
        if (cJSON_IsString(did) && did->valuestring) {
            status = wf_notif_set_string(&v->author.did, did->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
            status = wf_notif_set_string(&v->author.handle, handle->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
            status = wf_notif_set_string(&v->author.display_name,
                                         name->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
            status = wf_notif_set_string(&v->author.avatar, avatar->valuestring);
        }
    }
    if (status == WF_OK && cJSON_IsString(reason) && reason->valuestring) {
        status = wf_notif_set_string(&v->reason, reason->valuestring);
    }
    if (status == WF_OK && rs != NULL) {
        if (cJSON_IsString(rs) && rs->valuestring) {
            status = wf_notif_set_string(&v->reason_subject, rs->valuestring);
        } else if (!cJSON_IsNull(rs)) {
            status = WF_ERR_PARSE;
        }
    }
    if (status == WF_OK) {
        if (cJSON_IsBool(is_read)) {
            v->is_read = cJSON_IsTrue(is_read);
        } else if (is_read != NULL) {
            status = WF_ERR_PARSE;
        }
    }
    if (status == WF_OK && cJSON_IsString(indexed) && indexed->valuestring) {
        status = wf_notif_set_string(&v->indexed_at, indexed->valuestring);
    }
    if (status == WF_OK) {
        cJSON *rec = cJSON_DetachItemFromObject(obj, "record");
        if (rec) {
            if (cJSON_IsObject(rec) || cJSON_IsArray(rec)) {
                v->record = rec;
            } else {
                cJSON_Delete(rec);
            }
        }
    }
    if (status == WF_OK) {
        cJSON *lbl = cJSON_DetachItemFromObject(obj, "labels");
        if (lbl) {
            if (cJSON_IsArray(lbl)) {
                v->labels = lbl;
            } else {
                cJSON_Delete(lbl);
            }
        }
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "uri");
        cJSON_DetachItemFromObject(obj, "cid");
        cJSON_DetachItemFromObject(obj, "author");
        cJSON_DetachItemFromObject(obj, "reason");
        cJSON_DetachItemFromObject(obj, "reasonSubject");
        cJSON_DetachItemFromObject(obj, "isRead");
        cJSON_DetachItemFromObject(obj, "indexedAt");
        /* `obj` is still a child of the notifications array; ownership of the
         * remaining fields is transferred to `extra` by the caller via
         * cJSON_DetachItemViaPointer, so we do NOT take it here. */
    } else {
        wf_notification_view_reset(v);
    }
    return status;
}

wf_status wf_notification_parse_list(const char *json, size_t json_len,
                                     wf_notification_list *out) {
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
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "notifications");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_notification_view *items = NULL;
    cJSON **ptrs = NULL;
    if (count > 0) {
        items = (wf_notification_view *)calloc(count, sizeof(*items));
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
        status = wf_notification_parse_view(obj, &items[i]);
        if (status == WF_OK) {
            items[i].extra = cJSON_DetachItemViaPointer(arr, obj);
        } else {
            wf_notification_view_reset(&items[i]);
        }
    }
    free(ptrs);

    if (status == WF_OK) {
        out->items = items;
        out->count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_notif_set_string(&out->cursor, cursor->valuestring);
        }
        cJSON *seen = cJSON_GetObjectItemCaseSensitive(root, "seenAt");
        if (status == WF_OK && cJSON_IsString(seen) && seen->valuestring) {
            status = wf_notif_set_string(&out->seen_at, seen->valuestring);
        }
        cJSON *pri = cJSON_GetObjectItemCaseSensitive(root, "priority");
        if (status == WF_OK && cJSON_IsBool(pri)) {
            out->has_priority = true;
            out->priority = cJSON_IsTrue(pri);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_notification_view_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

wf_status wf_notification_parse_unread_count(const char *json, size_t json_len,
                                             wf_notification_unread_count *out) {
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

    cJSON *count = cJSON_GetObjectItemCaseSensitive(root, "count");
    if (!cJSON_IsNumber(count)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    out->count = (int64_t)count->valuedouble;
    cJSON_Delete(root);
    return WF_OK;
}

void wf_notification_list_free(wf_notification_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; ++i) {
        wf_notification_view_reset(&list->items[i]);
    }
    free(list->items);
    free(list->cursor);
    free(list->seen_at);
    memset(list, 0, sizeof(*list));
}

/* ---- Agent wrappers ---- */

wf_status wf_agent_list_notifications_rich_typed(wf_agent *agent, int limit,
                                                 const char *cursor,
                                                 wf_notification_list *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_notification_list list = {0};
    wf_lex_app_bsky_notification_list_notifications_main_params params = {0};
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_notification_list_notifications_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_notification_parse_list(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_get_unread_count_rich_typed(wf_agent *agent,
                                               int64_t *out_count) {
    if (!agent || !agent->client || !out_count) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_app_bsky_notification_get_unread_count_main_params params = {0};

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_notification_get_unread_count_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    wf_notification_unread_count uc = {0};
    status = wf_notification_parse_unread_count(res.body, res.body_len, &uc);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out_count = uc.count;
    }
    return status;
}

wf_status wf_agent_update_seen_typed(wf_agent *agent, const char *seen_at) {
    if (!agent || !agent->client || !seen_at || !seen_at[0]) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_app_bsky_notification_update_seen_main_input input = {0};
    input.seen_at = seen_at;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_notification_update_seen_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_register_push_typed(wf_agent *agent, const char *service_did,
                                       const char *token, const char *platform,
                                       const char *app_id, bool has_age_restricted,
                                       bool age_restricted) {
    if (!agent || !agent->client || !service_did || !service_did[0] || !token ||
        !token[0] || !platform || !platform[0] || !app_id || !app_id[0]) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_app_bsky_notification_register_push_main_input input = {0};
    input.service_did = service_did;
    input.token = token;
    input.platform = platform;
    input.app_id = app_id;
    input.has_age_restricted = has_age_restricted;
    input.age_restricted = age_restricted;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_notification_register_push_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_unregister_push_typed(wf_agent *agent, const char *service_did,
                                         const char *token, const char *platform,
                                         const char *app_id) {
    if (!agent || !agent->client || !service_did || !service_did[0] || !token ||
        !token[0] || !platform || !platform[0] || !app_id || !app_id[0]) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_app_bsky_notification_unregister_push_main_input input = {0};
    input.service_did = service_did;
    input.token = token;
    input.platform = platform;
    input.app_id = app_id;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_notification_unregister_push_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}
