/*
 * ozone_moderation_ops_typed.c — owning typed parsers + agent wrappers for the
 * moderation *operations* endpoints of the tools.ozone.* namespaces:
 *   - tools.ozone.report.*    (report lifecycle: query/get/assign/activity/stats)
 *   - tools.ozone.queue.*     (moderation queue CRUD + routing + assignment)
 *   - tools.ozone.signature.* (threat-signature correlation / related accounts)
 *
 * The agent wrappers reuse the generated owning lex wrappers in
 * wolfram/atproto_lex.h (call + decode) after syncing auth onto the agent's
 * primary XRPC client, then decode the raw response body into the hand-written
 * ergonomic owned struct with the matching `_parse` helper. The two endpoints
 * whose lexicon defines no output body (report.unassignModerator,
 * report.refreshStats, queue.unassignModerator) return the raw wf_response,
 * which the caller frees with wf_response_free.
 *
 * The hand-written ergonomic parsers follow the labeler_typed / ozone_typed
 * ownership model: static strdup/set_string/reset helpers, owned strings and
 * string arrays, a detached `extra` cJSON subtree for nested objects, and full
 * cleanup on the first error so a partially-populated struct is never leaked.
 */

#include "wolfram/ozone_moderation_ops_typed.h"

#include "agent/_internal.h"
#include "wolfram/atproto_lex.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* ---- local string/reset helpers (mirror ozone_typed.c) ---- */

