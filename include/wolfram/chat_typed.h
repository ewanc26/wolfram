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

/* ════════════════════════════════════════════════════════════════════════
 * chat.bsky.notification.*
 *
 * Chat notification preferences. Both endpoints route through the resolved
 * chat service client (same idiom as chat.bsky.convo.*). The response body is
 * parsed into owned structs here (rather than returned as raw JSON) because
 * the shape is small and stable: a `preferences` object with exactly two
 * `chatPreference` members (`chat` and `chatRequest`), each carrying an
 * `include` enum ("all" | "follows") and a `push` boolean.
 * ════════════════════════════════════════════════════════════════════════ */

/* A single chat notification preference (chat.bsky.notification.defs#
 * chatPreference). `include` is required ("all" or "follows"); `push` is
 * required. `has_push` lets callers distinguish an explicit `false` from a
 * missing field, though both source fields are required on the wire. */
typedef struct wf_chat_notification_preference {
    char *include;   /* "all" or "follows" (required) */
    int push;        /* required boolean (push notifications on/off) */
    int has_push;
} wf_chat_notification_preference;

/* Chat notification preferences (defs#preferences). Both members are
 * required on the wire. */
typedef struct wf_chat_notification_preferences {
    wf_chat_notification_preference chat;
    wf_chat_notification_preference chat_request;
} wf_chat_notification_preferences;

/* Parse a raw chat.bsky.notification.getPreferences / putPreferences JSON
 * body (the `{"preferences": {...}}` envelope) into owned structs.
 * Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or
 * a missing/invalid `preferences` object, WF_ERR_ALLOC on allocation failure,
 * WF_OK on success. On any error `out` is left fully reset (no partial leaks). */
wf_status wf_agent_parse_chat_notification_preferences(const char *json,
                                                        size_t json_len,
                                                        wf_chat_notification_preferences *out);

/* Free a parsed chat notification preferences struct and every owned string it
 * holds. Safe to call with NULL or on an already-reset struct. */
void wf_chat_notification_preferences_free(wf_chat_notification_preferences *p);

/* Get the authenticated user's chat notification preferences. Returns the
 * parsed `preferences` in `out`; the caller owns it and must free with
 * wf_chat_notification_preferences_free. On error `out` is left reset.
 * Returns WF_ERR_INVALID_ARG on NULL required inputs. */
wf_status wf_agent_chat_notification_get_preferences(wf_agent *agent,
                                                      wf_chat_notification_preferences *out);

/* Set the authenticated user's chat notification preferences. `in` carries
 * the full `chat` + `chatRequest` preference set to write; `out` receives the
 * server's resulting (full) preferences, owned by the caller and freed with
 * wf_chat_notification_preferences_free. On error `out` is left reset.
 * Returns WF_ERR_INVALID_ARG on NULL required inputs. */
wf_status wf_agent_chat_notification_put_preferences(wf_agent *agent,
                                                      const wf_chat_notification_preferences *in,
                                                      wf_chat_notification_preferences *out);

/* ── test-only body builders ────────────────────────────────────────────
 * These build the exact request JSON a wrapper would send, without doing any
 * network I/O. Exposed for deterministic, offline unit tests. The caller owns
 * the returned string (*out_json) and must free() it. */

wf_status wf_chat_build_accept_convo_body(const char *convo_id,
                                           char **out_json);

wf_status wf_chat_build_add_reaction_body(const char *convo_id,
                                           const char *message_id,
                                           const char *value,
                                           char **out_json);

wf_status wf_chat_build_create_group_body(const char *const *member_dids,
                                             size_t member_count,
                                             const char *name,
                                             char **out_json);

/* ════════════════════════════════════════════════════════════════════════
 * Extended owned-struct typed wrappers for the remaining chat.bsky.* endpoints.
 *
 * The wrappers below parse the raw chat-service JSON response into an OWNED
 * struct (mirroring listConvos/getConvo/getMessages). They are named with a
 * `_typed` suffix to avoid colliding with the existing raw-JSON wrappers that
 * return `wf_response *` (which remain available for callers wanting the body
 * verbatim). Each `out` struct is owned by the caller and freed with the
 * matching `*_free`; on error it is left reset. All route through the resolved
 * chat service client, exactly like the convo wrappers above.
 * ════════════════════════════════════════════════════════════════════════ */

/* chat.bsky.convo.getConvoAvailability — { canChat, hasConvo, convo }. */
typedef struct wf_chat_convo_availability {
    int can_chat;        /* required bool ("canChat") */
    int has_can_chat;
    int has_convo;       /* required bool ("hasConvo") */
    int has_has_convo;
    wf_chat_convo convo; /* present only when hasConvo is true */
} wf_chat_convo_availability;

wf_status wf_agent_parse_convo_availability(const char *json, size_t json_len,
                                            wf_chat_convo_availability *out);
