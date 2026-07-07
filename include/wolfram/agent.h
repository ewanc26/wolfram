/*
 * agent.h — high-level Bluesky convenience API.
 *
 * Wraps session management, XRPC transport, and richtext facet detection
 * into a single agent object similar to TypeScript's AtpAgent.
 */

#ifndef WOLFRAM_AGENT_H
#define WOLFRAM_AGENT_H

#include "wolfram/session.h"
#include "wolfram/repo.h"
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wf_agent wf_agent;

/* Lifecycle */
wf_agent *wf_agent_new(const char *service_url);
void wf_agent_free(wf_agent *agent);

/* Session management */
wf_status wf_agent_login(wf_agent *agent, const char *identifier, const char *password);
wf_status wf_agent_resume(wf_agent *agent, const wf_session_data *data);
wf_status wf_agent_get_session(wf_agent *agent);
wf_status wf_agent_logout(wf_agent *agent);
/* Copies the current session credentials; free the copy with wf_agent_session_data_free. */
wf_status wf_agent_get_session_data(wf_agent *agent, wf_session_data *out);
void wf_agent_session_data_free(wf_session_data *data);
const char *wf_agent_get_did(wf_agent *agent);
const char *wf_agent_get_handle(wf_agent *agent);

/* Post operations */
typedef struct wf_agent_post_result {
    char *uri;
    char *cid;
} wf_agent_post_result;

wf_status wf_agent_post(wf_agent *agent, const char *text, wf_agent_post_result *out);
wf_status wf_agent_post_with_facets(wf_agent *agent, const char *text,
                                    const char *facets_json, wf_agent_post_result *out);
wf_status wf_agent_delete_post(wf_agent *agent, const char *uri);
wf_status wf_agent_post_with_embed(wf_agent *agent, const char *text,
                                 const char *embed_json, wf_agent_post_result *out);
/* Generic record creation – works for any collection */
wf_status wf_agent_create_record(wf_agent *agent, const char *collection,
                                const char *record_json, wf_agent_post_result *out);
void wf_agent_post_result_free(wf_agent_post_result *result);

/* Update handle */
wf_status wf_agent_update_handle(wf_agent *agent, const char *new_handle);

/* Preferences */
wf_status wf_agent_put_preferences(wf_agent *agent, const char *prefs_json, wf_response *out);

/* Push notification registration */
wf_status wf_agent_register_push(wf_agent *agent, const char *service_did, const char *token, wf_response *out);
wf_status wf_agent_unregister_push(wf_agent *agent, const char *service_did, const char *token, wf_response *out);

/* Extended push APIs with platform and app_id */
wf_status wf_agent_register_push_ext(wf_agent *agent, const char *service_did, const char *token, const char *platform, const char *app_id, wf_response *out);
wf_status wf_agent_unregister_push_ext(wf_agent *agent, const char *service_did, const char *token, const char *platform, const char *app_id, wf_response *out);
wf_status wf_get_notif_endpoint(wf_agent *agent, const char *service_did, char **out_endpoint);

/* Reply API */
wf_status wf_agent_reply(wf_agent *agent, const char *text,
                         const char *parent_uri, const char *parent_cid,
                         wf_agent_post_result *out);

/* Quote with a record embed */
wf_status wf_agent_quote(wf_agent *agent, const char *text,
                          const char *quote_uri, const char *quote_cid,
                          wf_agent_post_result *out);

/* Quote with a record + media embed */
wf_status wf_agent_quote_with_media(wf_agent *agent, const char *text,
                                     const char *quote_uri, const char *quote_cid,
                                     cJSON *media_embed,
                                     wf_agent_post_result *out);


/* Record CRUD — wraps com.atproto.repo endpoints */
wf_status wf_agent_get_record(wf_agent *agent, const char *collection,
                               const char *rkey, wf_response *out);
wf_status wf_agent_put_record(wf_agent *agent, const char *collection,
                               const char *rkey, const char *record_json,
                               wf_agent_post_result *out);
wf_status wf_agent_list_records(wf_agent *agent, const char *collection,
                                int limit, const char *cursor,
                                wf_response *out);

/* Profile operations */
typedef struct wf_agent_profile {
    char *did;
    char *handle;
    char *display_name;
    char *description;
    char *avatar_cid;
    int followers_count;
    int follows_count;
    int posts_count;
} wf_agent_profile;

