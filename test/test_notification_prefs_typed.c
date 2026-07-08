/*
 * test_notification_prefs_typed.c — offline tests for the owned typed
 * notification-preferences wrappers. No network access.
 */

#include "wolfram/notification_prefs_typed.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
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
    /* 1. Parse a getPreferences body: { priority, priorities } */
    const char *json =
        "{\"priority\":true,\"priorities\":{\"like\":false,\"reply\":true}}";
    wf_notification_prefs prefs = {0};
    wf_status s = wf_notification_prefs_parse(json, strlen(json), &prefs);
    CHECK(s == WF_OK);
    CHECK(prefs.has_priority == 1);
    CHECK(prefs.priority == 1);
    CHECK(prefs.priorities != NULL);
    if (prefs.priorities) {
        cJSON *like = cJSON_GetObjectItemCaseSensitive(prefs.priorities, "like");
        cJSON *reply =
            cJSON_GetObjectItemCaseSensitive(prefs.priorities, "reply");
        CHECK(cJSON_IsBool(like) && !cJSON_IsTrue(like));
        CHECK(cJSON_IsBool(reply) && cJSON_IsTrue(reply));
    }
    wf_notification_prefs_free(&prefs);
    CHECK(prefs.priorities == NULL);

    /* 2. Optional / absent fields parse cleanly. */
    wf_notification_prefs minimal = {0};
    s = wf_notification_prefs_parse("{\"priority\":false}", strlen("{\"priority\":false}"), &minimal);
    CHECK(s == WF_OK);
    CHECK(minimal.has_priority == 1);
    CHECK(minimal.priority == 0);
    CHECK(minimal.priorities == NULL);
    wf_notification_prefs_free(&minimal);

    /* 3. Invalid JSON fails to parse. */
    wf_notification_prefs bad = {0};
    CHECK(wf_notification_prefs_parse("{not json", 8, &bad) == WF_ERR_PARSE);
    wf_notification_prefs_free(&bad);

    /* 4. NULL agent is rejected by both wrappers. */
    wf_notification_prefs out = {0};
    CHECK(wf_agent_get_notification_prefs(NULL, &out) == WF_ERR_INVALID_ARG);
    CHECK(wf_agent_put_notification_prefs(NULL, 1, NULL) == WF_ERR_INVALID_ARG);

    /* 5. Bad priorities_json is rejected (valid non-NULL agent; early return
     *    before any network access, so a sentinel pointer is safe here). */
    static int sentinel;
    wf_agent *fake = (wf_agent *)&sentinel;
    CHECK(wf_agent_put_notification_prefs(fake, 1, "not json") ==
          WF_ERR_INVALID_ARG);
    CHECK(wf_agent_put_notification_prefs(fake, 1, "[1,2,3]") ==
          WF_ERR_INVALID_ARG);

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("notification_prefs_typed: ok\n");
    return 0;
}
