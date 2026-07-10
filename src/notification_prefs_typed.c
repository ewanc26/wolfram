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

#include <string.h>

wf_status wf_notification_prefs_parse(const char *json, size_t len,
                                      wf_notification_prefs *out) {
    return wf_notification_v2_preferences_parse(json, len, out);
}

void wf_notification_prefs_free(wf_notification_prefs *p) {
    wf_notification_v2_preferences_free(p);
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
    if (!agent || priorities_json) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_put_notification_priority(agent, priority);
}

wf_status wf_agent_put_notification_priority(wf_agent *agent, int priority) {
    if (!agent) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_app_bsky_notification_put_preferences_main_input input = {0};
    input.priority = (priority != 0);

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_app_bsky_notification_put_preferences_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}