wf_status wf_agent_get_profile(wf_agent *agent, const char *actor, wf_agent_profile *out);
void wf_agent_profile_free(wf_agent_profile *profile);

/* Social graph */
wf_status wf_agent_follow(wf_agent *agent, const char *subject_did, wf_agent_post_result *out);
wf_status wf_agent_unfollow(wf_agent *agent, const char *follow_uri);
wf_status wf_agent_like(wf_agent *agent, const char *post_uri, const char *post_cid, wf_agent_post_result *out);
wf_status wf_agent_unlike(wf_agent *agent, const char *like_uri);
wf_status wf_agent_mute(wf_agent *agent, const char *actor);
wf_status wf_agent_unmute(wf_agent *agent, const char *actor);

/* Repost operations */
wf_status wf_agent_repost(wf_agent *agent, const char *post_uri, const char *post_cid,
                          wf_agent_post_result *out);
wf_status wf_agent_delete_repost(wf_agent *agent, const char *repost_uri);

/* Feed queries — return raw JSON in `out`; caller frees with wf_response_free. */
wf_status wf_agent_get_timeline(wf_agent *agent, int limit, const char *cursor, wf_response *out);
wf_status wf_agent_get_timeline_lex(wf_agent *agent, int limit, const char *cursor, wf_response *out);
wf_status wf_agent_get_author_feed(wf_agent *agent, const char *actor,
                                    int limit, const char *cursor, const char *filter,
                                    wf_response *out);
wf_status wf_agent_get_author_feed_lex(wf_agent *agent, const char *actor,
                                        int limit, const char *cursor, const char *filter,
                                        wf_response *out);
wf_status wf_agent_get_post_thread(wf_agent *agent, const char *uri, int depth,
                                   wf_response *out);
wf_status wf_agent_get_posts(wf_agent *agent, const char *const *uris, size_t uri_count,
                             wf_response *out);
wf_status wf_agent_search_posts(wf_agent *agent, const char *query,
                                 int limit, const char *cursor, 
                                 const char *since, const char *until,
                                 const char *author, const char *lang,
                                 wf_response *out);
wf_status wf_agent_search_posts_lex(wf_agent *agent, const char *query,
                                 int limit, const char *cursor, 
                                 const char *since, const char *until,
                                 const char *author, const char *lang,
                                 wf_response *out);
wf_status wf_agent_get_actor_likes(wf_agent *agent, const char *actor,
                                   int limit, const char *cursor,
                                   wf_response *out);
wf_status wf_agent_get_likes(wf_agent *agent, const char *uri,
                             int limit, const char *cursor,
                             wf_response *out);
wf_status wf_agent_get_likes_lex(wf_agent *agent, const char *uri,
                                 int limit, const char *cursor,
                                 wf_response *out);
wf_status wf_agent_get_reposted_by(wf_agent *agent, const char *uri,
                                    int limit, const char *cursor,
                                    wf_response *out);
wf_status wf_agent_get_quotes(wf_agent *agent, const char *uri,
                               int limit, const char *cursor,
                               wf_response *out);
wf_status wf_agent_get_quotes_lex(wf_agent *agent, const char *uri,
                                 int limit, const char *cursor,
                                 wf_response *out);
wf_status wf_agent_get_list_feed(wf_agent *agent, const char *list_uri,
                                  int limit, const char *cursor,
                                  wf_response *out);
wf_status wf_agent_get_list_feed_lex(wf_agent *agent, const char *list_uri,
                                    int limit, const char *cursor,
                                    wf_response *out);
wf_status wf_agent_get_feed(wf_agent *agent, const char *feed_uri,
                             int limit, const char *cursor,
                             wf_response *out);
wf_status wf_agent_get_feed_lex(wf_agent *agent, const char *feed_uri,
                               int limit, const char *cursor,
                               wf_response *out);
wf_status wf_agent_get_actor_feeds(wf_agent *agent, const char *actor,
                                     int limit, const char *cursor,
                                     wf_response *out);
wf_status wf_agent_get_actor_feeds_lex(wf_agent *agent, const char *actor,
                                        int limit, const char *cursor,
                                        wf_response *out);

/* Graph queries — return raw JSON in `out`; caller frees with wf_response_free. */
wf_status wf_agent_get_profiles(wf_agent *agent, const char *const *actors,
                                 size_t actors_count, int limit,
                                 const char *cursor, wf_response *out);