static char *wf_ops_strdup(const char *s) {
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

static wf_status wf_ops_set_string(char **dst, const char *src) {
    char *copy = wf_ops_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static wf_status wf_ops_set_string_array(cJSON *arr, char ***out_items,
                                         size_t *out_count) {
    if (!cJSON_IsArray(arr)) {
        return WF_ERR_PARSE;
    }
    size_t n = (size_t)cJSON_GetArraySize(arr);
    if (n == 0) {
        *out_items = NULL;
        *out_count = 0;
        return WF_OK;
    }
    char **items = (char **)calloc(n, sizeof(char *));
    if (!items) {
        return WF_ERR_ALLOC;
    }
    wf_status status = WF_OK;
    for (size_t i = 0; i < n; ++i) {
        cJSON *it = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsString(it) || !it->valuestring) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_ops_set_string(&items[i], it->valuestring);
    }
    if (status == WF_OK) {
        *out_items = items;
        *out_count = n;
    } else {
        for (size_t i = 0; i < n; ++i) {
            free(items[i]);
        }
        free(items);
        *out_items = NULL;
        *out_count = 0;
    }
    return status;
}

/* Generic array parser: each element is decoded by `parse_one` (which, on
 * success, hands back ownership of `obj` via the element's `extra` field),
 * then detached from `arr` so the element owns it. On the first error the
 * whole array is reset and freed and the out_items/out_count fields are
 * zeroed.
 */
typedef wf_status (*wf_ops_parse_one_fn)(cJSON *obj, void *out);
typedef void (*wf_ops_reset_one_fn)(void *p);

static wf_status wf_ops_parse_array(cJSON *arr, size_t elem_size,
                                    wf_ops_parse_one_fn parse_one,
                                    wf_ops_reset_one_fn reset_one,
                                    void **out_items, size_t *out_count) {
    if (!cJSON_IsArray(arr)) {
        return WF_ERR_PARSE;
    }
    size_t n = (size_t)cJSON_GetArraySize(arr);
    char *items = NULL;
    if (n > 0) {
        items = (char *)calloc(n, elem_size);
        if (!items) {
            return WF_ERR_ALLOC;
        }
    }
    wf_status status = WF_OK;
    for (size_t i = 0; i < n && status == WF_OK; ++i) {
        /* Always read index 0: DetachItemViaPointer removes the parsed
         * element from the array, so the next element shifts to index 0 and
         * a fixed index would skip it (and later hit a NULL). */
        cJSON *obj = cJSON_GetArrayItem(arr, 0);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = parse_one(obj, items + i * elem_size);
        if (status == WF_OK) {
            /* parse_one handed back ownership of `obj` via the element's
             * `extra`; detach it now so it is no longer a child of `arr`. */
            cJSON_DetachItemViaPointer(arr, obj);
        } else {
            reset_one(items + i * elem_size);
        }
    }
    if (status == WF_OK) {
        *out_items = items;
        *out_count = n;
    } else {
        for (size_t i = 0; i < n; ++i) {
            reset_one(items + i * elem_size);
        }
        free(items);
        *out_items = NULL;
        *out_count = 0;
    }
    return status;
}

/* ================================================================== */
/* tools.ozone.report.defs#reportView                                 */
/* ================================================================== */

static void wf_ozone_ops_report_view_reset(wf_ozone_ops_report_view *s) {
    if (!s) {
        return;
    }
    free(s->status);
    free(s->report_type);
    free(s->reported_by);
    free(s->comment);
    free(s->created_at);
    free(s->updated_at);
    free(s->queued_at);
    free(s->action_note);
    if (s->extra) {
        cJSON_Delete(s->extra);
    }
    memset(s, 0, sizeof(*s));
}

static wf_status wf_ozone_ops_parse_report_view_obj(
    cJSON *obj, wf_ozone_ops_report_view *s) {
    wf_status status = WF_OK;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "id");
    cJSON *event_id = cJSON_GetObjectItemCaseSensitive(obj, "eventId");
    cJSON *status_f = cJSON_GetObjectItemCaseSensitive(obj, "status");
    cJSON *report_type = cJSON_GetObjectItemCaseSensitive(obj, "reportType");
    cJSON *reported_by = cJSON_GetObjectItemCaseSensitive(obj, "reportedBy");
    cJSON *comment = cJSON_GetObjectItemCaseSensitive(obj, "comment");
    cJSON *created_at = cJSON_GetObjectItemCaseSensitive(obj, "createdAt");
    cJSON *updated_at = cJSON_GetObjectItemCaseSensitive(obj, "updatedAt");
    cJSON *queued_at = cJSON_GetObjectItemCaseSensitive(obj, "queuedAt");
    cJSON *action_note = cJSON_GetObjectItemCaseSensitive(obj, "actionNote");
    cJSON *related = cJSON_GetObjectItemCaseSensitive(obj, "relatedReportCount");
    cJSON *muted = cJSON_GetObjectItemCaseSensitive(obj, "isMuted");

    if (cJSON_IsNumber(id)) {
        s->has_id = true;
        s->id = (int64_t)id->valuedouble;
    }
    if (cJSON_IsNumber(event_id)) {
        s->has_event_id = true;
        s->event_id = (int64_t)event_id->valuedouble;
    }
    if (cJSON_IsString(status_f) && status_f->valuestring) {
        status = wf_ops_set_string(&s->status, status_f->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(report_type) &&
        report_type->valuestring) {
        status = wf_ops_set_string(&s->report_type, report_type->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(reported_by) &&
        reported_by->valuestring) {
        status = wf_ops_set_string(&s->reported_by, reported_by->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(comment) && comment->valuestring) {
        status = wf_ops_set_string(&s->comment, comment->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(created_at) &&
        created_at->valuestring) {
        status = wf_ops_set_string(&s->created_at, created_at->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(updated_at) &&
        updated_at->valuestring) {
        status = wf_ops_set_string(&s->updated_at, updated_at->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(queued_at) && queued_at->valuestring) {
        status = wf_ops_set_string(&s->queued_at, queued_at->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(action_note) &&
        action_note->valuestring) {
        status = wf_ops_set_string(&s->action_note, action_note->valuestring);
    }
    if (status == WF_OK && cJSON_IsNumber(related)) {
        s->has_related_report_count = true;
        s->related_report_count = (int64_t)related->valuedouble;
    }
    if (status == WF_OK && cJSON_IsBool(muted)) {
        s->has_is_muted = true;
        s->is_muted = cJSON_IsTrue(muted);
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "id");
        cJSON_DetachItemFromObject(obj, "eventId");
        cJSON_DetachItemFromObject(obj, "status");
        cJSON_DetachItemFromObject(obj, "reportType");
        cJSON_DetachItemFromObject(obj, "reportedBy");
        cJSON_DetachItemFromObject(obj, "comment");
        cJSON_DetachItemFromObject(obj, "createdAt");
        cJSON_DetachItemFromObject(obj, "updatedAt");
        cJSON_DetachItemFromObject(obj, "queuedAt");
        cJSON_DetachItemFromObject(obj, "actionNote");
        cJSON_DetachItemFromObject(obj, "relatedReportCount");
        cJSON_DetachItemFromObject(obj, "isMuted");
        s->extra = obj;
    } else {
        wf_ozone_ops_report_view_reset(s);
    }
    return status;
}

wf_status wf_ozone_ops_parse_report_list(const char *json, size_t json_len,
                                         wf_ozone_ops_report_list *out) {
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
    wf_status status = WF_ERR_PARSE;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "reports");
    if (cJSON_IsArray(arr)) {
        status = wf_ops_parse_array(
            arr, sizeof(wf_ozone_ops_report_view),
            (wf_ops_parse_one_fn)wf_ozone_ops_parse_report_view_obj,
            (wf_ops_reset_one_fn)wf_ozone_ops_report_view_reset,
            (void **)&out->reports, &out->report_count);
    }
    if (status == WF_OK) {
        cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cur) && cur->valuestring) {
            status = wf_ops_set_string(&out->cursor, cur->valuestring);
        }
    }
    if (status != WF_OK) {
        wf_ozone_ops_report_list_free(out);
    }
    cJSON_Delete(root);
    return status;
}

wf_status wf_ozone_ops_parse_report_view(const char *json, size_t json_len,
                                         wf_ozone_ops_report_view *out) {
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
    wf_status status = wf_ozone_ops_parse_report_view_obj(root, out);
    if (status != WF_OK) {
        cJSON_Delete(root);
    }
    return status;
}

wf_status wf_ozone_ops_parse_wrapped_report(const char *json, size_t json_len,
                                            wf_ozone_ops_report_view *out) {
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
    wf_status status = WF_ERR_PARSE;
    cJSON *rep = cJSON_GetObjectItemCaseSensitive(root, "report");
    if (cJSON_IsObject(rep)) {
        status = wf_ozone_ops_parse_report_view_obj(rep, out);
        if (status == WF_OK) {
            cJSON_DetachItemFromObject(root, "report");
        }
    }
    cJSON_Delete(root);
    return status;
}

void wf_ozone_ops_report_view_free(wf_ozone_ops_report_view *v) {
    wf_ozone_ops_report_view_reset(v);
}

void wf_ozone_ops_report_list_free(wf_ozone_ops_report_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->report_count; ++i) {
        wf_ozone_ops_report_view_reset(&l->reports[i]);
    }
    free(l->reports);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

/* ================================================================== */
/* tools.ozone.report.defs#reportActivityView                         */
/* ================================================================== */

static void wf_ozone_ops_report_activity_view_reset(
    wf_ozone_ops_report_activity_view *s) {
    if (!s) {
        return;
    }
    free(s->created_by);
    free(s->created_at);
    free(s->internal_note);
    free(s->public_note);
    if (s->extra) {
        cJSON_Delete(s->extra);
    }
    memset(s, 0, sizeof(*s));
}

static wf_status wf_ozone_ops_parse_report_activity_view_obj(
    cJSON *obj, wf_ozone_ops_report_activity_view *s) {
    wf_status status = WF_OK;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "id");
    cJSON *report_id = cJSON_GetObjectItemCaseSensitive(obj, "reportId");
    cJSON *auto_f = cJSON_GetObjectItemCaseSensitive(obj, "isAutomated");
    cJSON *created_by = cJSON_GetObjectItemCaseSensitive(obj, "createdBy");
    cJSON *created_at = cJSON_GetObjectItemCaseSensitive(obj, "createdAt");
    cJSON *internal = cJSON_GetObjectItemCaseSensitive(obj, "internalNote");
    cJSON *public_f = cJSON_GetObjectItemCaseSensitive(obj, "publicNote");

    if (cJSON_IsNumber(id)) {
        s->has_id = true;
        s->id = (int64_t)id->valuedouble;
    }
    if (cJSON_IsNumber(report_id)) {
        s->has_report_id = true;
        s->report_id = (int64_t)report_id->valuedouble;
    }
    if (cJSON_IsBool(auto_f)) {
        s->has_is_automated = true;
        s->is_automated = cJSON_IsTrue(auto_f);
    }
    if (cJSON_IsString(created_by) && created_by->valuestring) {
        status = wf_ops_set_string(&s->created_by, created_by->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(created_at) &&
        created_at->valuestring) {
        status = wf_ops_set_string(&s->created_at, created_at->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(internal) && internal->valuestring) {
        status = wf_ops_set_string(&s->internal_note, internal->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(public_f) && public_f->valuestring) {
        status = wf_ops_set_string(&s->public_note, public_f->valuestring);
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "id");
        cJSON_DetachItemFromObject(obj, "reportId");
        cJSON_DetachItemFromObject(obj, "isAutomated");
        cJSON_DetachItemFromObject(obj, "createdBy");
        cJSON_DetachItemFromObject(obj, "createdAt");
        cJSON_DetachItemFromObject(obj, "internalNote");
        cJSON_DetachItemFromObject(obj, "publicNote");
        s->extra = obj;
    } else {
        wf_ozone_ops_report_activity_view_reset(s);
    }
    return status;
}

wf_status wf_ozone_ops_parse_report_activity_list(
    const char *json, size_t json_len, wf_ozone_ops_report_activity_list *out) {
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
    wf_status status = WF_ERR_PARSE;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "activities");
    if (cJSON_IsArray(arr)) {
        status = wf_ops_parse_array(
            arr, sizeof(wf_ozone_ops_report_activity_view),
            (wf_ops_parse_one_fn)wf_ozone_ops_parse_report_activity_view_obj,
            (wf_ops_reset_one_fn)wf_ozone_ops_report_activity_view_reset,
            (void **)&out->activities, &out->activity_count);
    }
    if (status == WF_OK) {
        cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cur) && cur->valuestring) {
            status = wf_ops_set_string(&out->cursor, cur->valuestring);
        }
    }
    if (status != WF_OK) {
        wf_ozone_ops_report_activity_list_free(out);
    }
    cJSON_Delete(root);
    return status;
}

wf_status wf_ozone_ops_parse_wrapped_activity(
    const char *json, size_t json_len, wf_ozone_ops_report_activity_view *out) {
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
    wf_status status = WF_ERR_PARSE;
    cJSON *act = cJSON_GetObjectItemCaseSensitive(root, "activity");
    if (cJSON_IsObject(act)) {
        status = wf_ozone_ops_parse_report_activity_view_obj(act, out);
        if (status == WF_OK) {
            cJSON_DetachItemFromObject(root, "activity");
        }
    }
    cJSON_Delete(root);
    return status;
}

void wf_ozone_ops_report_activity_view_free(
    wf_ozone_ops_report_activity_view *v) {
    wf_ozone_ops_report_activity_view_reset(v);
}

void wf_ozone_ops_report_activity_list_free(
    wf_ozone_ops_report_activity_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->activity_count; ++i) {
        wf_ozone_ops_report_activity_view_reset(&l->activities[i]);
    }
    free(l->activities);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

/* ================================================================== */
/* tools.ozone.report.defs#assignmentView                            */
/* ================================================================== */

static void wf_ozone_ops_report_assignment_view_reset(
    wf_ozone_ops_report_assignment_view *s) {
    if (!s) {
        return;
    }
    free(s->did);
    free(s->start_at);
    free(s->end_at);
    if (s->extra) {
        cJSON_Delete(s->extra);
    }
    memset(s, 0, sizeof(*s));
}

static wf_status wf_ozone_ops_parse_report_assignment_view_obj(
    cJSON *obj, wf_ozone_ops_report_assignment_view *s) {
    wf_status status = WF_OK;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "id");
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *report_id = cJSON_GetObjectItemCaseSensitive(obj, "reportId");
    cJSON *start = cJSON_GetObjectItemCaseSensitive(obj, "startAt");
    cJSON *end = cJSON_GetObjectItemCaseSensitive(obj, "endAt");

    if (cJSON_IsNumber(id)) {
        s->has_id = true;
        s->id = (int64_t)id->valuedouble;
    }
    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_ops_set_string(&s->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsNumber(report_id)) {
        s->has_report_id = true;
        s->report_id = (int64_t)report_id->valuedouble;
    }
    if (status == WF_OK && cJSON_IsString(start) && start->valuestring) {
        status = wf_ops_set_string(&s->start_at, start->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(end) && end->valuestring) {
        status = wf_ops_set_string(&s->end_at, end->valuestring);
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "id");
        cJSON_DetachItemFromObject(obj, "did");
        cJSON_DetachItemFromObject(obj, "reportId");
        cJSON_DetachItemFromObject(obj, "startAt");
        cJSON_DetachItemFromObject(obj, "endAt");
        s->extra = obj;
    } else {
        wf_ozone_ops_report_assignment_view_reset(s);
    }
    return status;
}