void wf_chat_convo_availability_free(wf_chat_convo_availability *a);
wf_status wf_agent_chat_get_convo_availability_typed(wf_agent *agent,
        const char *const *member_dids, size_t member_count,
        wf_chat_convo_availability *out);

/* A paginated member list (getConvoMembers / moderation.getConvoMembers).
 * `members` is an array of profile-view-basics. */
typedef struct wf_chat_convo_members {
    wf_agent_profile_view *members;
    size_t member_count;
    char *cursor;        /* optional; NULL when absent */
} wf_chat_convo_members;

wf_status wf_agent_parse_convo_members(const char *json, size_t json_len,
                                       wf_chat_convo_members *out);
void wf_chat_convo_members_free(wf_chat_convo_members *m);
wf_status wf_agent_chat_get_convo_members_typed(wf_agent *agent,
        const char *convo_id, wf_chat_convo_members *out);
wf_status wf_agent_chat_mod_get_convo_members_typed(wf_agent *agent,
        const char *convo_id, int limit, const char *cursor,
        wf_chat_convo_members *out);

/* chat.bsky.convo.getUnreadCounts — aggregate counts. */
typedef struct wf_chat_unread_counts {
    int unread_accepted_convos;   /* required ("unreadAcceptedConvos") */
    int has_unread_accepted;
    int unread_request_convos;    /* required ("unreadRequestConvos") */
    int has_unread_request;
} wf_chat_unread_counts;

wf_status wf_agent_parse_unread_counts(const char *json, size_t json_len,
                                       wf_chat_unread_counts *out);
void wf_chat_unread_counts_free(wf_chat_unread_counts *u);
wf_status wf_agent_chat_get_unread_counts_typed(wf_agent *agent,
        wf_chat_unread_counts *out);

/* chat.bsky.convo.listConvoRequests — convoViews under the "requests" key.
 * Reuses wf_chat_convo_list (same shape as listConvos) and its free. */
wf_status wf_agent_parse_convo_requests(const char *json, size_t json_len,
                                        wf_chat_convo_list *out);
wf_status wf_agent_chat_list_convo_requests_typed(wf_agent *agent, int limit,
        const char *cursor, wf_chat_convo_list *out);

/* chat.bsky.convo.getLog — an array of union log events + cursor. Only the
 * bounded identifying fields (id/rev/$type/convoId) are retained. */
typedef struct wf_chat_log_event {
    char *id;
    char *rev;
    char *type;       /* union $type tag */
    char *convo_id;
} wf_chat_log_event;

typedef struct wf_chat_log {
    wf_chat_log_event *events;
    size_t event_count;
    char *cursor;     /* optional; NULL when absent */
} wf_chat_log;

wf_status wf_agent_parse_log(const char *json, size_t json_len, wf_chat_log *out);
void wf_chat_log_free(wf_chat_log *l);
wf_status wf_agent_chat_get_log_typed(wf_agent *agent, int limit,
        const char *cursor, wf_chat_log *out);

/* chat.bsky.convo.sendMessageBatch — { items: [{convoId, message}] }. */
typedef struct wf_chat_message_batch_item {
    char *convo_id;
    wf_chat_message message;
} wf_chat_message_batch_item;

typedef struct wf_chat_message_batch {
    wf_chat_message_batch_item *items;
    size_t item_count;
} wf_chat_message_batch;

wf_status wf_agent_parse_message_batch(const char *json, size_t json_len,
        wf_chat_message_batch *out);
void wf_chat_message_batch_free(wf_chat_message_batch *b);
wf_status wf_agent_chat_send_message_batch_typed(wf_agent *agent,
        const char *items_json, wf_chat_message_batch *out);

/* chat.bsky.convo.leaveConvo — { convoId, rev }. */
typedef struct wf_chat_convo_ref {
    char *convo_id;
    char *rev;
} wf_chat_convo_ref;

wf_status wf_agent_parse_convo_ref(const char *json, size_t json_len,
        wf_chat_convo_ref *out);
void wf_chat_convo_ref_free(wf_chat_convo_ref *r);
wf_status wf_agent_chat_leave_convo_typed(wf_agent *agent, const char *convo_id,
        wf_chat_convo_ref *out);

/* chat.bsky.convo.updateAllRead — { updatedCount }. */
typedef struct wf_chat_updated_count {
    int updated_count;        /* required ("updatedCount") */
    int has_updated_count;
} wf_chat_updated_count;

wf_status wf_agent_parse_updated_count(const char *json, size_t json_len,
        wf_chat_updated_count *out);
void wf_chat_updated_count_free(wf_chat_updated_count *u);
wf_status wf_agent_chat_update_all_read_typed(wf_agent *agent,
        const char *convo_id, wf_chat_updated_count *out);

