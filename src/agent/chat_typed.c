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

/* ── chat.bsky.notification parses ────────────────────────────────────── */

static void wf_chat_notification_pref_reset(wf_chat_notification_preference *p) {
    if (!p) {
        return;
    }
    free(p->include);
    memset(p, 0, sizeof(*p));
}

static wf_status wf_chat_notification_pref_parse(wf_chat_notification_preference *out,
                                                  cJSON *obj) {
    wf_status status = WF_OK;
    if (!cJSON_IsObject(obj)) {
        return WF_ERR_PARSE;
    }
    cJSON *include = cJSON_GetObjectItemCaseSensitive(obj, "include");
    cJSON *push = cJSON_GetObjectItemCaseSensitive(obj, "push");

    if (cJSON_IsString(include) && include->valuestring) {
        status = wf_chat_set_string(&out->include, include->valuestring);
    }
    if (status == WF_OK) {
        if (cJSON_IsBool(push)) {
            out->push = cJSON_IsTrue(push) ? 1 : 0;
            out->has_push = 1;
        }
    }
    return status;
}

wf_status wf_agent_parse_chat_notification_preferences(const char *json,
                                                        size_t json_len,
                                                        wf_chat_notification_preferences *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *prefs = cJSON_GetObjectItemCaseSensitive(root, "preferences");
    if (!cJSON_IsObject(prefs)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    cJSON *chat = cJSON_GetObjectItemCaseSensitive(prefs, "chat");
    cJSON *chat_request = cJSON_GetObjectItemCaseSensitive(prefs, "chatRequest");
    if (!cJSON_IsObject(chat) || !cJSON_IsObject(chat_request)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    status = wf_chat_notification_pref_parse(&out->chat, chat);
    if (status == WF_OK) {
        status = wf_chat_notification_pref_parse(&out->chat_request, chat_request);
    }

    if (status != WF_OK) {
        wf_chat_notification_pref_reset(&out->chat);
        wf_chat_notification_pref_reset(&out->chat_request);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_chat_notification_preferences_free(wf_chat_notification_preferences *p) {
    if (!p) {
        return;
    }
    wf_chat_notification_pref_reset(&p->chat);
    wf_chat_notification_pref_reset(&p->chat_request);
    memset(p, 0, sizeof(*p));
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

wf_status wf_chat_build_accept_convo_body(const char *convo_id,
                                            char **out_json) {
    if (!convo_id || !out_json) return WF_ERR_INVALID_ARG;
    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "convoId", convo_id)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;
    *out_json = json;
    return WF_OK;
}

wf_status wf_agent_chat_accept_convo(wf_agent *agent, const char *convo_id) {
    if (!agent || !convo_id) return WF_ERR_INVALID_ARG;
    char *json = NULL;
    wf_status s = wf_chat_build_accept_convo_body(convo_id, &json);
    if (s != WF_OK) return s;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) { free(json); return WF_ERR_INVALID_ARG; }
    wf_agent_sync_chat_auth(agent);
    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(cc, "chat.bsky.convo.acceptConvo",
                                          json, &res);
    free(json);
    wf_response_free(&res);
    return status;
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

wf_status wf_chat_build_add_reaction_body(const char *convo_id,
                                            const char *message_id,
                                            const char *value,
                                            char **out_json) {
    if (!convo_id || !message_id || !value || !out_json)
        return WF_ERR_INVALID_ARG;
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
    *out_json = json;
    return WF_OK;
}

wf_status wf_agent_chat_add_reaction(wf_agent *agent,
                                      const char *convo_id,
                                      const char *message_id,
                                      const char *value) {
    if (!agent || !convo_id || !message_id || !value) return WF_ERR_INVALID_ARG;

    char *json = NULL;
    wf_status s = wf_chat_build_add_reaction_body(convo_id, message_id,
                                                   value, &json);
    if (s != WF_OK) return s;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) { free(json); return WF_ERR_INVALID_ARG; }

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
    if (!cJSON_AddStringToObject(root, "status", convo_id)) {
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

wf_status wf_chat_build_create_group_body(const char *const *member_dids,
                                           size_t member_count,
                                           const char *name,
                                           char **out_json) {
    if (!member_dids || member_count == 0 || !name || !out_json)
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
    *out_json = json;
    return WF_OK;
}

wf_status wf_agent_chat_create_group(wf_agent *agent,
                                      const char *const *member_dids,
                                      size_t member_count,
                                      const char *name,
                                      wf_response *out) {
    if (!agent || !member_dids || member_count == 0 || !name || !out)
        return WF_ERR_INVALID_ARG;

    char *json = NULL;
    wf_status s = wf_chat_build_create_group_body(member_dids, member_count,
                                                  name, &json);
    if (s != WF_OK) return s;

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
    if (        !cJSON_AddStringToObject(root, "convoId", convo_id) ||
        !cJSON_AddStringToObject(root, "member", user_did)) {
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
    if (        !cJSON_AddStringToObject(root, "convoId", convo_id) ||
        !cJSON_AddStringToObject(root, "member", user_did)) {
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
    if (!agent || !convo_id || !join_rule || !out) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "convoId", convo_id) ||
        !cJSON_AddBoolToObject(root, "requireApproval", require_approval) ||
        !cJSON_AddStringToObject(root, "joinRule", join_rule)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
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

    wf_xrpc_param *params = calloc(code_count, sizeof(*params));
    if (!params) return WF_ERR_ALLOC;
    for (size_t i = 0; i < code_count; i++) {
        if (!codes[i]) { free(params); return WF_ERR_INVALID_ARG; }
        params[i].name = "codes";
        params[i].value = codes[i];
    }

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) { free(params); return WF_ERR_INVALID_ARG; }
    wf_agent_sync_chat_auth(agent);
    wf_status status = wf_xrpc_query_params(cc,
                                 "chat.bsky.group.getJoinLinkPreviews",
                                 params, code_count, out);
    free(params);
    return status;
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

/* ════════════════════════════════════════════════════════════════════════
 * chat.bsky.actor.*
 *
 * Chat-account-level operations. All route through the resolved chat service.
 * ════════════════════════════════════════════════════════════════════════ */

wf_status wf_agent_chat_get_status(wf_agent *agent, wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;
    wf_agent_sync_chat_auth(agent);
    return wf_xrpc_query(cc, "chat.bsky.actor.getStatus", NULL, out);
}

wf_status wf_agent_chat_delete_account(wf_agent *agent, wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;
    wf_agent_sync_chat_auth(agent);
    return wf_xrpc_procedure(cc, "chat.bsky.actor.deleteAccount", "{}", out);
}

wf_status wf_agent_chat_export_account_data(wf_agent *agent, wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;
    wf_agent_sync_chat_auth(agent);
    return wf_xrpc_query(cc, "chat.bsky.actor.exportAccountData", NULL, out);
}

/* ════════════════════════════════════════════════════════════════════════
 * chat.bsky.moderation.*
 *
 * Chat moderation endpoints for Ozone/moderator tooling. Route through the
 * resolved chat service client. Return raw JSON in `out`.
 * ════════════════════════════════════════════════════════════════════════ */

wf_status wf_agent_chat_mod_get_actor_metadata(wf_agent *agent,
                                                const char *actor_did,
                                                wf_response *out) {
    if (!agent || !actor_did || !out) return WF_ERR_INVALID_ARG;
    wf_xrpc_param params[] = {{"actor", actor_did}};
    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;
    wf_agent_sync_chat_auth(agent);
    return wf_xrpc_query_params(cc, "chat.bsky.moderation.getActorMetadata",
                                 params, 1, out);
}

wf_status wf_agent_chat_mod_get_message_context(wf_agent *agent,
                                                 const char *message_id,
                                                 const char *convo_id,
                                                 int before, int after,
                                                 int max_interleaved,
                                                 wf_response *out) {
    if (!agent || !message_id || !out) return WF_ERR_INVALID_ARG;

    wf_xrpc_param params[5];
    size_t pc = 0;
    char before_buf[16], after_buf[16], max_buf[16];

    params[pc].name = "messageId";
    params[pc].value = message_id;
    pc++;
    if (convo_id && convo_id[0]) {
        params[pc].name = "convoId";
        params[pc].value = convo_id;
        pc++;
    }
    if (before > 0) {
        if (!wf_agent_int_to_str(before, before_buf, sizeof(before_buf)))
            return WF_ERR_INVALID_ARG;
        params[pc].name = "before";
        params[pc].value = before_buf;
        pc++;
    }
    if (after > 0) {
        if (!wf_agent_int_to_str(after, after_buf, sizeof(after_buf)))
            return WF_ERR_INVALID_ARG;
        params[pc].name = "after";
        params[pc].value = after_buf;
        pc++;
    }
    if (max_interleaved > 0) {
        if (!wf_agent_int_to_str(max_interleaved, max_buf, sizeof(max_buf)))
            return WF_ERR_INVALID_ARG;
        params[pc].name = "maxInterleavedSystemMessages";
        params[pc].value = max_buf;
        pc++;
    }

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;
    wf_agent_sync_chat_auth(agent);
    return wf_xrpc_query_params(cc, "chat.bsky.moderation.getMessageContext",
                                 params, pc, out);
}

wf_status wf_agent_chat_mod_get_convo(wf_agent *agent, const char *convo_id,
                                       wf_response *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;
    wf_xrpc_param params[] = {{"convoId", convo_id}};
    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;
    wf_agent_sync_chat_auth(agent);
    return wf_xrpc_query_params(cc, "chat.bsky.moderation.getConvo",
                                 params, 1, out);
}

wf_status wf_agent_chat_mod_get_convos(wf_agent *agent,
                                        const char *const *convo_ids,
                                        size_t convo_count,
                                        wf_response *out) {
    if (!agent || !convo_ids || convo_count == 0 || !out)
        return WF_ERR_INVALID_ARG;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;

    /* Build the convoIds as a repeated query parameter. */
    wf_xrpc_param *params = calloc(convo_count, sizeof(*params));
    if (!params) return WF_ERR_ALLOC;
    for (size_t i = 0; i < convo_count; i++) {
        params[i].name = "convoIds";
        params[i].value = convo_ids[i];
    }

    wf_agent_sync_chat_auth(agent);
    wf_status status = wf_xrpc_query_params(cc, "chat.bsky.moderation.getConvos",
                                              params, convo_count, out);
    free(params);
    return status;
}

wf_status wf_agent_chat_mod_get_convo_members(wf_agent *agent,
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
    return wf_xrpc_query_params(cc, "chat.bsky.moderation.getConvoMembers",
                                 params, pc, out);
}

wf_status wf_agent_chat_mod_update_actor_access(wf_agent *agent,
                                                 const char *actor_did,
                                                 bool allow_access,
                                                 const char *ref,
                                                 wf_response *out) {
    if (!agent || !actor_did || !out) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "actor", actor_did) ||
        !cJSON_AddBoolToObject(root, "allowAccess", allow_access)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }
    if (ref && ref[0]) {
        if (!cJSON_AddStringToObject(root, "ref", ref)) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) { free(json); return WF_ERR_INVALID_ARG; }
    wf_agent_sync_chat_auth(agent);
    wf_status status = wf_xrpc_procedure(cc,
                          "chat.bsky.moderation.updateActorAccess",
                          json, out);
    free(json);
    return status;
}

/* ── remaining chat.bsky.convo wrappers ──────────────────────────────── */

wf_status wf_agent_chat_send_message_batch(wf_agent *agent,
                                            const char *items_json,
                                            wf_response *out) {
    if (!agent || !items_json || !out) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_Parse(items_json);
    if (!root) return WF_ERR_PARSE;

    cJSON *body = cJSON_CreateObject();
    if (!body) { cJSON_Delete(root); return WF_ERR_ALLOC; }
    if (!cJSON_AddItemToObject(body, "items", root)) {
        cJSON_Delete(root); cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return WF_ERR_ALLOC;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) { free(json); return WF_ERR_INVALID_ARG; }
    wf_agent_sync_chat_auth(agent);
    wf_status status = wf_xrpc_procedure(cc,
                          "chat.bsky.convo.sendMessageBatch",
                          json, out);
    free(json);
    return status;
}

wf_status wf_agent_chat_lock_convo(wf_agent *agent, const char *convo_id,
                                    wf_response *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "convoId", convo_id)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) { free(json); return WF_ERR_INVALID_ARG; }
    wf_agent_sync_chat_auth(agent);
    wf_status status = wf_xrpc_procedure(cc,
                          "chat.bsky.convo.lockConvo",
                          json, out);
    free(json);
    return status;
}

wf_status wf_agent_chat_unlock_convo(wf_agent *agent, const char *convo_id,
                                      wf_response *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "convoId", convo_id)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) { free(json); return WF_ERR_INVALID_ARG; }
    wf_agent_sync_chat_auth(agent);
    wf_status status = wf_xrpc_procedure(cc,
                          "chat.bsky.convo.unlockConvo",
                          json, out);
    free(json);
    return status;
}