wf_status wf_ozone_ops_parse_report_assignment_list(
    const char *json, size_t json_len, wf_ozone_ops_report_assignment_list *out) {
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
    wf_status status = WF_ERR_PARSE;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "assignments");
    if (cJSON_IsArray(arr)) {
        status = wf_ops_parse_array(
            arr, sizeof(wf_ozone_ops_report_assignment_view),
            (wf_ops_parse_one_fn)wf_ozone_ops_parse_report_assignment_view_obj,
            (wf_ops_reset_one_fn)wf_ozone_ops_report_assignment_view_reset,
            (void **)&out->assignments, &out->assignment_count);
    }
    if (status == WF_OK) {
        cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cur) && cur->valuestring) {
            status = wf_ops_set_string(&out->cursor, cur->valuestring);
        }
    }
    if (status != WF_OK) {
        wf_ozone_ops_report_assignment_list_free(out);
    }
    cJSON_Delete(root);
    return status;
}

wf_status wf_ozone_ops_parse_report_assignment_view(
    const char *json, size_t json_len, wf_ozone_ops_report_assignment_view *out) {
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
    wf_status status = wf_ozone_ops_parse_report_assignment_view_obj(root, out);
    if (status != WF_OK) {
        cJSON_Delete(root);
    }
    return status;
}

void wf_ozone_ops_report_assignment_view_free(
    wf_ozone_ops_report_assignment_view *v) {
    wf_ozone_ops_report_assignment_view_reset(v);
}

void wf_ozone_ops_report_assignment_list_free(
    wf_ozone_ops_report_assignment_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->assignment_count; ++i) {
        wf_ozone_ops_report_assignment_view_reset(&l->assignments[i]);
    }
    free(l->assignments);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

/* ================================================================== */
/* tools.ozone.report.defs#liveStats / historicalStats               */
/* ================================================================== */

static void wf_ozone_ops_live_stats_reset(wf_ozone_ops_live_stats *s) {
    if (!s) {
        return;
    }
    free(s->last_updated);
    if (s->extra) {
        cJSON_Delete(s->extra);
    }
    memset(s, 0, sizeof(*s));
}

