/*
 * chat_typed.c — typed parsers + agent wrappers for chat.bsky.convo.
 *
 * Mirrors feed_typed.c / notification.c: static strdup/set_string/reset
 * helpers, ownership via plain string copies, and full cleanup on the first
 * error. Wrappers call wf_xrpc_query_params through agent->client, then parse,
 * then wf_response_free.
 */

#include "wolfram/chat_typed.h"

#include "wolfram/agent.h"
#include "wolfram/xrpc.h"
#include "wolfram/identity.h"
#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

#include "_internal.h"

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_chat_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

static wf_status wf_chat_set_string(char **dst, const char *src) {
    char *copy = wf_chat_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static void wf_chat_profile_reset(wf_agent_profile_view *p) {
    if (!p) {
        return;
    }
    free(p->did);
    free(p->handle);
    free(p->display_name);
    free(p->avatar);
    memset(p, 0, sizeof(*p));
}

void wf_chat_message_reset(wf_chat_message *m) {
    if (!m) {
        return;
    }
    free(m->id);
    free(m->rev);
    free(m->text);
    free(m->sender);
    free(m->sent_at);
    memset(m, 0, sizeof(*m));
}

static void wf_chat_convo_reset(wf_chat_convo *c) {
    if (!c) {
        return;
    }
    free(c->id);
    free(c->rev);
    free(c->type);
    for (size_t i = 0; i < c->member_count; ++i) {
        wf_chat_profile_reset(&c->members[i]);
    }
    free(c->members);
    free(c->last_message_text);
    free(c->cursor);
    memset(c, 0, sizeof(*c));
}

/* Parse an integer from a cJSON number, tracking whether it was present. */
static wf_status wf_chat_parse_int(cJSON *num, int *dst, int *has) {
    if (cJSON_IsNumber(num)) {
        double v = num->valuedouble;
        if (v > 2147483647.0) {
            *dst = 2147483647;
        } else if (v < -2147483648.0) {
            *dst = -2147483648;
        } else {
            *dst = (int)v;
        }
        *has = 1;
    }
    return WF_OK;
}

/* Parse a profileViewBasic-like object (did/handle/displayName/avatar). */
static wf_status wf_chat_parse_profile(wf_agent_profile_view *p, cJSON *obj) {
    wf_status status = WF_OK;
    if (!cJSON_IsObject(obj)) {
        return WF_ERR_PARSE;
    }
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(obj, "handle");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");
    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_chat_set_string(&p->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
        status = wf_chat_set_string(&p->handle, handle->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_chat_set_string(&p->display_name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        status = wf_chat_set_string(&p->avatar, avatar->valuestring);
    }
    return status;
}

/* Parse the `members` array of a convoView into `out->members`. */
static wf_status wf_chat_parse_members(wf_chat_convo *out, cJSON *members) {
    if (!cJSON_IsArray(members)) {
        return WF_ERR_PARSE;
    }
    size_t count = (size_t)cJSON_GetArraySize(members);
    wf_agent_profile_view *arr = NULL;
    if (count > 0) {
        arr = (wf_agent_profile_view *)calloc(count, sizeof(*arr));
        if (!arr) {
            return WF_ERR_ALLOC;
        }
    }
    wf_status status = WF_OK;
    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *m = cJSON_GetArrayItem(members, (int)i);
        status = wf_chat_parse_profile(&arr[i], m);
        if (status != WF_OK) {
            wf_chat_profile_reset(&arr[i]);
        }
    }
    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_chat_profile_reset(&arr[i]);
        }
        free(arr);
        return status;
    }
    out->members = arr;
    out->member_count = count;
    return WF_OK;
}

