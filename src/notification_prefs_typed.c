/*
 * notification_prefs_typed.c — owned typed parsers + agent convenience
 * wrappers for app.bsky.notification.putPreferences / getPreferences.
 *
 * Follows the conventions of actor_typed.c / draft_typed.c: a single owned
 * parse entry point, full cleanup on the first error, and agent wrappers that
 * dispatch through the generated `wf_lex_app_bsky_notification_*` calls after
 * `wf_agent_sync_auth`.
 */

#include "wolfram/notification_prefs_typed.h"

#include "wolfram/atproto_lex.h"
#include "agent/_internal.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

wf_status wf_notification_prefs_parse(const char *json, size_t len,
                                      wf_notification_prefs *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;

    cJSON *pri = cJSON_GetObjectItemCaseSensitive(root, "priority");
    if (pri) {
        if (!cJSON_IsBool(pri)) {
            status = WF_ERR_PARSE;
        } else {
            out->has_priority = 1;
            out->priority = cJSON_IsTrue(pri) ? 1 : 0;
        }
    }

    if (status == WF_OK) {
        cJSON *priorities = cJSON_GetObjectItemCaseSensitive(root, "priorities");
        if (priorities) {
            if (!cJSON_IsObject(priorities)) {
                status = WF_ERR_PARSE;
            } else {
                /* Detach so `out` owns the subtree; it is released by _free. */
                out->priorities = cJSON_DetachItemFromObject(root, "priorities");
            }
        }
    }

    cJSON_Delete(root);

    if (status != WF_OK) {
        if (out->priorities) {
            cJSON_Delete(out->priorities);
        }
        memset(out, 0, sizeof(*out));
    }
    return status;
}

void wf_notification_prefs_free(wf_notification_prefs *p) {
    if (!p) {
        return;
    }
    if (p->priorities) {
        cJSON_Delete(p->priorities);
    }
    memset(p, 0, sizeof(*p));
}

wf_status wf_agent_get_notification_prefs(wf_agent *agent,
                                          wf_notification_prefs *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    wf_lex_app_bsky_notification_get_preferences_main_params params = {0};
    wf_response res = {0};

    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_app_bsky_notification_get_preferences_main_call(
        agent->client, &params, &res);
    if (status == WF_OK) {
        status = wf_notification_prefs_parse(res.body, res.body_len, out);
    }

    wf_response_free(&res);
    return status;
}

wf_status wf_agent_put_notification_prefs(wf_agent *agent, int priority,
                                          const char *priorities_json) {
    if (!agent) {
        return WF_ERR_INVALID_ARG;
    }

    /* Validate the optional priorities object up front. */
    if (priorities_json) {
        cJSON *p = cJSON_Parse(priorities_json);
        if (!p || !cJSON_IsObject(p)) {
            cJSON_Delete(p);
            return WF_ERR_INVALID_ARG;
        }
        cJSON_Delete(p);
    }

    wf_lex_app_bsky_notification_put_preferences_main_input input = {0};
    input.priority = (priority != 0);
    /* TODO: the generated v1 lex input currently carries only `priority`;
     * `priorities_json` is validated above but cannot yet be transmitted
     * until the generated lex schema gains a `priorities` field. Typed v2
     * preferences (the richer 13-slot model) are implemented separately in
     * notification_v2_typed.c (wf_notification_v2_preferences_build/parse and
     * wf_agent_put_notification_preferences_v2_typed). */

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_app_bsky_notification_put_preferences_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}
