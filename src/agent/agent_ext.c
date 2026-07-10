#include "wolfram/agent.h"
#include "wolfram/atproto_lex.h"
#include "wolfram/util.h"
#include "wolfram/syntax.h"
#include <cJSON.h>
#include "_internal.h"
#include <stdlib.h>
#include <string.h>
#include "wolfram/oauth/par.h"
#include "wolfram/oauth/metadata.h"
#include "wolfram/auth_client.h"
#include "wolfram/oauth/state.h"
#include <stdbool.h>


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
        wf_lex_app_bsky_actor_put_preferences_main_input_preferences_item_union *items =
            calloc(count, sizeof(*items));
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
            items[i].kind = -1;
            items[i].data = elem_json;
            items[i].length = strlen(elem_json);
        }
    }
    
    cJSON_Delete(root);
    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_app_bsky_actor_put_preferences_main_call(agent->client, &input, out);
    
    /* Clean up allocated strings and array */
    if (input.preferences.items) {
        for (size_t i = 0; i < input.preferences.count; ++i) {
            free((void*)input.preferences.items[i].data);
        }
        free((void*)input.preferences.items);
    }
    return status;
}

/* Helper to build a temporary OAuth session state for DPoP authentication */
static wf_status wf_agent_build_oauth_state(wf_agent *agent, wf_oauth_session_state *out_state) {
    if (!agent || !out_state) return WF_ERR_INVALID_ARG;
    // Ensure we have a logged‑in session.
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;

    // Copy the access JWT into the temporary state.
    out_state->access_token = strdup(agent->session->data.access_jwt);
    if (!out_state->access_token) return WF_ERR_ALLOC;

    // The DID of the authenticated user.
    out_state->subject = strdup(agent->session->data.did);
    if (!out_state->subject) { wf_oauth_session_state_free(out_state); return WF_ERR_ALLOC; }

    // Use the service URL as the issuer (good enough for DPoP proof generation).
    out_state->issuer = strdup(agent->service_url);
    if (!out_state->issuer) { wf_oauth_session_state_free(out_state); return WF_ERR_ALLOC; }

    // Generate a fresh DPoP key for this request.
    wf_oauth_dpop_key *dpop_key = NULL;
    wf_status dpop_status = wf_oauth_dpop_key_generate(&dpop_key);
    if (dpop_status != WF_OK) {
        wf_oauth_session_state_free(out_state);
        return dpop_status;
    }
    out_state->dpop_key = dpop_key;
    // No refresh token needed for push registration.
    out_state->refresh_token = NULL;

    return WF_OK;
}

/* Push notification registration – app.bsky.notification.registerPush (default platform/app_id) */
wf_status wf_agent_register_push(wf_agent *agent, const char *service_did, const char *token, wf_response *out) {
    return wf_agent_register_push_ext(agent, service_did, token, "web", "wolfram", out);
}

/* Push notification registration – extended version */
wf_status wf_agent_register_push_ext(wf_agent *agent, const char *service_did, const char *token, const char *platform, const char *app_id, wf_response *out) {
    if (!agent || !service_did || !token || !platform || !app_id || !out) return WF_ERR_INVALID_ARG;
    // Validate platform string.
    if (strcmp(platform, "ios") != 0 && strcmp(platform, "android") != 0 && strcmp(platform, "web") != 0) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_notification_register_push_main_input input = {
        .service_did = service_did,
        .token = token,
        .platform = platform,
        .app_id = app_id
    };
    // Build OAuth state for DPoP auth.
    wf_oauth_session_state oauth_state = {0};
    wf_status status = wf_agent_build_oauth_state(agent, &oauth_state);
    if (status != WF_OK) return status;
    // Fetch server metadata needed for DPoP.
    wf_oauth_server_metadata server_md = {0};
    status = wf_oauth_server_metadata_get(agent->client, oauth_state.issuer, &server_md);
    if (status != WF_OK) {
        wf_oauth_session_state_free(&oauth_state);
        return status;
    }
    // Minimal client auth.
    wf_oauth_client_auth client_auth = { .client_id = "" };
    // Choose appropriate XRPC client (local PDS or external notification service).
    wf_xrpc_client *push_client = NULL;
    bool push_client_created = false;
    char *notif_endpoint = NULL;
    if (strcmp(service_did, wf_agent_get_did(agent)) == 0) {
        push_client = agent->client;
    } else {
        wf_status ep_status = wf_get_notif_endpoint(agent, service_did, &notif_endpoint);
        if (ep_status != WF_OK) {
            wf_oauth_server_metadata_free(&server_md);
            wf_oauth_session_state_free(&oauth_state);
            return ep_status;
        }
        push_client = wf_xrpc_client_new(notif_endpoint);
        if (!push_client) {
            free(notif_endpoint);
            wf_oauth_server_metadata_free(&server_md);
            wf_oauth_session_state_free(&oauth_state);
            return WF_ERR_ALLOC;
        }
        push_client_created = true;
    }
    wf_auth_client *auth = wf_auth_client_new(push_client, &oauth_state, &server_md, &client_auth);
    if (!auth) {
        if (push_client_created) wf_xrpc_client_free(push_client);
        free(notif_endpoint);
        wf_oauth_server_metadata_free(&server_md);
        wf_oauth_session_state_free(&oauth_state);
        return WF_ERR_ALLOC;
    }
    status = wf_lex_app_bsky_notification_register_push_main_call_auth(auth, &input, out);
    wf_auth_client_free(auth);
    if (push_client_created) wf_xrpc_client_free(push_client);
    free(notif_endpoint);
    wf_oauth_server_metadata_free(&server_md);
    wf_oauth_session_state_free(&oauth_state);
    return status;
}

