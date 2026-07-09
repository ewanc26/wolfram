/*
 * graph_write.c — agent write wrappers for the app.bsky.graph lexicon.
 *
 * See include/wolfram/graph_write.h for the public API, the authoritative
 * wire format, ownership rules, and the conventions this follows. Record
 * writes route through the generic com.atproto.repo create/put/delete-record
 * transport (wf_agent_create_record / wf_agent_put_record / wf_agent_delete_record,
 * mirroring post.c's follow/like/block helpers); the mute/unmute procedures
 * route through the generated lex wrappers in atproto_lex.h.
 */

#include "wolfram/graph_write.h"

#include "wolfram/agent.h"
#include "wolfram/atproto_lex.h"
#include "wolfram/syntax.h"

#include "_internal.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Small local helpers (kept static per TU, mirroring graph_social_typed.c). */

static int wf_graph_make_ts(char *buf, size_t buf_len) {
    time_t now = time(NULL);
    struct tm tm_utc;
    struct tm *tmp = gmtime(&now);
    if (!tmp) {
        return 0;
    }
    tm_utc = *tmp;
    return strftime(buf, buf_len, "%Y-%m-%dT%H:%M:%SZ", &tm_utc) != 0;
}

/* Build the record object (without $type/createdAt), route it through
 * com.atproto.repo.createRecord, and parse {uri, cid} into `out`. The caller
 * owns `out` (free with wf_agent_post_result_free). */
static wf_status wf_graph_create_record(wf_agent *agent, const char *collection,
                                        const char *record_type, cJSON *record,
                                        wf_agent_post_result *out) {
    if (!wf_agent_is_logged_in(agent) || !record) {
        cJSON_Delete(record);
        return WF_ERR_INVALID_ARG;
    }

    char created_at[32];
    if (!wf_graph_make_ts(created_at, sizeof(created_at)) ||
        !wf_syntax_datetime_is_valid(created_at)) {
        cJSON_Delete(record);
        return WF_ERR_INVALID_ARG;
    }

    if (!cJSON_AddStringToObject(record, "$type", record_type) ||
        !cJSON_AddStringToObject(record, "createdAt", created_at)) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(record);
    cJSON_Delete(record);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    wf_status status = wf_agent_create_record(agent, collection, json, out);
    free(json);
    return status;
}

/* Validate an at-uri and return the parsed components. On success the caller
 * must wf_syntax_aturi_free the parse; on failure returns WF_ERR_PARSE with the
 * parse left reset. */
static wf_status wf_graph_require_aturi(const char *uri, wf_syntax_aturi *parsed) {
    if (!uri || !uri[0]) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_aturi_parse(uri, parsed)) {
        return WF_ERR_PARSE;
    }
    return WF_OK;
}

/* True when `authority` equals the logged-in session's DID. Mirrors the
 * (static) wf_agent_authority_matches_session helper in post.c. */
static int wf_graph_authority_is_session(const wf_agent *agent,
                                         const char *authority) {
    return agent && agent->session && agent->session->data.did && authority &&
           strcmp(authority, agent->session->data.did) == 0;
}

/* Delete a record identified by an at-uri, validating that the collection
 * matches `expected_collection` and the authority belongs to the session. */
static wf_status wf_graph_delete_by_uri(wf_agent *agent, const char *uri,
                                        const char *expected_collection) {
    if (!agent || !uri) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    wf_status status = wf_graph_require_aturi(uri, &parsed);
    if (status != WF_OK) {
        return status;
    }

    if (!parsed.authority || !wf_graph_authority_is_session(agent, parsed.authority) ||
        !parsed.collection || !parsed.record_key ||
        strcmp(parsed.collection, expected_collection) != 0) {
        status = WF_ERR_INVALID_ARG;
    } else {
        status = wf_agent_delete_record(agent, parsed.collection, parsed.record_key);
    }

    wf_syntax_aturi_free(&parsed);
    return status;
}

/* ------------------------------------------------------------------ */
/* Procedure writes: mute / unmute (thread + actor list)               */
/* ------------------------------------------------------------------ */