wf_status wf_agent_get_follows(wf_agent *agent, const char *actor,
                                 int limit, const char *cursor, wf_response *out);
wf_status wf_agent_get_followers(wf_agent *agent, const char *actor,
                                   int limit, const char *cursor, wf_response *out);
wf_status wf_agent_get_blocks(wf_agent *agent, int limit, const char *cursor,
                               wf_response *out);
wf_status wf_agent_get_mutes(wf_agent *agent, int limit, const char *cursor,
                              wf_response *out);
wf_status wf_agent_get_known_followers(wf_agent *agent, const char *actor,
                                       int limit, const char *cursor,
                                       wf_response *out);
wf_status wf_agent_get_relationships(wf_agent *agent, const char *actor,
                                     const char *const *others, size_t others_count,
                                     wf_response *out);
wf_status wf_agent_get_list(wf_agent *agent, const char *list_uri,
                            int limit, const char *cursor,
                            wf_response *out);
wf_status wf_agent_get_lists(wf_agent *agent, const char *actor,
                              int limit, const char *cursor,
                              wf_response *out);
wf_status wf_agent_get_suggested_follows_by_actor(wf_agent *agent,
                                                   const char *actor,
                                                   wf_response *out);

/* Notifications — return raw JSON in `out`; caller frees with wf_response_free. */
wf_status wf_agent_list_notifications(wf_agent *agent, int limit, const char *cursor,
                                     wf_response *out);
wf_status wf_agent_update_seen_notifications(wf_agent *agent, const char *seen_at);
wf_status wf_agent_get_unread_count(wf_agent *agent, wf_response *out);

/* Notifications — typed output.
 *
 * `wf_agent_list_notifications_typed` issues the same request as
 * `wf_agent_list_notifications` but parses the JSON body into owned C
 * structs. `record` is an owned `cJSON` subtree detached from the response;
 * free the whole list with `wf_agent_notification_list_free`.
 *
 * `wf_agent_parse_notifications` parses an already-fetched JSON body and is
 * the shared backend used by the typed wrapper (exposed for offline testing).
 */
typedef struct wf_agent_profile_view {
    char *did;
    char *handle;
    char *display_name;
    char *avatar;
} wf_agent_profile_view;

typedef struct wf_agent_label {
    char *src;
    char *uri;
    char *val;
    char *cts;
} wf_agent_label;

typedef struct wf_agent_notification {
    char *uri;
    char *cid;
    wf_agent_profile_view author;
    char *reason;
    char *reason_subject;
    cJSON *record;          /* owned parsed record; NULL when absent */
    int is_read;
    char *indexed_at;
    wf_agent_label *labels;
    size_t label_count;
} wf_agent_notification;

typedef struct wf_agent_notification_list {
    wf_agent_notification *notifications;
    size_t notification_count;
    char *cursor;
    char *seen_at;
    int priority;
    int has_priority;
} wf_agent_notification_list;

wf_status wf_agent_parse_notifications(const char *json, size_t json_len,
                                       wf_agent_notification_list *out);
wf_status wf_agent_list_notifications_typed(wf_agent *agent, int limit,
                                            const char *cursor,
                                            wf_agent_notification_list *out);
void wf_agent_notification_list_free(wf_agent_notification_list *list);

/* Typed unread count — writes the integer `count` from getUnreadCount. */
wf_status wf_agent_get_unread_count_typed(wf_agent *agent, int *out_count);

/* Actor search — return raw JSON in `out`; caller frees with wf_response_free. */
wf_status wf_agent_search_actors(wf_agent *agent, const char *query,
                                int limit, const char *cursor, wf_response *out);
wf_status wf_agent_search_actors_typeahead(wf_agent *agent, const char *query,
                                           int limit, wf_response *out);

/* Profile update — upsert the caller's profile record via putRecord. */
typedef struct wf_agent_profile_update {
    const char *display_name;  /* NULL to omit */
    const char *description;   /* NULL to omit */
    const char *avatar_cid;    /* CID of a pre-uploaded blob, or NULL */
    const char *banner_cid;    /* CID of a pre-uploaded blob, or NULL */
} wf_agent_profile_update;

wf_status wf_agent_update_profile(wf_agent *agent, const wf_agent_profile_update *update);

/* Blob upload — wraps wf_xrpc_upload_blob on the agent's XRPC client. */
wf_status wf_agent_upload_blob(wf_agent *agent, const void *data, size_t data_len,
                               const char *content_type, wf_response *out);

