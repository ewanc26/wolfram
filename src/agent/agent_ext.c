#include "wolfram/agent.h"
#include "wolfram/atproto_lex.h"
#include "wolfram/util.h"
#include <cJSON.h>

/* Update handle – com.atproto.identity.updateHandle */
wf_status wf_agent_update_handle(wf_agent *agent, const char *new_handle) {
    if (!agent || !new_handle || !new_handle[0]) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_handle_is_valid(new_handle)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_identity_update_handle_main_input input = { .handle = new_handle };
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_com_atproto_identity_update_handle_main_call_auth(agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

/* Preferences – app.bsky.actor.putPreferences */
wf_status wf_agent_put_preferences(wf_agent *agent, const char *prefs_json, wf_response *out) {
    if (!agent || !prefs_json || !out) return WF_ERR_INVALID_ARG;
    
    /* Parse the preferences JSON array */
    cJSON *root = cJSON_Parse(prefs_json);
    if (!root) return WF_ERR_INVALID_ARG;
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return WF_ERR_INVALID_ARG;
    }
    
    size_t count = cJSON_GetArraySize(root);
    wf_lex_app_bsky_actor_put_preferences_main_input input = {0};
    
    if (count) {
        wf_lex_json *items = calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
        input.preferences.items = items;
        input.preferences.count = count;
        
        for (size_t i = 0; i < count; ++i) {
            cJSON *elem = cJSON_GetArrayItem(root, i);
            if (!elem) {
                for (size_t j = 0; j < i; ++j) free((void*)items[j].data);
                free(items);
                cJSON_Delete(root);
                return WF_ERR_INVALID_ARG;
            }
            char *elem_json = cJSON_PrintUnformatted(elem);
            if (!elem_json) {
                for (size_t j = 0; j < i; ++j) free((void*)items[j].data);
                free(items);
                cJSON_Delete(root);
                return WF_ERR_ALLOC;
            }
            items[i].data = elem_json;
            items[i].length = strlen(elem_json);
        }
    }
    
    cJSON_Delete(root);
    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_app_bsky_actor_put_preferences_main_call_auth(agent->client, &input, out);
    
    /* Clean up allocated strings and array */
    if (input.preferences.items) {
        for (size_t i = 0; i < input.preferences.count; ++i) {
            free((void*)input.preferences.items[i].data);
        }
        free((void*)input.preferences.items);
    }
    return status;
}

/* Push notification registration – app.bsky.notification.registerPush */
wf_status wf_agent_register_push(wf_agent *agent, const char *service_did, const char *token, wf_response *out) {
    if (!agent || !service_did || !token || !out) return WF_ERR_INVALID_ARG;
    wf_lex_app_bsky_notification_register_push_main_input input = {
        .service_did = service_did,
        .token = token,
        .platform = "web",
        .app_id = "wolfram"
    };
    wf_agent_sync_auth(agent);
    return wf_lex_app_bsky_notification_register_push_main_call_auth(agent->client, &input, out);
}

/* Push notification unregistration – app.bsky.notification.unregisterPush */
wf_status wf_agent_unregister_push(wf_agent *agent, const char *service_did, const char *token, wf_response *out) {
    if (!agent || !service_did || !token || !out) return WF_ERR_INVALID_ARG;
    wf_lex_app_bsky_notification_unregister_push_main_input input = {
        .service_did = service_did,
        .token = token,
        .platform = "web",
        .app_id = "wolfram"
    };
    wf_agent_sync_auth(agent);
    return wf_lex_app_bsky_notification_unregister_push_main_call_auth(agent->client, &input, out);
}
