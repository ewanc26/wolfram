/*
 * test_notification_typed.c — offline tests for the app.bsky.notification core
 * typed parsers. Builds representative listNotifications and getUnreadCount
 * response bodies with cJSON, calls the parsers, asserts ownership/fields, and
 * frees. Agent wrappers require live auth and are exercised only for
 * NULL-argument validation.
 */

#include "wolfram/notification_typed.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    /* ---- listNotifications: three different reasons ---- */
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "notifications");

    /* like */
    cJSON *n1 = cJSON_CreateObject();
    cJSON_AddStringToObject(n1, "uri", "at://did:plc:alice/app.bsky.feed.post/aa");
    cJSON_AddStringToObject(n1, "cid", "bafyreigh1");
    cJSON *a1 = cJSON_AddObjectToObject(n1, "author");
    cJSON_AddStringToObject(a1, "did", "did:plc:alice");
    cJSON_AddStringToObject(a1, "handle", "alice.bsky.social");
    cJSON_AddStringToObject(a1, "displayName", "Alice");
    cJSON_AddStringToObject(a1, "avatar", "https://cdn.bsky.app/img/alice.jpg");
    cJSON_AddStringToObject(n1, "reason", "like");
    cJSON_AddFalseToObject(n1, "isRead");
    cJSON_AddStringToObject(n1, "indexedAt", "2026-07-01T00:00:00.000Z");
    cJSON *rec1 = cJSON_AddObjectToObject(n1, "record");
    cJSON_AddStringToObject(rec1, "text", "hello");
    cJSON_AddItemToArray(arr, n1);

    /* follow */
    cJSON *n2 = cJSON_CreateObject();
    cJSON_AddStringToObject(n2, "uri", "at://did:plc:bob/app.bsky.graph.follow/bb");
    cJSON_AddStringToObject(n2, "cid", "bafyreigh2");
    cJSON *a2 = cJSON_AddObjectToObject(n2, "author");
    cJSON_AddStringToObject(a2, "did", "did:plc:bob");
    cJSON_AddStringToObject(a2, "handle", "bob.bsky.social");
    cJSON_AddStringToObject(n2, "reason", "follow");
    cJSON_AddTrueToObject(n2, "isRead");
    cJSON_AddStringToObject(n2, "indexedAt", "2026-07-02T00:00:00.000Z");
    cJSON_AddItemToArray(arr, n2);

    /* reply with reasonSubject */
    cJSON *n3 = cJSON_CreateObject();
    cJSON_AddStringToObject(n3, "uri", "at://did:plc:carol/app.bsky.feed.post/cc");
    cJSON_AddStringToObject(n3, "cid", "bafyreigh3");
    cJSON *a3 = cJSON_AddObjectToObject(n3, "author");
    cJSON_AddStringToObject(a3, "did", "did:plc:carol");
    cJSON_AddStringToObject(a3, "handle", "carol.bsky.social");
    cJSON_AddStringToObject(a3, "displayName", "Carol");
    cJSON_AddStringToObject(n3, "reason", "reply");
    cJSON_AddStringToObject(n3, "reasonSubject",
                            "at://did:plc:alice/app.bsky.feed.post/aa");
    cJSON_AddFalseToObject(n3, "isRead");
    cJSON_AddStringToObject(n3, "indexedAt", "2026-07-03T00:00:00.000Z");
    cJSON *rec3 = cJSON_AddObjectToObject(n3, "record");
    cJSON_AddStringToObject(rec3, "text", "nice post");
    cJSON_AddItemToArray(arr, n3);

    cJSON_AddStringToObject(root, "cursor", "next-cursor");
    cJSON_AddStringToObject(root, "seenAt", "2026-07-03T00:00:00.000Z");
    cJSON_AddTrueToObject(root, "priority");

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        fprintf(stderr, "failed to serialize test JSON\n");
        return 1;
    }

    /* ---- invalid args -> WF_ERR_INVALID_ARG ---- */
    wf_notification_list list = {0};
    WF_CHECK(wf_notification_parse_list(NULL, 0, &list) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_notification_parse_list(json, strlen(json), NULL) ==
             WF_ERR_INVALID_ARG);

    wf_notification_unread_count uc = {0};
    WF_CHECK(wf_notification_parse_unread_count(NULL, 0, &uc) ==
             WF_ERR_INVALID_ARG);

    /* ---- parse list ---- */
    WF_CHECK(wf_notification_parse_list(json, strlen(json), &list) == WF_OK);
    WF_CHECK(list.count == 3);
    WF_CHECK(list.cursor && strcmp(list.cursor, "next-cursor") == 0);
    WF_CHECK(list.seen_at &&
             strcmp(list.seen_at, "2026-07-03T00:00:00.000Z") == 0);
    WF_CHECK(list.has_priority == 1 && list.priority == 1);

    wf_notification_view *v0 = &list.items[0];
    WF_CHECK(v0->uri && strcmp(v0->uri,
                              "at://did:plc:alice/app.bsky.feed.post/aa") == 0);
    WF_CHECK(v0->author.did && strcmp(v0->author.did, "did:plc:alice") == 0);
    WF_CHECK(v0->author.handle &&
             strcmp(v0->author.handle, "alice.bsky.social") == 0);
    WF_CHECK(v0->author.display_name &&
             strcmp(v0->author.display_name, "Alice") == 0);
    WF_CHECK(v0->author.avatar &&
             strcmp(v0->author.avatar, "https://cdn.bsky.app/img/alice.jpg") ==
                 0);
    WF_CHECK(v0->reason && strcmp(v0->reason, "like") == 0);
    WF_CHECK(v0->is_read == 0);
    WF_CHECK(v0->record && cJSON_IsObject(v0->record));
    WF_CHECK(v0->indexed_at &&
             strcmp(v0->indexed_at, "2026-07-01T00:00:00.000Z") == 0);

    wf_notification_view *v1 = &list.items[1];
    WF_CHECK(v1->reason && strcmp(v1->reason, "follow") == 0);
    WF_CHECK(v1->is_read == 1);
    WF_CHECK(v1->reason_subject == NULL);
    WF_CHECK(v1->record == NULL);
    WF_CHECK(v1->author.display_name == NULL);

    wf_notification_view *v2 = &list.items[2];
    WF_CHECK(v2->reason && strcmp(v2->reason, "reply") == 0);
    WF_CHECK(v2->reason_subject &&
             strcmp(v2->reason_subject,
                    "at://did:plc:alice/app.bsky.feed.post/aa") == 0);
    WF_CHECK(v2->record && cJSON_IsObject(v2->record));

    wf_notification_list_free(&list);
    WF_CHECK(list.count == 0 && list.items == NULL && list.cursor == NULL &&
             list.seen_at == NULL);
    free(json);

    /* ---- getUnreadCount ---- */
    static const char *kUnread = "{\"count\": 42}";
    wf_notification_unread_count uc2 = {0};
    WF_CHECK(wf_notification_parse_unread_count(kUnread, strlen(kUnread),
                                                &uc2) == WF_OK);
    WF_CHECK(uc2.count == 42);

    /* ---- Agent wrapper NULL validation (no live session) ---- */
    wf_notification_list rl = {0};
    WF_CHECK(wf_agent_list_notifications_rich_typed(NULL, 10, NULL, &rl) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_list_notifications_rich_typed(NULL, 10, NULL, NULL) ==
             WF_ERR_INVALID_ARG);

    int64_t cnt = 0;
    WF_CHECK(wf_agent_get_unread_count_rich_typed(NULL, &cnt) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_unread_count_rich_typed(NULL, NULL) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_agent_update_seen_typed(NULL, "2026-07-03T00:00:00.000Z") ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_update_seen_typed(NULL, NULL) == WF_ERR_INVALID_ARG);

    WF_CHECK(wf_agent_register_push_typed(NULL, "did:web:pn", "tok", "ios",
                                          "app", false, false) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_register_push_typed(NULL, NULL, "tok", "ios", "app", false,
                                          false) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_register_push_typed(NULL, "did:web:pn", NULL, "ios", "app",
                                          false, false) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_register_push_typed(NULL, "did:web:pn", "tok", NULL, "app",
                                          false, false) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_register_push_typed(NULL, "did:web:pn", "tok", "ios", NULL,
                                          false, false) == WF_ERR_INVALID_ARG);

    WF_CHECK(wf_agent_unregister_push_typed(NULL, "did:web:pn", "tok", "ios",
                                            "app") == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_unregister_push_typed(NULL, NULL, "tok", "ios", "app") ==
             WF_ERR_INVALID_ARG);

    printf("notification_typed: all checks passed\n");
    return 0;
}
