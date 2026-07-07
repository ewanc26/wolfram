/*
 * moderation_actions.c — typed parsers and agent wrappers for moderation /
 * social-graph action procedures (com.atproto.moderation.createReport,
 * app.bsky.graph.muteActorList, app.bsky.graph.blockActorList).
 *
 * Parsers mirror notification.c / feed_typed.c: static strdup/set_string/reset
 * helpers, ownership via cJSON, and a full reset-on-error contract. The
 * wrappers build a cJSON input body, call wf_xrpc_procedure (mirroring
 * wf_agent_update_seen_notifications), then parse the response body.
 */

#include "wolfram/moderation_actions.h"
#include "wolfram/agent.h"
#include "_internal.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_mod_strdup(const char *s) {
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

static wf_status wf_mod_set_string(char **dst, const char *src) {
    char *copy = wf_mod_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static void wf_mod_list_view_reset(wf_mod_list_view *v) {
    if (!v) {
        return;
    }
    free(v->uri);
    free(v->cid);
    free(v->name);
    free(v->purpose);
    free(v->description);
    free(v->avatar);
    memset(v, 0, sizeof(*v));
}

static void wf_mod_report_reset(wf_moderation_report *r) {
    if (!r) {
        return;
    }
    free(r->id);
    free(r->reason);
    free(r->subject_uri);
    memset(r, 0, sizeof(*r));
}

static void wf_mod_list_view_result_reset(wf_mod_list_view_result *r) {
    if (!r) {
        return;
    }
    wf_mod_list_view_reset(&r->list);
    free(r->cursor);
    memset(r, 0, sizeof(*r));
}

/* Parse the `subject` union of a createReport output. The union is either a
 * repoRef `{ "repo": did }` or a strongRef `{ "uri": at-uri, "cid": ... }`.
 * We capture whichever string is present into `out->subject_uri`. */
static wf_status wf_mod_parse_subject(wf_moderation_report *out, cJSON *subject) {
    if (!cJSON_IsObject(subject)) {
        return WF_OK;
    }
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(subject, "uri");
    cJSON *repo = cJSON_GetObjectItemCaseSensitive(subject, "repo");
    if (cJSON_IsString(uri) && uri->valuestring) {
        return wf_mod_set_string(&out->subject_uri, uri->valuestring);
    }
    if (cJSON_IsString(repo) && repo->valuestring) {
        return wf_mod_set_string(&out->subject_uri, repo->valuestring);
    }
    return WF_OK;
}

wf_status wf_agent_parse_report(const char *json, size_t json_len,
                                wf_moderation_report *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;

    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (cJSON_IsNumber(id)) {
        char buf[32];
        int n = snprintf(buf, sizeof(buf), "%lld",
                         (long long)id->valuedouble);
        if (n > 0 && n < (int)sizeof(buf)) {
            status = wf_mod_set_string(&out->id, buf);
        } else {
            status = WF_ERR_ALLOC;
        }
    }

    if (status == WF_OK) {
        cJSON *reason = cJSON_GetObjectItemCaseSensitive(root, "reason");
        if (cJSON_IsString(reason) && reason->valuestring) {
            status = wf_mod_set_string(&out->reason, reason->valuestring);
        }
    }

    if (status == WF_OK) {
        cJSON *subject = cJSON_GetObjectItemCaseSensitive(root, "subject");
        status = wf_mod_parse_subject(out, subject);
    }

    if (status != WF_OK) {
        wf_mod_report_reset(out);
    }

    cJSON_Delete(root);
    return status;
}

void wf_moderation_report_free(wf_moderation_report *report) {
    wf_mod_report_reset(report);
}

static wf_status wf_mod_parse_list_view(wf_mod_list_view *v, cJSON *obj) {
    wf_status status = WF_OK;

    cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(obj, "cid");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "name");
    cJSON *purpose = cJSON_GetObjectItemCaseSensitive(obj, "purpose");
    cJSON *description = cJSON_GetObjectItemCaseSensitive(obj, "description");
    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");

    if (cJSON_IsString(uri) && uri->valuestring) {
        status = wf_mod_set_string(&v->uri, uri->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring) {
        status = wf_mod_set_string(&v->cid, cid->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_mod_set_string(&v->name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(purpose) && purpose->valuestring) {
        status = wf_mod_set_string(&v->purpose, purpose->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(description) && description->valuestring) {
        status = wf_mod_set_string(&v->description, description->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        status = wf_mod_set_string(&v->avatar, avatar->valuestring);
    }

    return status;
}

wf_status wf_agent_parse_list_view_result(const char *json, size_t json_len,
                                          wf_mod_list_view_result *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;

    cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "list");
    if (!cJSON_IsObject(list)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    status = wf_mod_parse_list_view(&out->list, list);

    if (status == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_mod_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        wf_mod_list_view_result_reset(out);
    }

    cJSON_Delete(root);
    return status;
}

void wf_mod_list_view_result_free(wf_mod_list_view_result *result) {
    wf_mod_list_view_result_reset(result);
}

/* Shared wrapper for the two list-action procedures (muteActorList /
 * blockActorList): they share the identical input/output shape. */
static wf_status wf_mod_actor_list_action(wf_agent *agent, const char *nsid,
                                          const char *list_at_uri,
                                          wf_mod_list_view_result *out) {
    if (!agent || !list_at_uri || !out) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(root, "list", list_at_uri)) {
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
    wf_status status = wf_xrpc_procedure(agent->client, nsid, json, &res);
    free(json);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_list_view_result(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_create_report(wf_agent *agent, const char *reason,
                                 const char *subject_uri,
                                 const char *subject_repo_did,
                                 wf_moderation_report *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    /* Exactly one of subject_uri / subject_repo_did must be supplied. */
    if ((subject_uri == NULL) == (subject_repo_did == NULL)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WF_ERR_ALLOC;
    }

    cJSON *subject = cJSON_CreateObject();
    if (!subject) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }
    int ok;
    if (subject_repo_did) {
        ok = cJSON_AddStringToObject(subject, "repo", subject_repo_did) != NULL;
    } else {
        ok = cJSON_AddStringToObject(subject, "uri", subject_uri) != NULL;
    }
    if (!ok) {
        cJSON_Delete(subject);
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }
    cJSON_AddItemToObject(root, "subject", subject);

    if (reason) {
        if (!cJSON_AddStringToObject(root, "reason", reason)) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    wf_agent_sync_auth(agent);

    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(agent->client,
                                         "com.atproto.moderation.createReport",
                                         json, &res);
    free(json);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_report(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_mute_actor_list(wf_agent *agent, const char *list_at_uri,
                                   wf_mod_list_view_result *out) {
    return wf_mod_actor_list_action(agent, "app.bsky.graph.muteActorList",
                                    list_at_uri, out);
}

wf_status wf_agent_block_actor_list(wf_agent *agent, const char *list_at_uri,
                                   wf_mod_list_view_result *out) {
    return wf_mod_actor_list_action(agent, "app.bsky.graph.blockActorList",
                                    list_at_uri, out);
}
