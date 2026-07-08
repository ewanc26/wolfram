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
#include "wolfram/moderation.h"
#include "wolfram/atproto_lex.h"
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
wf_status wf_agent_get_preferences_typed(
    wf_agent *agent,
    wf_lex_app_bsky_actor_get_preferences_main_output **out);
wf_status wf_agent_put_preferences_json(
    wf_agent *agent,
    const wf_lex_app_bsky_actor_put_preferences_main_input *input,
    wf_response *out);

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
wf_status wf_agent_mute_thread(wf_agent *agent, const char *root_uri);
wf_status wf_agent_unmute_thread(wf_agent *agent, const char *root_uri);
wf_status wf_agent_block(wf_agent *agent, const char *subject_did,
                          wf_agent_post_result *out);
wf_status wf_agent_unblock(wf_agent *agent, const char *block_uri);

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
wf_status wf_agent_describe_feed_generator(wf_agent *agent, wf_response *out);
wf_status wf_agent_get_feed_generator(wf_agent *agent, const char *feed_uri,
                                       wf_response *out);
wf_status wf_agent_get_feed_generators(wf_agent *agent,
                                        const char *const *feed_uris,
                                        size_t feed_count,
                                        wf_response *out);
wf_status wf_agent_get_suggested_feeds(wf_agent *agent, wf_response *out);
wf_status wf_agent_get_suggestions(wf_agent *agent, int limit,
                                    const char *cursor, wf_response *out);

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
wf_status wf_agent_get_list_blocks(wf_agent *agent, int limit,
                                    const char *cursor, wf_response *out);
wf_status wf_agent_get_list_mutes(wf_agent *agent, int limit,
                                   const char *cursor, wf_response *out);
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

/* Starter packs */
wf_status wf_agent_get_actor_starter_packs(wf_agent *agent, const char *actor,
                                            int limit, const char *cursor,
                                            wf_response *out);
wf_status wf_agent_get_starter_pack(wf_agent *agent,
                                     const char *starter_pack_uri,
                                     wf_response *out);
wf_status wf_agent_get_starter_packs(wf_agent *agent,
                                      const char *const *uris,
                                      size_t uri_count,
                                      wf_response *out);
wf_status wf_agent_search_starter_packs(wf_agent *agent, const char *query,
                                         int limit, const char *cursor,
                                         wf_response *out);
wf_status wf_agent_get_starter_packs_with_membership(wf_agent *agent,
                                                      const char *actor,
                                                      int limit,
                                                      const char *cursor,
                                                      wf_response *out);

/* List management — create, update, delete lists and list items. */
typedef struct wf_agent_create_list_params {
    const char *purpose;     /* app.bsky.graph.defs#modlist / #curatelist / #referencelist */
    const char *name;        /* required */
    const char *description; /* optional */
    const char *description_facets_json; /* optional JSON array of facets */
    const char *avatar_cid;  /* optional CID of a pre-uploaded blob */
} wf_agent_create_list_params;

wf_status wf_agent_create_list(wf_agent *agent,
                              const wf_agent_create_list_params *params,
                              wf_agent_post_result *out);

typedef struct wf_agent_update_list_params {
    const char *list_uri;   /* at:// URI of the list */
    const char *name;
    const char *description;
    const char *description_facets_json;
    const char *avatar_cid;
} wf_agent_update_list_params;

wf_status wf_agent_update_list(wf_agent *agent,
                              const wf_agent_update_list_params *params);
wf_status wf_agent_delete_list(wf_agent *agent, const char *list_uri);
wf_status wf_agent_add_list_item(wf_agent *agent,
                                 const char *list_uri,
                                 const char *subject_did,
                                 wf_agent_post_result *out);
wf_status wf_agent_remove_list_item(wf_agent *agent,
                                    const char *list_item_uri);
wf_status wf_agent_mute_mod_list(wf_agent *agent, const char *list_uri);
wf_status wf_agent_unmute_mod_list(wf_agent *agent, const char *list_uri);
wf_status wf_agent_block_mod_list(wf_agent *agent, const char *list_uri,
                                  wf_agent_post_result *out);
wf_status wf_agent_unblock_mod_list(wf_agent *agent, const char *list_uri);

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

/* Repository mirror persistence — bridge to the optional SQLite wf_store.
 *
 * Persistence is BEST-EFFORT: the functions only act when a store has been
 * attached via wf_agent_attach_store. When no store is attached (or the
 * store module is not built), seeding / applying diffs keeps working purely
 * in-memory. The agent never frees the attached store; the caller owns it. */
#ifdef WOLFRAM_BUILD_STORE
#include "wolfram/store.h"