wf_status wf_agent_chat_get_log(wf_agent *agent, int limit,
                                 const char *cursor, wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;

    wf_xrpc_param params[2];
    size_t pc = 0;
    char limit_buf[16];

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
    return wf_xrpc_query_params(cc, "chat.bsky.convo.getLog",
                                 params, pc, out);
}

/* ════════════════════════════════════════════════════════════════════════
 * chat.bsky.notification.*
 * ════════════════════════════════════════════════════════════════════════ */

wf_status wf_agent_chat_notification_get_preferences(wf_agent *agent,
                                                      wf_chat_notification_preferences *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;

    wf_agent_sync_chat_auth(agent);
    wf_response res = {0};
    wf_status status =
        wf_lex_chat_bsky_notification_get_preferences_main_call(cc, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_chat_notification_preferences(res.body,
                                                           res.body_len, out);
    wf_response_free(&res);
    if (status != WF_OK) {
        wf_chat_notification_preferences_free(out);
    }
    return status;
}

wf_status wf_agent_chat_notification_put_preferences(wf_agent *agent,
                                                      const wf_chat_notification_preferences *in,
                                                      wf_chat_notification_preferences *out) {
    if (!agent || !in || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    /* Build the generated input from the owned preference struct. Both `chat`
     * and `chatRequest` are always present in the owned struct. */
    wf_lex_chat_bsky_notification_defs_chat_preference chat_lex;
    memset(&chat_lex, 0, sizeof(chat_lex));
    chat_lex.include = in->chat.include;
    chat_lex.push = in->chat.push;

    wf_lex_chat_bsky_notification_defs_chat_preference chat_request_lex;
    memset(&chat_request_lex, 0, sizeof(chat_request_lex));
    chat_request_lex.include = in->chat_request.include;
    chat_request_lex.push = in->chat_request.push;

    wf_lex_chat_bsky_notification_put_preferences_main_input input;
    memset(&input, 0, sizeof(input));
    input.has_chat = 1;
    input.chat = &chat_lex;
    input.has_chat_request = 1;
    input.chat_request = &chat_request_lex;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) return WF_ERR_INVALID_ARG;

    wf_agent_sync_chat_auth(agent);
    wf_response res = {0};
    wf_status status =
        wf_lex_chat_bsky_notification_put_preferences_main_call(cc, &input, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_chat_notification_preferences(res.body,
                                                           res.body_len, out);
    wf_response_free(&res);
    if (status != WF_OK) {
        wf_chat_notification_preferences_free(out);
    }
    return status;
}

/* ════════════════════════════════════════════════════════════════════════
 * Extended owned-struct typed wrappers for the remaining chat.bsky.* endpoints.
 *
 * Each `*_typed` wrapper mirrors the existing owned-struct wrappers above: it
 * resolves the chat service, issues the call through `agent->chat_client`,
 * parses the JSON `res.body` into an owned struct (resetting `out` on error),
 * then frees the response. Where the corresponding raw wrapper already exposes
 * a `wf_response *out`, the typed wrapper simply delegates to it and parses the
 * body; for the fire-and-forget procedures it issues the call directly.
 * ════════════════════════════════════════════════════════════════════════ */

/* ── getConvoAvailability ──────────────────────────────────────────────── */

static void wf_chat_convo_availability_reset(wf_chat_convo_availability *a) {
    if (!a) return;
    wf_chat_convo_reset(&a->convo);
    memset(a, 0, sizeof(*a));
}

wf_status wf_agent_parse_convo_availability(const char *json, size_t json_len,
                                            wf_chat_convo_availability *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;

    wf_status status = WF_OK;
    cJSON *can = cJSON_GetObjectItemCaseSensitive(root, "canChat");
    if (cJSON_IsBool(can)) {
        out->can_chat = cJSON_IsTrue(can);
        out->has_can_chat = 1;
    }
    cJSON *hc = cJSON_GetObjectItemCaseSensitive(root, "hasConvo");
    if (cJSON_IsBool(hc)) {
        out->has_convo = cJSON_IsTrue(hc);
        out->has_has_convo = 1;
    }
    cJSON *convo = cJSON_GetObjectItemCaseSensitive(root, "convo");
    if (cJSON_IsObject(convo)) {
        status = wf_chat_parse_convo_view(&out->convo, convo);
    }

    if (status != WF_OK) wf_chat_convo_availability_reset(out);
    cJSON_Delete(root);
    return status;
}

void wf_chat_convo_availability_free(wf_chat_convo_availability *a) {
    if (!a) return;
    wf_chat_convo_availability_reset(a);
}

wf_status wf_agent_chat_get_convo_availability_typed(wf_agent *agent,
        const char *const *member_dids, size_t member_count,
        wf_chat_convo_availability *out) {
    if (!agent || !member_dids || member_count == 0 || !out)
        return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    wf_response res = {0};
    wf_status s = wf_agent_chat_get_convo_availability(agent, member_dids,
                                                      member_count, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_convo_availability(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_convo_availability_free(out);
    return s;
}

/* ── convo members (getConvoMembers / moderation.getConvoMembers) ───────── */

static void wf_chat_convo_members_reset(wf_chat_convo_members *m) {
    if (!m) return;
    for (size_t i = 0; i < m->member_count; ++i)
        wf_chat_profile_reset(&m->members[i]);
    free(m->members);
    free(m->cursor);
    memset(m, 0, sizeof(*m));
}

wf_status wf_agent_parse_convo_members(const char *json, size_t json_len,
                                       wf_chat_convo_members *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "members");
    if (!cJSON_IsArray(arr)) { cJSON_Delete(root); return WF_ERR_PARSE; }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_agent_profile_view *items = NULL;
    if (count > 0) {
        items = (wf_agent_profile_view *)calloc(count, sizeof(*items));
        if (!items) { cJSON_Delete(root); return WF_ERR_ALLOC; }
    }
    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        status = wf_chat_parse_profile(&items[i],
                                       cJSON_GetArrayItem(arr, (int)i));
        if (status != WF_OK) wf_chat_profile_reset(&items[i]);
    }

    if (status == WF_OK) {
        out->members = items;
        out->member_count = count;
        cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cur) && cur->valuestring)
            status = wf_chat_set_string(&out->cursor, cur->valuestring);
    }
    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) wf_chat_profile_reset(&items[i]);
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