static wf_status wf_ozone_ops_parse_live_stats_obj(cJSON *obj,
                                                   wf_ozone_ops_live_stats *s) {
    wf_status status = WF_OK;
    cJSON *pending = cJSON_GetObjectItemCaseSensitive(obj, "pendingCount");
    cJSON *actioned = cJSON_GetObjectItemCaseSensitive(obj, "actionedCount");
    cJSON *escalated = cJSON_GetObjectItemCaseSensitive(obj, "escalatedCount");
    cJSON *inbound = cJSON_GetObjectItemCaseSensitive(obj, "inboundCount");
    cJSON *rate = cJSON_GetObjectItemCaseSensitive(obj, "actionRate");
    cJSON *avg = cJSON_GetObjectItemCaseSensitive(obj, "avgHandlingTimeSec");
    cJSON *last = cJSON_GetObjectItemCaseSensitive(obj, "lastUpdated");

    if (cJSON_IsNumber(pending)) {
        s->has_pending_count = true;
        s->pending_count = (int64_t)pending->valuedouble;
    }
    if (cJSON_IsNumber(actioned)) {
        s->has_actioned_count = true;
        s->actioned_count = (int64_t)actioned->valuedouble;
    }
    if (cJSON_IsNumber(escalated)) {
        s->has_escalated_count = true;
        s->escalated_count = (int64_t)escalated->valuedouble;
    }
    if (cJSON_IsNumber(inbound)) {
        s->has_inbound_count = true;
        s->inbound_count = (int64_t)inbound->valuedouble;
    }
    if (cJSON_IsNumber(rate)) {
        s->has_action_rate = true;
        s->action_rate = (int64_t)rate->valuedouble;
    }
    if (cJSON_IsNumber(avg)) {
        s->has_avg_handling_time_sec = true;
        s->avg_handling_time_sec = (int64_t)avg->valuedouble;
    }
    if (cJSON_IsString(last) && last->valuestring) {
        status = wf_ops_set_string(&s->last_updated, last->valuestring);
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "pendingCount");
        cJSON_DetachItemFromObject(obj, "actionedCount");
        cJSON_DetachItemFromObject(obj, "escalatedCount");
        cJSON_DetachItemFromObject(obj, "inboundCount");
        cJSON_DetachItemFromObject(obj, "actionRate");
        cJSON_DetachItemFromObject(obj, "avgHandlingTimeSec");
        cJSON_DetachItemFromObject(obj, "lastUpdated");
        s->extra = obj;
    } else {
        wf_ozone_ops_live_stats_reset(s);
    }
    return status;
}

wf_status wf_ozone_ops_parse_live_stats(const char *json, size_t json_len,
                                        wf_ozone_ops_live_stats *out) {
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
    wf_status status = WF_ERR_PARSE;
    cJSON *stats = cJSON_GetObjectItemCaseSensitive(root, "stats");
    if (cJSON_IsObject(stats)) {
        status = wf_ozone_ops_parse_live_stats_obj(stats, out);
        if (status == WF_OK) {
            cJSON_DetachItemFromObject(root, "stats");
        }
    }
    cJSON_Delete(root);
    return status;
}

void wf_ozone_ops_live_stats_free(wf_ozone_ops_live_stats *v) {
    wf_ozone_ops_live_stats_reset(v);
}

static void wf_ozone_ops_historical_stats_reset(
    wf_ozone_ops_historical_stats *s) {
    if (!s) {
        return;
    }
    free(s->date);
    free(s->computed_at);
    if (s->extra) {
        cJSON_Delete(s->extra);
    }
    memset(s, 0, sizeof(*s));
}

static wf_status wf_ozone_ops_parse_historical_stats_obj(
    cJSON *obj, wf_ozone_ops_historical_stats *s) {
    wf_status status = WF_OK;
    cJSON *date = cJSON_GetObjectItemCaseSensitive(obj, "date");
    cJSON *computed = cJSON_GetObjectItemCaseSensitive(obj, "computedAt");
    cJSON *pending = cJSON_GetObjectItemCaseSensitive(obj, "pendingCount");
    cJSON *actioned = cJSON_GetObjectItemCaseSensitive(obj, "actionedCount");
    cJSON *escalated = cJSON_GetObjectItemCaseSensitive(obj, "escalatedCount");
    cJSON *inbound = cJSON_GetObjectItemCaseSensitive(obj, "inboundCount");
    cJSON *rate = cJSON_GetObjectItemCaseSensitive(obj, "actionRate");
    cJSON *avg = cJSON_GetObjectItemCaseSensitive(obj, "avgHandlingTimeSec");

    if (cJSON_IsString(date) && date->valuestring) {
        status = wf_ops_set_string(&s->date, date->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(computed) && computed->valuestring) {
        status = wf_ops_set_string(&s->computed_at, computed->valuestring);
    }
    if (status == WF_OK && cJSON_IsNumber(pending)) {
        s->has_pending_count = true;
        s->pending_count = (int64_t)pending->valuedouble;
    }
    if (status == WF_OK && cJSON_IsNumber(actioned)) {
        s->has_actioned_count = true;
        s->actioned_count = (int64_t)actioned->valuedouble;
    }
    if (status == WF_OK && cJSON_IsNumber(escalated)) {
        s->has_escalated_count = true;
        s->escalated_count = (int64_t)escalated->valuedouble;
    }
    if (status == WF_OK && cJSON_IsNumber(inbound)) {
        s->has_inbound_count = true;
        s->inbound_count = (int64_t)inbound->valuedouble;
    }
    if (status == WF_OK && cJSON_IsNumber(rate)) {
        s->has_action_rate = true;
        s->action_rate = (int64_t)rate->valuedouble;
    }
    if (status == WF_OK && cJSON_IsNumber(avg)) {
        s->has_avg_handling_time_sec = true;
        s->avg_handling_time_sec = (int64_t)avg->valuedouble;
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "date");
        cJSON_DetachItemFromObject(obj, "computedAt");
        cJSON_DetachItemFromObject(obj, "pendingCount");
        cJSON_DetachItemFromObject(obj, "actionedCount");
        cJSON_DetachItemFromObject(obj, "escalatedCount");
        cJSON_DetachItemFromObject(obj, "inboundCount");
        cJSON_DetachItemFromObject(obj, "actionRate");
        cJSON_DetachItemFromObject(obj, "avgHandlingTimeSec");
        s->extra = obj;
    } else {
        wf_ozone_ops_historical_stats_reset(s);
    }
    return status;
}

