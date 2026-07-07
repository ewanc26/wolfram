/*
 * chat_typed.h — owned typed parsers for chat.bsky.convo responses.
 *
 * Parses the raw JSON bodies returned by `chat.bsky.convo.listConvos`,
 * `chat.bsky.convo.getConvo`, and `chat.bsky.convo.getMessages` into owned
 * C structs. `members` in a conversation is stored as a `wf_agent_profile_view`
 * array (reusing the type from agent.h, which mirrors
 * chat.bsky.actor.defs#profileViewBasic). Free the lists with the matching
 * `*_free` function.
 *
 * NOTE ON SERVICE ENDPOINT: chat.bsky.convo endpoints are served by a separate
 * Bluesky chat service, NOT the user's main PDS. The agent wrappers below
 * lazily resolve the chat service URL (via wf_agent_chat_service_resolve) and
 * issue every call through a dedicated `agent->chat_client`, so callers do not
 * need to configure anything manually. When the server does not advertise a
 * chat service, the wrappers fall back to WF_CHAT_DEFAULT_ENDPOINT.
 *
 * Conventions mirror feed_typed.c / notification.c:
 *   - `wf_status` error codes (WF_ERR_INVALID_ARG, WF_ERR_PARSE, WF_ERR_ALLOC,
 *     WF_OK)
 *   - static strdup/set_string/reset helpers local to the translation unit
 *   - ownership via plain string copies; full reset-on-error
 */

#ifndef WOLFRAM_CHAT_TYPED_H
#define WOLFRAM_CHAT_TYPED_H

#include "wolfram/agent.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Default Bluesky chat service endpoint, used as a fallback when the user's
 * PDS does not advertise a chat service via com.atproto.server.describeServer.
 * Mirrors the rsky reference hardcode (rsky-common/src/lib.rs:204). */
#define WF_CHAT_DEFAULT_ENDPOINT "https://api.bsky.chat"

/* A single conversation (chat.bsky.convo.defs#convoView, bounded). */
typedef struct wf_chat_convo {
    char *id;                          /* convo id (required) */
    char *rev;                         /* revision (required) */
    char *type;                        /* optional kind union tag; NULL if absent */
    wf_agent_profile_view *members;    /* array of member profile views */
    size_t member_count;
    char *last_message_text;           /* optional text of last message; NULL if absent */
    int unread_count;                  /* required unreadCount */
    int has_unread_count;
    char *cursor;                      /* optional; NULL when absent */
} wf_chat_convo;

/* A single message (chat.bsky.convo.defs#messageView, bounded). */
typedef struct wf_chat_message {
    char *id;                          /* message id (required) */
    char *rev;                         /* revision (required) */
    char *text;                        /* message text (required) */
    char *sender;                      /* sender actor did/handle (required) */
    char *sent_at;                     /* RFC 3339 datetime (required) */
} wf_chat_message;

/* A parsed listConvos page. */
typedef struct wf_chat_convo_list {
    wf_chat_convo *convos;
    size_t convo_count;
    char *cursor;                      /* optional; NULL when absent */
} wf_chat_convo_list;

/* A parsed getMessages page. */
typedef struct wf_chat_message_list {
    wf_chat_message *messages;
    size_t message_count;
    char *cursor;                      /* optional; NULL when absent */
} wf_chat_message_list;

/* Parse a raw listConvos JSON body into owned structs.
 * Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or
 * a missing `convos` array, WF_ERR_ALLOC on allocation failure, WF_OK on
 * success. On any error `out` is left fully reset (no partial leaks). */
wf_status wf_agent_parse_convos(const char *json, size_t json_len,
                                wf_chat_convo_list *out);

/* Free a parsed convo list and every owned subtree it holds. */
void wf_chat_convo_list_free(wf_chat_convo_list *list);

/* Free a single parsed convo (as returned by wf_agent_parse_convo). */
void wf_chat_convo_free(wf_chat_convo *c);

/* Parse a raw getConvo JSON body into a single owned convo (the `convo` object,
 * which embeds a `messages` array on the wire — that array is NOT retained by
 * this bounded parser). Same ownership/error rules as wf_agent_parse_convos. */
wf_status wf_agent_parse_convo(const char *json, size_t json_len,
                               wf_chat_convo *out);

