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

/* Graph endpoint implementations */

wf_status wf_agent_get_profiles(wf_agent *agent, const char *const *actors,
                                size_t actors_count, int limit,
                                const char *cursor, wf_response *out) {
    if (!agent || !actors || actors_count == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param *params = (wf_xrpc_param *)calloc(actors_count + 2,
                                                    sizeof(*params));
    if (!params) {
        return WF_ERR_ALLOC;
    }

    size_t param_count = 0;
    for (size_t i = 0; i < actors_count; ++i) {
        if (!actors[i] || !wf_syntax_at_identifier_is_valid(actors[i])) {
            free(params);
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "actors";
        params[param_count].value = actors[i];
        param_count++;
    }

    char limit_buf[16];
    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf))) {
            free(params);
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
    wf_status status = wf_xrpc_query_params(agent->client,
                                            "app.bsky.actor.getProfiles",
                                            params, param_count, out);
    free(params);
    return status;
}

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
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
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
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
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
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
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
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
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
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
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
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
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
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
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

wf_status wf_agent_get_actor_starter_packs(wf_agent *agent, const char *actor,
                                            int limit, const char *cursor,
                                            wf_response *out) {
    if (!agent || !actor || !out) return WF_ERR_INVALID_ARG;
    if (!wf_syntax_at_identifier_is_valid(actor)) return WF_ERR_INVALID_ARG;

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "actor";
    params[param_count].value = actor;
    param_count++;

    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf)))
            return WF_ERR_INVALID_ARG;
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
    return wf_xrpc_query_params(agent->client,
                                "app.bsky.graph.getActorStarterPacks",
                                params, param_count, out);
}

wf_status wf_agent_get_starter_pack(wf_agent *agent, const char *starter_pack_uri,
                                     wf_response *out) {
    if (!agent || !starter_pack_uri || !out) return WF_ERR_INVALID_ARG;

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(starter_pack_uri, &parsed)) return WF_ERR_PARSE;
    wf_syntax_aturi_free(&parsed);

    wf_xrpc_param params[1];
    params[0].name = "starterPack";
    params[0].value = starter_pack_uri;

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client,
                                "app.bsky.graph.getStarterPack",
                                params, 1, out);
}

wf_status wf_agent_get_starter_packs(wf_agent *agent,
                                      const char *const *uris, size_t uri_count,
                                      wf_response *out) {
    if (!agent || !uris || uri_count == 0 || !out) return WF_ERR_INVALID_ARG;

    for (size_t i = 0; i < uri_count; ++i) {
        if (!uris[i]) return WF_ERR_INVALID_ARG;
        wf_syntax_aturi parsed = {0};
        if (!wf_syntax_aturi_parse(uris[i], &parsed)) return WF_ERR_PARSE;
        wf_syntax_aturi_free(&parsed);
    }

    wf_xrpc_param *params = calloc(uri_count, sizeof(*params));
    if (!params) return WF_ERR_ALLOC;

    for (size_t i = 0; i < uri_count; ++i) {
        params[i].name = "uris";
        params[i].value = uris[i];
    }

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_query_params(agent->client,
                                             "app.bsky.graph.getStarterPacks",
                                             params, uri_count, out);
    free(params);
    return status;
}

wf_status wf_agent_search_starter_packs(wf_agent *agent, const char *query,
                                         int limit, const char *cursor,
                                         wf_response *out) {
    if (!agent || !query || !out) return WF_ERR_INVALID_ARG;
    if (!query[0]) return WF_ERR_INVALID_ARG;

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "q";
    params[param_count].value = query;
    param_count++;

    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf)))
            return WF_ERR_INVALID_ARG;
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
    return wf_xrpc_query_params(agent->client,
                                "app.bsky.graph.searchStarterPacks",
                                params, param_count, out);
}

wf_status wf_agent_get_starter_packs_with_membership(wf_agent *agent,
                                                      const char *actor,
                                                      int limit,
                                                      const char *cursor,
                                                      wf_response *out) {
    if (!agent || !actor || !out) return WF_ERR_INVALID_ARG;
    if (!wf_syntax_at_identifier_is_valid(actor)) return WF_ERR_INVALID_ARG;

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "actor";
    params[param_count].value = actor;
    param_count++;

    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf)))
            return WF_ERR_INVALID_ARG;
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
    return wf_xrpc_query_params(agent->client,
                                "app.bsky.graph.getStarterPacksWithMembership",
                                params, param_count, out);
}