/* chat.bsky.convo.addReaction / removeReaction — { message: messageView }. */
wf_status wf_agent_parse_message_ref(const char *json, size_t json_len,
        wf_chat_message *out);
wf_status wf_agent_chat_add_reaction_typed(wf_agent *agent,
        const char *convo_id, const char *message_id, const char *value,
        wf_chat_message *out);
wf_status wf_agent_chat_remove_reaction_typed(wf_agent *agent,
        const char *convo_id, const char *message_id, const char *value,
        wf_chat_message *out);

/* convoView-returning endpoints reuse wf_chat_convo + wf_chat_convo_free. */
wf_status wf_agent_chat_get_convo_for_members_typed(wf_agent *agent,
        const char *const *member_dids, size_t member_count,
        wf_chat_convo *out);
wf_status wf_agent_chat_accept_convo_typed(wf_agent *agent,
        const char *convo_id, wf_chat_convo *out);
wf_status wf_agent_chat_mute_convo_typed(wf_agent *agent,
        const char *convo_id, wf_chat_convo *out);
wf_status wf_agent_chat_unmute_convo_typed(wf_agent *agent,
        const char *convo_id, wf_chat_convo *out);
wf_status wf_agent_chat_lock_convo_typed(wf_agent *agent,
        const char *convo_id, wf_chat_convo *out);
wf_status wf_agent_chat_unlock_convo_typed(wf_agent *agent,
        const char *convo_id, wf_chat_convo *out);
wf_status wf_agent_chat_update_read_typed(wf_agent *agent,
        const char *convo_id, const char *message_id, wf_chat_convo *out);
wf_status wf_agent_chat_create_group_typed(wf_agent *agent,
        const char *const *member_dids, size_t member_count, const char *name,
        wf_chat_convo *out);
wf_status wf_agent_chat_edit_group_typed(wf_agent *agent, const char *convo_id,
        const char *name, wf_chat_convo *out);
wf_status wf_agent_chat_add_members_typed(wf_agent *agent, const char *convo_id,
        const char *const *member_dids, size_t member_count,
        wf_chat_convo *out);
wf_status wf_agent_chat_remove_members_typed(wf_agent *agent,
        const char *convo_id, const char *const *member_dids,
        size_t member_count, wf_chat_convo *out);
wf_status wf_agent_chat_approve_join_request_typed(wf_agent *agent,
        const char *convo_id, const char *user_did, wf_chat_convo *out);

/* chat.bsky.group.listMutualGroups / moderation.getConvos — convoViews under
 * the "convos" key. Reuse wf_chat_convo_list + wf_chat_convo_list_free. */
wf_status wf_agent_parse_convo_array(const char *json, size_t json_len,
        const char *key, wf_chat_convo_list *out);
wf_status wf_agent_chat_list_mutual_groups_typed(wf_agent *agent,
        const char *convo_id, int limit, const char *cursor,
        wf_chat_convo_list *out);

/* chat.bsky.group join links — { joinLink: { code, enabledStatus,
 * requireApproval, joinRule, createdAt } }. */
typedef struct wf_chat_join_link {
    char *code;
    char *enabled_status;     /* "enabled" | "disabled" | "invalid" */
    int require_approval;     /* required bool */
    int has_require_approval;
    char *join_rule;          /* e.g. "all-members" */
    char *created_at;
} wf_chat_join_link;

wf_status wf_agent_parse_join_link(const char *json, size_t json_len,
        wf_chat_join_link *out);
void wf_chat_join_link_free(wf_chat_join_link *j);
wf_status wf_agent_chat_create_join_link_typed(wf_agent *agent,
        const char *convo_id, bool require_approval, const char *join_rule,
        wf_chat_join_link *out);
wf_status wf_agent_chat_edit_join_link_typed(wf_agent *agent,
        const char *convo_id, const char *code, wf_chat_join_link *out);
wf_status wf_agent_chat_disable_join_link_typed(wf_agent *agent,
        const char *convo_id, wf_chat_join_link *out);
wf_status wf_agent_chat_enable_join_link_typed(wf_agent *agent,
        const char *convo_id, wf_chat_join_link *out);

/* chat.bsky.group.getJoinLinkPreviews — union array of previews. */
typedef struct wf_chat_join_link_preview {
    char *code;
    char *name;
    char *join_rule;
    int require_approval;     /* present on the full (valid) variant */
    int has_require_approval;
    char *kind;               /* $type tag (valid / disabled / invalid) */
} wf_chat_join_link_preview;

typedef struct wf_chat_join_link_previews {
    wf_chat_join_link_preview *items;
    size_t item_count;
} wf_chat_join_link_previews;

wf_status wf_agent_parse_join_link_previews(const char *json, size_t json_len,
        wf_chat_join_link_previews *out);
