#include "wolfram/agent.h"

#include "wolfram/identity.h"
#include "wolfram/repo.h"
#include "wolfram/richtext.h"
#include "wolfram/server.h"
#include "wolfram/session.h"
#include "wolfram/syntax.h"
#include <cJSON.h>
#include "wolfram/atproto_lex.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Duplicate helper definitions (static) */

#include "_internal.h"

/* Notification endpoint implementations */

/* Local copies of the small string/reset helpers used across the agent
 * sources (kept static per translation unit to avoid linkage conflicts). */
static char *wf_agent_notif_strdup(const char *s) {
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

static wf_status wf_agent_notif_set_string(char **dst, const char *src) {
    char *copy = wf_agent_notif_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static void wf_agent_label_reset(wf_agent_label *label) {
    if (!label) {
        return;
    }
    free(label->src);
    free(label->uri);
    free(label->val);
    free(label->cts);
    memset(label, 0, sizeof(*label));
}

static void wf_agent_notification_reset(wf_agent_notification *n) {
    if (!n) {
        return;
    }
    free(n->uri);
    free(n->cid);
    free(n->author.did);
    free(n->author.handle);
    free(n->author.display_name);
    free(n->author.avatar);
    free(n->reason);
    free(n->reason_subject);
    if (n->record) {
        cJSON_Delete(n->record);
    }
    free(n->indexed_at);
    for (size_t i = 0; i < n->label_count; ++i) {
        wf_agent_label_reset(&n->labels[i]);
    }
    free(n->labels);
    memset(n, 0, sizeof(*n));
}

wf_status wf_agent_parse_notifications(const char *json, size_t json_len,
                                       wf_agent_notification_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *notifications = cJSON_GetObjectItemCaseSensitive(root, "notifications");
    if (!cJSON_IsArray(notifications)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(notifications);
    wf_agent_notification *items = NULL;
    if (count > 0) {
        items = (wf_agent_notification *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        wf_agent_notification *n = &items[i];
        cJSON *obj = cJSON_GetArrayItem(notifications, (int)i);

        cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
        cJSON *cid = cJSON_GetObjectItemCaseSensitive(obj, "cid");
        cJSON *author = cJSON_GetObjectItemCaseSensitive(obj, "author");
        cJSON *reason = cJSON_GetObjectItemCaseSensitive(obj, "reason");
        cJSON *reason_subject = cJSON_GetObjectItemCaseSensitive(obj, "reasonSubject");
        cJSON *is_read = cJSON_GetObjectItemCaseSensitive(obj, "isRead");
        cJSON *indexed_at = cJSON_GetObjectItemCaseSensitive(obj, "indexedAt");
        cJSON *labels = cJSON_GetObjectItemCaseSensitive(obj, "labels");

        if (cJSON_IsString(uri) && uri->valuestring) {
            status = wf_agent_notif_set_string(&n->uri, uri->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring) {
            status = wf_agent_notif_set_string(&n->cid, cid->valuestring);
        }

        if (status == WF_OK && cJSON_IsObject(author)) {
            cJSON *a_did = cJSON_GetObjectItemCaseSensitive(author, "did");
            cJSON *a_handle = cJSON_GetObjectItemCaseSensitive(author, "handle");
            cJSON *a_name = cJSON_GetObjectItemCaseSensitive(author, "displayName");
            cJSON *a_avatar = cJSON_GetObjectItemCaseSensitive(author, "avatar");
            if (cJSON_IsString(a_did) && a_did->valuestring) {
                status = wf_agent_notif_set_string(&n->author.did, a_did->valuestring);
            }
            if (status == WF_OK && cJSON_IsString(a_handle) && a_handle->valuestring) {
                status = wf_agent_notif_set_string(&n->author.handle, a_handle->valuestring);
            }
            if (status == WF_OK && cJSON_IsString(a_name) && a_name->valuestring) {
                status = wf_agent_notif_set_string(&n->author.display_name, a_name->valuestring);
            }
            if (status == WF_OK && cJSON_IsString(a_avatar) && a_avatar->valuestring) {
                status = wf_agent_notif_set_string(&n->author.avatar, a_avatar->valuestring);
            }
        }

        if (status == WF_OK && cJSON_IsString(reason) && reason->valuestring) {
            status = wf_agent_notif_set_string(&n->reason, reason->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(reason_subject) && reason_subject->valuestring) {
            status = wf_agent_notif_set_string(&n->reason_subject, reason_subject->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(indexed_at) && indexed_at->valuestring) {
            status = wf_agent_notif_set_string(&n->indexed_at, indexed_at->valuestring);
        }
        if (status == WF_OK && cJSON_IsBool(is_read)) {
            n->is_read = cJSON_IsTrue(is_read) ? 1 : 0;
        }

        /* Take ownership of the `record` subtree (type `unknown`). */
        if (status == WF_OK) {
            cJSON *record = cJSON_DetachItemFromObject(obj, "record");
            if (record) {
                n->record = record;
            }
        }

        /* Parse labels (com.atproto.label.defs#label). */
        if (status == WF_OK && cJSON_IsArray(labels)) {
            int label_count = cJSON_GetArraySize(labels);
            if (label_count > 0) {
                n->labels = (wf_agent_label *)calloc((size_t)label_count, sizeof(*n->labels));
                if (!n->labels) {
                    status = WF_ERR_ALLOC;
                }
                for (int l = 0; l < label_count && status == WF_OK; ++l) {
                    cJSON *lab = cJSON_GetArrayItem(labels, l);
                    cJSON *l_src = cJSON_GetObjectItemCaseSensitive(lab, "src");
                    cJSON *l_uri = cJSON_GetObjectItemCaseSensitive(lab, "uri");
                    cJSON *l_val = cJSON_GetObjectItemCaseSensitive(lab, "val");
                    cJSON *l_cts = cJSON_GetObjectItemCaseSensitive(lab, "cts");
                    wf_agent_label *out_label = &n->labels[n->label_count];
                    if (cJSON_IsString(l_src) && l_src->valuestring) {
                        status = wf_agent_notif_set_string(&out_label->src, l_src->valuestring);
                    }
                    if (status == WF_OK && cJSON_IsString(l_uri) && l_uri->valuestring) {
                        status = wf_agent_notif_set_string(&out_label->uri, l_uri->valuestring);
                    }
                    if (status == WF_OK && cJSON_IsString(l_val) && l_val->valuestring) {
                        status = wf_agent_notif_set_string(&out_label->val, l_val->valuestring);
                    }
                    if (status == WF_OK && cJSON_IsString(l_cts) && l_cts->valuestring) {
                        status = wf_agent_notif_set_string(&out_label->cts, l_cts->valuestring);
                    }
                    if (status == WF_OK) {
                        n->label_count++;
                    } else {
                        wf_agent_label_reset(out_label);
                    }
                }
            }
        }

        if (status != WF_OK) {
            wf_agent_notification_reset(n);
        }
    }

    if (status == WF_OK) {
        out->notifications = items;
        out->notification_count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_agent_notif_set_string(&out->cursor, cursor->valuestring);
        }

        cJSON *seen_at = cJSON_GetObjectItemCaseSensitive(root, "seenAt");
        if (status == WF_OK && cJSON_IsString(seen_at) && seen_at->valuestring) {
            status = wf_agent_notif_set_string(&out->seen_at, seen_at->valuestring);
        }

        cJSON *priority = cJSON_GetObjectItemCaseSensitive(root, "priority");
        if (status == WF_OK && cJSON_IsBool(priority)) {
            out->has_priority = 1;
            out->priority = cJSON_IsTrue(priority) ? 1 : 0;
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_agent_notification_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_notification_list_free(wf_agent_notification_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->notification_count; ++i) {
        wf_agent_notification_reset(&list->notifications[i]);
    }
    free(list->notifications);
    free(list->cursor);
    free(list->seen_at);
    memset(list, 0, sizeof(*list));
}

wf_status wf_agent_list_notifications_typed(wf_agent *agent, int limit,
                                            const char *cursor,
                                            wf_agent_notification_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
    if (!agent->client) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_list_notifications(agent, limit, cursor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_notifications(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_unread_count_typed(wf_agent *agent, int *out_count) {
    if (!agent || !out_count) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_unread_count(agent, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
    if (!root) {
        wf_response_free(&res);
        return WF_ERR_PARSE;
    }

    cJSON *count = cJSON_GetObjectItemCaseSensitive(root, "count");
    if (!cJSON_IsNumber(count)) {
        cJSON_Delete(root);
        wf_response_free(&res);
        return WF_ERR_PARSE;
    }

    double value = count->valuedouble;
    if (value > (double)INT_MAX) {
        *out_count = INT_MAX;
    } else if (value < (double)INT_MIN) {
        *out_count = INT_MIN;
    } else {
        *out_count = (int)value;
    }

    cJSON_Delete(root);
    wf_response_free(&res);
    return WF_OK;
}

wf_status wf_agent_list_notifications(wf_agent *agent, int limit, const char *cursor,
                                       wf_response *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
    if (!agent->client) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[2];
    size_t param_count = 0;
    char limit_buf[16];

    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf))) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "limit";
        params[param_count].value = limit_buf;
        param_count++;
    }
    if (cursor && cursor[0]) {
        params[param_count].name = "cursor";
        params[param_count].value = cursor;
        param_count++;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.notification.listNotifications",
                               params, param_count, out);
}

wf_status wf_agent_update_seen_notifications(wf_agent *agent, const char *seen_at) {
    if (!agent || !seen_at) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_syntax_datetime_is_valid(seen_at)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(root, "seenAt", seen_at)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    wf_agent_sync_auth(agent);

    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(agent->client,
                                         "app.bsky.notification.updateSeen",
                                         json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_unread_count(wf_agent *agent, wf_response *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client,
                                "app.bsky.notification.getUnreadCount",
                                NULL, 0, out);
}