/* Handle resolution — resolve a handle to a DID via com.atproto.identity.resolveHandle. */
wf_status wf_agent_resolve_handle(wf_agent *agent, const char *handle, char **out_did);

/* Sync endpoints — wraps com.atproto.sync endpoints (no auth required). */
wf_status wf_agent_sync_get_blob(wf_agent *agent, const char *did, const char *cid,
                                 wf_response *out);
wf_status wf_agent_sync_get_blocks(wf_agent *agent, const char *did,
                                   const char *const *cids, size_t cid_count,
                                   wf_response *out);
wf_status wf_agent_sync_get_record(wf_agent *agent, const char *did,
                                   const char *collection, const char *rkey,
                                   wf_response *out);
wf_status wf_agent_sync_list_blobs(wf_agent *agent, const char *did,
                                   int limit, const char *cursor,
                                   const char *since, wf_response *out);

/* Batch record operations — wraps com.atproto.repo.applyWrites */
typedef enum {
    WF_AGENT_WRITE_CREATE,
    WF_AGENT_WRITE_UPDATE,
    WF_AGENT_WRITE_DELETE
} wf_agent_write_type;

typedef struct wf_agent_write {
    wf_agent_write_type type;
    const char *collection;
    const char *rkey;         /* NULL for auto-generated on create */
    const char *value_json;   /* JSON string of record value; NULL for delete */
} wf_agent_write;

wf_status wf_agent_apply_writes(wf_agent *agent,
                                 const wf_agent_write *writes, size_t write_count,
                                 wf_response *out);

/* Server account management — wraps com.atproto.server endpoints. */
typedef struct wf_agent_server_description {
    char *did;
    char **available_user_domains;
    size_t available_user_domain_count;
    int invite_code_required;
    int phone_verification_required;
    char *privacy_policy;
    char *terms_of_service;
    char *contact_email;
} wf_agent_server_description;

wf_status wf_agent_describe_server(wf_agent *agent, wf_agent_server_description *out);
void wf_agent_server_description_free(wf_agent_server_description *desc);

typedef struct wf_agent_app_password {
    char *name;
    char *created_at;
    int privileged;
} wf_agent_app_password;

wf_status wf_agent_create_app_password(wf_agent *agent, const char *name, int privileged,
                                       wf_agent_app_password *out);
void wf_agent_app_password_free(wf_agent_app_password *pwd);

typedef struct wf_agent_app_password_list {
    wf_agent_app_password *passwords;
    size_t password_count;
} wf_agent_app_password_list;

wf_status wf_agent_list_app_passwords(wf_agent *agent, wf_agent_app_password_list *out);
void wf_agent_app_password_list_free(wf_agent_app_password_list *list);

wf_status wf_agent_revoke_app_password(wf_agent *agent, const char *name);
wf_status wf_agent_delete_account(wf_agent *agent, const char *did, const char *password,
                                  const char *token);

/* Repository sync — local verified repo mirror for incremental diff application. */
wf_status wf_agent_set_did(wf_agent *agent, const char *did);
wf_status wf_agent_set_signing_key(wf_agent *agent, const char *key);
wf_status wf_agent_seed_repo(wf_agent *agent, const wf_car *car);
wf_status wf_agent_apply_repo_diff(wf_agent *agent,
                                   const unsigned char *car_bytes, size_t car_len);
wf_status wf_agent_repo_head(wf_agent *agent, char **out_head);
wf_status wf_agent_invert_repo_operations(wf_agent *agent,
                                          const wf_repo_operation *operations,
                                          size_t count,
                                          wf_repo_operation **out_inverse);
wf_status wf_agent_mirror_get_record(wf_agent *agent, const char *collection,
                                     const char *rkey,
                                     unsigned char **out_data, size_t *out_len);

/* Typed feed/thread parsers — convert raw JSON from app.bsky.feed timeline,
 * author-feed, and post-thread responses into owned C structs. Implemented in
 * feed_typed.c / thread_typed.c; their full declarations live in the headers
 * below so the structs stay next to their parsers. */
#include "wolfram/feed_typed.h"
#include "wolfram/thread_typed.h"
#include "wolfram/graph_typed.h"
#include "wolfram/list_typed.h"
#include "wolfram/moderation_typed.h"
#include "wolfram/feedgen_typed.h"
#include "wolfram/chat_typed.h"
#include "wolfram/moderation_actions.h"
#include "wolfram/lexcall.h"