/* Parse a raw getMessages JSON body into owned structs.
 * Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or
 * a missing `messages` array, WF_ERR_ALLOC on allocation failure, WF_OK on
 * success. On any error `out` is left fully reset (no partial leaks). */
wf_status wf_agent_parse_messages(const char *json, size_t json_len,
                                  wf_chat_message_list *out);

/* Free a parsed message list and every owned subtree it holds. */
void wf_chat_message_list_free(wf_chat_message_list *list);

/* Parse a raw chat.bsky.convo.defs#messageView JSON body into an owned
 * wf_chat_message. Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on
 * malformed JSON, WF_ERR_ALLOC on allocation failure, WF_OK on success. On any
 * error `out` is left fully reset (no partial leaks). */
wf_status wf_agent_parse_message(const char *json, size_t json_len,
                                 wf_chat_message *out);

/* Reset (free all owned strings and zero) a wf_chat_message. Safe to call with
 * NULL or on an already-reset message. Used to release a single message as
 * returned by wf_agent_parse_message / wf_agent_chat_send_message. */
void wf_chat_message_reset(wf_chat_message *m);

/* Typed high-level wrappers — issue the corresponding agent XRPC call and
 * parse the JSON body into `out`. On success `out` is owned by the caller and
 * must be freed with the matching `*_free`; on error it is left reset.
 *
 * These use the agent's XRPC client directly (see the service-endpoint note in
 * this header's top comment). */
wf_status wf_agent_chat_list_convos(wf_agent *agent, int limit,
                                    const char *cursor,
                                    wf_chat_convo_list *out);
wf_status wf_agent_chat_get_convo(wf_agent *agent, const char *convo_id,
                                  wf_chat_convo *out);
wf_status wf_agent_chat_get_messages(wf_agent *agent, const char *convo_id,
                                     int limit, const char *cursor,
                                     wf_chat_message_list *out);

/* Send a message to a conversation.
 *
 * Builds the `chat.bsky.convo.sendMessage` request body `{ "convoId",
 * "message": { "text", "$type": "chat.bsky.convo.defs#messageInput" } }`. If
 * `facets_json` is non-NULL and non-empty it is parsed as a JSON array and
 * attached as `message.facets`. The call is routed through the resolved chat
 * service client (see wf_agent_chat_service_resolve). On success `out` holds
 * the resulting messageView and must be freed with wf_chat_message_reset-like
 * cleanup (the message struct is a plain value; free its owned strings via
 * wf_chat_message_list_free on a list, or reset it directly). On error `out`
 * is left fully reset. Returns WF_ERR_INVALID_ARG on NULL required inputs. */
wf_status wf_agent_chat_send_message(wf_agent *agent, const char *convo_id,
                                     const char *text, const char *facets_json,
                                     wf_chat_message *out);

/* Resolve (lazily) the Bluesky chat service endpoint and assign it to
 * `agent->chat_client`.
 *
 * Strategy:
 *   1. If `agent->chat_client` is already set, return WF_OK immediately.
 *   2. Query `com.atproto.server.describeServer` on the user's PDS.
 *   3. If the response advertises a `chat` DID, resolve that DID to its chat
 *      service endpoint (type `BskyChatService` / `AtprotoChatProxy`).
 *   4. Otherwise (no `chat` field, or resolution fails) fall back to
 *      WF_CHAT_DEFAULT_ENDPOINT.
 *
 * Returns WF_OK with `agent->chat_client` ready on success, or a `wf_status`
 * error if the underlying XRPC call fails irrecoverably. */
wf_status wf_agent_chat_service_resolve(wf_agent *agent);

/* Extract the chat service DID from a `com.atproto.server.describeServer`
 * response body. On WF_OK, `*out_did` is either:
 *   - heap-allocated DID string (caller frees with free()) when the server
 *     advertised a `chat` service, or
 *   - NULL when no `chat` field was present (caller should fall back to the
 *     default endpoint).
 * Returns WF_ERR_INVALID_ARG on NULL inputs and WF_ERR_PARSE on malformed JSON.
 * Pure/offline: performs no network I/O. */
wf_status wf_agent_chat_service_did_from_describe(const char *json,
                                                  size_t json_len,
                                                  char **out_did);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_CHAT_TYPED_H */