/* Push notification unregistration – app.bsky.notification.unregisterPush (default platform/app_id) */
wf_status wf_agent_unregister_push(wf_agent *agent, const char *service_did, const char *token, wf_response *out) {
    return wf_agent_unregister_push_ext(agent, service_did, token, "web", "wolfram", out);
}

/* Push notification unregistration – extended version */
wf_status wf_agent_unregister_push_ext(wf_agent *agent, const char *service_did, const char *token, const char *platform, const char *app_id, wf_response *out) {
    if (!agent || !service_did || !token || !platform || !app_id || !out) return WF_ERR_INVALID_ARG;
    // Validate platform string.
    if (strcmp(platform, "ios") != 0 && strcmp(platform, "android") != 0 && strcmp(platform, "web") != 0) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_notification_unregister_push_main_input input = {
        .service_did = service_did,
        .token = token,
        .platform = platform,
        .app_id = app_id
    };
    // Build OAuth state for DPoP auth.
    wf_oauth_session_state oauth_state = {0};
    wf_status status = wf_agent_build_oauth_state(agent, &oauth_state);
    if (status != WF_OK) return status;
    // Fetch server metadata.
    wf_oauth_server_metadata server_md = {0};
    status = wf_oauth_server_metadata_get(agent->client, oauth_state.issuer, &server_md);
    if (status != WF_OK) {
        wf_oauth_session_state_free(&oauth_state);
        return status;
    }
    wf_oauth_client_auth client_auth = { .client_id = "" };
    // Choose appropriate XRPC client (local PDS or external notification service).
    wf_xrpc_client *push_client = NULL;
    bool push_client_created = false;
    char *notif_endpoint = NULL;
    if (strcmp(service_did, wf_agent_get_did(agent)) == 0) {
        push_client = agent->client;
    } else {
        wf_status ep_status = wf_get_notif_endpoint(agent, service_did, &notif_endpoint);
        if (ep_status != WF_OK) {
            wf_oauth_server_metadata_free(&server_md);
            wf_oauth_session_state_free(&oauth_state);
            return ep_status;
        }
        push_client = wf_xrpc_client_new(notif_endpoint);
        if (!push_client) {
            free(notif_endpoint);
            wf_oauth_server_metadata_free(&server_md);
            wf_oauth_session_state_free(&oauth_state);
            return WF_ERR_ALLOC;
        }
        push_client_created = true;
    }
    wf_auth_client *auth = wf_auth_client_new(push_client, &oauth_state, &server_md, &client_auth);
    if (!auth) {
        if (push_client_created) wf_xrpc_client_free(push_client);
        free(notif_endpoint);
        wf_oauth_server_metadata_free(&server_md);
        wf_oauth_session_state_free(&oauth_state);
        return WF_ERR_ALLOC;
    }
    status = wf_lex_app_bsky_notification_unregister_push_main_call_auth(auth, &input, out);
    wf_auth_client_free(auth);
    if (push_client_created) wf_xrpc_client_free(push_client);
    free(notif_endpoint);
    wf_oauth_server_metadata_free(&server_md);
    wf_oauth_session_state_free(&oauth_state);
    return status;
}

