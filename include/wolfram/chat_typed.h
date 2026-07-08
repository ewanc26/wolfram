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
#include <stdbool.h>

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

/* Extended conversation operations — wrappers for the remaining
 * chat.bsky.convo.* endpoints. Unless noted, these route through the resolved
 * chat service client (same as listConvos/getConvo/etc.). */

/* Accept an incoming conversation request. */
wf_status wf_agent_chat_accept_convo(wf_agent *agent, const char *convo_id);

/* Leave a conversation. */
wf_status wf_agent_chat_leave_convo(wf_agent *agent, const char *convo_id);

/* Mute or unmute a conversation. */
wf_status wf_agent_chat_mute_convo(wf_agent *agent, const char *convo_id);
wf_status wf_agent_chat_unmute_convo(wf_agent *agent, const char *convo_id);

/* Delete a message for yourself only. */
wf_status wf_agent_chat_delete_message_for_self(wf_agent *agent,
                                                  const char *convo_id,
                                                  const char *message_id);

/* Add or remove a reaction on a message. `value` is the reaction emoji/text. */
wf_status wf_agent_chat_add_reaction(wf_agent *agent,
                                      const char *convo_id,
                                      const char *message_id,
                                      const char *value);
wf_status wf_agent_chat_remove_reaction(wf_agent *agent,
                                         const char *convo_id,
                                         const char *message_id,
                                         const char *value);

/* Mark a specific message as read, or mark the entire conversation as read. */
wf_status wf_agent_chat_update_read(wf_agent *agent,
                                     const char *convo_id,
                                     const char *message_id);
wf_status wf_agent_chat_update_all_read(wf_agent *agent,
                                         const char *convo_id);

/* Query endpoints — return raw JSON in `out`; caller frees with
 * wf_response_free. */

/* Check whether the given DIDs are available for chat. Returns the raw
 * getConvoAvailability response. */
wf_status wf_agent_chat_get_convo_availability(wf_agent *agent,
                                                const char *const *member_dids,
                                                size_t member_count,
                                                wf_response *out);

/* Get or create a conversation for the given member DIDs. Returns raw JSON. */
wf_status wf_agent_chat_get_convo_for_members(wf_agent *agent,
                                               const char *const *member_dids,
                                               size_t member_count,
                                               wf_response *out);

/* Get the members of a conversation. Returns raw JSON. */
wf_status wf_agent_chat_get_convo_members(wf_agent *agent,
                                           const char *convo_id,
                                           wf_response *out);

/* Get unread conversation counts. Returns raw JSON. */
wf_status wf_agent_chat_get_unread_counts(wf_agent *agent,
                                           wf_response *out);

/* List pending conversation requests (incoming). Returns raw JSON. */
wf_status wf_agent_chat_list_convo_requests(wf_agent *agent, int limit,
                                              const char *cursor,
                                              wf_response *out);

/* ════════════════════════════════════════════════════════════════════════
 * chat.bsky.group.*
 *
 * All endpoints route through the resolved chat service client (same as
 * chat.bsky.convo.*). Queries return raw JSON in `out`; caller frees with
 * wf_response_free. Procedures with no meaningful return body set `out` to
 * the server response (caller should still call wf_response_free).
 * ════════════════════════════════════════════════════════════════════════ */

/* Create a group conversation. `member_dids` must be non-empty (max 49). */
wf_status wf_agent_chat_create_group(wf_agent *agent,
                                      const char *const *member_dids,
                                      size_t member_count,
                                      const char *name,
                                      wf_response *out);

/* Edit a group's name. Returns the updated convo view in `out`. */
wf_status wf_agent_chat_edit_group(wf_agent *agent, const char *convo_id,
                                    const char *name, wf_response *out);

/* Add or remove members from a group. Members are added in 'request' status. */
wf_status wf_agent_chat_add_members(wf_agent *agent, const char *convo_id,
                                     const char *const *member_dids,
                                     size_t member_count,
                                     wf_response *out);
wf_status wf_agent_chat_remove_members(wf_agent *agent, const char *convo_id,
                                        const char *const *member_dids,
                                        size_t member_count,
                                        wf_response *out);

/* Request to join a group conversation. */
wf_status wf_agent_chat_request_join(wf_agent *agent, const char *convo_id);

/* Withdraw a pending join request. */
wf_status wf_agent_chat_withdraw_join_request(wf_agent *agent,
                                               const char *convo_id);

/* Approve or reject a user's request to join a group. */
wf_status wf_agent_chat_approve_join_request(wf_agent *agent,
                                              const char *convo_id,
                                              const char *user_did,
                                              wf_response *out);
wf_status wf_agent_chat_reject_join_request(wf_agent *agent,
                                             const char *convo_id,
                                             const char *user_did,
                                             wf_response *out);

