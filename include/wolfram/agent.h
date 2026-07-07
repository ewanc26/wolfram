/*
 * agent.h — high-level Bluesky convenience API.
 *
 * Wraps session management, XRPC transport, and richtext facet detection
 * into a single agent object similar to TypeScript's AtpAgent.
 */

#ifndef WOLFRAM_AGENT_H
#define WOLFRAM_AGENT_H

#include "wolfram/session.h"

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
void wf_agent_post_result_free(wf_agent_post_result *result);

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
wf_status wf_agent_get_author_feed(wf_agent *agent, const char *actor,
                                   int limit, const char *cursor, const char *filter,
                                   wf_response *out);
wf_status wf_agent_get_post_thread(wf_agent *agent, const char *uri, int depth,
                                   wf_response *out);
wf_status wf_agent_get_posts(wf_agent *agent, const char *const *uris, size_t uri_count,
                             wf_response *out);
wf_status wf_agent_search_posts(wf_agent *agent, const char *query,
                                int limit, const char *cursor, const char *sort,
                                const char *since, const char *until,
                                const char *author, const char *lang,
                                wf_response *out);
wf_status wf_agent_get_actor_likes(wf_agent *agent, const char *actor,
                                   int limit, const char *cursor,
                                   wf_response *out);
wf_status wf_agent_get_likes(wf_agent *agent, const char *uri,
                             int limit, const char *cursor,
                             wf_response *out);
wf_status wf_agent_get_reposted_by(wf_agent *agent, const char *uri,
                                   int limit, const char *cursor,
                                   wf_response *out);

/* Graph queries — return raw JSON in `out`; caller frees with wf_response_free. */
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

/* Notifications — return raw JSON in `out`; caller frees with wf_response_free. */
wf_status wf_agent_list_notifications(wf_agent *agent, int limit, const char *cursor,
                                        wf_response *out);
wf_status wf_agent_update_seen_notifications(wf_agent *agent, const char *seen_at);
wf_status wf_agent_get_unread_count(wf_agent *agent, wf_response *out);

/* Actor search — return raw JSON in `out`; caller frees with wf_response_free. */
wf_status wf_agent_search_actors(wf_agent *agent, const char *query,
                                int limit, const char *cursor, wf_response *out);

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

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_AGENT_H */