wf_status wf_ozone_ops_parse_historical_stats_list(
    const char *json, size_t json_len, wf_ozone_ops_historical_stats_list *out) {
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
    wf_status status = WF_ERR_PARSE;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "stats");
    if (cJSON_IsArray(arr)) {
        status = wf_ops_parse_array(
            arr, sizeof(wf_ozone_ops_historical_stats),
            (wf_ops_parse_one_fn)wf_ozone_ops_parse_historical_stats_obj,
            (wf_ops_reset_one_fn)wf_ozone_ops_historical_stats_reset,
            (void **)&out->stats, &out->stats_count);
    }
    if (status == WF_OK) {
        cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cur) && cur->valuestring) {
            status = wf_ops_set_string(&out->cursor, cur->valuestring);
        }
    }
    if (status != WF_OK) {
        wf_ozone_ops_historical_stats_list_free(out);
    }
    cJSON_Delete(root);
    return status;
}

void wf_ozone_ops_historical_stats_list_free(
    wf_ozone_ops_historical_stats_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->stats_count; ++i) {
        wf_ozone_ops_historical_stats_reset(&l->stats[i]);
    }
    free(l->stats);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

/* ================================================================== */
/* tools.ozone.queue.defs#queueView                                   */
/* ================================================================== */

static void wf_ozone_ops_queue_view_reset(wf_ozone_ops_queue_view *s) {
    if (!s) {
        return;
    }
    free(s->name);
    free(s->collection);
    free(s->description);
    free(s->created_by);
    free(s->created_at);
    free(s->updated_at);
    free(s->deleted_at);
    for (size_t i = 0; i < s->subject_type_count; ++i) {
        free(s->subject_types[i]);
    }
    free(s->subject_types);
    for (size_t i = 0; i < s->report_type_count; ++i) {
        free(s->report_types[i]);
    }
    free(s->report_types);
    if (s->extra) {
        cJSON_Delete(s->extra);
    }
    memset(s, 0, sizeof(*s));
}

static wf_status wf_ozone_ops_parse_queue_view_obj(cJSON *obj,
                                                   wf_ozone_ops_queue_view *s) {
    wf_status status = WF_OK;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "id");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "name");
    cJSON *subject_types = cJSON_GetObjectItemCaseSensitive(obj, "subjectTypes");
    cJSON *collection = cJSON_GetObjectItemCaseSensitive(obj, "collection");
    cJSON *report_types = cJSON_GetObjectItemCaseSensitive(obj, "reportTypes");
    cJSON *description = cJSON_GetObjectItemCaseSensitive(obj, "description");
    cJSON *created_by = cJSON_GetObjectItemCaseSensitive(obj, "createdBy");
    cJSON *created_at = cJSON_GetObjectItemCaseSensitive(obj, "createdAt");
    cJSON *updated_at = cJSON_GetObjectItemCaseSensitive(obj, "updatedAt");
    cJSON *enabled = cJSON_GetObjectItemCaseSensitive(obj, "enabled");
    cJSON *deleted_at = cJSON_GetObjectItemCaseSensitive(obj, "deletedAt");

    if (cJSON_IsNumber(id)) {
        s->has_id = true;
        s->id = (int64_t)id->valuedouble;
    }
    if (cJSON_IsString(name) && name->valuestring) {
        status = wf_ops_set_string(&s->name, name->valuestring);
    }
    if (status == WF_OK && subject_types != NULL) {
        status = wf_ops_set_string_array(subject_types, &s->subject_types,
                                         &s->subject_type_count);
    }
    if (status == WF_OK && cJSON_IsString(collection) &&
        collection->valuestring) {
        status = wf_ops_set_string(&s->collection, collection->valuestring);
    }
    if (status == WF_OK && report_types != NULL) {
        status = wf_ops_set_string_array(report_types, &s->report_types,
                                         &s->report_type_count);
    }
    if (status == WF_OK && cJSON_IsString(description) &&
        description->valuestring) {
        status = wf_ops_set_string(&s->description, description->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(created_by) &&
        created_by->valuestring) {
        status = wf_ops_set_string(&s->created_by, created_by->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(created_at) &&
        created_at->valuestring) {
        status = wf_ops_set_string(&s->created_at, created_at->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(updated_at) &&
        updated_at->valuestring) {
        status = wf_ops_set_string(&s->updated_at, updated_at->valuestring);
    }
    if (status == WF_OK && cJSON_IsBool(enabled)) {
        s->has_enabled = true;
        s->enabled = cJSON_IsTrue(enabled);
    }
    if (status == WF_OK && cJSON_IsString(deleted_at) &&
        deleted_at->valuestring) {
        status = wf_ops_set_string(&s->deleted_at, deleted_at->valuestring);
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "id");
        cJSON_DetachItemFromObject(obj, "name");
        cJSON_DetachItemFromObject(obj, "subjectTypes");
        cJSON_DetachItemFromObject(obj, "collection");
        cJSON_DetachItemFromObject(obj, "reportTypes");
        cJSON_DetachItemFromObject(obj, "description");
        cJSON_DetachItemFromObject(obj, "createdBy");
        cJSON_DetachItemFromObject(obj, "createdAt");
        cJSON_DetachItemFromObject(obj, "updatedAt");
        cJSON_DetachItemFromObject(obj, "enabled");
        cJSON_DetachItemFromObject(obj, "deletedAt");
        s->extra = obj;
    } else {
        wf_ozone_ops_queue_view_reset(s);
    }
    return status;
}

wf_status wf_ozone_ops_parse_queue_list(const char *json, size_t json_len,
                                        wf_ozone_ops_queue_list *out) {
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
    wf_status status = WF_ERR_PARSE;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "queues");
    if (cJSON_IsArray(arr)) {
        status = wf_ops_parse_array(
            arr, sizeof(wf_ozone_ops_queue_view),
            (wf_ops_parse_one_fn)wf_ozone_ops_parse_queue_view_obj,
            (wf_ops_reset_one_fn)wf_ozone_ops_queue_view_reset,
            (void **)&out->queues, &out->queue_count);
    }
    if (status == WF_OK) {
        cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cur) && cur->valuestring) {
            status = wf_ops_set_string(&out->cursor, cur->valuestring);
        }
    }
    if (status != WF_OK) {
        wf_ozone_ops_queue_list_free(out);
    }
    cJSON_Delete(root);
    return status;
}

