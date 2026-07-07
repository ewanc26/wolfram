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

typedef struct wf_agent {
    wf_xrpc_client *client;
    wf_session *session;
    char *service_url;
    char *mirror_did;
    char *mirror_signing_key;
    wf_car mirror;
} wf_agent;

static int wf_agent_int_to_str(int value, char *buf, size_t buf_len) {
    return snprintf(buf, buf_len, "%d", value) > 0;
}

static int wf_agent_is_logged_in(const wf_agent *agent) {
    return agent && agent->session && wf_session_has_session(agent->session) &&
           agent->session->data.did && agent->session->data.access_jwt;
}

static void wf_agent_sync_auth(wf_agent *agent) {
    if (!agent || !agent->client || !agent->session) {
        return;
    }
    wf_xrpc_client_set_auth(agent->client,
                            wf_agent_is_logged_in(agent) ? agent->session->data.access_jwt : NULL);
}

/* Graph endpoint implementations */

wf_status wf_agent_get_follows(wf_agent *agent, const char *actor,
                               int limit, const char *cursor, wf_response *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];
    params[param_count].name = "actor";
    params[param_count].value = actor;
    param_count++;
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
    return wf_xrpc_query_params(agent->client, "app.bsky.graph.getFollows",
                                 params, param_count, out);
}

wf_status wf_agent_get_followers(wf_agent *agent, const char *actor,
                                 int limit, const char *cursor, wf_response *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];
    params[param_count].name = "actor";
    params[param_count].value = actor;
    param_count++;
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
    return wf_xrpc_query_params(agent->client, "app.bsky.graph.getFollowers",
                                 params, param_count, out);
}

wf_status wf_agent_get_blocks(wf_agent *agent, int limit, const char *cursor,
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
    return wf_xrpc_query_params(agent->client, "app.bsky.graph.getBlocks",
                                 params, param_count, out);
}

wf_status wf_agent_get_mutes(wf_agent *agent, int limit, const char *cursor,
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
    return wf_xrpc_query_params(agent->client, "app.bsky.graph.getMutes",
                                 params, param_count, out);
}

wf_status wf_agent_get_known_followers(wf_agent *agent, const char *actor,
                                       int limit, const char *cursor,
                                       wf_response *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];
    params[param_count].name = "actor";
    params[param_count].value = actor;
    param_count++;
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
    return wf_xrpc_query_params(agent->client, "app.bsky.graph.getKnownFollowers",
                                 params, param_count, out);
}

wf_status wf_agent_get_relationships(wf_agent *agent, const char *actor,
                                   const char *const *others, size_t others_count,
                                   wf_response *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_did_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_xrpc_param *params = calloc(1 + others_count, sizeof(*params));
    if (!params) return WF_ERR_ALLOC;
    size_t param_count = 0;
    params[param_count].name = "actor";
    params[param_count].value = actor;
    param_count++;
    for (size_t i = 0; i < others_count; i++) {
        if (!others[i] || !others[i][0] || !wf_syntax_did_is_valid(others[i])) {
            free(params);
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "others";
        params[param_count].value = others[i];
        param_count++;
    }
    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_query_params(agent->client,
                                            "app.bsky.graph.getRelationships",
                                            params, param_count, out);
    free(params);
    return status;
}

wf_status wf_agent_get_list(wf_agent *agent, const char *list_uri,
                            int limit, const char *cursor,
                            wf_response *out) {
    if (!agent || !list_uri || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(list_uri, &parsed)) {
        return WF_ERR_PARSE;
    }
    wf_syntax_aturi_free(&parsed);
    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];
    params[param_count].name = "list";
    params[param_count].value = list_uri;
    param_count++;
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
    return wf_xrpc_query_params(agent->client, "app.bsky.graph.getList",
                                 params, param_count, out);
}

wf_status wf_agent_get_lists(wf_agent *agent, const char *actor,
                             int limit, const char *cursor,
                             wf_response *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];
    params[param_count].name = "actor";
    params[param_count].value = actor;
    param_count++;
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
    return wf_xrpc_query_params(agent->client, "app.bsky.graph.getLists",
                                 params, param_count, out);
}

wf_status wf_agent_get_suggested_follows_by_actor(wf_agent *agent,
                                                   const char *actor,
                                                   wf_response *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_xrpc_param params[1];
    size_t param_count = 0;
    params[param_count].name = "actor";
    params[param_count].value = actor;
    param_count++;
    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.graph.getSuggestedFollowsByActor",
                                 params, param_count, out);
}