/* Attach an open store to the agent (caller retains ownership). Once
 * attached, wf_agent_seed_repo / wf_agent_apply_repo_diff persist the
 * mirror HEAD and blocks automatically. */
wf_status wf_agent_attach_store(wf_agent *agent, wf_store *store);

/* Reload a previously persisted mirror for the agent's DID into the
 * in-memory mirror. Returns WF_ERR_NOT_FOUND when no mirror is persisted,
 * WF_ERR_INVALID_ARG when no store/DID is set, WF_OK on success. */
wf_status wf_agent_mirror_load_from_store(wf_agent *agent);

/* Return the store currently attached to the agent, or NULL when no store
 * is attached (or the store module is not compiled). Borrowed; valid for
 * the lifetime of the agent. */
wf_store *wf_agent_get_store(wf_agent *agent);

/* Label persistence — bridge labels <-> the optional SQLite wf_store.
 *
 * Best-effort: when no store is attached these functions are no-ops and
 * moderation continues to use only live API data. The agent never frees the
 * attached store; the caller owns it. */

/* Attach a store for label persistence. Equivalent to wf_agent_attach_store;
 * the same attached store backs both the repo mirror and persisted labels. */
wf_status wf_agent_attach_label_store(wf_agent *agent, wf_store *store);

/* Load persisted labels (for the agent's DID and any reachable followed/known
 * DIDs) into the agent's moderation context. Returns WF_OK when no store is
 * attached (nothing to load). The agent owns the loaded labels. */
wf_status wf_agent_load_labels_from_store(wf_agent *agent);

/* Persist a single label via the attached store. Returns WF_OK (best-effort)
 * when no store is attached. The label's `uri`/`cid`/`val`/`src`/`cts` are
 * copied into the store. */
wf_status wf_agent_persist_label(wf_agent *agent, const wf_mod_label *label);

/* Return the agent's currently loaded persisted labels (set by
 * wf_agent_load_labels_from_store). Borrowed; valid until the next load or
 * wf_agent_free. */
const wf_mod_label *wf_agent_get_persisted_labels(const wf_agent *agent,
                                                 size_t *out_count);
#endif

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
#include "wolfram/threadgate_postgate.h"
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

/* ------------------------------------------------------------------ */
/* Moderation — wiring the decision engine into the agent              */
/* ------------------------------------------------------------------ */

/* Raw `app.bsky.actor.getProfile` — returns the raw JSON response body
 * in `out`; caller frees it with wf_response_free. The body carries the full
 * profileView (including `viewer` state and `labels`) needed to build a
 * moderation subject. NULL agent/out -> WF_ERR_INVALID_ARG. */
wf_status wf_agent_get_profile_raw(wf_agent *agent, const char *actor, wf_response *out);

/* Fetch the current user's `app.bsky.actor.getPreferences` and return the
 * `preferences` array as a heap-owned JSON string in *out_json.
 * On WF_OK, caller frees *out_json with free(). NULL agent/out_json ->
 * WF_ERR_INVALID_ARG. */
wf_status wf_agent_get_preferences(wf_agent *agent, char **out_json);

/*
 * Build a wf_mod_opts for the current user, loading:
 *   - opts->user_did : borrowed from the agent's session (do NOT free)
 *   - opts->prefs    : the user's moderation preferences (free with
 *                      wf_mod_prefs_free)
 *   - opts->label_defs : interpreted label value definitions for each
 *                      configured labeler (free with wf_mod_label_defs_free)
 *
 * Performs network I/O (preferences + one record fetch per labeler). On
 * failure, returns an error and leaves `out` only partially populated; the
 * caller should still release any prefs/label_defs that were assigned. NULL
 * agent/out -> WF_ERR_INVALID_ARG.
 */
wf_status wf_agent_moderate_init_opts(wf_agent *agent, wf_mod_opts *out);

/* Moderate an actor's profile: fetch it, build a wf_mod_subject_profile,
 * load the user's opts, and merge wf_mod_decide_account + wf_mod_decide_profile.
 * On WF_OK, *out is a heap-owned wf_mod_decision freed by the caller with
 * wf_mod_decision_free. NULL agent/actor/out -> WF_ERR_INVALID_ARG. */
wf_status wf_agent_moderate_profile(wf_agent *agent, const char *actor, wf_mod_decision **out);

/* Moderate a post: fetch its thread, build a wf_mod_subject_post (author
 * viewer/labels + post labels/text + embed), load the user's opts, and call
 * wf_mod_decide_post. On WF_OK, *out is a heap-owned wf_mod_decision freed by
 * the caller with wf_mod_decision_free. NULL agent/uri/out -> WF_ERR_INVALID_ARG. */
wf_status wf_agent_moderate_post(wf_agent *agent, const char *uri, wf_mod_decision **out);