wf_status wf_ozone_ops_parse_wrapped_queue(const char *json, size_t json_len,
                                           wf_ozone_ops_queue_view *out) {
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
    wf_status status = WF_ERR_PARSE;
    cJSON *q = cJSON_GetObjectItemCaseSensitive(root, "queue");
    if (cJSON_IsObject(q)) {
        status = wf_ozone_ops_parse_queue_view_obj(q, out);
        if (status == WF_OK) {
            cJSON_DetachItemFromObject(root, "queue");
        }
    }
    cJSON_Delete(root);
    return status;
}

void wf_ozone_ops_queue_view_free(wf_ozone_ops_queue_view *v) {
    wf_ozone_ops_queue_view_reset(v);
}

void wf_ozone_ops_queue_list_free(wf_ozone_ops_queue_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->queue_count; ++i) {
        wf_ozone_ops_queue_view_reset(&l->queues[i]);
    }
    free(l->queues);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

/* ================================================================== */
/* tools.ozone.queue.defs#assignmentView                              */
/* ================================================================== */

static void wf_ozone_ops_queue_assignment_view_reset(
    wf_ozone_ops_queue_assignment_view *s) {
    if (!s) {
        return;
    }
    free(s->did);
    free(s->start_at);
    free(s->end_at);
    if (s->extra) {
        cJSON_Delete(s->extra);
    }
    memset(s, 0, sizeof(*s));
}

static wf_status wf_ozone_ops_parse_queue_assignment_view_obj(
    cJSON *obj, wf_ozone_ops_queue_assignment_view *s) {
    wf_status status = WF_OK;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "id");
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *start = cJSON_GetObjectItemCaseSensitive(obj, "startAt");
    cJSON *end = cJSON_GetObjectItemCaseSensitive(obj, "endAt");

    if (cJSON_IsNumber(id)) {
        s->has_id = true;
        s->id = (int64_t)id->valuedouble;
    }
    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_ops_set_string(&s->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(start) && start->valuestring) {
        status = wf_ops_set_string(&s->start_at, start->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(end) && end->valuestring) {
        status = wf_ops_set_string(&s->end_at, end->valuestring);
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "id");
        cJSON_DetachItemFromObject(obj, "did");
        cJSON_DetachItemFromObject(obj, "startAt");
        cJSON_DetachItemFromObject(obj, "endAt");
        s->extra = obj;
    } else {
        wf_ozone_ops_queue_assignment_view_reset(s);
    }
    return status;
}

wf_status wf_ozone_ops_parse_queue_assignment_list(
    const char *json, size_t json_len, wf_ozone_ops_queue_assignment_list *out) {
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
    wf_status status = WF_ERR_PARSE;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "assignments");
    if (cJSON_IsArray(arr)) {
        status = wf_ops_parse_array(
            arr, sizeof(wf_ozone_ops_queue_assignment_view),
            (wf_ops_parse_one_fn)wf_ozone_ops_parse_queue_assignment_view_obj,
            (wf_ops_reset_one_fn)wf_ozone_ops_queue_assignment_view_reset,
            (void **)&out->assignments, &out->assignment_count);
    }
    if (status == WF_OK) {
        cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cur) && cur->valuestring) {
            status = wf_ops_set_string(&out->cursor, cur->valuestring);
        }
    }
    if (status != WF_OK) {
        wf_ozone_ops_queue_assignment_list_free(out);
    }
    cJSON_Delete(root);
    return status;
}

wf_status wf_ozone_ops_parse_queue_assignment_view(
    const char *json, size_t json_len, wf_ozone_ops_queue_assignment_view *out) {
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
    wf_status status =
        wf_ozone_ops_parse_queue_assignment_view_obj(root, out);
    if (status != WF_OK) {
        cJSON_Delete(root);
    }
    return status;
}

void wf_ozone_ops_queue_assignment_view_free(
    wf_ozone_ops_queue_assignment_view *v) {
    wf_ozone_ops_queue_assignment_view_reset(v);
}

void wf_ozone_ops_queue_assignment_list_free(
    wf_ozone_ops_queue_assignment_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->assignment_count; ++i) {
        wf_ozone_ops_queue_assignment_view_reset(&l->assignments[i]);
    }
    free(l->assignments);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

/* ================================================================== */
/* tools.ozone.queue deleteQueue / routeReports results             */
/* ================================================================== */

wf_status wf_ozone_ops_parse_delete_queue_result(
    const char *json, size_t json_len, wf_ozone_ops_delete_queue_result *out) {
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
    cJSON *deleted = cJSON_GetObjectItemCaseSensitive(root, "deleted");
    cJSON *migrated = cJSON_GetObjectItemCaseSensitive(root, "reportsMigrated");
    if (cJSON_IsBool(deleted)) {
        out->has_deleted = true;
        out->deleted = cJSON_IsTrue(deleted);
    }
    if (cJSON_IsNumber(migrated)) {
        out->has_reports_migrated = true;
        out->reports_migrated = (int64_t)migrated->valuedouble;
    }
    cJSON_Delete(root);
    return WF_OK;
}

wf_status wf_ozone_ops_parse_route_reports_result(
    const char *json, size_t json_len, wf_ozone_ops_route_reports_result *out) {
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
    cJSON *assigned = cJSON_GetObjectItemCaseSensitive(root, "assigned");
    cJSON *unmatched = cJSON_GetObjectItemCaseSensitive(root, "unmatched");
    if (cJSON_IsNumber(assigned)) {
        out->has_assigned = true;
        out->assigned = (int64_t)assigned->valuedouble;
    }
    if (cJSON_IsNumber(unmatched)) {
        out->has_unmatched = true;
        out->unmatched = (int64_t)unmatched->valuedouble;
    }
    cJSON_Delete(root);
    return WF_OK;
}

/* ================================================================== */
/* tools.ozone.signature.defs#sigDetail + accounts                   */
/* ================================================================== */

static void wf_ozone_ops_sig_detail_reset(wf_ozone_ops_sig_detail *d) {
    if (!d) {
        return;
    }
    free(d->property);
    free(d->value);
    if (d->extra) {
        cJSON_Delete(d->extra);
    }
    memset(d, 0, sizeof(*d));
}

static wf_status wf_ozone_ops_parse_sig_detail_obj(cJSON *obj,
                                                   wf_ozone_ops_sig_detail *d) {
    wf_status status = WF_OK;
    cJSON *property = cJSON_GetObjectItemCaseSensitive(obj, "property");
    cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, "value");

    if (cJSON_IsString(property) && property->valuestring) {
        status = wf_ops_set_string(&d->property, property->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(value) && value->valuestring) {
        status = wf_ops_set_string(&d->value, value->valuestring);
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "property");
        cJSON_DetachItemFromObject(obj, "value");
        d->extra = obj;
    } else {
        wf_ozone_ops_sig_detail_reset(d);
    }
    return status;
}