/* Parse a single convoView object (already extracted from the document). */
static wf_status wf_chat_parse_convo_view(wf_chat_convo *out, cJSON *obj) {
    wf_status status = WF_OK;
    if (!cJSON_IsObject(obj)) {
        return WF_ERR_PARSE;
    }
    cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "id");
    cJSON *rev = cJSON_GetObjectItemCaseSensitive(obj, "rev");
    cJSON *members = cJSON_GetObjectItemCaseSensitive(obj, "members");
    cJSON *last_message = cJSON_GetObjectItemCaseSensitive(obj, "lastMessage");
    cJSON *unread = cJSON_GetObjectItemCaseSensitive(obj, "unreadCount");
    cJSON *kind = cJSON_GetObjectItemCaseSensitive(obj, "kind");
    cJSON *cursor = cJSON_GetObjectItemCaseSensitive(obj, "cursor");

    if (cJSON_IsString(id) && id->valuestring) {
        status = wf_chat_set_string(&out->id, id->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(rev) && rev->valuestring) {
        status = wf_chat_set_string(&out->rev, rev->valuestring);
    }
    if (status == WF_OK) {
        status = wf_chat_parse_members(out, members);
    }
    if (status == WF_OK) {
        status = wf_chat_parse_int(unread, &out->unread_count,
                                   &out->has_unread_count);
    }
    if (status == WF_OK && cJSON_IsObject(last_message)) {
        cJSON *text = cJSON_GetObjectItemCaseSensitive(last_message, "text");
        if (cJSON_IsString(text) && text->valuestring) {
            status = wf_chat_set_string(&out->last_message_text,
                                        text->valuestring);
        }
    }
    if (status == WF_OK && cJSON_IsString(kind) && kind->valuestring) {
        status = wf_chat_set_string(&out->type, kind->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(cursor) && cursor->valuestring) {
        status = wf_chat_set_string(&out->cursor, cursor->valuestring);
    }
    return status;
}

/* Parse a single messageView object (messageView / deletedMessageView / etc.).
 * Only the bounded fields we expose (id/rev/text/sender/sentAt) are read. */
static wf_status wf_chat_parse_message(wf_chat_message *out, cJSON *obj) {
    wf_status status = WF_OK;
    if (!cJSON_IsObject(obj)) {
        return WF_ERR_PARSE;
    }
    cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "id");
    cJSON *rev = cJSON_GetObjectItemCaseSensitive(obj, "rev");
    cJSON *text = cJSON_GetObjectItemCaseSensitive(obj, "text");
    cJSON *sender = cJSON_GetObjectItemCaseSensitive(obj, "sender");
    cJSON *sent_at = cJSON_GetObjectItemCaseSensitive(obj, "sentAt");

    if (cJSON_IsString(id) && id->valuestring) {
        status = wf_chat_set_string(&out->id, id->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(rev) && rev->valuestring) {
        status = wf_chat_set_string(&out->rev, rev->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(text) && text->valuestring) {
        status = wf_chat_set_string(&out->text, text->valuestring);
    }
    /* sender is #messageViewSender { did } */
    if (status == WF_OK && cJSON_IsObject(sender)) {
        cJSON *did = cJSON_GetObjectItemCaseSensitive(sender, "did");
        if (cJSON_IsString(did) && did->valuestring) {
            status = wf_chat_set_string(&out->sender, did->valuestring);
        }
    } else if (status == WF_OK && cJSON_IsString(sender) && sender->valuestring) {
        status = wf_chat_set_string(&out->sender, sender->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(sent_at) && sent_at->valuestring) {
        status = wf_chat_set_string(&out->sent_at, sent_at->valuestring);
    }
    return status;
}

wf_status wf_agent_parse_convos(const char *json, size_t json_len,
                                wf_chat_convo_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "convos");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_chat_convo *items = NULL;
    if (count > 0) {
        items = (wf_chat_convo *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        wf_chat_convo *c = &items[i];
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        status = wf_chat_parse_convo_view(c, obj);
        if (status != WF_OK) {
            wf_chat_convo_reset(c);
        }
    }

    if (status == WF_OK) {
        out->convos = items;
        out->convo_count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_chat_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_chat_convo_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_chat_convo_list_free(wf_chat_convo_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->convo_count; ++i) {
        wf_chat_convo_reset(&list->convos[i]);
    }
    free(list->convos);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

void wf_chat_convo_free(wf_chat_convo *c) {
    if (!c) {
        return;
    }
    wf_chat_convo_reset(c);
}

wf_status wf_agent_parse_convo(const char *json, size_t json_len,
                               wf_chat_convo *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *convo = cJSON_GetObjectItemCaseSensitive(root, "convo");
    if (!cJSON_IsObject(convo)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    status = wf_chat_parse_convo_view(out, convo);

    if (status != WF_OK) {
        wf_chat_convo_reset(out);
    }

    cJSON_Delete(root);
    return status;
}

wf_status wf_agent_parse_messages(const char *json, size_t json_len,
                                  wf_chat_message_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "messages");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_chat_message *items = NULL;
    if (count > 0) {
        items = (wf_chat_message *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        wf_chat_message *m = &items[i];
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        status = wf_chat_parse_message(m, obj);
        if (status != WF_OK) {
            wf_chat_message_reset(m);
        }
    }

    if (status == WF_OK) {
        out->messages = items;
        out->message_count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_chat_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_chat_message_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_chat_message_list_free(wf_chat_message_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->message_count; ++i) {
        wf_chat_message_reset(&list->messages[i]);
    }
    free(list->messages);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

/* Typed high-level wrappers. */

/* Default chat service endpoint (mirrors rsky rsky-common/src/lib.rs:204). */
static const char *wf_chat_default_endpoint(void) {
    return WF_CHAT_DEFAULT_ENDPOINT;
}

/* Resolve a chat-service DID to its service endpoint URL by fetching and
 * parsing its DID document. Honours both the rsky `BskyChatService` service
 * type and the atproto `AtprotoChatProxy` service type. Returns WF_OK and a
 * heap-allocated endpoint in *out_url on success (caller frees), or
 * WF_ERR_NOT_FOUND / WF_ERR_ALLOC on failure (and *out_url left NULL). This
 * reuses the shared identity resolver `wf_did_resolve_service` rather than
 * re-implementing DID-document fetching. */
static wf_status wf_chat_resolve_did_endpoint(wf_xrpc_client *client,
                                              const char *did,
                                              char **out_url) {
    *out_url = NULL;
    if (!client || !did || !out_url) {
        return WF_ERR_INVALID_ARG;
    }

    char *ep = NULL;
    wf_status status = wf_did_resolve_service(client, did, "BskyChatService", &ep);
    if (status == WF_ERR_NOT_FOUND) {
        status = wf_did_resolve_service(client, did, "AtprotoChatProxy", &ep);
    }
    if (status == WF_OK) {
        *out_url = ep;
    } else {
        free(ep);
    }
    return status;
}

wf_status wf_agent_chat_service_did_from_describe(const char *json,
                                                  size_t json_len,
                                                  char **out_did) {
    if (!json || !out_did) {
        return WF_ERR_INVALID_ARG;
    }
    *out_did = NULL;

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *chat = cJSON_GetObjectItemCaseSensitive(root, "chat");
    if (cJSON_IsString(chat) && chat->valuestring && chat->valuestring[0]) {
        *out_did = wf_chat_strdup(chat->valuestring);
        if (!*out_did) {
            status = WF_ERR_ALLOC;
        }
    }

    cJSON_Delete(root);
    return status;
}

wf_status wf_agent_chat_service_resolve(wf_agent *agent) {
    if (!agent || !agent->client) {
        return WF_ERR_INVALID_ARG;
    }
    if (agent->chat_client) {
        return WF_OK;
    }

    const char *endpoint = wf_chat_default_endpoint();

    wf_response res = {0};
    wf_status status = wf_xrpc_query_params(agent->client,
                                            "com.atproto.server.describeServer",
                                            NULL, 0, &res);
    if (status == WF_OK) {
        char *chat_did = NULL;
        if (wf_agent_chat_service_did_from_describe(res.body, res.body_len,
                                                    &chat_did) == WF_OK &&
            chat_did) {
            char *resolved = NULL;
            wf_status rstatus =
                wf_chat_resolve_did_endpoint(agent->client, chat_did, &resolved);
            if (rstatus == WF_OK && resolved) {
                endpoint = resolved;
            }
            free(resolved);
        }
        free(chat_did);
        wf_response_free(&res);
    }

    agent->chat_client = wf_xrpc_client_new(endpoint);
    if (!agent->chat_client) {
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}

/* Lazily resolve the chat service and return the chat client (or NULL). */
static wf_xrpc_client *wf_agent_chat_client(wf_agent *agent) {
    if (!agent) {
        return NULL;
    }
    if (wf_agent_chat_service_resolve(agent) != WF_OK) {
        return NULL;
    }
    return agent->chat_client;
}

wf_status wf_agent_parse_message(const char *json, size_t json_len,
                                 wf_chat_message *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = wf_chat_parse_message(out, root);
    cJSON_Delete(root);
    if (status != WF_OK) {
        wf_chat_message_reset(out);
    }
    return status;
}

wf_status wf_agent_chat_list_convos(wf_agent *agent, int limit,
                                    const char *cursor,
                                    wf_chat_convo_list *out) {
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

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_agent_sync_chat_auth(agent);
    wf_status status = wf_xrpc_query_params(cc,
                                            "chat.bsky.convo.listConvos",
                                            params, param_count, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_convos(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_chat_get_convo(wf_agent *agent, const char *convo_id,
                                  wf_chat_convo *out) {
    if (!agent || !convo_id || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[1];
    params[0].name = "convoId";
    params[0].value = convo_id;
    size_t param_count = 1;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_agent_sync_chat_auth(agent);
    wf_status status = wf_xrpc_query_params(cc,
                                            "chat.bsky.convo.getConvo",
                                            params, param_count, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_convo(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_chat_get_messages(wf_agent *agent, const char *convo_id,
                                     int limit, const char *cursor,
                                     wf_chat_message_list *out) {
    if (!agent || !convo_id || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[3];
    size_t param_count = 0;

    params[param_count].name = "convoId";
    params[param_count].value = convo_id;
    param_count++;

    if (limit > 0) {
        char limit_buf[16];
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

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_agent_sync_chat_auth(agent);
    wf_status status = wf_xrpc_query_params(cc,
                                            "chat.bsky.convo.getMessages",
                                            params, param_count, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_messages(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_chat_send_message(wf_agent *agent, const char *convo_id,
                                     const char *text, const char *facets_json,
                                     wf_chat_message *out) {
    if (!agent || !convo_id || !text || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(root, "convoId", convo_id)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    cJSON *message = cJSON_CreateObject();
    if (!message ||
        !cJSON_AddItemToObject(root, "message", message) ||
        !cJSON_AddStringToObject(message, "text", text) ||
        !cJSON_AddStringToObject(message, "$type",
                                 "chat.bsky.convo.defs#messageInput")) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    if (facets_json && facets_json[0]) {
        cJSON *facets = cJSON_Parse(facets_json);
        if (!facets) {
            cJSON_Delete(root);
            return WF_ERR_PARSE;
        }
        if (!cJSON_AddItemToObject(message, "facets", facets)) {
            cJSON_Delete(facets);
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    wf_response res = {0};
    wf_agent_sync_chat_auth(agent);
    wf_status status = wf_xrpc_procedure(cc, "chat.bsky.convo.sendMessage",
                                         json, &res);
    free(json);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_message(res.body, res.body_len, out);
    wf_response_free(&res);
    if (status != WF_OK) {
        wf_chat_message_reset(out);
    }
    return status;
}

/* ── Private helper: simple one-argument chat procedure ────────────────── */
static wf_status wf_chat_procedure_1arg(wf_agent *agent,
                                         const char *nsid,
                                         const char *key,
                                         const char *value) {
    if (!agent || !nsid || !key || !value) return WF_ERR_INVALID_ARG;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, key, value)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_chat_auth(agent);
    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(cc, nsid, json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_chat_accept_convo(wf_agent *agent, const char *convo_id) {
    return wf_chat_procedure_1arg(agent, "chat.bsky.convo.acceptConvo",
                                  "convoId", convo_id);
}

wf_status wf_agent_chat_leave_convo(wf_agent *agent, const char *convo_id) {
    return wf_chat_procedure_1arg(agent, "chat.bsky.convo.leaveConvo",
                                  "convoId", convo_id);
}

wf_status wf_agent_chat_mute_convo(wf_agent *agent, const char *convo_id) {
    return wf_chat_procedure_1arg(agent, "chat.bsky.convo.muteConvo",
                                  "convoId", convo_id);
}

wf_status wf_agent_chat_unmute_convo(wf_agent *agent, const char *convo_id) {
    return wf_chat_procedure_1arg(agent, "chat.bsky.convo.unmuteConvo",
                                  "convoId", convo_id);
}

wf_status wf_agent_chat_delete_message_for_self(wf_agent *agent,
                                                  const char *convo_id,
                                                  const char *message_id) {
    if (!agent || !convo_id || !message_id) return WF_ERR_INVALID_ARG;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "convoId", convo_id) ||
        !cJSON_AddStringToObject(root, "messageId", message_id)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_chat_auth(agent);
    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(cc,
                                          "chat.bsky.convo.deleteMessageForSelf",
                                          json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_chat_add_reaction(wf_agent *agent,
                                      const char *convo_id,
                                      const char *message_id,
                                      const char *value) {
    if (!agent || !convo_id || !message_id || !value) return WF_ERR_INVALID_ARG;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "convoId", convo_id) ||
        !cJSON_AddStringToObject(root, "messageId", message_id) ||
        !cJSON_AddStringToObject(root, "value", value)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_chat_auth(agent);
    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(cc,
                                          "chat.bsky.convo.addReaction",
                                          json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_chat_remove_reaction(wf_agent *agent,
                                         const char *convo_id,
                                         const char *message_id,
                                         const char *value) {
    if (!agent || !convo_id || !message_id || !value) return WF_ERR_INVALID_ARG;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "convoId", convo_id) ||
        !cJSON_AddStringToObject(root, "messageId", message_id) ||
        !cJSON_AddStringToObject(root, "value", value)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_chat_auth(agent);
    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(cc,
                                          "chat.bsky.convo.removeReaction",
                                          json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_chat_update_read(wf_agent *agent,
                                     const char *convo_id,
                                     const char *message_id) {
    if (!agent || !convo_id || !message_id) return WF_ERR_INVALID_ARG;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "convoId", convo_id) ||
        !cJSON_AddStringToObject(root, "messageId", message_id)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_chat_auth(agent);
    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(cc,
                                          "chat.bsky.convo.updateRead",
                                          json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_chat_update_all_read(wf_agent *agent,
                                         const char *convo_id) {
    if (!agent || !convo_id) return WF_ERR_INVALID_ARG;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "convoId", convo_id)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }
    if (!cJSON_AddStringToObject(root, "status", "read")) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_chat_auth(agent);
    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(cc,
                                          "chat.bsky.convo.updateAllRead",
                                          json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_chat_get_convo_availability(wf_agent *agent,
                                                const char *const *member_dids,
                                                size_t member_count,
                                                wf_response *out) {
    if (!agent || !member_dids || member_count == 0 || !out)
        return WF_ERR_INVALID_ARG;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;

    wf_xrpc_param *params = calloc(member_count, sizeof(*params));
    if (!params) return WF_ERR_ALLOC;

    for (size_t i = 0; i < member_count; ++i) {
        if (!member_dids[i]) { free(params); return WF_ERR_INVALID_ARG; }
        params[i].name = "members";
        params[i].value = member_dids[i];
    }

    wf_agent_sync_chat_auth(agent);
    wf_status status = wf_xrpc_query_params(cc,
                                             "chat.bsky.convo.getConvoAvailability",
                                             params, member_count, out);
    free(params);
    return status;
}

wf_status wf_agent_chat_get_convo_for_members(wf_agent *agent,
                                               const char *const *member_dids,
                                               size_t member_count,
                                               wf_response *out) {
    if (!agent || !member_dids || member_count == 0 || !out)
        return WF_ERR_INVALID_ARG;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;

    wf_xrpc_param *params = calloc(member_count, sizeof(*params));
    if (!params) return WF_ERR_ALLOC;

    for (size_t i = 0; i < member_count; ++i) {
        if (!member_dids[i]) { free(params); return WF_ERR_INVALID_ARG; }
        params[i].name = "members";
        params[i].value = member_dids[i];
    }

    wf_agent_sync_chat_auth(agent);
    wf_status status = wf_xrpc_query_params(cc,
                                             "chat.bsky.convo.getConvoForMembers",
                                             params, member_count, out);
    free(params);
    return status;
}

wf_status wf_agent_chat_get_convo_members(wf_agent *agent,
                                           const char *convo_id,
                                           wf_response *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;

    wf_xrpc_param params[1];
    params[0].name = "convoId";
    params[0].value = convo_id;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;

    wf_agent_sync_chat_auth(agent);
    return wf_xrpc_query_params(cc, "chat.bsky.convo.getConvoMembers",
                                params, 1, out);
}

wf_status wf_agent_chat_get_unread_counts(wf_agent *agent,
                                           wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;

    wf_agent_sync_chat_auth(agent);
    return wf_xrpc_query_params(cc, "chat.bsky.convo.getUnreadCounts",
                                NULL, 0, out);
}

wf_status wf_agent_chat_list_convo_requests(wf_agent *agent, int limit,
                                             const char *cursor,
                                             wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;

    wf_xrpc_param params[2];
    size_t param_count = 0;
    char limit_buf[16];

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

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;

    wf_agent_sync_chat_auth(agent);
    return wf_xrpc_query_params(cc, "chat.bsky.convo.listConvoRequests",
                                params, param_count, out);
}

/* ════════════════════════════════════════════════════════════════════════
 * chat.bsky.group.*
 *
 * All endpoints route through the resolved chat service client (same as
 * chat.bsky.convo.*). Queries return raw JSON in `out`.
 * ════════════════════════════════════════════════════════════════════════ */

/* ── helpers ─────────────────────────────────────────────────────────── */

/* Build a procedure body with a single string array field. */
static cJSON *wf_chat_group_build_members(const char *convo_id,
                                          const char *const *members,
                                          size_t member_count) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    if (!cJSON_AddStringToObject(root, "convoId", convo_id)) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON *arr = cJSON_AddArrayToObject(root, "members");
    if (!arr) { cJSON_Delete(root); return NULL; }
    for (size_t i = 0; i < member_count; i++) {
        cJSON *item = cJSON_CreateString(members[i]);
        if (!item || !cJSON_AddItemToArray(arr, item)) {
            cJSON_Delete(item); cJSON_Delete(root);
            return NULL;
        }
    }
    return root;
}

static wf_status wf_chat_group_procedure_json(wf_agent *agent,
                                               const char *nsid,
                                               const char *json,
                                               wf_response *out) {
    if (!agent || !nsid || !json) return WF_ERR_INVALID_ARG;
    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;
    wf_agent_sync_chat_auth(agent);
    return wf_xrpc_procedure(cc, nsid, json, out);
}

/* ── createGroup ─────────────────────────────────────────────────────── */

wf_status wf_agent_chat_create_group(wf_agent *agent,
                                      const char *const *member_dids,
                                      size_t member_count,
                                      const char *name,
                                      wf_response *out) {
    if (!agent || !member_dids || member_count == 0 || !name || !out)
        return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;

    cJSON *arr = cJSON_AddArrayToObject(root, "members");
    if (!arr) { cJSON_Delete(root); return WF_ERR_ALLOC; }
    for (size_t i = 0; i < member_count; i++) {
        cJSON *item = cJSON_CreateString(member_dids[i]);
        if (!item || !cJSON_AddItemToArray(arr, item)) {
            cJSON_Delete(item); cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }
    if (!cJSON_AddStringToObject(root, "name", name)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_status status = wf_chat_group_procedure_json(agent,
                      "chat.bsky.group.createGroup", json, out);
    free(json);
    return status;
}

/* ── editGroup ───────────────────────────────────────────────────────── */

wf_status wf_agent_chat_edit_group(wf_agent *agent, const char *convo_id,
                                    const char *name, wf_response *out) {
    if (!agent || !convo_id || !name || !out) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "convoId", convo_id) ||
        !cJSON_AddStringToObject(root, "name", name)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_status status = wf_chat_group_procedure_json(agent,
                      "chat.bsky.group.editGroup", json, out);
    free(json);
    return status;
}

/* ── addMembers / removeMembers ──────────────────────────────────────── */

wf_status wf_agent_chat_add_members(wf_agent *agent, const char *convo_id,
                                     const char *const *member_dids,
                                     size_t member_count,
                                     wf_response *out) {
    if (!agent || !convo_id || !member_dids || member_count == 0 || !out)
        return WF_ERR_INVALID_ARG;

    cJSON *root = wf_chat_group_build_members(convo_id, member_dids,
                                              member_count);
    if (!root) return WF_ERR_ALLOC;

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_status status = wf_chat_group_procedure_json(agent,
                      "chat.bsky.group.addMembers", json, out);
    free(json);
    return status;
}

wf_status wf_agent_chat_remove_members(wf_agent *agent, const char *convo_id,
                                        const char *const *member_dids,
                                        size_t member_count,
                                        wf_response *out) {
    if (!agent || !convo_id || !member_dids || member_count == 0 || !out)
        return WF_ERR_INVALID_ARG;

    cJSON *root = wf_chat_group_build_members(convo_id, member_dids,
                                              member_count);
    if (!root) return WF_ERR_ALLOC;

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_status status = wf_chat_group_procedure_json(agent,
                      "chat.bsky.group.removeMembers", json, out);
    free(json);
    return status;
}

/* ── requestJoin / withdrawJoinRequest ───────────────────────────────── */

wf_status wf_agent_chat_request_join(wf_agent *agent, const char *convo_id) {
    return wf_chat_procedure_1arg(agent, "chat.bsky.group.requestJoin",
                                  "convoId", convo_id);
}

wf_status wf_agent_chat_withdraw_join_request(wf_agent *agent,
                                               const char *convo_id) {
    return wf_chat_procedure_1arg(agent, "chat.bsky.group.withdrawJoinRequest",
                                  "convoId", convo_id);
}

/* ── approveJoinRequest / rejectJoinRequest ──────────────────────────── */

wf_status wf_agent_chat_approve_join_request(wf_agent *agent,
                                              const char *convo_id,
                                              const char *user_did,
                                              wf_response *out) {
    if (!agent || !convo_id || !user_did || !out) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "convoId", convo_id) ||
        !cJSON_AddStringToObject(root, "userId", user_did)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    return wf_chat_group_procedure_json(agent,
               "chat.bsky.group.approveJoinRequest", json, out);
}

wf_status wf_agent_chat_reject_join_request(wf_agent *agent,
                                             const char *convo_id,
                                             const char *user_did,
                                             wf_response *out) {
    if (!agent || !convo_id || !user_did || !out) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "convoId", convo_id) ||
        !cJSON_AddStringToObject(root, "userId", user_did)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    return wf_chat_group_procedure_json(agent,
               "chat.bsky.group.rejectJoinRequest", json, out);
}

/* ── listJoinRequests ────────────────────────────────────────────────── */

wf_status wf_agent_chat_list_join_requests(wf_agent *agent,
                                            const char *convo_id,
                                            int limit, const char *cursor,
                                            wf_response *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;

    wf_xrpc_param params[3];
    size_t pc = 0;
    char limit_buf[16];

    params[pc].name = "convoId";
    params[pc].value = convo_id;
    pc++;
    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf)))
            return WF_ERR_INVALID_ARG;
        params[pc].name = "limit";
        params[pc].value = limit_buf;
        pc++;
    }
    if (cursor && cursor[0]) {
        params[pc].name = "cursor";
        params[pc].value = cursor;
        pc++;
    }

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;
    wf_agent_sync_chat_auth(agent);
    return wf_xrpc_query_params(cc, "chat.bsky.group.listJoinRequests",
                                params, pc, out);
}

/* ── updateJoinRequestsRead ──────────────────────────────────────────── */

wf_status wf_agent_chat_update_join_requests_read(wf_agent *agent,
                                                    const char *convo_id) {
    return wf_chat_procedure_1arg(agent,
                                  "chat.bsky.group.updateJoinRequestsRead",
                                  "convoId", convo_id);
}

/* ── createJoinLink ──────────────────────────────────────────────────── */

wf_status wf_agent_chat_create_join_link(wf_agent *agent,
                                          const char *convo_id,
                                          bool require_approval,
                                          const char *join_rule,
                                          wf_response *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "convoId", convo_id) ||
        !cJSON_AddBoolToObject(root, "requireApproval", require_approval)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }
    if (join_rule && join_rule[0]) {
        if (!cJSON_AddStringToObject(root, "joinRule", join_rule)) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    return wf_chat_group_procedure_json(agent,
               "chat.bsky.group.createJoinLink", json, out);
}

/* ── editJoinLink ────────────────────────────────────────────────────── */

wf_status wf_agent_chat_edit_join_link(wf_agent *agent,
                                        const char *convo_id,
                                        const char *code,
                                        wf_response *out) {
    if (!agent || !convo_id || !code || !out) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "convoId", convo_id) ||
        !cJSON_AddStringToObject(root, "code", code)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    return wf_chat_group_procedure_json(agent,
               "chat.bsky.group.editJoinLink", json, out);
}

/* ── disableJoinLink / enableJoinLink ────────────────────────────────── */

wf_status wf_agent_chat_disable_join_link(wf_agent *agent,
                                           const char *convo_id) {
    return wf_chat_procedure_1arg(agent, "chat.bsky.group.disableJoinLink",
                                  "convoId", convo_id);
}

wf_status wf_agent_chat_enable_join_link(wf_agent *agent,
                                          const char *convo_id) {
    return wf_chat_procedure_1arg(agent, "chat.bsky.group.enableJoinLink",
                                  "convoId", convo_id);
}

/* ── getJoinLinkPreviews ─────────────────────────────────────────────── */

wf_status wf_agent_chat_get_join_link_previews(wf_agent *agent,
                                                const char *const *codes,
                                                size_t code_count,
                                                wf_response *out) {
    if (!agent || !codes || code_count == 0 || !out)
        return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    cJSON *arr = cJSON_AddArrayToObject(root, "codes");
    if (!arr) { cJSON_Delete(root); return WF_ERR_ALLOC; }
    for (size_t i = 0; i < code_count; i++) {
        cJSON *item = cJSON_CreateString(codes[i]);
        if (!item || !cJSON_AddItemToArray(arr, item)) {
            cJSON_Delete(item); cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    return wf_chat_group_procedure_json(agent,
               "chat.bsky.group.getJoinLinkPreviews", json, out);
}

/* ── listMutualGroups ────────────────────────────────────────────────── */

wf_status wf_agent_chat_list_mutual_groups(wf_agent *agent,
                                            const char *convo_id,
                                            int limit, const char *cursor,
                                            wf_response *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;

    wf_xrpc_param params[3];
    size_t pc = 0;
    char limit_buf[16];

    params[pc].name = "convoId";
    params[pc].value = convo_id;
    pc++;
    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf)))
            return WF_ERR_INVALID_ARG;
        params[pc].name = "limit";
        params[pc].value = limit_buf;
        pc++;
    }
    if (cursor && cursor[0]) {
        params[pc].name = "cursor";
        params[pc].value = cursor;
        pc++;
    }

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;
    wf_agent_sync_chat_auth(agent);
    return wf_xrpc_query_params(cc, "chat.bsky.group.listMutualGroups",
                                params, pc, out);
}