void wf_chat_join_link_previews_free(wf_chat_join_link_previews *l);
wf_status wf_agent_chat_get_join_link_previews_typed(wf_agent *agent,
        const char *const *codes, size_t code_count,
        wf_chat_join_link_previews *out);

/* chat.bsky.group.listJoinRequests — { requests: [{convoId, requestedBy,
 * requestedAt}] }. */
typedef struct wf_chat_join_request {
    char *convo_id;
    wf_agent_profile_view requested_by;
    char *requested_at;
} wf_chat_join_request;

typedef struct wf_chat_join_requests {
    wf_chat_join_request *items;
    size_t item_count;
    char *cursor;     /* optional; NULL when absent */
} wf_chat_join_requests;

wf_status wf_agent_parse_join_requests(const char *json, size_t json_len,
        wf_chat_join_requests *out);
void wf_chat_join_requests_free(wf_chat_join_requests *r);
wf_status wf_agent_chat_list_join_requests_typed(wf_agent *agent,
        const char *convo_id, int limit, const char *cursor,
        wf_chat_join_requests *out);

/* chat.bsky.group.requestJoin — { status, convo? }. */
typedef struct wf_chat_request_join {
    char *status;
    wf_chat_convo convo;   /* present only when status == "joined" */
    int has_convo;
} wf_chat_request_join;

wf_status wf_agent_parse_request_join(const char *json, size_t json_len,
        wf_chat_request_join *out);
void wf_chat_request_join_free(wf_chat_request_join *r);
wf_status wf_agent_chat_request_join_typed(wf_agent *agent,
        const char *convo_id, wf_chat_request_join *out);

/* chat.bsky.moderation.getActorMetadata — { day, month, all } period stats. */
typedef struct wf_chat_actor_metadata_period {
    int messages_sent;        /* required */
    int has_messages_sent;
    int messages_received;    /* required */
    int has_messages_received;
    int convos;               /* required */
    int has_convos;
    int convos_started;       /* required */
    int has_convos_started;
} wf_chat_actor_metadata_period;

typedef struct wf_chat_actor_metadata {
    wf_chat_actor_metadata_period day;
    wf_chat_actor_metadata_period month;
    wf_chat_actor_metadata_period all;
} wf_chat_actor_metadata;

wf_status wf_agent_parse_actor_metadata(const char *json, size_t json_len,
        wf_chat_actor_metadata *out);
void wf_chat_actor_metadata_free(wf_chat_actor_metadata *m);
wf_status wf_agent_chat_mod_get_actor_metadata_typed(wf_agent *agent,
        const char *actor_did, wf_chat_actor_metadata *out);

/* chat.bsky.moderation.getConvo(s) — moderation convoView { id, rev, kind }.
 * `kind` is a JSON union; only its $type tag is retained. */
typedef struct wf_chat_mod_convo {
    char *id;
    char *rev;
    char *type;       /* kind $type tag */
} wf_chat_mod_convo;

typedef struct wf_chat_mod_convo_list {
    wf_chat_mod_convo *convos;
    size_t convo_count;
} wf_chat_mod_convo_list;

wf_status wf_agent_parse_mod_convo(const char *json, size_t json_len,
        wf_chat_mod_convo *out);
void wf_chat_mod_convo_free(wf_chat_mod_convo *c);
wf_status wf_agent_parse_mod_convos(const char *json, size_t json_len,
        wf_chat_mod_convo_list *out);
void wf_chat_mod_convo_list_free(wf_chat_mod_convo_list *l);
wf_status wf_agent_chat_mod_get_convos_typed(wf_agent *agent,
        const char *const *convo_ids, size_t convo_count,
        wf_chat_mod_convo_list *out);
wf_status wf_agent_chat_mod_get_convo_typed(wf_agent *agent,
        const char *convo_id, wf_chat_mod_convo *out);

/* chat.bsky.moderation.getMessageContext — { messages: [...] }. Reuses
 * wf_chat_message_list + wf_chat_message_list_free. */
wf_status wf_agent_chat_mod_get_message_context_typed(wf_agent *agent,
        const char *message_id, const char *convo_id, int before, int after,
        int max_interleaved, wf_chat_message_list *out);

/* chat.bsky.actor.getStatus — { chatDisabled, canCreateGroups,
 * groupMemberLimit }. */
typedef struct wf_chat_actor_status {
    int chat_disabled;        /* required bool */
    int has_chat_disabled;
    int can_create_groups;    /* required bool */
    int has_can_create_groups;
    int64_t group_member_limit;  /* required int */
    int has_group_member_limit;
} wf_chat_actor_status;

wf_status wf_agent_parse_actor_status(const char *json, size_t json_len,
        wf_chat_actor_status *out);
void wf_chat_actor_status_free(wf_chat_actor_status *s);
wf_status wf_agent_chat_get_status_typed(wf_agent *agent,
        wf_chat_actor_status *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_CHAT_TYPED_H */
