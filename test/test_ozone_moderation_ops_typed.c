/*
 * test_ozone_moderation_ops_typed.c — offline tests for the
 * tools.ozone.report.* / queue.* / signature.* moderation-ops typed wrappers.
 *
 * Hardcodes representative response bodies (built with cJSON), asserts the
 * hand-written ergonomic parsers populate their owned structs correctly, then
 * frees them (including repeated/multiple free for idempotency). Agent
 * wrappers require live auth and are exercised only for NULL-argument
 * validation.
 */

#include "wolfram/ozone_moderation_ops_typed.h"
#include "wolfram/atproto_lex.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- fixtures ---- */

static char *json_report_list(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "reports", arr);

    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "id", 7);
    cJSON_AddNumberToObject(r, "eventId", 3);
    cJSON_AddStringToObject(r, "status", "open");
    cJSON_AddStringToObject(r, "reportType",
                            "com.atproto.moderation.defs#reasonViolation");
    cJSON_AddStringToObject(r, "reportedBy", "did:plc:reporter");
    cJSON_AddStringToObject(r, "comment", "inappropriate");
    cJSON_AddStringToObject(r, "createdAt", "2026-01-01T00:00:00.000Z");
    cJSON_AddStringToObject(r, "updatedAt", "2026-01-02T00:00:00.000Z");
    cJSON_AddStringToObject(r, "queuedAt", "2026-01-03T00:00:00.000Z");
    cJSON_AddStringToObject(r, "actionNote", "actioned");
    cJSON_AddNumberToObject(r, "relatedReportCount", 2);
    cJSON_AddBoolToObject(r, "isMuted", 0);
    cJSON *subject = cJSON_CreateObject();
    cJSON_AddStringToObject(subject, "did", "did:plc:subject");
    cJSON_AddItemToObject(r, "subject", subject);
    cJSON_AddItemToArray(arr, r);

    cJSON_AddStringToObject(root, "cursor", "cur-1");
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *json_report_view_bare(void) {
    cJSON *r = cJSON_CreateObject();
    cJSON_AddNumberToObject(r, "id", 9);
    cJSON_AddNumberToObject(r, "eventId", 4);
    cJSON_AddStringToObject(r, "status", "escalated");
    cJSON_AddStringToObject(r, "reportType",
                            "com.atproto.moderation.defs#reasonSpam");
    cJSON_AddStringToObject(r, "reportedBy", "did:plc:reporter2");
    cJSON_AddStringToObject(r, "comment", "spammy");
    cJSON_AddStringToObject(r, "createdAt", "2026-02-01T00:00:00.000Z");
    cJSON_AddBoolToObject(r, "isMuted", 1);
    cJSON *subject = cJSON_CreateObject();
    cJSON_AddStringToObject(subject, "did", "did:plc:subject2");
    cJSON_AddItemToObject(r, "subject", subject);
    char *out = cJSON_PrintUnformatted(r);
    cJSON_Delete(r);
    return out;
}

