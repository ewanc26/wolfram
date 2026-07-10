/*
 * test_notification_prefs_typed.c — offline tests for the owned typed
 * notification-preferences wrappers. No network access.
 */

#include "wolfram/notification_prefs_typed.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                         \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__,          \
                    __LINE__);                                              \
            failures++;                                                     \
        }                                                                   \
    } while (0)

int main(void) {
    /* 1. Parse the authoritative getPreferences output envelope. */
    const char *json =
        "{\"preferences\":{"
        "\"chat\":{\"include\":\"all\",\"push\":false},"
        "\"follow\":{\"include\":\"follows\",\"list\":true,\"push\":true},"
        "\"like\":{\"include\":\"all\",\"list\":true,\"push\":false},"
        "\"likeViaRepost\":{\"include\":\"all\",\"list\":false,\"push\":false},"
        "\"mention\":{\"include\":\"follows\",\"list\":true,\"push\":true},"
        "\"quote\":{\"include\":\"all\",\"list\":false,\"push\":true},"
        "\"reply\":{\"include\":\"all\",\"list\":true,\"push\":false},"
        "\"repost\":{\"include\":\"follows\",\"list\":false,\"push\":false},"
        "\"repostViaRepost\":{\"include\":\"all\",\"list\":true,\"push\":true},"
        "\"starterpackJoined\":{\"list\":true,\"push\":false},"
        "\"subscribedPost\":{\"list\":false,\"push\":true},"
        "\"unverified\":{\"list\":true,\"push\":false},"
        "\"verified\":{\"list\":false,\"push\":true}}}";
    wf_notification_prefs prefs = {0};
    wf_status s = wf_notification_prefs_parse(json, strlen(json), &prefs);
    CHECK(s == WF_OK);
    CHECK(prefs.has_chat == 1);
    CHECK(prefs.chat.include == WF_NOTIF_V2_INCLUDE_ALL);
    CHECK(prefs.has_follow == 1);
    CHECK(prefs.follow.include == WF_NOTIF_V2_INCLUDE_FOLLOWS);
    CHECK(prefs.follow.list == 1 && prefs.follow.push == 1);
    CHECK(prefs.has_verified == 1);
    CHECK(prefs.verified.list == 0 && prefs.verified.push == 1);
    wf_notification_prefs_free(&prefs);
    CHECK(prefs.has_chat == 0);

    /* 2. The obsolete v1 top-level shape is not a preferences response. */
    wf_notification_prefs minimal = {0};
    s = wf_notification_prefs_parse("{\"priority\":false}",
                                    strlen("{\"priority\":false}"), &minimal);
    CHECK(s == WF_ERR_PARSE);
    wf_notification_prefs_free(&minimal);

    /* 3. Invalid JSON fails to parse. */
    wf_notification_prefs bad = {0};
    CHECK(wf_notification_prefs_parse("{not json", 8, &bad) == WF_ERR_PARSE);
    wf_notification_prefs_free(&bad);

    /* 4. NULL agent is rejected by both wrappers. */
    wf_notification_prefs out = {0};
    CHECK(wf_agent_get_notification_prefs(NULL, &out) == WF_ERR_INVALID_ARG);
    CHECK(wf_agent_put_notification_prefs(NULL, 1, NULL) == WF_ERR_INVALID_ARG);

    /* 5. Any invented priorities_json is rejected before network access. */
    static int sentinel;
    wf_agent *fake = (wf_agent *)&sentinel;
    CHECK(wf_agent_put_notification_prefs(fake, 1, "not json") ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_put_notification_prefs(fake, 1, "[1,2,3]") ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_put_notification_prefs(fake, 1, "{\"like\":true}") ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_put_notification_priority(NULL, 1) == WF_ERR_INVALID_ARG);

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("notification_prefs_typed: ok\n");
    return 0;
}
