/*
 * test_agent_notifications.c — unit tests for the typed notification API.
 *
 * Exercises the offline JSON parser (`wf_agent_parse_notifications`) with a
 * fixture; no network calls are made.
 */

#include "wolfram/agent.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WF_TEST_FIXTURE_DIR
#define WF_TEST_FIXTURE_DIR "test/fixtures"
#endif

static char *read_entire_file(const char *path, size_t *len_out) {
    FILE *fp = fopen(path, "rb");
    char *buf = NULL;
    long size;
    size_t len;

    if (!fp) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    len = (size_t)size;
    buf = malloc(len + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (len > 0 && fread(buf, 1, len, fp) != len) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    buf[len] = '\0';
    fclose(fp);
    if (len_out) {
        *len_out = len;
    }
    return buf;
}

static char *load_fixture(const char *filename, size_t *len_out) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", WF_TEST_FIXTURE_DIR, filename);
    return read_entire_file(path, len_out);
}

int main(void) {
    /* Invalid-argument handling. */
    wf_agent_notification_list list = {0};
    WF_CHECK(wf_agent_parse_notifications(NULL, 0, &list) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_parse_notifications("{}", 2, NULL) == WF_ERR_INVALID_ARG);

    /* Malformed JSON. */
    WF_CHECK(wf_agent_parse_notifications("not json", 7, &list) == WF_ERR_PARSE);

    /* Missing `notifications` array. */
    WF_CHECK(wf_agent_parse_notifications("{\"cursor\":\"x\"}", 13, &list) == WF_ERR_PARSE);

    /* Valid fixture. */
    size_t len = 0;
    char *json = load_fixture("listNotifications.json", &len);
    WF_CHECK(json != NULL);

    if (json) {
        wf_status status = wf_agent_parse_notifications(json, len, &list);
        WF_CHECK(status == WF_OK);
        WF_CHECK(list.notification_count == 2);
        WF_CHECK(list.cursor && strcmp(list.cursor, "cursor-token-123") == 0);
        WF_CHECK(list.seen_at && strcmp(list.seen_at, "2026-07-01T08:00:00Z") == 0);
        WF_CHECK(list.has_priority == 1);
        WF_CHECK(list.priority == 1);

        if (list.notification_count == 2) {
            wf_agent_notification *n0 = &list.notifications[0];
            WF_CHECK(n0->uri && strstr(n0->uri, "app.bsky.feed.like") != NULL);
            WF_CHECK(n0->author.did && strcmp(n0->author.did, "did:plc:author1") == 0);
            WF_CHECK(n0->author.handle && strcmp(n0->author.handle, "alice.example.com") == 0);
            WF_CHECK(n0->author.display_name && strcmp(n0->author.display_name, "Alice") == 0);
            WF_CHECK(n0->author.avatar && strstr(n0->author.avatar, "avatar1") != NULL);
            WF_CHECK(n0->reason && strcmp(n0->reason, "like") == 0);
            WF_CHECK(n0->reason_subject && strstr(n0->reason_subject, "subject1") != NULL);
            WF_CHECK(n0->is_read == 0);
            WF_CHECK(n0->record != NULL);
            WF_CHECK(cJSON_IsObject(n0->record));
            WF_CHECK(n0->label_count == 1);
            if (n0->label_count == 1) {
                WF_CHECK(n0->labels[0].val && strcmp(n0->labels[0].val, "!no-unauthenticated") == 0);
                WF_CHECK(n0->labels[0].src && strcmp(n0->labels[0].src, "did:plc:labeler") == 0);
            }

            wf_agent_notification *n1 = &list.notifications[1];
            WF_CHECK(n1->reason && strcmp(n1->reason, "follow") == 0);
            WF_CHECK(n1->is_read == 1);
            WF_CHECK(n1->author.avatar == NULL);
            WF_CHECK(n1->label_count == 0);
            WF_CHECK(n1->record != NULL);
        }

        wf_agent_notification_list_free(&list);
        /* After free the struct is zeroed and a double-free is safe. */
        WF_CHECK(list.notifications == NULL);
        WF_CHECK(list.notification_count == 0);
        wf_agent_notification_list_free(&list);

        free(json);
    }

    WF_TEST_SUMMARY();
    return 0; /* unreachable */
}
