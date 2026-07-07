#include "wolfram/agent.h"

#include "wolfram/identity.h"
#include "wolfram/repo.h"
#include "wolfram/richtext.h"
#include "wolfram/server.h"
#include "wolfram/session.h"
#include "wolfram/syntax.h"
#include <cJSON.h>
#include "wolfram/atproto_lex.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Duplicate helper definitions (static) */

#include "_internal.h"

/* Notification endpoint implementations */

wf_status wf_agent_list_notifications(wf_agent *agent, int limit, const char *cursor,
                                       wf_response *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[2];
    size_t param_count = 0;
    char limit_buf[16];

    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf))) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "limit";
        params[param_count].value = limit_buf;
        param_count++;
    }
    if (cursor && cursor[0]) {
        params[param_count].name = "cursor";
        params[param_count].value = cursor;
        param_count++;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.notification.listNotifications",
                               params, param_count, out);
}

wf_status wf_agent_update_seen_notifications(wf_agent *agent, const char *seen_at) {
    if (!agent || !seen_at) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_syntax_datetime_is_valid(seen_at)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(root, "seenAt", seen_at)) {
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
    wf_status status = wf_xrpc_procedure(agent->client,
                                         "app.bsky.notification.updateSeen",
                                         json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_unread_count(wf_agent *agent, wf_response *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client,
                                "app.bsky.notification.getUnreadCount",
                                NULL, 0, out);
}