/*
 * Cursor-based pagination convenience layer.
 *
 * The helpers below loop an underlying read endpoint page by page. Each
 * page's `cursor` is read from the response (the typed `cursor` field when a
 * typed parser is available, otherwise the raw top-level "cursor" JSON
 * field) and fed to the next page, until the cursor is exhausted, `max_pages`
 * is reached, or a transport/callback error occurs.
 *
 * `max_pages <= 0` means "iterate until the cursor is exhausted".
 *
 * Ownership:
 *   - The `out_last_cursor` output (when non-NULL) receives a heap-allocated
 *     copy of the final cursor, or NULL when the sequence ended because the
 *     cursor was exhausted. The caller frees it with free().
 *   - Every `wf_response` / typed list handed to an `on_page` callback is a
 *     *borrow* valid only for the duration of that callback invocation; the
 *     iterator frees it afterwards. Callbacks must not retain the pointer.
 */

/* Extract the top-level "cursor" string from a raw response body.
 * On WF_OK, *out_cursor is either a heap-allocated copy (caller frees with
 * free()) or NULL when the field is absent or not a string. Returns
 * WF_ERR_INVALID_ARG on NULL inputs and WF_ERR_PARSE if the body is not valid
 * JSON. */
wf_status wf_response_cursor(const wf_response *resp, char **out_cursor);

/* Generic per-page iterator over a raw wf_response.
 *
 * `call` issues a single page (signature compatible with the raw read
 * helpers, extended with a `void *ud` closure); `on_page` is invoked for each
 * fetched page with a borrow of that page's response and the cursor that
 * produced it. `max_pages <= 0` iterates until the cursor is exhausted.
 * `out_last_cursor` receives a heap-allocated copy of the final cursor (or
 * NULL if exhausted); the caller frees it with free().
 *
 * Either `call` or `on_page` may return a non-WF_OK status to abort the loop;
 * that status is propagated to the caller. */
typedef wf_status (*wf_agent_page_call_fn)(wf_agent *agent, int limit,
                                            const char *cursor,
                                            wf_response *out, void *ud);
typedef wf_status (*wf_agent_page_cb)(wf_agent *agent, const char *cursor,
                                       wf_response *resp, void *ud);

wf_status wf_agent_page(wf_agent *agent,
                        wf_agent_page_call_fn call,
                        int limit, int max_pages,
                        wf_agent_page_cb on_page, void *ud,
                        char **out_last_cursor);

/* Typed paged wrappers. `on_page` is invoked once per page with a borrow of
 * the parsed list and the cursor that produced it. `out_last_cursor` receives
 * a heap-allocated copy of the final cursor (caller frees with free()) or NULL
 * when the sequence was exhausted. */
typedef wf_status (*wf_agent_timeline_page_cb)(wf_agent *agent,
                                                const wf_agent_feed_list *feed,
                                                const char *cursor, void *ud);
wf_status wf_agent_get_timeline_paged(wf_agent *agent, int limit, int max_pages,
                                      wf_agent_timeline_page_cb on_page,
                                      void *ud, char **out_last_cursor);

typedef wf_status (*wf_agent_author_feed_page_cb)(wf_agent *agent,
                                                   const wf_agent_feed_list *feed,
                                                   const char *cursor, void *ud);
wf_status wf_agent_get_author_feed_paged(wf_agent *agent, const char *actor,
                                         int limit, int max_pages,
                                         wf_agent_author_feed_page_cb on_page,
                                         void *ud, char **out_last_cursor);

typedef wf_status (*wf_agent_notifications_page_cb)(
    wf_agent *agent, const wf_agent_notification_list *list,
    const char *cursor, void *ud);
wf_status wf_agent_list_notifications_paged(wf_agent *agent, int limit,
                                            int max_pages,
                                            wf_agent_notifications_page_cb on_page,
                                            void *ud, char **out_last_cursor);

typedef wf_status (*wf_agent_records_page_cb)(wf_agent *agent,
                                               const wf_response *resp,
                                               const char *cursor, void *ud);
wf_status wf_agent_list_records_paged(wf_agent *agent, const char *collection,
                                      int limit, int max_pages,
                                      wf_agent_records_page_cb on_page,
                                      void *ud, char **out_last_cursor);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_AGENT_H */
