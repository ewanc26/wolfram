/*
 * test_notification_v2_typed.c — offline tests for the app.bsky.notification
 * v2 preferences + activity subscription typed parsers. Hardcodes
 * representative response bodies and asserts the owned structs are populated
 * correctly, then freed. Agent wrappers require live auth and are exercised
 * only for NULL-argument validation.
 */

#include "wolfram/notification_v2_typed.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* app.bsky.notification.putActivitySubscription response
 * (`{ subject, activitySubscription }`). */
static const char *kPutActivitySubJson =
    "{"
    "  \"subject\": \"did:plc:alice00000000000000000000\","
    "  \"activitySubscription\": { \"post\": true, \"reply\": false }"
    "}";

/* app.bsky.notification.listActivitySubscriptions response
 * (`{ cursor, subscriptions: profileView[] }`). */
static const char *kListActivityJson =
    "{"
    "  \"cursor\": \"next-cursor-xyz\","
    "  \"subscriptions\": ["
    "    {"
    "      \"did\": \"did:plc:bob0000000000000000000000\","
    "      \"handle\": \"bob.bsky.social\","
    "      \"displayName\": \"Bob\","
    "      \"avatar\": \"https://cdn.bsky.app/img/bob.jpg\","
    "      \"indexedAt\": \"2026-07-01T00:00:00.000Z\""
    "    },"
    "    {"
    "      \"did\": \"did:plc:carol0000000000000000000\","
    "      \"handle\": \"carol.bsky.social\""
    "    }"
    "  ]"
    "}";

int main(void) {
    /* ---- Invalid args -> WF_ERR_INVALID_ARG ---- */
    wf_notif_v2_activity_subscription_result put = {0};
    WF_CHECK(wf_notif_v2_parse_put_activity_subscription(NULL, 0, &put) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_notif_v2_parse_put_activity_subscription(
                 kPutActivitySubJson, strlen(kPutActivitySubJson), NULL) ==
             WF_ERR_INVALID_ARG);

    wf_notif_v2_subscription_view_list list = {0};
    WF_CHECK(wf_notif_v2_parse_list_activity_subscriptions(NULL, 0, &list) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_notif_v2_parse_list_activity_subscriptions(
                 kListActivityJson, strlen(kListActivityJson), NULL) ==
             WF_ERR_INVALID_ARG);

    wf_notif_v2_preferences prefs = {0};
    WF_CHECK(wf_notif_v2_parse_preferences(NULL, 0, &prefs) ==
             WF_ERR_INVALID_ARG);

    /* ---- putActivitySubscription ---- */
    WF_CHECK(wf_notif_v2_parse_put_activity_subscription(
                 kPutActivitySubJson, strlen(kPutActivitySubJson), &put) ==
             WF_OK);
    WF_CHECK(put.subject &&
             strcmp(put.subject, "did:plc:alice00000000000000000000") == 0);
    WF_CHECK(put.has_activity_subscription == 1);
    WF_CHECK(put.activity_subscription.has_post == 1);
    WF_CHECK(put.activity_subscription.post == 1);
    WF_CHECK(put.activity_subscription.has_reply == 1);
    WF_CHECK(put.activity_subscription.reply == 0);
    wf_notif_v2_activity_subscription_result_free(&put);
    WF_CHECK(put.subject == NULL && put.extra == NULL &&
             put.activity_subscription.extra == NULL);

    /* ---- listActivitySubscriptions ---- */
    WF_CHECK(wf_notif_v2_parse_list_activity_subscriptions(
                 kListActivityJson, strlen(kListActivityJson), &list) ==
             WF_OK);
    WF_CHECK(list.count == 2);
    WF_CHECK(list.cursor && strcmp(list.cursor, "next-cursor-xyz") == 0);
    WF_CHECK(list.items[0].did &&
             strcmp(list.items[0].did,
                    "did:plc:bob0000000000000000000000") == 0);
    WF_CHECK(list.items[0].handle &&
             strcmp(list.items[0].handle, "bob.bsky.social") == 0);
    WF_CHECK(list.items[0].display_name &&
             strcmp(list.items[0].display_name, "Bob") == 0);
    WF_CHECK(list.items[0].avatar &&
             strcmp(list.items[0].avatar,
                    "https://cdn.bsky.app/img/bob.jpg") == 0);
    /* The unknown `indexedAt` is preserved in `extra`. */
    WF_CHECK(list.items[0].extra &&
             cJSON_GetObjectItemCaseSensitive(list.items[0].extra,
                                              "indexedAt") != NULL);
    /* Second item has no avatar/displayName; extra holds no known scalars. */
    WF_CHECK(list.items[1].did &&
             strcmp(list.items[1].did,
                    "did:plc:carol0000000000000000000") == 0);
    WF_CHECK(list.items[1].handle &&
             strcmp(list.items[1].handle, "carol.bsky.social") == 0);
    WF_CHECK(list.items[1].display_name == NULL);
    WF_CHECK(list.items[1].avatar == NULL);
    wf_notif_v2_subscription_view_list_free(&list);
    WF_CHECK(list.count == 0 && list.items == NULL && list.cursor == NULL);

    /* ---- putPreferencesV2 output (preferences object) ---- */
    static const char *kPrefsJson =
        "{ \"preferences\": { \"like\": { \"include\": \"all\", \"list\": true, "
        "\"push\": false } } }";
    WF_CHECK(wf_notif_v2_parse_preferences(kPrefsJson, strlen(kPrefsJson),
                                           &prefs) == WF_OK);
    WF_CHECK(prefs.preferences &&
             cJSON_GetObjectItemCaseSensitive(prefs.preferences, "like") !=
                 NULL);
    wf_notif_v2_preferences_free(&prefs);
    WF_CHECK(prefs.preferences == NULL);

    /* ---- Agent wrapper NULL validation (no live session). ---- */
    const char *deleted[1] = {"like"};
    WF_CHECK(wf_agent_put_notification_preferences_v2(
                 NULL, "{}", NULL, 0, &prefs) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_put_notification_preferences_v2(
                 NULL, NULL, NULL, 0, &prefs) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_put_notification_preferences_v2(
                 NULL, "{}", deleted, 1, &prefs) == WF_ERR_INVALID_ARG);
    static int sentinel;
    WF_CHECK(wf_agent_put_notification_preferences_v2(
                 (wf_agent *)&sentinel, "{}", deleted, 0, &prefs) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_put_notification_preferences_v2(
                 NULL, "{}", NULL, 0, NULL) == WF_ERR_INVALID_ARG);

    WF_CHECK(wf_agent_list_activity_subscriptions(NULL, 0, NULL, &list) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_list_activity_subscriptions(NULL, 0, NULL, NULL) ==
             WF_ERR_INVALID_ARG);

    WF_CHECK(wf_agent_put_activity_subscription(NULL, "{}", &put) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_put_activity_subscription(NULL, NULL, &put) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_put_activity_subscription(NULL, "{}", NULL) ==
             WF_ERR_INVALID_ARG);

    printf("notification_v2_typed: all checks passed\n");
    return 0;
}
