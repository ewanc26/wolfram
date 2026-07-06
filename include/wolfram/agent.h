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

/* Graph queries — return raw JSON in `out`; caller frees with wf_response_free. */
wf_status wf_agent_get_follows(wf_agent *agent, const char *actor,
                               int limit, const char *cursor, wf_response *out);
wf_status wf_agent_get_followers(wf_agent *agent, const char *actor,
                                 int limit, const char *cursor, wf_response *out);

/* Notifications — return raw JSON in `out`; caller frees with wf_response_free. */
wf_status wf_agent_list_notifications(wf_agent *agent, int limit, const char *cursor,
                                       wf_response *out);
wf_status wf_agent_update_seen_notifications(wf_agent *agent, const char *seen_at);

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

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_AGENT_H */