static wf_status wf_ozone_ops_parse_sig_detail_array(
    cJSON *arr, wf_ozone_ops_sig_detail **out_items, size_t *out_count) {
    if (arr == NULL) {
        *out_items = NULL;
        *out_count = 0;
        return WF_OK;
    }
    return wf_ops_parse_array(
        arr, sizeof(wf_ozone_ops_sig_detail),
        (wf_ops_parse_one_fn)wf_ozone_ops_parse_sig_detail_obj,
        (wf_ops_reset_one_fn)wf_ozone_ops_sig_detail_reset,
        (void **)out_items, out_count);
}

wf_status wf_ozone_ops_parse_sig_detail_list(
    const char *json, size_t json_len, wf_ozone_ops_sig_detail_list *out) {
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
    wf_status status = WF_ERR_PARSE;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "details");
    if (cJSON_IsArray(arr)) {
        status = wf_ozone_ops_parse_sig_detail_array(arr, &out->details,
                                                     &out->detail_count);
    }
    if (status != WF_OK) {
        wf_ozone_ops_sig_detail_list_free(out);
    }
    cJSON_Delete(root);
    return status;
}

void wf_ozone_ops_sig_detail_list_free(wf_ozone_ops_sig_detail_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->detail_count; ++i) {
        wf_ozone_ops_sig_detail_reset(&l->details[i]);
    }
    free(l->details);
    memset(l, 0, sizeof(*l));
}

static void wf_ozone_ops_account_reset(wf_ozone_ops_account *a) {
    if (!a) {
        return;
    }
    free(a->did);
    free(a->handle);
    free(a->email);
    free(a->indexed_at);
    if (a->extra) {
        cJSON_Delete(a->extra);
    }
    memset(a, 0, sizeof(*a));
}

static wf_status wf_ozone_ops_parse_account_obj(cJSON *obj,
                                                wf_ozone_ops_account *a) {
    wf_status status = WF_OK;
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(obj, "handle");
    cJSON *email = cJSON_GetObjectItemCaseSensitive(obj, "email");
    cJSON *indexed = cJSON_GetObjectItemCaseSensitive(obj, "indexedAt");

    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_ops_set_string(&a->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
        status = wf_ops_set_string(&a->handle, handle->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(email) && email->valuestring) {
        status = wf_ops_set_string(&a->email, email->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(indexed) && indexed->valuestring) {
        status = wf_ops_set_string(&a->indexed_at, indexed->valuestring);
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "did");
        cJSON_DetachItemFromObject(obj, "handle");
        cJSON_DetachItemFromObject(obj, "email");
        cJSON_DetachItemFromObject(obj, "indexedAt");
        a->extra = obj;
    } else {
        wf_ozone_ops_account_reset(a);
    }
    return status;
}

wf_status wf_ozone_ops_parse_account_list(const char *json, size_t json_len,
                                          wf_ozone_ops_account_list *out) {
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
    wf_status status = WF_ERR_PARSE;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "accounts");
    if (cJSON_IsArray(arr)) {
        status = wf_ops_parse_array(
            arr, sizeof(wf_ozone_ops_account),
            (wf_ops_parse_one_fn)wf_ozone_ops_parse_account_obj,
            (wf_ops_reset_one_fn)wf_ozone_ops_account_reset,
            (void **)&out->accounts, &out->account_count);
    }
    if (status == WF_OK) {
        cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cur) && cur->valuestring) {
            status = wf_ops_set_string(&out->cursor, cur->valuestring);
        }
    }
    if (status != WF_OK) {
        wf_ozone_ops_account_list_free(out);
    }
    cJSON_Delete(root);
    return status;
}

void wf_ozone_ops_account_free(wf_ozone_ops_account *v) {
    wf_ozone_ops_account_reset(v);
}

void wf_ozone_ops_account_list_free(wf_ozone_ops_account_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->account_count; ++i) {
        wf_ozone_ops_account_reset(&l->accounts[i]);
    }
    free(l->accounts);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

static void wf_ozone_ops_related_account_reset(wf_ozone_ops_related_account *r) {
    if (!r) {
        return;
    }
    wf_ozone_ops_account_reset(&r->account);
    for (size_t i = 0; i < r->similarity_count; ++i) {
        wf_ozone_ops_sig_detail_reset(&r->similarities[i]);
    }
    free(r->similarities);
    if (r->extra) {
        cJSON_Delete(r->extra);
    }
    memset(r, 0, sizeof(*r));
}

static wf_status wf_ozone_ops_parse_related_account_obj(
    cJSON *obj, wf_ozone_ops_related_account *r) {
    wf_status status = WF_ERR_PARSE;
    cJSON *acct = cJSON_GetObjectItemCaseSensitive(obj, "account");
    if (cJSON_IsObject(acct)) {
        status = wf_ozone_ops_parse_account_obj(acct, &r->account);
    }
    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "account");
        cJSON *sims = cJSON_GetObjectItemCaseSensitive(obj, "similarities");
        status = wf_ozone_ops_parse_sig_detail_array(sims, &r->similarities,
                                                     &r->similarity_count);
    }
    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "similarities");
        r->extra = obj;
    } else {
        wf_ozone_ops_related_account_reset(r);
    }
    return status;
}

wf_status wf_ozone_ops_parse_related_account_list(
    const char *json, size_t json_len, wf_ozone_ops_related_account_list *out) {
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
    wf_status status = WF_ERR_PARSE;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "accounts");
    if (cJSON_IsArray(arr)) {
        status = wf_ops_parse_array(
            arr, sizeof(wf_ozone_ops_related_account),
            (wf_ops_parse_one_fn)wf_ozone_ops_parse_related_account_obj,
            (wf_ops_reset_one_fn)wf_ozone_ops_related_account_reset,
            (void **)&out->accounts, &out->account_count);
    }
    if (status == WF_OK) {
        cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cur) && cur->valuestring) {
            status = wf_ops_set_string(&out->cursor, cur->valuestring);
        }
    }
    if (status != WF_OK) {
        wf_ozone_ops_related_account_list_free(out);
    }
    cJSON_Delete(root);
    return status;
}

void wf_ozone_ops_related_account_list_free(
    wf_ozone_ops_related_account_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->account_count; ++i) {
        wf_ozone_ops_related_account_reset(&l->accounts[i]);
    }
    free(l->accounts);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