wf_status wf_agent_mute_thread(wf_agent *agent, const char *root_uri) {
    if (!agent || !root_uri || !root_uri[0]) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(root_uri, &parsed)) return WF_ERR_PARSE;
    wf_syntax_aturi_free(&parsed);

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "root", root_uri)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(agent->client,
                                          "app.bsky.graph.muteThread",
                                          json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_unmute_thread(wf_agent *agent, const char *root_uri) {
    if (!agent || !root_uri || !root_uri[0]) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(root_uri, &parsed)) return WF_ERR_PARSE;
    wf_syntax_aturi_free(&parsed);

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "root", root_uri)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(agent->client,
                                          "app.bsky.graph.unmuteThread",
                                          json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_list_blocks(wf_agent *agent, int limit,
                                    const char *cursor, wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;

    wf_xrpc_param params[2];
    size_t param_count = 0;
    char limit_buf[16];

    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf)))
            return WF_ERR_INVALID_ARG;
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
    return wf_xrpc_query_params(agent->client,
                                WF_LEX_APP_BSKY_GRAPH_GET_LIST_BLOCKS_NSID,
                                params, param_count, out);
}

wf_status wf_agent_get_list_mutes(wf_agent *agent, int limit,
                                   const char *cursor, wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;

    wf_xrpc_param params[2];
    size_t param_count = 0;
    char limit_buf[16];

    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf)))
            return WF_ERR_INVALID_ARG;
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
    return wf_xrpc_query_params(agent->client,
                                WF_LEX_APP_BSKY_GRAPH_GET_LIST_MUTES_NSID,
                                params, param_count, out);
}

/* ── muteActor / unmuteActor (explicit, single-actor) ────────────────── */

wf_status wf_agent_mute_actor(wf_agent *agent, const char *actor) {
    if (!agent || !actor || !actor[0]) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;
    if (!wf_syntax_at_identifier_is_valid(actor)) return WF_ERR_INVALID_ARG;

    wf_lex_app_bsky_graph_mute_actor_main_input in = { .actor = actor };
    char *json = NULL;
    wf_status status = wf_lex_app_bsky_graph_mute_actor_main_input_encode_json(&in, &json);
    if (status != WF_OK) return status;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    status = wf_xrpc_procedure(agent->client,
                               WF_LEX_APP_BSKY_GRAPH_MUTE_ACTOR_NSID,
                               json, &res);
    wf_lex_app_bsky_graph_mute_actor_main_json_free(json);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_unmute_actor(wf_agent *agent, const char *actor) {
    if (!agent || !actor || !actor[0]) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;
    if (!wf_syntax_at_identifier_is_valid(actor)) return WF_ERR_INVALID_ARG;

    wf_lex_app_bsky_graph_unmute_actor_main_input in = { .actor = actor };
    char *json = NULL;
    wf_status status = wf_lex_app_bsky_graph_unmute_actor_main_input_encode_json(&in, &json);
    if (status != WF_OK) return status;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    status = wf_xrpc_procedure(agent->client,
                               WF_LEX_APP_BSKY_GRAPH_UNMUTE_ACTOR_NSID,
                               json, &res);
    wf_lex_app_bsky_graph_unmute_actor_main_json_free(json);
    wf_response_free(&res);
    return status;
}

/* ── unmuteActorList (muteActorList lives in moderation_actions.c) ───── */

wf_status wf_agent_unmute_actor_list(wf_agent *agent, const char *list_uri) {
    if (!agent || !list_uri || !list_uri[0]) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(list_uri, &parsed)) return WF_ERR_PARSE;
    wf_syntax_aturi_free(&parsed);

    wf_lex_app_bsky_graph_unmute_actor_list_main_input in = { .list = list_uri };
    char *json = NULL;
    wf_status status = wf_lex_app_bsky_graph_unmute_actor_list_main_input_encode_json(&in, &json);
    if (status != WF_OK) return status;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    status = wf_xrpc_procedure(agent->client,
                               WF_LEX_APP_BSKY_GRAPH_UNMUTE_ACTOR_LIST_NSID,
                               json, &res);
    wf_lex_app_bsky_graph_unmute_actor_list_main_json_free(json);
    wf_response_free(&res);
    return status;
}

/* ── getSuggestedFollowsByActor alias ───────────────────────────────── */

wf_status wf_agent_get_suggested_follows(wf_agent *agent,
                                         const char *actor,
                                         wf_response *out) {
    return wf_agent_get_suggested_follows_by_actor(agent, actor, out);
}
