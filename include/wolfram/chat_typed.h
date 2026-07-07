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
 * Bluesky chat service, NOT the user's main PDS. The agent wrappers below issue
 * calls through `agent->client` (the agent's XRPC client); it is the caller's
 * responsibility to point that client at the correct chat service URL (e.g. by
 * setting the agent service URL) before invoking these wrappers in production.
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

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_CHAT_TYPED_H */