static char *json_wrapped_report(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *r = cJSON_Parse(json_report_view_bare());
    cJSON_AddItemToObject(root, "report", r);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *json_activity_list(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "activities", arr);

    cJSON *a = cJSON_CreateObject();
    cJSON_AddNumberToObject(a, "id", 11);
    cJSON_AddNumberToObject(a, "reportId", 7);
    cJSON_AddBoolToObject(a, "isAutomated", 1);
    cJSON_AddStringToObject(a, "createdBy", "did:plc:mod");
    cJSON_AddStringToObject(a, "createdAt", "2026-01-04T00:00:00.000Z");
    cJSON_AddStringToObject(a, "internalNote", "auto-routed");
    cJSON_AddStringToObject(a, "publicNote", "thanks");
    cJSON *activity = cJSON_CreateObject();
    cJSON_AddStringToObject(activity, "previousStatus", "open");
    cJSON_AddItemToObject(a, "activity", activity);
    cJSON_AddItemToArray(arr, a);

    cJSON_AddStringToObject(root, "cursor", "act-cur");
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *json_wrapped_activity(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *a = cJSON_Parse(json_activity_list());
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(a, "activities");
    cJSON *first = cJSON_GetArrayItem(arr, 0);
    cJSON *act = cJSON_DetachItemViaPointer(arr, first);
    cJSON_AddItemToObject(root, "activity", act);
    cJSON_Delete(a);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *json_assignment_list(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "assignments", arr);

    cJSON *a = cJSON_CreateObject();
    cJSON_AddNumberToObject(a, "id", 21);
    cJSON_AddStringToObject(a, "did", "did:plc:mod");
    cJSON_AddNumberToObject(a, "reportId", 7);
    cJSON_AddStringToObject(a, "startAt", "2026-01-05T00:00:00.000Z");
    cJSON_AddStringToObject(a, "endAt", "2026-01-06T00:00:00.000Z");
    cJSON *mod = cJSON_CreateObject();
    cJSON_AddStringToObject(mod, "did", "did:plc:mod");
    cJSON_AddItemToObject(a, "moderator", mod);
    cJSON_AddItemToArray(arr, a);

    cJSON_AddStringToObject(root, "cursor", "asn-cur");
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *json_assignment_view_bare(void) {
    cJSON *a = cJSON_CreateObject();
    cJSON_AddNumberToObject(a, "id", 22);
    cJSON_AddStringToObject(a, "did", "did:plc:mod2");
    cJSON_AddNumberToObject(a, "reportId", 9);
    cJSON_AddStringToObject(a, "startAt", "2026-01-07T00:00:00.000Z");
    cJSON *queue = cJSON_CreateObject();
    cJSON_AddNumberToObject(queue, "id", 1);
    cJSON_AddItemToObject(a, "queue", queue);
    char *out = cJSON_PrintUnformatted(a);
    cJSON_Delete(a);
    return out;
}

static char *json_live_stats(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *s = cJSON_CreateObject();
    cJSON_AddNumberToObject(s, "pendingCount", 5);
    cJSON_AddNumberToObject(s, "actionedCount", 3);
    cJSON_AddNumberToObject(s, "escalatedCount", 1);
    cJSON_AddNumberToObject(s, "inboundCount", 10);
    cJSON_AddNumberToObject(s, "actionRate", 30);
    cJSON_AddNumberToObject(s, "avgHandlingTimeSec", 120);
    cJSON_AddStringToObject(s, "lastUpdated", "2026-01-08T00:00:00.000Z");
    cJSON_AddItemToObject(root, "stats", s);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *json_historical_stats(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "stats", arr);

    cJSON *s = cJSON_CreateObject();
    cJSON_AddStringToObject(s, "date", "2026-01-08");
    cJSON_AddStringToObject(s, "computedAt", "2026-01-09T00:00:00.000Z");
    cJSON_AddNumberToObject(s, "pendingCount", 4);
    cJSON_AddNumberToObject(s, "actionedCount", 2);
    cJSON_AddNumberToObject(s, "escalatedCount", 0);
    cJSON_AddNumberToObject(s, "inboundCount", 6);
    cJSON_AddNumberToObject(s, "actionRate", 33);
    cJSON_AddNumberToObject(s, "avgHandlingTimeSec", 90);
    cJSON_AddItemToArray(arr, s);

    cJSON_AddStringToObject(root, "cursor", "hist-cur");
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *json_queue_list(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "queues", arr);

    cJSON *q = cJSON_CreateObject();
    cJSON_AddNumberToObject(q, "id", 1);
    cJSON_AddStringToObject(q, "name", "Default");
    cJSON *st = cJSON_CreateArray();
    cJSON_AddItemToArray(st, cJSON_CreateString("account"));
    cJSON_AddItemToArray(st, cJSON_CreateString("repo"));
    cJSON_AddItemToObject(q, "subjectTypes", st);
    cJSON_AddStringToObject(q, "collection", "app.bsky.feed.post");
    cJSON *rt = cJSON_CreateArray();
    cJSON_AddItemToArray(rt, cJSON_CreateString("com.atproto.moderation.defs#reasonSpam"));
    cJSON_AddItemToObject(q, "reportTypes", rt);
    cJSON_AddStringToObject(q, "description", "default queue");
    cJSON_AddStringToObject(q, "createdBy", "did:plc:admin");
    cJSON_AddStringToObject(q, "createdAt", "2026-01-01T00:00:00.000Z");
    cJSON_AddStringToObject(q, "updatedAt", "2026-01-02T00:00:00.000Z");
    cJSON_AddBoolToObject(q, "enabled", 1);
    cJSON *stats = cJSON_CreateObject();
    cJSON_AddNumberToObject(stats, "pendingCount", 5);
    cJSON_AddItemToObject(q, "stats", stats);
    cJSON_AddItemToArray(arr, q);

    cJSON_AddStringToObject(root, "cursor", "q-cur");
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *json_wrapped_queue(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *q = cJSON_Parse(json_queue_list());
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(q, "queues");
    cJSON *first = cJSON_GetArrayItem(arr, 0);
    cJSON *qo = cJSON_DetachItemViaPointer(arr, first);
    cJSON_AddItemToObject(root, "queue", qo);
    cJSON_Delete(q);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *json_queue_assignment_list(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "assignments", arr);

    cJSON *a = cJSON_CreateObject();
    cJSON_AddNumberToObject(a, "id", 31);
    cJSON_AddStringToObject(a, "did", "did:plc:mod");
    cJSON_AddStringToObject(a, "startAt", "2026-01-10T00:00:00.000Z");
    cJSON_AddStringToObject(a, "endAt", "2026-01-11T00:00:00.000Z");
    cJSON *queue = cJSON_CreateObject();
    cJSON_AddNumberToObject(queue, "id", 1);
    cJSON_AddItemToObject(a, "queue", queue);
    cJSON_AddItemToArray(arr, a);

    cJSON_AddStringToObject(root, "cursor", "qa-cur");
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *json_queue_assignment_view_bare(void) {
    cJSON *a = cJSON_CreateObject();
    cJSON_AddNumberToObject(a, "id", 32);
    cJSON_AddStringToObject(a, "did", "did:plc:mod3");
    cJSON_AddStringToObject(a, "startAt", "2026-01-12T00:00:00.000Z");
    cJSON *mod = cJSON_CreateObject();
    cJSON_AddStringToObject(mod, "did", "did:plc:mod3");
    cJSON_AddItemToObject(a, "moderator", mod);
    char *out = cJSON_PrintUnformatted(a);
    cJSON_Delete(a);
    return out;
}

static char *json_delete_queue_result(void) {
    return cJSON_PrintUnformatted(
        cJSON_Parse("{\"deleted\":true,\"reportsMigrated\":7}"));
}

static char *json_route_reports_result(void) {
    return cJSON_PrintUnformatted(
        cJSON_Parse("{\"assigned\":4,\"unmatched\":1}"));
}

static char *json_sig_detail_list(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "details", arr);
    cJSON *d1 = cJSON_CreateObject();
    cJSON_AddStringToObject(d1, "property", "ip");
    cJSON_AddStringToObject(d1, "value", "1.2.3.4");
    cJSON_AddItemToArray(arr, d1);
    cJSON *d2 = cJSON_CreateObject();
    cJSON_AddStringToObject(d2, "property", "email");
    cJSON_AddStringToObject(d2, "value", "a@b.com");
    cJSON_AddItemToArray(arr, d2);
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *json_related_account_list(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "accounts", arr);

    cJSON *ra = cJSON_CreateObject();
    cJSON *acct = cJSON_CreateObject();
    cJSON_AddStringToObject(acct, "did", "did:plc:related");
    cJSON_AddStringToObject(acct, "handle", "related.example.com");
    cJSON_AddStringToObject(acct, "email", "r@example.com");
    cJSON_AddStringToObject(acct, "indexedAt", "2026-01-13T00:00:00.000Z");
    cJSON_AddItemToObject(ra, "account", acct);
    cJSON *sims = cJSON_CreateArray();
    cJSON_AddItemToObject(ra, "similarities", sims);
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "property", "ip");
    cJSON_AddStringToObject(d, "value", "1.2.3.4");
    cJSON_AddItemToArray(sims, d);
    cJSON_AddItemToArray(arr, ra);

    cJSON_AddStringToObject(root, "cursor", "rel-cur");
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *json_account_list(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "accounts", arr);

    cJSON *a = cJSON_CreateObject();
    cJSON_AddStringToObject(a, "did", "did:plc:found");
    cJSON_AddStringToObject(a, "handle", "found.example.com");
    cJSON_AddStringToObject(a, "email", "f@example.com");
    cJSON_AddStringToObject(a, "indexedAt", "2026-01-14T00:00:00.000Z");
    cJSON_AddItemToArray(arr, a);

    cJSON_AddStringToObject(root, "cursor", "acc-cur");
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

int main(void) {
    /* ---- report list ---- */
    {
        char *json = json_report_list();
        WF_CHECK(json != NULL);
        wf_ozone_ops_report_list list = {0};
        wf_status st = wf_ozone_ops_parse_report_list(json, strlen(json), &list);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(list.report_count == 1);
        WF_CHECK(list.reports != NULL);
        if (list.reports) {
            wf_ozone_ops_report_view *r = &list.reports[0];
            WF_CHECK(r->has_id && r->id == 7);
            WF_CHECK(r->has_event_id && r->event_id == 3);
            WF_CHECK(r->status && strcmp(r->status, "open") == 0);
            WF_CHECK(r->report_type &&
                     strcmp(r->report_type,
                            "com.atproto.moderation.defs#reasonViolation") == 0);
            WF_CHECK(r->reported_by &&
                     strcmp(r->reported_by, "did:plc:reporter") == 0);
            WF_CHECK(r->comment && strcmp(r->comment, "inappropriate") == 0);
            WF_CHECK(r->created_at &&
                     strcmp(r->created_at, "2026-01-01T00:00:00.000Z") == 0);
            WF_CHECK(r->queued_at &&
                     strcmp(r->queued_at, "2026-01-03T00:00:00.000Z") == 0);
            WF_CHECK(r->action_note && strcmp(r->action_note, "actioned") == 0);
            WF_CHECK(r->has_related_report_count && r->related_report_count == 2);
            WF_CHECK(r->has_is_muted && r->is_muted == false);
            WF_CHECK(r->extra != NULL);
        }
        WF_CHECK(list.cursor && strcmp(list.cursor, "cur-1") == 0);
        wf_ozone_ops_report_list_free(&list);
        wf_ozone_ops_report_list_free(&list); /* idempotent */
    }

    /* ---- report view (bare) ---- */
    {
        char *json = json_report_view_bare();
        wf_ozone_ops_report_view v = {0};
        wf_status st = wf_ozone_ops_parse_report_view(json, strlen(json), &v);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(v.has_id && v.id == 9);
        WF_CHECK(v.status && strcmp(v.status, "escalated") == 0);
        WF_CHECK(v.has_is_muted && v.is_muted == true);
        WF_CHECK(v.extra != NULL);
        wf_ozone_ops_report_view_free(&v);
    }

    /* ---- wrapped report ({report}) ---- */
    {
        char *json = json_wrapped_report();
        wf_ozone_ops_report_view v = {0};
        wf_status st = wf_ozone_ops_parse_wrapped_report(json, strlen(json), &v);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(v.has_id && v.id == 9);
        WF_CHECK(v.extra != NULL);
        wf_ozone_ops_report_view_free(&v);
    }

    /* ---- activity list ---- */
    {
        char *json = json_activity_list();
        wf_ozone_ops_report_activity_list list = {0};
        wf_status st =
            wf_ozone_ops_parse_report_activity_list(json, strlen(json), &list);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(list.activity_count == 1);
        if (list.activities) {
            wf_ozone_ops_report_activity_view *a = &list.activities[0];
            WF_CHECK(a->has_id && a->id == 11);
            WF_CHECK(a->has_report_id && a->report_id == 7);
            WF_CHECK(a->has_is_automated && a->is_automated == true);
            WF_CHECK(a->created_by && strcmp(a->created_by, "did:plc:mod") == 0);
            WF_CHECK(a->internal_note &&
                     strcmp(a->internal_note, "auto-routed") == 0);
            WF_CHECK(a->public_note && strcmp(a->public_note, "thanks") == 0);
            WF_CHECK(a->extra != NULL);
        }
        WF_CHECK(list.cursor && strcmp(list.cursor, "act-cur") == 0);
        wf_ozone_ops_report_activity_list_free(&list);
    }

    /* ---- wrapped activity ({activity}) ---- */
    {
        char *json = json_wrapped_activity();
        wf_ozone_ops_report_activity_view v = {0};
        wf_status st =
            wf_ozone_ops_parse_wrapped_activity(json, strlen(json), &v);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(v.has_report_id && v.report_id == 7);
        WF_CHECK(v.extra != NULL);
        wf_ozone_ops_report_activity_view_free(&v);
    }

    /* ---- assignment list ---- */
    {
        char *json = json_assignment_list();
        wf_ozone_ops_report_assignment_list list = {0};
        wf_status st = wf_ozone_ops_parse_report_assignment_list(
            json, strlen(json), &list);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(list.assignment_count == 1);
        if (list.assignments) {
            wf_ozone_ops_report_assignment_view *a = &list.assignments[0];
            WF_CHECK(a->has_id && a->id == 21);
            WF_CHECK(a->did && strcmp(a->did, "did:plc:mod") == 0);
            WF_CHECK(a->has_report_id && a->report_id == 7);
            WF_CHECK(a->start_at &&
                     strcmp(a->start_at, "2026-01-05T00:00:00.000Z") == 0);
            WF_CHECK(a->end_at &&
                     strcmp(a->end_at, "2026-01-06T00:00:00.000Z") == 0);
            WF_CHECK(a->extra != NULL);
        }
        WF_CHECK(list.cursor && strcmp(list.cursor, "asn-cur") == 0);
        wf_ozone_ops_report_assignment_list_free(&list);
    }

    /* ---- assignment view (bare) ---- */
    {
        char *json = json_assignment_view_bare();
        wf_ozone_ops_report_assignment_view v = {0};
        wf_status st =
            wf_ozone_ops_parse_report_assignment_view(json, strlen(json), &v);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(v.has_id && v.id == 22);
        WF_CHECK(v.did && strcmp(v.did, "did:plc:mod2") == 0);
        WF_CHECK(v.has_report_id && v.report_id == 9);
        WF_CHECK(v.extra != NULL);
        wf_ozone_ops_report_assignment_view_free(&v);
    }

    /* ---- live stats ---- */
    {
        char *json = json_live_stats();
        wf_ozone_ops_live_stats v = {0};
        wf_status st = wf_ozone_ops_parse_live_stats(json, strlen(json), &v);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(v.has_pending_count && v.pending_count == 5);
        WF_CHECK(v.has_actioned_count && v.actioned_count == 3);
        WF_CHECK(v.has_escalated_count && v.escalated_count == 1);
        WF_CHECK(v.has_inbound_count && v.inbound_count == 10);
        WF_CHECK(v.has_action_rate && v.action_rate == 30);
        WF_CHECK(v.has_avg_handling_time_sec && v.avg_handling_time_sec == 120);
        WF_CHECK(v.last_updated &&
                 strcmp(v.last_updated, "2026-01-08T00:00:00.000Z") == 0);
        wf_ozone_ops_live_stats_free(&v);
    }

    /* ---- historical stats list ---- */
    {
        char *json = json_historical_stats();
        wf_ozone_ops_historical_stats_list list = {0};
        wf_status st = wf_ozone_ops_parse_historical_stats_list(
            json, strlen(json), &list);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(list.stats_count == 1);
        if (list.stats) {
            wf_ozone_ops_historical_stats *h = &list.stats[0];
            WF_CHECK(h->date && strcmp(h->date, "2026-01-08") == 0);
            WF_CHECK(h->has_action_rate && h->action_rate == 33);
            WF_CHECK(h->extra != NULL);
        }
        WF_CHECK(list.cursor && strcmp(list.cursor, "hist-cur") == 0);
        wf_ozone_ops_historical_stats_list_free(&list);
    }

    /* ---- queue list ---- */
    {
        char *json = json_queue_list();
        wf_ozone_ops_queue_list list = {0};
        wf_status st = wf_ozone_ops_parse_queue_list(json, strlen(json), &list);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(list.queue_count == 1);
        if (list.queues) {
            wf_ozone_ops_queue_view *q = &list.queues[0];
            WF_CHECK(q->has_id && q->id == 1);
            WF_CHECK(q->name && strcmp(q->name, "Default") == 0);
            WF_CHECK(q->subject_type_count == 2);
            WF_CHECK(q->report_type_count == 1);
            WF_CHECK(q->collection &&
                     strcmp(q->collection, "app.bsky.feed.post") == 0);
            WF_CHECK(q->has_enabled && q->enabled == true);
            WF_CHECK(q->extra != NULL); /* detached queueStats */
        }
        WF_CHECK(list.cursor && strcmp(list.cursor, "q-cur") == 0);
        wf_ozone_ops_queue_list_free(&list);
    }

    /* ---- wrapped queue ({queue}) ---- */
    {
        char *json = json_wrapped_queue();
        wf_ozone_ops_queue_view v = {0};
        wf_status st = wf_ozone_ops_parse_wrapped_queue(json, strlen(json), &v);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(v.has_id && v.id == 1);
        WF_CHECK(v.extra != NULL);
        wf_ozone_ops_queue_view_free(&v);
    }

    /* ---- queue assignment list ---- */
    {
        char *json = json_queue_assignment_list();
        wf_ozone_ops_queue_assignment_list list = {0};
        wf_status st = wf_ozone_ops_parse_queue_assignment_list(
            json, strlen(json), &list);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(list.assignment_count == 1);
        if (list.assignments) {
            wf_ozone_ops_queue_assignment_view *a = &list.assignments[0];
            WF_CHECK(a->has_id && a->id == 31);
            WF_CHECK(a->did && strcmp(a->did, "did:plc:mod") == 0);
            WF_CHECK(a->extra != NULL);
        }
        WF_CHECK(list.cursor && strcmp(list.cursor, "qa-cur") == 0);
        wf_ozone_ops_queue_assignment_list_free(&list);
    }

    /* ---- queue assignment view (bare) ---- */
    {
        char *json = json_queue_assignment_view_bare();
        wf_ozone_ops_queue_assignment_view v = {0};
        wf_status st =
            wf_ozone_ops_parse_queue_assignment_view(json, strlen(json), &v);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(v.has_id && v.id == 32);
        WF_CHECK(v.did && strcmp(v.did, "did:plc:mod3") == 0);
        WF_CHECK(v.extra != NULL);
        wf_ozone_ops_queue_assignment_view_free(&v);
    }

    /* ---- delete queue result ---- */
    {
        char *json = json_delete_queue_result();
        wf_ozone_ops_delete_queue_result v = {0};
        wf_status st =
            wf_ozone_ops_parse_delete_queue_result(json, strlen(json), &v);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(v.has_deleted && v.deleted == true);
        WF_CHECK(v.has_reports_migrated && v.reports_migrated == 7);
    }

    /* ---- route reports result ---- */
    {
        char *json = json_route_reports_result();
        wf_ozone_ops_route_reports_result v = {0};
        wf_status st =
            wf_ozone_ops_parse_route_reports_result(json, strlen(json), &v);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(v.has_assigned && v.assigned == 4);
        WF_CHECK(v.has_unmatched && v.unmatched == 1);
    }

    /* ---- sig detail list ---- */
    {
        char *json = json_sig_detail_list();
        wf_ozone_ops_sig_detail_list list = {0};
        wf_status st =
            wf_ozone_ops_parse_sig_detail_list(json, strlen(json), &list);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(list.detail_count == 2);
        if (list.details) {
            WF_CHECK(list.details[0].property &&
                     strcmp(list.details[0].property, "ip") == 0);
            WF_CHECK(list.details[0].value &&
                     strcmp(list.details[0].value, "1.2.3.4") == 0);
            WF_CHECK(list.details[0].extra != NULL);
        }
        wf_ozone_ops_sig_detail_list_free(&list);
    }

    /* ---- related account list ---- */
    {
        char *json = json_related_account_list();
        wf_ozone_ops_related_account_list list = {0};
        wf_status st =
            wf_ozone_ops_parse_related_account_list(json, strlen(json), &list);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(list.account_count == 1);
        if (list.accounts) {
            wf_ozone_ops_related_account *ra = &list.accounts[0];
            WF_CHECK(ra->account.did &&
                     strcmp(ra->account.did, "did:plc:related") == 0);
            WF_CHECK(ra->account.handle &&
                     strcmp(ra->account.handle, "related.example.com") == 0);
            WF_CHECK(ra->similarity_count == 1);
            WF_CHECK(ra->similarities &&
                     strcmp(ra->similarities[0].property, "ip") == 0);
            WF_CHECK(ra->extra != NULL);
        }
        WF_CHECK(list.cursor && strcmp(list.cursor, "rel-cur") == 0);
        wf_ozone_ops_related_account_list_free(&list);
    }

    /* ---- account list ---- */
    {
        char *json = json_account_list();
        wf_ozone_ops_account_list list = {0};
        wf_status st = wf_ozone_ops_parse_account_list(json, strlen(json), &list);
        free(json);
        WF_CHECK(st == WF_OK);
        WF_CHECK(list.account_count == 1);
        if (list.accounts) {
            wf_ozone_ops_account *a = &list.accounts[0];
            WF_CHECK(a->did && strcmp(a->did, "did:plc:found") == 0);
            WF_CHECK(a->handle && strcmp(a->handle, "found.example.com") == 0);
            WF_CHECK(a->extra != NULL);
        }
        WF_CHECK(list.cursor && strcmp(list.cursor, "acc-cur") == 0);
        wf_ozone_ops_account_list_free(&list);
    }

    /* ---- invalid-arg validation for parsers ---- */
    {
        wf_ozone_ops_report_list list = {0};
        WF_CHECK(wf_ozone_ops_parse_report_list(NULL, 0, &list) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ozone_ops_parse_report_list("not json", 7, &list) ==
                 WF_ERR_PARSE);
    }

    /* ---- agent wrappers: NULL-argument validation (offline) ---- */
    {
        wf_agent *agent = wf_agent_new("https://mod.example.com");
        WF_CHECK(agent != NULL);

        wf_lex_tools_ozone_report_query_reports_main_params rq = {0};
        wf_ozone_ops_report_list rl = {0};
        WF_CHECK(wf_ozone_ops_report_query_reports(NULL, &rq, &rl) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ozone_ops_report_query_reports(agent, NULL, &rl) ==
                 WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_report_assign_moderator_main_input ai = {0};
        wf_ozone_ops_report_assignment_view av = {0};
        WF_CHECK(wf_ozone_ops_report_assign_moderator(NULL, &ai, &av) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ozone_ops_report_assign_moderator(agent, NULL, &av) ==
                 WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_report_refresh_stats_main_input ri = {0};
        wf_response r = {0};
        WF_CHECK(wf_ozone_ops_report_refresh_stats(NULL, &ri, &r) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ozone_ops_report_refresh_stats(agent, NULL, &r) ==
                 WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_queue_route_reports_main_input qi = {0};
        wf_ozone_ops_route_reports_result rr = {0};
        WF_CHECK(wf_ozone_ops_queue_route_reports(agent, NULL, &rr) ==
                 WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_signature_search_accounts_main_params sq = {0};
        wf_ozone_ops_account_list al = {0};
        WF_CHECK(wf_ozone_ops_signature_search_accounts(agent, NULL, &al) ==
                 WF_ERR_INVALID_ARG);

        wf_agent_free(agent);
    }

    WF_TEST_SUMMARY();
}