/* ================================================================== */
/* Agent wrappers (sync auth, call generated lex wrapper, decode)    */
/* ================================================================== */

#define WF_OPS_Q(ns, op, call_op, OutType, parsefn)                         \
    wf_status wf_ozone_ops_##ns##_##op(                                      \
        wf_agent *agent,                                                     \
        const wf_lex_tools_ozone_##ns##_##call_op##_main_params *params,     \
        OutType *out) {                                                      \
        if (!agent || !agent->client || !params || !out) {                   \
            return WF_ERR_INVALID_ARG;                                       \
        }                                                                     \
        memset(out, 0, sizeof(*out));                                        \
        wf_agent_sync_auth(agent);                                           \
        wf_response res = {0};                                               \
        wf_status st = wf_lex_tools_ozone_##ns##_##call_op##_main_call(      \
            agent->client, params, &res);                                    \
        if (st != WF_OK) {                                                   \
            wf_response_free(&res);                                          \
            return st;                                                       \
        }                                                                     \
        st = parsefn(res.body, res.body_len, out);                          \
        wf_response_free(&res);                                              \
        return st;                                                           \
    }

#define WF_OPS_P(ns, op, call_op, OutType, parsefn)                         \
    wf_status wf_ozone_ops_##ns##_##op(                                      \
        wf_agent *agent,                                                     \
        const wf_lex_tools_ozone_##ns##_##call_op##_main_input *input,       \
        OutType *out) {                                                      \
        if (!agent || !agent->client || !input || !out) {                    \
            return WF_ERR_INVALID_ARG;                                       \
        }                                                                     \
        memset(out, 0, sizeof(*out));                                        \
        wf_agent_sync_auth(agent);                                           \
        wf_response res = {0};                                               \
        wf_status st = wf_lex_tools_ozone_##ns##_##call_op##_main_call(      \
            agent->client, input, &res);                                     \
        if (st != WF_OK) {                                                   \
            wf_response_free(&res);                                          \
            return st;                                                       \
        }                                                                     \
        st = parsefn(res.body, res.body_len, out);                          \
        wf_response_free(&res);                                              \
        return st;                                                           \
    }

#define WF_OPS_RAW(ns, op, call_op)                                          \
    wf_status wf_ozone_ops_##ns##_##op(                                       \
        wf_agent *agent,                                                     \
        const wf_lex_tools_ozone_##ns##_##call_op##_main_input *input,        \
        wf_response *out) {                                                  \
        if (!agent || !agent->client || !input || !out) {                    \
            return WF_ERR_INVALID_ARG;                                       \
        }                                                                     \
        wf_agent_sync_auth(agent);                                           \
        return wf_lex_tools_ozone_##ns##_##call_op##_main_call(              \
            agent->client, input, out);                                       \
    }

/* report.* */
WF_OPS_Q(report, query_reports, query_reports, wf_ozone_ops_report_list,
         wf_ozone_ops_parse_report_list)
WF_OPS_Q(report, get_report, get_report, wf_ozone_ops_report_view,
         wf_ozone_ops_parse_report_view)
WF_OPS_Q(report, get_latest_report, get_latest_report, wf_ozone_ops_report_view,
         wf_ozone_ops_parse_wrapped_report)
WF_OPS_Q(report, get_live_stats, get_live_stats, wf_ozone_ops_live_stats,
         wf_ozone_ops_parse_live_stats)
WF_OPS_Q(report, get_historical_stats, get_historical_stats,
         wf_ozone_ops_historical_stats_list,
         wf_ozone_ops_parse_historical_stats_list)
WF_OPS_Q(report, get_assignments, get_assignments,
         wf_ozone_ops_report_assignment_list,
         wf_ozone_ops_parse_report_assignment_list)
WF_OPS_Q(report, query_activities, query_activities,
         wf_ozone_ops_report_activity_list,
         wf_ozone_ops_parse_report_activity_list)
WF_OPS_Q(report, list_activities, list_activities,
         wf_ozone_ops_report_activity_list,
         wf_ozone_ops_parse_report_activity_list)
WF_OPS_P(report, create_activity, create_activity,
         wf_ozone_ops_report_activity_view, wf_ozone_ops_parse_wrapped_activity)
WF_OPS_P(report, assign_moderator, assign_moderator,
         wf_ozone_ops_report_assignment_view,
         wf_ozone_ops_parse_report_assignment_view)
WF_OPS_P(report, unassign_moderator, unassign_moderator,
         wf_ozone_ops_report_assignment_view,
         wf_ozone_ops_parse_report_assignment_view)
WF_OPS_P(report, reassign_queue, reassign_queue, wf_ozone_ops_report_view,
         wf_ozone_ops_parse_wrapped_report)
WF_OPS_RAW(report, refresh_stats, refresh_stats)

/* queue.* */
WF_OPS_Q(queue, list_queues, list_queues, wf_ozone_ops_queue_list,
         wf_ozone_ops_parse_queue_list)
WF_OPS_Q(queue, get_assignments, get_assignments,
         wf_ozone_ops_queue_assignment_list,
         wf_ozone_ops_parse_queue_assignment_list)
WF_OPS_P(queue, create_queue, create_queue, wf_ozone_ops_queue_view,
         wf_ozone_ops_parse_wrapped_queue)
WF_OPS_P(queue, update_queue, update_queue, wf_ozone_ops_queue_view,
         wf_ozone_ops_parse_wrapped_queue)
WF_OPS_P(queue, delete_queue, delete_queue, wf_ozone_ops_delete_queue_result,
         wf_ozone_ops_parse_delete_queue_result)
WF_OPS_P(queue, route_reports, route_reports,
         wf_ozone_ops_route_reports_result,
         wf_ozone_ops_parse_route_reports_result)
WF_OPS_P(queue, assign_moderator, assign_moderator,
         wf_ozone_ops_queue_assignment_view,
         wf_ozone_ops_parse_queue_assignment_view)
WF_OPS_RAW(queue, unassign_moderator, unassign_moderator)

/* signature.* */
WF_OPS_Q(signature, find_correlation, find_correlation,
         wf_ozone_ops_sig_detail_list, wf_ozone_ops_parse_sig_detail_list)
WF_OPS_Q(signature, find_related_accounts, find_related_accounts,
         wf_ozone_ops_related_account_list,
         wf_ozone_ops_parse_related_account_list)
WF_OPS_Q(signature, search_accounts, search_accounts,
         wf_ozone_ops_account_list, wf_ozone_ops_parse_account_list)