void wf_chat_convo_members_free(wf_chat_convo_members *m) {
    if (!m) return;
    wf_chat_convo_members_reset(m);
}

wf_status wf_agent_chat_get_convo_members_typed(wf_agent *agent,
        const char *convo_id, wf_chat_convo_members *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_get_convo_members(agent, convo_id, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_convo_members(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_convo_members_free(out);
    return s;
}

wf_status wf_agent_chat_mod_get_convo_members_typed(wf_agent *agent,
        const char *convo_id, int limit, const char *cursor,
        wf_chat_convo_members *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_mod_get_convo_members(agent, convo_id, limit,
                                                      cursor, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_convo_members(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_convo_members_free(out);
    return s;
}

/* ── getUnreadCounts ──────────────────────────────────────────────────── */

static void wf_chat_unread_counts_reset(wf_chat_unread_counts *u) {
    if (!u) return;
    memset(u, 0, sizeof(*u));
}

wf_status wf_agent_parse_unread_counts(const char *json, size_t json_len,
                                       wf_chat_unread_counts *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;

    cJSON *a = cJSON_GetObjectItemCaseSensitive(root, "unreadAcceptedConvos");
    if (cJSON_IsNumber(a)) {
        out->unread_accepted_convos = (int)a->valuedouble;
        out->has_unread_accepted = 1;
    }
    cJSON *r = cJSON_GetObjectItemCaseSensitive(root, "unreadRequestConvos");
    if (cJSON_IsNumber(r)) {
        out->unread_request_convos = (int)r->valuedouble;
        out->has_unread_request = 1;
    }
    cJSON_Delete(root);
    return WF_OK;
}

void wf_chat_unread_counts_free(wf_chat_unread_counts *u) {
    if (!u) return;
    wf_chat_unread_counts_reset(u);
}

wf_status wf_agent_chat_get_unread_counts_typed(wf_agent *agent,
        wf_chat_unread_counts *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_get_unread_counts(agent, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_unread_counts(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_unread_counts_free(out);
    return s;
}

/* ── convoView array under an arbitrary key (requests / convos) ─────────── */

wf_status wf_agent_parse_convo_array(const char *json, size_t json_len,
                                     const char *key, wf_chat_convo_list *out) {
    if (!json || !out || !key) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsArray(arr)) { cJSON_Delete(root); return WF_ERR_PARSE; }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_chat_convo *items = NULL;
    if (count > 0) {
        items = (wf_chat_convo *)calloc(count, sizeof(*items));
        if (!items) { cJSON_Delete(root); return WF_ERR_ALLOC; }
    }
    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        status = wf_chat_parse_convo_view(&items[i],
                                          cJSON_GetArrayItem(arr, (int)i));
        if (status != WF_OK) wf_chat_convo_reset(&items[i]);
    }
    if (status == WF_OK) {
        out->convos = items;
        out->convo_count = count;
        cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cur) && cur->valuestring)
            status = wf_chat_set_string(&out->cursor, cur->valuestring);
    }
    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) wf_chat_convo_reset(&items[i]);
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

wf_status wf_agent_parse_convo_requests(const char *json, size_t json_len,
                                        wf_chat_convo_list *out) {
    return wf_agent_parse_convo_array(json, json_len, "requests", out);
}

wf_status wf_agent_chat_list_convo_requests_typed(wf_agent *agent, int limit,
        const char *cursor, wf_chat_convo_list *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_list_convo_requests(agent, limit, cursor, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_convo_requests(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_convo_list_free(out);
    return s;
}

wf_status wf_agent_chat_list_mutual_groups_typed(wf_agent *agent,
        const char *convo_id, int limit, const char *cursor,
        wf_chat_convo_list *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_list_mutual_groups(agent, convo_id, limit,
                                                   cursor, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_convo_array(res.body, res.body_len, "convos", out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_convo_list_free(out);
    return s;
}

wf_status wf_agent_chat_mod_get_convos_typed(wf_agent *agent,
        const char *const *convo_ids, size_t convo_count,
        wf_chat_mod_convo_list *out) {
    if (!agent || !convo_ids || convo_count == 0 || !out)
        return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_mod_get_convos(agent, convo_ids, convo_count,
                                               &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_mod_convos(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_mod_convo_list_free(out);
    return s;
}

/* ── getLog ───────────────────────────────────────────────────────────── */

static void wf_chat_log_event_reset(wf_chat_log_event *e) {
    if (!e) return;
    free(e->id);
    free(e->rev);
    free(e->type);
    free(e->convo_id);
    memset(e, 0, sizeof(*e));
}

static void wf_chat_log_reset(wf_chat_log *l) {
    if (!l) return;
    for (size_t i = 0; i < l->event_count; ++i)
        wf_chat_log_event_reset(&l->events[i]);
    free(l->events);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

wf_status wf_agent_parse_log(const char *json, size_t json_len, wf_chat_log *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "logs");
    if (!cJSON_IsArray(arr)) { cJSON_Delete(root); return WF_ERR_PARSE; }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_chat_log_event *items = NULL;
    if (count > 0) {
        items = (wf_chat_log_event *)calloc(count, sizeof(*items));
        if (!items) { cJSON_Delete(root); return WF_ERR_ALLOC; }
    }
    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *o = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(o)) { status = WF_ERR_PARSE; break; }
        cJSON *id = cJSON_GetObjectItemCaseSensitive(o, "id");
        cJSON *rev = cJSON_GetObjectItemCaseSensitive(o, "rev");
        cJSON *tp = cJSON_GetObjectItemCaseSensitive(o, "$type");
        cJSON *cid = cJSON_GetObjectItemCaseSensitive(o, "convoId");
        if (cJSON_IsString(id) && id->valuestring)
            status = wf_chat_set_string(&items[i].id, id->valuestring);
        if (status == WF_OK && cJSON_IsString(rev) && rev->valuestring)
            status = wf_chat_set_string(&items[i].rev, rev->valuestring);
        if (status == WF_OK && cJSON_IsString(tp) && tp->valuestring)
            status = wf_chat_set_string(&items[i].type, tp->valuestring);
        if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring)
            status = wf_chat_set_string(&items[i].convo_id, cid->valuestring);
        if (status != WF_OK) wf_chat_log_event_reset(&items[i]);
    }
    if (status == WF_OK) {
        out->events = items;
        out->event_count = count;
        cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cur) && cur->valuestring)
            status = wf_chat_set_string(&out->cursor, cur->valuestring);
    }
    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) wf_chat_log_event_reset(&items[i]);
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

void wf_chat_log_free(wf_chat_log *l) {
    if (!l) return;
    wf_chat_log_reset(l);
}

wf_status wf_agent_chat_get_log_typed(wf_agent *agent, int limit,
        const char *cursor, wf_chat_log *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_get_log(agent, limit, cursor, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_log(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_log_free(out);
    return s;
}

/* ── sendMessageBatch ─────────────────────────────────────────────────── */

static void wf_chat_message_batch_item_reset(wf_chat_message_batch_item *it) {
    if (!it) return;
    free(it->convo_id);
    wf_chat_message_reset(&it->message);
    memset(it, 0, sizeof(*it));
}

static void wf_chat_message_batch_reset(wf_chat_message_batch *b) {
    if (!b) return;
    for (size_t i = 0; i < b->item_count; ++i)
        wf_chat_message_batch_item_reset(&b->items[i]);
    free(b->items);
    memset(b, 0, sizeof(*b));
}

wf_status wf_agent_parse_message_batch(const char *json, size_t json_len,
        wf_chat_message_batch *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "items");
    if (!cJSON_IsArray(arr)) { cJSON_Delete(root); return WF_ERR_PARSE; }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_chat_message_batch_item *items = NULL;
    if (count > 0) {
        items = (wf_chat_message_batch_item *)calloc(count, sizeof(*items));
        if (!items) { cJSON_Delete(root); return WF_ERR_ALLOC; }
    }
    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *o = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(o)) { status = WF_ERR_PARSE; break; }
        cJSON *cid = cJSON_GetObjectItemCaseSensitive(o, "convoId");
        cJSON *msg = cJSON_GetObjectItemCaseSensitive(o, "message");
        if (cJSON_IsString(cid) && cid->valuestring)
            status = wf_chat_set_string(&items[i].convo_id, cid->valuestring);
        if (status == WF_OK && cJSON_IsObject(msg))
            status = wf_chat_parse_message(&items[i].message, msg);
        if (status != WF_OK) wf_chat_message_batch_item_reset(&items[i]);
    }
    if (status == WF_OK) {
        out->items = items;
        out->item_count = count;
    }
    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i)
            wf_chat_message_batch_item_reset(&items[i]);
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

void wf_chat_message_batch_free(wf_chat_message_batch *b) {
    if (!b) return;
    wf_chat_message_batch_reset(b);
}

wf_status wf_agent_chat_send_message_batch_typed(wf_agent *agent,
        const char *items_json, wf_chat_message_batch *out) {
    if (!agent || !items_json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_send_message_batch(agent, items_json, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_message_batch(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_message_batch_free(out);
    return s;
}

/* ── leaveConvo ───────────────────────────────────────────────────────── */

static void wf_chat_convo_ref_reset(wf_chat_convo_ref *r) {
    if (!r) return;
    free(r->convo_id);
    free(r->rev);
    memset(r, 0, sizeof(*r));
}

wf_status wf_agent_parse_convo_ref(const char *json, size_t json_len,
        wf_chat_convo_ref *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;

    wf_status status = WF_OK;
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(root, "convoId");
    cJSON *rev = cJSON_GetObjectItemCaseSensitive(root, "rev");
    if (cJSON_IsString(cid) && cid->valuestring)
        status = wf_chat_set_string(&out->convo_id, cid->valuestring);
    if (status == WF_OK && cJSON_IsString(rev) && rev->valuestring)
        status = wf_chat_set_string(&out->rev, rev->valuestring);
    if (status != WF_OK) wf_chat_convo_ref_reset(out);
    cJSON_Delete(root);
    return status;
}

void wf_chat_convo_ref_free(wf_chat_convo_ref *r) {
    if (!r) return;
    wf_chat_convo_ref_reset(r);
}

wf_status wf_agent_chat_leave_convo_typed(wf_agent *agent, const char *convo_id,
        wf_chat_convo_ref *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    char *json = NULL;
    wf_status s = wf_chat_build_accept_convo_body(convo_id, &json);
    if (s != WF_OK) return s;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) { free(json); return WF_ERR_INVALID_ARG; }
    wf_agent_sync_chat_auth(agent);
    wf_response res = {0};
    s = wf_xrpc_procedure(cc, "chat.bsky.convo.leaveConvo", json, &res);
    free(json);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_convo_ref(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_convo_ref_free(out);
    return s;
}

/* ── updateAllRead ────────────────────────────────────────────────────── */

static void wf_chat_updated_count_reset(wf_chat_updated_count *u) {
    if (!u) return;
    memset(u, 0, sizeof(*u));
}

wf_status wf_agent_parse_updated_count(const char *json, size_t json_len,
        wf_chat_updated_count *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;
    cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "updatedCount");
    if (cJSON_IsNumber(c)) {
        out->updated_count = (int)c->valuedouble;
        out->has_updated_count = 1;
    }
    cJSON_Delete(root);
    return WF_OK;
}

void wf_chat_updated_count_free(wf_chat_updated_count *u) {
    if (!u) return;
    wf_chat_updated_count_reset(u);
}

wf_status wf_agent_chat_update_all_read_typed(wf_agent *agent,
        const char *convo_id, wf_chat_updated_count *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "status", convo_id)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) { free(json); return WF_ERR_INVALID_ARG; }
    wf_agent_sync_chat_auth(agent);
    wf_response res = {0};
    wf_status s = wf_xrpc_procedure(cc, "chat.bsky.convo.updateAllRead",
                                    json, &res);
    free(json);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_updated_count(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_updated_count_free(out);
    return s;
}

/* ── addReaction / removeReaction ─────────────────────────────────────── */

wf_status wf_agent_parse_message_ref(const char *json, size_t json_len,
        wf_chat_message *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;
    cJSON *m = cJSON_GetObjectItemCaseSensitive(root, "message");
    if (!cJSON_IsObject(m)) { cJSON_Delete(root); return WF_ERR_PARSE; }
    wf_status status = wf_chat_parse_message(out, m);
    if (status != WF_OK) wf_chat_message_reset(out);
    cJSON_Delete(root);
    return status;
}

wf_status wf_agent_chat_add_reaction_typed(wf_agent *agent,
        const char *convo_id, const char *message_id, const char *value,
        wf_chat_message *out) {
    if (!agent || !convo_id || !message_id || !value || !out)
        return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    char *json = NULL;
    wf_status s = wf_chat_build_add_reaction_body(convo_id, message_id, value,
                                                  &json);
    if (s != WF_OK) return s;
    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) { free(json); return WF_ERR_INVALID_ARG; }
    wf_agent_sync_chat_auth(agent);
    wf_response res = {0};
    s = wf_xrpc_procedure(cc, "chat.bsky.convo.addReaction", json, &res);
    free(json);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_message_ref(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_message_reset(out);
    return s;
}

wf_status wf_agent_chat_remove_reaction_typed(wf_agent *agent,
        const char *convo_id, const char *message_id, const char *value,
        wf_chat_message *out) {
    if (!agent || !convo_id || !message_id || !value || !out)
        return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    /* removeReaction shares the {convoId, messageId, value} body shape. */
    char *json = NULL;
    wf_status s = wf_chat_build_add_reaction_body(convo_id, message_id, value,
                                                  &json);
    if (s != WF_OK) return s;
    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) { free(json); return WF_ERR_INVALID_ARG; }
    wf_agent_sync_chat_auth(agent);
    wf_response res = {0};
    s = wf_xrpc_procedure(cc, "chat.bsky.convo.removeReaction", json, &res);
    free(json);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_message_ref(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_message_reset(out);
    return s;
}

/* ── convoView-returning endpoints (reuse wf_agent_parse_convo) ─────────── */

/* Helper: parse a raw response's body into a convoView-owned struct. */
static wf_status wf_chat_typed_parse_convo(wf_response *res, wf_chat_convo *out) {
    wf_status s = wf_agent_parse_convo(res->body, res->body_len, out);
    if (s != WF_OK) wf_chat_convo_free(out);
    return s;
}

wf_status wf_agent_chat_get_convo_for_members_typed(wf_agent *agent,
        const char *const *member_dids, size_t member_count,
        wf_chat_convo *out) {
    if (!agent || !member_dids || member_count == 0 || !out)
        return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_get_convo_for_members(agent, member_dids,
                                                      member_count, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_chat_typed_parse_convo(&res, out);
    wf_response_free(&res);
    return s;
}

/* Fire-and-forget convoView procedures: build the {convoId} body (reusing the
 * accept-convo body builder) and parse the returned convoView. */
static wf_status wf_chat_typed_convo_procedure(wf_agent *agent,
        const char *nsid, const char *convo_id, wf_chat_convo *out) {
    char *json = NULL;
    wf_status s = wf_chat_build_accept_convo_body(convo_id, &json);
    if (s != WF_OK) return s;
    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) { free(json); return WF_ERR_INVALID_ARG; }
    wf_agent_sync_chat_auth(agent);
    wf_response res = {0};
    s = wf_xrpc_procedure(cc, nsid, json, &res);
    free(json);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_chat_typed_parse_convo(&res, out);
    wf_response_free(&res);
    return s;
}

wf_status wf_agent_chat_accept_convo_typed(wf_agent *agent,
        const char *convo_id, wf_chat_convo *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    return wf_chat_typed_convo_procedure(agent, "chat.bsky.convo.acceptConvo",
                                         convo_id, out);
}

wf_status wf_agent_chat_mute_convo_typed(wf_agent *agent,
        const char *convo_id, wf_chat_convo *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    return wf_chat_typed_convo_procedure(agent, "chat.bsky.convo.muteConvo",
                                         convo_id, out);
}

wf_status wf_agent_chat_unmute_convo_typed(wf_agent *agent,
        const char *convo_id, wf_chat_convo *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    return wf_chat_typed_convo_procedure(agent, "chat.bsky.convo.unmuteConvo",
                                         convo_id, out);
}

wf_status wf_agent_chat_lock_convo_typed(wf_agent *agent,
        const char *convo_id, wf_chat_convo *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    return wf_chat_typed_convo_procedure(agent, "chat.bsky.convo.lockConvo",
                                         convo_id, out);
}

wf_status wf_agent_chat_unlock_convo_typed(wf_agent *agent,
        const char *convo_id, wf_chat_convo *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    return wf_chat_typed_convo_procedure(agent, "chat.bsky.convo.unlockConvo",
                                         convo_id, out);
}

wf_status wf_agent_chat_update_read_typed(wf_agent *agent,
        const char *convo_id, const char *message_id, wf_chat_convo *out) {
    if (!agent || !convo_id || !message_id || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

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

    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) { free(json); return WF_ERR_INVALID_ARG; }
    wf_agent_sync_chat_auth(agent);
    wf_response res = {0};
    wf_status s = wf_xrpc_procedure(cc, "chat.bsky.convo.updateRead", json, &res);
    free(json);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_chat_typed_parse_convo(&res, out);
    wf_response_free(&res);
    return s;
}

/* Group convoView-returning procedures delegate to the raw wrappers. */
wf_status wf_agent_chat_create_group_typed(wf_agent *agent,
        const char *const *member_dids, size_t member_count, const char *name,
        wf_chat_convo *out) {
    if (!agent || !member_dids || member_count == 0 || !name || !out)
        return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_create_group(agent, member_dids, member_count,
                                             name, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_chat_typed_parse_convo(&res, out);
    wf_response_free(&res);
    return s;
}

wf_status wf_agent_chat_edit_group_typed(wf_agent *agent, const char *convo_id,
        const char *name, wf_chat_convo *out) {
    if (!agent || !convo_id || !name || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_edit_group(agent, convo_id, name, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_chat_typed_parse_convo(&res, out);
    wf_response_free(&res);
    return s;
}

wf_status wf_agent_chat_add_members_typed(wf_agent *agent, const char *convo_id,
        const char *const *member_dids, size_t member_count,
        wf_chat_convo *out) {
    if (!agent || !convo_id || !member_dids || member_count == 0 || !out)
        return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_add_members(agent, convo_id, member_dids,
                                            member_count, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_chat_typed_parse_convo(&res, out);
    wf_response_free(&res);
    return s;
}

wf_status wf_agent_chat_remove_members_typed(wf_agent *agent,
        const char *convo_id, const char *const *member_dids,
        size_t member_count, wf_chat_convo *out) {
    if (!agent || !convo_id || !member_dids || member_count == 0 || !out)
        return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_remove_members(agent, convo_id, member_dids,
                                               member_count, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_chat_typed_parse_convo(&res, out);
    wf_response_free(&res);
    return s;
}

wf_status wf_agent_chat_approve_join_request_typed(wf_agent *agent,
        const char *convo_id, const char *user_did, wf_chat_convo *out) {
    if (!agent || !convo_id || !user_did || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_approve_join_request(agent, convo_id, user_did,
                                                     &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_chat_typed_parse_convo(&res, out);
    wf_response_free(&res);
    return s;
}

/* ── group join links ─────────────────────────────────────────────────── */

static void wf_chat_join_link_reset(wf_chat_join_link *j) {
    if (!j) return;
    free(j->code);
    free(j->enabled_status);
    free(j->join_rule);
    free(j->created_at);
    memset(j, 0, sizeof(*j));
}

wf_status wf_agent_parse_join_link(const char *json, size_t json_len,
        wf_chat_join_link *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;

    wf_status status = WF_OK;
    cJSON *jl = cJSON_GetObjectItemCaseSensitive(root, "joinLink");
    if (!cJSON_IsObject(jl)) { cJSON_Delete(root); return WF_ERR_PARSE; }

    cJSON *code = cJSON_GetObjectItemCaseSensitive(jl, "code");
    cJSON *es = cJSON_GetObjectItemCaseSensitive(jl, "enabledStatus");
    cJSON *ra = cJSON_GetObjectItemCaseSensitive(jl, "requireApproval");
    cJSON *jr = cJSON_GetObjectItemCaseSensitive(jl, "joinRule");
    cJSON *ca = cJSON_GetObjectItemCaseSensitive(jl, "createdAt");
    if (cJSON_IsString(code) && code->valuestring)
        status = wf_chat_set_string(&out->code, code->valuestring);
    if (status == WF_OK && cJSON_IsString(es) && es->valuestring)
        status = wf_chat_set_string(&out->enabled_status, es->valuestring);
    if (status == WF_OK && cJSON_IsBool(ra)) {
        out->require_approval = cJSON_IsTrue(ra);
        out->has_require_approval = 1;
    }
    if (status == WF_OK && cJSON_IsString(jr) && jr->valuestring)
        status = wf_chat_set_string(&out->join_rule, jr->valuestring);
    if (status == WF_OK && cJSON_IsString(ca) && ca->valuestring)
        status = wf_chat_set_string(&out->created_at, ca->valuestring);

    if (status != WF_OK) wf_chat_join_link_reset(out);
    cJSON_Delete(root);
    return status;
}

void wf_chat_join_link_free(wf_chat_join_link *j) {
    if (!j) return;
    wf_chat_join_link_reset(j);
}

/* Issue a fire-and-forget join-link procedure and parse its joinLink body. */
static wf_status wf_chat_join_link_typed_body(wf_agent *agent,
        const char *nsid, const char *convo_id, wf_chat_join_link *out) {
    char *json = NULL;
    wf_status s = wf_chat_build_accept_convo_body(convo_id, &json);
    if (s != WF_OK) return s;
    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) { free(json); return WF_ERR_INVALID_ARG; }
    wf_agent_sync_chat_auth(agent);
    wf_response res = {0};
    s = wf_xrpc_procedure(cc, nsid, json, &res);
    free(json);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_join_link(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_join_link_free(out);
    return s;
}

wf_status wf_agent_chat_create_join_link_typed(wf_agent *agent,
        const char *convo_id, bool require_approval, const char *join_rule,
        wf_chat_join_link *out) {
    if (!agent || !convo_id || !join_rule || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_create_join_link(agent, convo_id,
                                                 require_approval, join_rule,
                                                 &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_join_link(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_join_link_free(out);
    return s;
}

wf_status wf_agent_chat_edit_join_link_typed(wf_agent *agent,
        const char *convo_id, const char *code, wf_chat_join_link *out) {
    if (!agent || !convo_id || !code || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_edit_join_link(agent, convo_id, code, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_join_link(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_join_link_free(out);
    return s;
}

wf_status wf_agent_chat_disable_join_link_typed(wf_agent *agent,
        const char *convo_id, wf_chat_join_link *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    return wf_chat_join_link_typed_body(agent, "chat.bsky.group.disableJoinLink",
                                        convo_id, out);
}

wf_status wf_agent_chat_enable_join_link_typed(wf_agent *agent,
        const char *convo_id, wf_chat_join_link *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    return wf_chat_join_link_typed_body(agent, "chat.bsky.group.enableJoinLink",
                                        convo_id, out);
}

/* ── group getJoinLinkPreviews ────────────────────────────────────────── */

static void wf_chat_join_link_preview_reset(wf_chat_join_link_preview *p) {
    if (!p) return;
    free(p->code);
    free(p->name);
    free(p->join_rule);
    free(p->kind);
    memset(p, 0, sizeof(*p));
}

static void wf_chat_join_link_previews_reset(wf_chat_join_link_previews *l) {
    if (!l) return;
    for (size_t i = 0; i < l->item_count; ++i)
        wf_chat_join_link_preview_reset(&l->items[i]);
    free(l->items);
    memset(l, 0, sizeof(*l));
}

wf_status wf_agent_parse_join_link_previews(const char *json, size_t json_len,
        wf_chat_join_link_previews *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "joinLinkPreviews");
    if (!cJSON_IsArray(arr)) { cJSON_Delete(root); return WF_ERR_PARSE; }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_chat_join_link_preview *items = NULL;
    if (count > 0) {
        items = (wf_chat_join_link_preview *)calloc(count, sizeof(*items));
        if (!items) { cJSON_Delete(root); return WF_ERR_ALLOC; }
    }
    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *o = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(o)) { status = WF_ERR_PARSE; break; }
        cJSON *tp = cJSON_GetObjectItemCaseSensitive(o, "$type");
        cJSON *code = cJSON_GetObjectItemCaseSensitive(o, "code");
        cJSON *name = cJSON_GetObjectItemCaseSensitive(o, "name");
        cJSON *jr = cJSON_GetObjectItemCaseSensitive(o, "joinRule");
        cJSON *ra = cJSON_GetObjectItemCaseSensitive(o, "requireApproval");
        if (cJSON_IsString(tp) && tp->valuestring)
            status = wf_chat_set_string(&items[i].kind, tp->valuestring);
        if (status == WF_OK && cJSON_IsString(code) && code->valuestring)
            status = wf_chat_set_string(&items[i].code, code->valuestring);
        if (status == WF_OK && cJSON_IsString(name) && name->valuestring)
            status = wf_chat_set_string(&items[i].name, name->valuestring);
        if (status == WF_OK && cJSON_IsString(jr) && jr->valuestring)
            status = wf_chat_set_string(&items[i].join_rule, jr->valuestring);
        if (status == WF_OK && cJSON_IsBool(ra)) {
            items[i].require_approval = cJSON_IsTrue(ra);
            items[i].has_require_approval = 1;
        }
        if (status != WF_OK) wf_chat_join_link_preview_reset(&items[i]);
    }
    if (status == WF_OK) {
        out->items = items;
        out->item_count = count;
    }
    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i)
            wf_chat_join_link_preview_reset(&items[i]);
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

void wf_chat_join_link_previews_free(wf_chat_join_link_previews *l) {
    if (!l) return;
    wf_chat_join_link_previews_reset(l);
}

wf_status wf_agent_chat_get_join_link_previews_typed(wf_agent *agent,
        const char *const *codes, size_t code_count,
        wf_chat_join_link_previews *out) {
    if (!agent || !codes || code_count == 0 || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_get_join_link_previews(agent, codes, code_count,
                                                       &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_join_link_previews(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_join_link_previews_free(out);
    return s;
}

/* ── group listJoinRequests ───────────────────────────────────────────── */

static void wf_chat_join_request_reset(wf_chat_join_request *r) {
    if (!r) return;
    free(r->convo_id);
    free(r->requested_at);
    wf_chat_profile_reset(&r->requested_by);
    memset(r, 0, sizeof(*r));
}

static void wf_chat_join_requests_reset(wf_chat_join_requests *r) {
    if (!r) return;
    for (size_t i = 0; i < r->item_count; ++i)
        wf_chat_join_request_reset(&r->items[i]);
    free(r->items);
    free(r->cursor);
    memset(r, 0, sizeof(*r));
}

wf_status wf_agent_parse_join_requests(const char *json, size_t json_len,
        wf_chat_join_requests *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "requests");
    if (!cJSON_IsArray(arr)) { cJSON_Delete(root); return WF_ERR_PARSE; }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_chat_join_request *items = NULL;
    if (count > 0) {
        items = (wf_chat_join_request *)calloc(count, sizeof(*items));
        if (!items) { cJSON_Delete(root); return WF_ERR_ALLOC; }
    }
    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *o = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(o)) { status = WF_ERR_PARSE; break; }
        cJSON *cid = cJSON_GetObjectItemCaseSensitive(o, "convoId");
        cJSON *by = cJSON_GetObjectItemCaseSensitive(o, "requestedBy");
        cJSON *at = cJSON_GetObjectItemCaseSensitive(o, "requestedAt");
        if (cJSON_IsString(cid) && cid->valuestring)
            status = wf_chat_set_string(&items[i].convo_id, cid->valuestring);
        if (status == WF_OK && cJSON_IsObject(by))
            status = wf_chat_parse_profile(&items[i].requested_by, by);
        if (status == WF_OK && cJSON_IsString(at) && at->valuestring)
            status = wf_chat_set_string(&items[i].requested_at, at->valuestring);
        if (status != WF_OK) wf_chat_join_request_reset(&items[i]);
    }
    if (status == WF_OK) {
        out->items = items;
        out->item_count = count;
        cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cur) && cur->valuestring)
            status = wf_chat_set_string(&out->cursor, cur->valuestring);
    }
    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) wf_chat_join_request_reset(&items[i]);
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

void wf_chat_join_requests_free(wf_chat_join_requests *r) {
    if (!r) return;
    wf_chat_join_requests_reset(r);
}

wf_status wf_agent_chat_list_join_requests_typed(wf_agent *agent,
        const char *convo_id, int limit, const char *cursor,
        wf_chat_join_requests *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_list_join_requests(agent, convo_id, limit,
                                                   cursor, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_join_requests(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_join_requests_free(out);
    return s;
}

/* ── group requestJoin ────────────────────────────────────────────────── */

static void wf_chat_request_join_reset(wf_chat_request_join *r) {
    if (!r) return;
    free(r->status);
    wf_chat_convo_reset(&r->convo);
    memset(r, 0, sizeof(*r));
}

wf_status wf_agent_parse_request_join(const char *json, size_t json_len,
        wf_chat_request_join *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;

    wf_status status = WF_OK;
    cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (cJSON_IsString(st) && st->valuestring)
        status = wf_chat_set_string(&out->status, st->valuestring);
    cJSON *convo = cJSON_GetObjectItemCaseSensitive(root, "convo");
    if (status == WF_OK && cJSON_IsObject(convo)) {
        out->has_convo = 1;
        status = wf_chat_parse_convo_view(&out->convo, convo);
    }
    if (status != WF_OK) wf_chat_request_join_reset(out);
    cJSON_Delete(root);
    return status;
}

void wf_chat_request_join_free(wf_chat_request_join *r) {
    if (!r) return;
    wf_chat_request_join_reset(r);
}

wf_status wf_agent_chat_request_join_typed(wf_agent *agent,
        const char *convo_id, wf_chat_request_join *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    /* requestJoin takes a `code`; the raw wrapper passes it under `convoId`,
     * so mirror that exactly to keep behaviour identical. */
    char *json = NULL;
    wf_status s = wf_chat_build_accept_convo_body(convo_id, &json);
    if (s != WF_OK) return s;
    wf_xrpc_client *cc = wf_agent_chat_client(agent);
    if (!cc) { free(json); return WF_ERR_INVALID_ARG; }
    wf_agent_sync_chat_auth(agent);
    wf_response res = {0};
    s = wf_xrpc_procedure(cc, "chat.bsky.group.requestJoin", json, &res);
    free(json);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_request_join(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_request_join_free(out);
    return s;
}

/* ── moderation.getActorMetadata ──────────────────────────────────────── */

static void wf_chat_actor_metadata_period_reset(wf_chat_actor_metadata_period *p) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
}

static void wf_chat_actor_metadata_reset(wf_chat_actor_metadata *m) {
    if (!m) return;
    wf_chat_actor_metadata_period_reset(&m->day);
    wf_chat_actor_metadata_period_reset(&m->month);
    wf_chat_actor_metadata_period_reset(&m->all);
}

static wf_status wf_chat_parse_metadata_period(cJSON *obj,
        wf_chat_actor_metadata_period *out) {
    if (!cJSON_IsObject(obj)) return WF_ERR_PARSE;
    cJSON *ms = cJSON_GetObjectItemCaseSensitive(obj, "messagesSent");
    cJSON *mr = cJSON_GetObjectItemCaseSensitive(obj, "messagesReceived");
    cJSON *cv = cJSON_GetObjectItemCaseSensitive(obj, "convos");
    cJSON *cs = cJSON_GetObjectItemCaseSensitive(obj, "convosStarted");
    if (cJSON_IsNumber(ms)) { out->messages_sent = (int)ms->valuedouble; out->has_messages_sent = 1; }
    if (cJSON_IsNumber(mr)) { out->messages_received = (int)mr->valuedouble; out->has_messages_received = 1; }
    if (cJSON_IsNumber(cv)) { out->convos = (int)cv->valuedouble; out->has_convos = 1; }
    if (cJSON_IsNumber(cs)) { out->convos_started = (int)cs->valuedouble; out->has_convos_started = 1; }
    return WF_OK;
}

wf_status wf_agent_parse_actor_metadata(const char *json, size_t json_len,
        wf_chat_actor_metadata *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;

    wf_status status = WF_OK;
    cJSON *day = cJSON_GetObjectItemCaseSensitive(root, "day");
    cJSON *month = cJSON_GetObjectItemCaseSensitive(root, "month");
    cJSON *all = cJSON_GetObjectItemCaseSensitive(root, "all");
    if (cJSON_IsObject(day)) status = wf_chat_parse_metadata_period(day, &out->day);
    if (status == WF_OK && cJSON_IsObject(month))
        status = wf_chat_parse_metadata_period(month, &out->month);
    if (status == WF_OK && cJSON_IsObject(all))
        status = wf_chat_parse_metadata_period(all, &out->all);

    if (status != WF_OK) wf_chat_actor_metadata_reset(out);
    cJSON_Delete(root);
    return status;
}

void wf_chat_actor_metadata_free(wf_chat_actor_metadata *m) {
    if (!m) return;
    wf_chat_actor_metadata_reset(m);
}

wf_status wf_agent_chat_mod_get_actor_metadata_typed(wf_agent *agent,
        const char *actor_did, wf_chat_actor_metadata *out) {
    if (!agent || !actor_did || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_mod_get_actor_metadata(agent, actor_did, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_actor_metadata(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_actor_metadata_free(out);
    return s;
}

/* ── moderation.getConvo(s) ───────────────────────────────────────────── */

static void wf_chat_mod_convo_reset(wf_chat_mod_convo *c) {
    if (!c) return;
    free(c->id);
    free(c->rev);
    free(c->type);
    memset(c, 0, sizeof(*c));
}

static void wf_chat_mod_convo_list_reset(wf_chat_mod_convo_list *l) {
    if (!l) return;
    for (size_t i = 0; i < l->convo_count; ++i)
        wf_chat_mod_convo_reset(&l->convos[i]);
    free(l->convos);
    memset(l, 0, sizeof(*l));
}

static wf_status wf_chat_parse_mod_convo_obj(cJSON *obj, wf_chat_mod_convo *out) {
    wf_status status = WF_OK;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "id");
    cJSON *rev = cJSON_GetObjectItemCaseSensitive(obj, "rev");
    cJSON *kind = cJSON_GetObjectItemCaseSensitive(obj, "kind");
    if (cJSON_IsString(id) && id->valuestring)
        status = wf_chat_set_string(&out->id, id->valuestring);
    if (status == WF_OK && cJSON_IsString(rev) && rev->valuestring)
        status = wf_chat_set_string(&out->rev, rev->valuestring);
    if (status == WF_OK && cJSON_IsObject(kind)) {
        cJSON *tp = cJSON_GetObjectItemCaseSensitive(kind, "$type");
        if (cJSON_IsString(tp) && tp->valuestring)
            status = wf_chat_set_string(&out->type, tp->valuestring);
    }
    return status;
}

wf_status wf_agent_parse_mod_convo(const char *json, size_t json_len,
        wf_chat_mod_convo *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;
    wf_status status = wf_chat_parse_mod_convo_obj(root, out);
    if (status != WF_OK) wf_chat_mod_convo_reset(out);
    cJSON_Delete(root);
    return status;
}

void wf_chat_mod_convo_free(wf_chat_mod_convo *c) {
    if (!c) return;
    wf_chat_mod_convo_reset(c);
}

wf_status wf_agent_parse_mod_convos(const char *json, size_t json_len,
        wf_chat_mod_convo_list *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "convos");
    if (!cJSON_IsArray(arr)) { cJSON_Delete(root); return WF_ERR_PARSE; }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_chat_mod_convo *items = NULL;
    if (count > 0) {
        items = (wf_chat_mod_convo *)calloc(count, sizeof(*items));
        if (!items) { cJSON_Delete(root); return WF_ERR_ALLOC; }
    }
    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        status = wf_chat_parse_mod_convo_obj(cJSON_GetArrayItem(arr, (int)i),
                                             &items[i]);
        if (status != WF_OK) wf_chat_mod_convo_reset(&items[i]);
    }
    if (status == WF_OK) {
        out->convos = items;
        out->convo_count = count;
    }
    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) wf_chat_mod_convo_reset(&items[i]);
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

void wf_chat_mod_convo_list_free(wf_chat_mod_convo_list *l) {
    if (!l) return;
    wf_chat_mod_convo_list_reset(l);
}

wf_status wf_agent_chat_mod_get_convo_typed(wf_agent *agent,
        const char *convo_id, wf_chat_mod_convo *out) {
    if (!agent || !convo_id || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_mod_get_convo(agent, convo_id, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_mod_convo(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_mod_convo_free(out);
    return s;
}

wf_status wf_agent_chat_mod_get_message_context_typed(wf_agent *agent,
        const char *message_id, const char *convo_id, int before, int after,
        int max_interleaved, wf_chat_message_list *out) {
    if (!agent || !message_id || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_mod_get_message_context(agent, message_id,
                                                        convo_id, before, after,
                                                        max_interleaved, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_messages(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_message_list_free(out);
    return s;
}

/* ── actor.getStatus ──────────────────────────────────────────────────── */

static void wf_chat_actor_status_reset(wf_chat_actor_status *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
}

wf_status wf_agent_parse_actor_status(const char *json, size_t json_len,
        wf_chat_actor_status *out) {
    if (!json || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) return WF_ERR_PARSE;

    cJSON *cd = cJSON_GetObjectItemCaseSensitive(root, "chatDisabled");
    if (cJSON_IsBool(cd)) { out->chat_disabled = cJSON_IsTrue(cd); out->has_chat_disabled = 1; }
    cJSON *cg = cJSON_GetObjectItemCaseSensitive(root, "canCreateGroups");
    if (cJSON_IsBool(cg)) { out->can_create_groups = cJSON_IsTrue(cg); out->has_can_create_groups = 1; }
    cJSON *gl = cJSON_GetObjectItemCaseSensitive(root, "groupMemberLimit");
    if (cJSON_IsNumber(gl)) { out->group_member_limit = (int64_t)gl->valuedouble; out->has_group_member_limit = 1; }
    cJSON_Delete(root);
    return WF_OK;
}

void wf_chat_actor_status_free(wf_chat_actor_status *s) {
    if (!s) return;
    wf_chat_actor_status_reset(s);
}

wf_status wf_agent_chat_get_status_typed(wf_agent *agent,
        wf_chat_actor_status *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_response res = {0};
    wf_status s = wf_agent_chat_get_status(agent, &res);
    if (s != WF_OK) { wf_response_free(&res); return s; }
    s = wf_agent_parse_actor_status(res.body, res.body_len, out);
    wf_response_free(&res);
    if (s != WF_OK) wf_chat_actor_status_free(out);
    return s;
}