/* List join requests for a group (group owner only). */
wf_status wf_agent_chat_list_join_requests(wf_agent *agent,
                                            const char *convo_id,
                                            int limit, const char *cursor,
                                            wf_response *out);

/* Mark join requests as read. */
wf_status wf_agent_chat_update_join_requests_read(wf_agent *agent,
                                                    const char *convo_id);

/* Create a join link for a group. */
wf_status wf_agent_chat_create_join_link(wf_agent *agent,
                                          const char *convo_id,
                                          bool require_approval,
                                          const char *join_rule,
                                          wf_response *out);

/* Edit, disable, or enable a join link. editJoinLink returns the updated link. */
wf_status wf_agent_chat_edit_join_link(wf_agent *agent,
                                        const char *convo_id,
                                        const char *code,
                                        wf_response *out);
wf_status wf_agent_chat_disable_join_link(wf_agent *agent,
                                           const char *convo_id);
wf_status wf_agent_chat_enable_join_link(wf_agent *agent,
                                          const char *convo_id);

/* Get previews for one or more join link codes. */
wf_status wf_agent_chat_get_join_link_previews(wf_agent *agent,
                                                const char *const *codes,
                                                size_t code_count,
                                                wf_response *out);

/* List mutual groups between the authenticated user and another user. */
wf_status wf_agent_chat_list_mutual_groups(wf_agent *agent,
                                            const char *convo_id,
                                            int limit, const char *cursor,
                                            wf_response *out);

/* ════════════════════════════════════════════════════════════════════════
 * chat.bsky.actor.*
 *
 * Chat-account-level operations. All route through the resolved chat service.
 * ════════════════════════════════════════════════════════════════════════ */

/* Get the authenticated user's chat status. Returns raw JSON. */
wf_status wf_agent_chat_get_status(wf_agent *agent, wf_response *out);

/* Delete the authenticated user's chat account data. Returns raw JSON. */
wf_status wf_agent_chat_delete_account(wf_agent *agent, wf_response *out);

/* Export the authenticated user's chat account data (JSONL). Returns raw. */
wf_status wf_agent_chat_export_account_data(wf_agent *agent, wf_response *out);

/* ════════════════════════════════════════════════════════════════════════
 * chat.bsky.moderation.*
 *
 * Chat moderation endpoints for Ozone/moderator tooling. Route through the
 * resolved chat service client. Return raw JSON in `out`; caller frees with
 * wf_response_free.
 * ════════════════════════════════════════════════════════════════════════ */

/* Get chat metadata for an actor (message counts, convo counts). */
wf_status wf_agent_chat_mod_get_actor_metadata(wf_agent *agent,
                                                const char *actor_did,
                                                wf_response *out);

/* Get message context (surrounding messages) for moderation review. */
wf_status wf_agent_chat_mod_get_message_context(wf_agent *agent,
                                                 const char *message_id,
                                                 const char *convo_id,
                                                 int before, int after,
                                                 int max_interleaved,
                                                 wf_response *out);

/* Get a single conversation by ID (moderation, non-member access). */
wf_status wf_agent_chat_mod_get_convo(wf_agent *agent, const char *convo_id,
                                       wf_response *out);

/* Get conversations by IDs (moderation, non-member access). */
wf_status wf_agent_chat_mod_get_convos(wf_agent *agent,
                                        const char *const *convo_ids,
                                        size_t convo_count,
                                        wf_response *out);

/* Get members of a conversation (moderation, non-member access). */
wf_status wf_agent_chat_mod_get_convo_members(wf_agent *agent,
                                               const char *convo_id,
                                               int limit, const char *cursor,
                                               wf_response *out);

/* Update a user's chat access (allow/block from chat). */
wf_status wf_agent_chat_mod_update_actor_access(wf_agent *agent,
                                                  const char *actor_did,
                                                  bool allow_access,
                                                  const char *ref,
                                                  wf_response *out);

/* ── remaining chat.bsky.convo wrappers ──────────────────────────────── */

/*
 * Send a batch of messages. `items_json` is a JSON array of batchItem objects:
 * [{"convoId": "...", "message": {"text": "...", ...}}, ...].
 */
wf_status wf_agent_chat_send_message_batch(wf_agent *agent,
                                            const char *items_json,
                                            wf_response *out);

/* Lock or unlock a group conversation. */
wf_status wf_agent_chat_lock_convo(wf_agent *agent, const char *convo_id,
                                    wf_response *out);
wf_status wf_agent_chat_unlock_convo(wf_agent *agent, const char *convo_id,
                                      wf_response *out);

/* Get the conversation event log. Returns raw JSON. */
wf_status wf_agent_chat_get_log(wf_agent *agent, int limit,
                                 const char *cursor, wf_response *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_CHAT_TYPED_H */