/*
 * Offline-testable JSON shims (no network). Parse a profileView JSON object
 * into a wf_mod_subject_profile. String fields are BORROWED from `obj` (the
 * caller must keep `obj` alive until after the decision is computed), while
 * *out_labels is a caller-owned wf_mod_label array (free with
 * wf_mod_labels_free). *out is left zeroed/borrowed; on WF_OK the caller must
 * free *out_labels after use.
 */
wf_status wf_agent_mod_profile_subject_from_json(const cJSON *obj,
                                                 wf_mod_subject_profile *out,
                                                 wf_mod_label **out_labels,
                                                 size_t *out_label_count);

/*
 * Offline-testable JSON shim (no network). Parse a post + author JSON into a
 * wf_mod_subject_post. `post` is the postView object and `author` its `author`
 * profileView. String fields are BORROWED from `post`/`author` (keep them
 * alive), while *out_labels (post content labels) and *out_author_labels
 * (author labels) are caller-owned wf_mod_label arrays (free each with
 * wf_mod_labels_free). On WF_OK the caller must free both label arrays after
 * the decision is computed.
 */
wf_status wf_agent_mod_post_subject_from_json(const cJSON *post,
                                              const cJSON *author,
                                              wf_mod_subject_post *out,
                                              wf_mod_label **out_labels,
                                              size_t *out_label_count,
                                                                 wf_mod_label **out_author_labels,
                                                                 size_t *out_author_label_count);

/* ── actor status ───────────────────────────────────────────────────── */

/*
 * Create or update the authenticated user's actor status record (rkey "self").
 * `status` is the status string (e.g. "app.bsky.actor.status#live").
 * `duration_minutes` is optional (pass 0 or negative to omit).
 * `embed_json` is an optional JSON embed object string (pass NULL or "" to
 * omit). On success, `*out` receives the URI and CID.
 */
wf_status wf_agent_put_actor_status(wf_agent *agent, const char *status,
                                     int duration_minutes,
                                     const char *embed_json,
                                     wf_agent_post_result *out);

/*
 * Poll the status of a video processing job by job ID.
 * Returns raw JSON; caller frees with wf_response_free.
 */
wf_status wf_agent_get_video_job_status(wf_agent *agent, const char *job_id,
                                          wf_response *out);

/*
 * Get the authenticated user's video upload limits.
 * Returns raw JSON; caller frees with wf_response_free.
 */
wf_status wf_agent_get_video_upload_limits(wf_agent *agent,
                                             wf_response *out);

/* ── server wrappers ─────────────────────────────────────────────────── */

/*
 * Create an invite code. `use_count` is the number of uses allowed.
 * Returns raw JSON; caller frees with wf_response_free.
 */
wf_status wf_agent_create_invite_code(wf_agent *agent, int use_count,
                                       wf_response *out);

/*
 * Get the authenticated user's available invite codes.
 * Returns raw JSON; caller frees with wf_response_free.
 */
wf_status wf_agent_get_account_invite_codes(wf_agent *agent, int limit,
                                             const char *cursor,
                                             wf_response *out);

/* Activate or deactivate the authenticated user's account. */
wf_status wf_agent_activate_account(wf_agent *agent, wf_response *out);
wf_status wf_agent_deactivate_account(wf_agent *agent, wf_response *out);

/* Check the authenticated user's account status (migration, import, etc.). */
wf_status wf_agent_check_account_status(wf_agent *agent, wf_response *out);

/* Confirm an email with a token, or update the account email. */
wf_status wf_agent_confirm_email(wf_agent *agent, const char *email,
                                  const char *token, wf_response *out);
wf_status wf_agent_update_email(wf_agent *agent, const char *email,
                                 wf_response *out);

/* ── identity wrappers ───────────────────────────────────────────────── */

/* Resolve a DID to its DID document. Returns raw JSON. */
wf_status wf_agent_resolve_did(wf_agent *agent, const char *did,
                                wf_response *out);

/* Get recommended DID credentials for account creation. Returns raw JSON. */
wf_status wf_agent_get_recommended_did_credentials(wf_agent *agent,
                                                    wf_response *out);

/* Describe a repository (owner DID, handle, collections, etc.). */
wf_status wf_agent_describe_repo(wf_agent *agent, const char *repo,
                                  wf_response *out);

/*
 * Send feed interactions for suggestion training.
 * `feed_uri` may be NULL. `interactions_json` is a JSON array of interaction
 * objects (see app.bsky.feed.sendInteractions).
 */
wf_status wf_agent_send_interactions(wf_agent *agent,
                                      const char *feed_uri,
                                      const char *interactions_json,
                                      wf_response *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_AGENT_H */