static wf_status wf_graph_call_procedure(
    wf_agent *agent,
    wf_status (*call)(wf_xrpc_client *, const void *, wf_response *),
    const void *input) {
    if (!agent || !agent->client) {
        return WF_ERR_INVALID_ARG;
    }
    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = call(agent->client, input, &res);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_graph_mute_thread(wf_agent *agent, const char *root_uri) {
    if (!agent || !root_uri || !root_uri[0]) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_syntax_aturi parsed = {0};
    wf_status status = wf_graph_require_aturi(root_uri, &parsed);
    wf_syntax_aturi_free(&parsed);
    if (status != WF_OK) {
        return status;
    }

    wf_lex_app_bsky_graph_mute_thread_main_input in = { .root = root_uri };
    return wf_graph_call_procedure(
        agent,
        (wf_status(*)(wf_xrpc_client *, const void *,
                      wf_response *))wf_lex_app_bsky_graph_mute_thread_main_call,
        &in);
}

wf_status wf_agent_graph_unmute_thread(wf_agent *agent, const char *root_uri) {
    if (!agent || !root_uri || !root_uri[0]) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_syntax_aturi parsed = {0};
    wf_status status = wf_graph_require_aturi(root_uri, &parsed);
    wf_syntax_aturi_free(&parsed);
    if (status != WF_OK) {
        return status;
    }

    wf_lex_app_bsky_graph_unmute_thread_main_input in = { .root = root_uri };
    return wf_graph_call_procedure(
        agent,
        (wf_status(*)(wf_xrpc_client *, const void *,
                      wf_response *))wf_lex_app_bsky_graph_unmute_thread_main_call,
        &in);
}

wf_status wf_agent_graph_mute_actor_list(wf_agent *agent, const char *list_uri) {
    if (!agent || !list_uri || !list_uri[0]) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_syntax_aturi parsed = {0};
    wf_status status = wf_graph_require_aturi(list_uri, &parsed);
    wf_syntax_aturi_free(&parsed);
    if (status != WF_OK) {
        return status;
    }

    wf_lex_app_bsky_graph_mute_actor_list_main_input in = { .list = list_uri };
    return wf_graph_call_procedure(
        agent,
        (wf_status(*)(wf_xrpc_client *, const void *,
                      wf_response *))wf_lex_app_bsky_graph_mute_actor_list_main_call,
        &in);
}

wf_status wf_agent_graph_unmute_actor_list(wf_agent *agent,
                                           const char *list_uri) {
    if (!agent || !list_uri || !list_uri[0]) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_syntax_aturi parsed = {0};
    wf_status status = wf_graph_require_aturi(list_uri, &parsed);
    wf_syntax_aturi_free(&parsed);
    if (status != WF_OK) {
        return status;
    }

    wf_lex_app_bsky_graph_unmute_actor_list_main_input in = { .list = list_uri };
    return wf_graph_call_procedure(
        agent,
        (wf_status(*)(wf_xrpc_client *, const void *,
                      wf_response *))wf_lex_app_bsky_graph_unmute_actor_list_main_call,
        &in);
}

/* ------------------------------------------------------------------ */
/* Record writes: block / unblock                                     */
/* ------------------------------------------------------------------ */

wf_status wf_agent_graph_block(wf_agent *agent, const char *subject_did,
                               wf_agent_post_result *out) {
    if (!agent || !subject_did || !subject_did[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_did_is_valid(subject_did)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *record = cJSON_CreateObject();
    if (!record) {
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(record, "subject", subject_did)) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    return wf_graph_create_record(agent, "app.bsky.graph.block",
                                  "app.bsky.graph.block", record, out);
}

wf_status wf_agent_graph_unblock(wf_agent *agent, const char *block_uri) {
    /* app.bsky.graph.block is a record; unblocking deletes it. */
    return wf_graph_delete_by_uri(agent, block_uri, "app.bsky.graph.block");
}

/* ------------------------------------------------------------------ */
/* Record writes: list (create / update / delete)                     */
/* ------------------------------------------------------------------ */

wf_status wf_agent_graph_create_list(wf_agent *agent, const char *purpose,
                                     const char *name, const char *description,
                                     wf_agent_post_result *out) {
    if (!agent || !purpose || !purpose[0] || !name || !name[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *record = cJSON_CreateObject();
    if (!record) {
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(record, "purpose", purpose) ||
        !cJSON_AddStringToObject(record, "name", name)) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }
    if (description && description[0]) {
        if (!cJSON_AddStringToObject(record, "description", description)) {
            cJSON_Delete(record);
            return WF_ERR_ALLOC;
        }
    }

    return wf_graph_create_record(agent, "app.bsky.graph.list",
                                  "app.bsky.graph.list", record, out);
}

wf_status wf_agent_graph_update_list(wf_agent *agent, const char *rkey,
                                     const char *record_json,
                                     wf_agent_post_result *out) {
    if (!agent || !rkey || !record_json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }
    if (wf_syntax_nsid_validate("app.bsky.graph.list") != WF_OK) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_put_record(agent, "app.bsky.graph.list", rkey, record_json,
                               out);
}

wf_status wf_agent_graph_delete_list(wf_agent *agent, const char *list_uri) {
    return wf_graph_delete_by_uri(agent, list_uri, "app.bsky.graph.list");
}

/* ------------------------------------------------------------------ */
/* Record writes: listitem (create / delete)                          */
/* ------------------------------------------------------------------ */

wf_status wf_agent_graph_create_list_item(wf_agent *agent, const char *list_uri,
                                          const char *subject_did,
                                          wf_agent_post_result *out) {
    if (!agent || !list_uri || !list_uri[0] || !subject_did ||
        !subject_did[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_did_is_valid(subject_did)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_syntax_aturi parsed = {0};
    wf_status status = wf_graph_require_aturi(list_uri, &parsed);
    wf_syntax_aturi_free(&parsed);
    if (status != WF_OK) {
        return status;
    }

    cJSON *record = cJSON_CreateObject();
    if (!record) {
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(record, "subject", subject_did) ||
        !cJSON_AddStringToObject(record, "list", list_uri)) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    return wf_graph_create_record(agent, "app.bsky.graph.listitem",
                                  "app.bsky.graph.listitem", record, out);
}

wf_status wf_agent_graph_delete_list_item(wf_agent *agent,
                                          const char *list_item_uri) {
    return wf_graph_delete_by_uri(agent, list_item_uri,
                                  "app.bsky.graph.listitem");
}

/* ------------------------------------------------------------------ */
/* Record writes: starterpack (create / update / delete)              */
/* ------------------------------------------------------------------ */

wf_status wf_agent_graph_create_starter_pack(wf_agent *agent, const char *name,
                                              const char *list_uri,
                                              const char *description,
                                              const char *feeds_json,
                                              wf_agent_post_result *out) {
    if (!agent || !name || !name[0] || !list_uri || !list_uri[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_syntax_aturi parsed = {0};
    wf_status status = wf_graph_require_aturi(list_uri, &parsed);
    wf_syntax_aturi_free(&parsed);
    if (status != WF_OK) {
        return status;
    }

    cJSON *record = cJSON_CreateObject();
    if (!record) {
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(record, "name", name) ||
        !cJSON_AddStringToObject(record, "list", list_uri)) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }
    if (description && description[0]) {
        if (!cJSON_AddStringToObject(record, "description", description)) {
            cJSON_Delete(record);
            return WF_ERR_ALLOC;
        }
    }
    if (feeds_json && feeds_json[0]) {
        cJSON *feeds = cJSON_Parse(feeds_json);
        if (!feeds || !cJSON_IsArray(feeds)) {
            cJSON_Delete(feeds);
            cJSON_Delete(record);
            return WF_ERR_PARSE;
        }
        if (!cJSON_AddItemToObject(record, "feeds", feeds)) {
            cJSON_Delete(feeds);
            cJSON_Delete(record);
            return WF_ERR_ALLOC;
        }
    }

    return wf_graph_create_record(agent, "app.bsky.graph.starterpack",
                                  "app.bsky.graph.starterpack", record, out);
}

wf_status wf_agent_graph_update_starter_pack(wf_agent *agent, const char *rkey,
                                             const char *record_json,
                                             wf_agent_post_result *out) {
    if (!agent || !rkey || !record_json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_agent_put_record(agent, "app.bsky.graph.starterpack", rkey,
                               record_json, out);
}

wf_status wf_agent_graph_delete_starter_pack(wf_agent *agent,
                                             const char *starter_pack_uri) {
    return wf_graph_delete_by_uri(agent, starter_pack_uri,
                                  "app.bsky.graph.starterpack");
}

/* ------------------------------------------------------------------ */
/* Record writes: listblock (create / delete)                         */
/* ------------------------------------------------------------------ */

wf_status wf_agent_graph_create_list_block(wf_agent *agent,
                                           const char *list_at_uri,
                                           wf_agent_post_result *out) {
    if (!agent || !list_at_uri || !list_at_uri[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_syntax_aturi parsed = {0};
    wf_status status = wf_graph_require_aturi(list_at_uri, &parsed);
    wf_syntax_aturi_free(&parsed);
    if (status != WF_OK) {
        return status;
    }

    cJSON *record = cJSON_CreateObject();
    if (!record) {
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(record, "subject", list_at_uri)) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    return wf_graph_create_record(agent, "app.bsky.graph.listblock",
                                  "app.bsky.graph.listblock", record, out);
}

wf_status wf_agent_graph_delete_list_block(wf_agent *agent,
                                           const char *list_block_uri) {
    return wf_graph_delete_by_uri(agent, list_block_uri,
                                  "app.bsky.graph.listblock");
}
