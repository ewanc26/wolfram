#include "wolfram/agent.h"
#include "wolfram/util.h"

#include "wolfram/identity.h"
#include "wolfram/xrpc.h"
#include "wolfram/repo.h"
#include "wolfram/richtext.h"
#include "wolfram/server.h"
#include "wolfram/session.h"
#include "wolfram/syntax.h"
#include "wolfram/store.h"

#include "_agent_struct.h"

#include <cJSON.h>
#include "wolfram/atproto_lex.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WF_AGENT_CREATE_RECORD_NSID "com.atproto.repo.createRecord"
#define WF_AGENT_DELETE_RECORD_NSID "com.atproto.repo.deleteRecord"
#define WF_AGENT_PUT_RECORD_NSID "com.atproto.repo.putRecord"
#define WF_AGENT_POST_COLLECTION "app.bsky.feed.post"
#define WF_AGENT_POST_RECORD_TYPE "app.bsky.feed.post"
#define WF_AGENT_FOLLOW_COLLECTION "app.bsky.graph.follow"
#define WF_AGENT_FOLLOW_RECORD_TYPE "app.bsky.graph.follow"
#define WF_AGENT_LIKE_COLLECTION "app.bsky.feed.like"
#define WF_AGENT_LIKE_RECORD_TYPE "app.bsky.feed.like"
#define WF_AGENT_REPOST_COLLECTION "app.bsky.feed.repost"
#define WF_AGENT_REPOST_RECORD_TYPE "app.bsky.feed.repost"
#define WF_AGENT_PROFILE_COLLECTION "app.bsky.actor.profile"
#define WF_AGENT_PROFILE_RECORD_TYPE "app.bsky.actor.profile"
#define WF_AGENT_PROFILE_RKEY "self"
#define WF_AGENT_UPLOAD_BLOB_NSID "com.atproto.repo.uploadBlob"
#define WF_AGENT_RESOLVE_HANDLE_NSID "com.atproto.identity.resolveHandle"
#define WF_AGENT_FACET_MENTION_TYPE "app.bsky.richtext.facet#mention"
#define WF_AGENT_FACET_LINK_TYPE "app.bsky.richtext.facet#link"
#define WF_AGENT_FACET_TAG_TYPE "app.bsky.richtext.facet#tag"

wf_xrpc_client *wf_agent_get_xrpc_client(wf_agent *agent) {
    return agent ? agent->client : NULL;
}

static void wf_agent_post_result_reset(wf_agent_post_result *result) {
    if (!result) {
        return;
    }

    free(result->uri);
    free(result->cid);
    memset(result, 0, sizeof(*result));
}

static void wf_agent_profile_reset(wf_agent_profile *profile) {
    if (!profile) {
        return;
    }

    free(profile->did);
    free(profile->handle);
    free(profile->display_name);
    free(profile->description);
    free(profile->avatar);
    free(profile->avatar_cid);
    free(profile->following);
    free(profile->blocking);
    free(profile->pinned_post_uri);
    memset(profile, 0, sizeof(*profile));
}

static void
wf_agent_server_description_reset(wf_agent_server_description *desc) {
    if (!desc) {
        return;
    }

    free(desc->did);
    for (size_t i = 0; i < desc->available_user_domain_count; ++i) {
        free(desc->available_user_domains[i]);
    }
    free(desc->available_user_domains);
    free(desc->privacy_policy);
    free(desc->terms_of_service);
    free(desc->contact_email);
    memset(desc, 0, sizeof(*desc));
    desc->invite_code_required = -1;
    desc->phone_verification_required = -1;
}

static void wf_agent_app_password_reset(wf_agent_app_password *pwd) {
    if (!pwd) {
        return;
    }

    free(pwd->name);
    free(pwd->created_at);
    memset(pwd, 0, sizeof(*pwd));
    pwd->privileged = -1;
}

static void wf_agent_app_password_list_reset(wf_agent_app_password_list *list) {
    if (!list) {
        return;
    }

    for (size_t i = 0; i < list->password_count; ++i) {
        wf_agent_app_password_reset(&list->passwords[i]);
    }
    free(list->passwords);
    memset(list, 0, sizeof(*list));
}

static void wf_agent_session_data_reset(wf_session_data *data) {
    if (!data) {
        return;
    }

    free(data->access_jwt);
    free(data->refresh_jwt);
    free(data->handle);
    free(data->did);
    free(data->email);
    free(data->status);
    free(data->pds_url);
    memset(data, 0, sizeof(*data));
    data->email_confirmed = -1;
    data->email_auth_factor = -1;
    data->active = -1;
}

static int wf_agent_is_logged_in(const wf_agent *agent);

static wf_status wf_agent_session_data_copy(wf_session_data *dst,
                                            const wf_session_data *src) {
    if (!dst || !src) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_session_data_reset(dst);

    wf_status status = wf_str_set(&dst->access_jwt, src->access_jwt);
    if (status == WF_OK) {
        status = wf_str_set(&dst->refresh_jwt, src->refresh_jwt);
    }
    if (status == WF_OK) {
        status = wf_str_set(&dst->handle, src->handle);
    }
    if (status == WF_OK) {
        status = wf_str_set(&dst->did, src->did);
    }
    if (status == WF_OK) {
        status = wf_str_set(&dst->email, src->email);
    }
    if (status == WF_OK) {
        status = wf_str_set(&dst->status, src->status);
    }
    if (status == WF_OK) {
        status = wf_str_set(&dst->pds_url, src->pds_url);
    }

    if (status == WF_OK) {
        dst->email_confirmed = src->email_confirmed;
        dst->email_auth_factor = src->email_auth_factor;
        dst->active = src->active;
    } else {
        wf_agent_session_data_reset(dst);
    }

    return status;
}

static void wf_agent_sync_auth(wf_agent *agent) {
    if (!agent || !agent->client || !agent->session) {
        return;
    }

    wf_xrpc_client_set_auth(agent->client, wf_agent_is_logged_in(agent)
                                               ? agent->session->data.access_jwt
                                               : NULL);
}

/* Route the agent's PDS client at the session's discovered PDS endpoint (from
 * the DID document), so data-plane XRPC calls hit the account's real PDS rather
 * than the login/entryway host. No-op when no PDS was discovered. */
static void wf_agent_apply_session_pds(wf_agent *agent) {
    if (!agent || !agent->client || !agent->session) {
        return;
    }
    const char *pds = agent->session->data.pds_url;
    if (pds && pds[0]) {
        wf_xrpc_client_set_base_url(agent->client, pds);
    }
}

/* Transport refresh hook: on an expired/invalid access token the transport
 * calls this to refresh the session and re-install the new access JWT (and any
 * re-pointed PDS) before retrying the original request exactly once. */
static wf_status wf_agent_refresh_cb(void *userdata) {
    wf_agent *agent = userdata;
    if (!agent || !agent->session || !wf_session_has_session(agent->session)) {
        return WF_ERR_STATE;
    }

    wf_status status = wf_session_refresh(agent->session);
    if (status == WF_OK) {
        wf_xrpc_client_set_auth(agent->client, agent->session->data.access_jwt);
        wf_agent_apply_session_pds(agent);
    }
    return status;
}

static int wf_agent_make_rfc3339_timestamp(char *buf, size_t buf_len) {
    time_t now = time(NULL);
    struct tm tm_utc;
    struct tm *tmp = gmtime(&now);
    if (!tmp) {
        return 0;
    }

    tm_utc = *tmp;
    return strftime(buf, buf_len, "%Y-%m-%dT%H:%M:%SZ", &tm_utc) != 0;
}

static int wf_agent_is_logged_in(const wf_agent *agent) {
    return agent && agent->session && wf_session_has_session(agent->session) &&
           agent->session->data.did && agent->session->data.access_jwt;
}

static wf_status wf_agent_profile_from_response(const wf_response *res,
                                                wf_agent_profile *out) {
    if (!res || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_profile_reset(out);

    cJSON *root = cJSON_ParseWithLength(res->body, res->body_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    cJSON *did = cJSON_GetObjectItemCaseSensitive(root, "did");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(root, "handle");
    if (!cJSON_IsString(did) || !cJSON_IsString(handle) || !did->valuestring ||
        !handle->valuestring || !wf_syntax_did_is_valid(did->valuestring) ||
        !wf_syntax_handle_is_valid(handle->valuestring)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = wf_str_set(&out->did, did->valuestring);
    if (status == WF_OK) {
        status = wf_str_set(&out->handle, handle->valuestring);
    }

    cJSON *display_name = cJSON_GetObjectItemCaseSensitive(root, "displayName");
    if (status == WF_OK && cJSON_IsString(display_name) &&
        display_name->valuestring) {
        status = wf_str_set(&out->display_name, display_name->valuestring);
    }

    cJSON *description = cJSON_GetObjectItemCaseSensitive(root, "description");
    if (status == WF_OK && cJSON_IsString(description) &&
        description->valuestring) {
        status = wf_str_set(&out->description, description->valuestring);
    }

    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(root, "avatar");
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        /* A bare string rather than a blob object: keep the server value in
         * avatar_cid as before, and in avatar where the URL belongs. */
        status = wf_str_set(&out->avatar_cid, avatar->valuestring);
        if (status == WF_OK) {
            status = wf_str_set(&out->avatar, avatar->valuestring);
        }
    } else if (status == WF_OK && cJSON_IsObject(avatar)) {
        /* The shape getProfile actually sends. Reading only the string form
         * meant a real server response yielded no avatar at all, so a client
         * drawing a profile had nothing to fetch. */
        cJSON *url = cJSON_GetObjectItemCaseSensitive(avatar, "url");
        cJSON *ref = cJSON_GetObjectItemCaseSensitive(avatar, "ref");
        cJSON *link = cJSON_IsObject(ref)
                          ? cJSON_GetObjectItemCaseSensitive(ref, "$link")
                          : NULL;

        if (cJSON_IsString(url) && url->valuestring) {
            status = wf_str_set(&out->avatar, url->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(link) && link->valuestring) {
            status = wf_str_set(&out->avatar_cid, link->valuestring);
        }
    }

    cJSON *pinned = cJSON_GetObjectItemCaseSensitive(root, "pinnedPost");
    if (status == WF_OK && cJSON_IsObject(pinned)) {
        cJSON *pinned_uri = cJSON_GetObjectItemCaseSensitive(pinned, "uri");
        if (cJSON_IsString(pinned_uri) && pinned_uri->valuestring) {
            status = wf_str_set(&out->pinned_post_uri, pinned_uri->valuestring);
        }
    }

    cJSON *viewer = cJSON_GetObjectItemCaseSensitive(root, "viewer");
    if (status == WF_OK && cJSON_IsObject(viewer)) {
        cJSON *following =
            cJSON_GetObjectItemCaseSensitive(viewer, "following");
        if (cJSON_IsString(following) && following->valuestring) {
            status = wf_str_set(&out->following, following->valuestring);
        }

        cJSON *blocking = cJSON_GetObjectItemCaseSensitive(viewer, "blocking");
        if (status == WF_OK && cJSON_IsString(blocking) &&
            blocking->valuestring) {
            status = wf_str_set(&out->blocking, blocking->valuestring);
        }

        cJSON *muted = cJSON_GetObjectItemCaseSensitive(viewer, "muted");
        if (cJSON_IsBool(muted)) {
            out->muted = cJSON_IsTrue(muted);
        }
    }

    cJSON *followers_count =
        cJSON_GetObjectItemCaseSensitive(root, "followersCount");
    cJSON *follows_count =
        cJSON_GetObjectItemCaseSensitive(root, "followsCount");
    cJSON *posts_count = cJSON_GetObjectItemCaseSensitive(root, "postsCount");

    if (status == WF_OK && cJSON_IsNumber(followers_count)) {
        double value = followers_count->valuedouble;
        if (value > (double)INT_MAX) {
            out->followers_count = INT_MAX;
        } else if (value < (double)INT_MIN) {
            out->followers_count = INT_MIN;
        } else {
            out->followers_count = (int)value;
        }
    }
    if (status == WF_OK && cJSON_IsNumber(follows_count)) {
        double value = follows_count->valuedouble;
        if (value > (double)INT_MAX) {
            out->follows_count = INT_MAX;
        } else if (value < (double)INT_MIN) {
            out->follows_count = INT_MIN;
        } else {
            out->follows_count = (int)value;
        }
    }
    if (status == WF_OK && cJSON_IsNumber(posts_count)) {
        double value = posts_count->valuedouble;
        if (value > (double)INT_MAX) {
            out->posts_count = INT_MAX;
        } else if (value < (double)INT_MIN) {
            out->posts_count = INT_MIN;
        } else {
            out->posts_count = (int)value;
        }
    }

    if (status != WF_OK) {
        wf_agent_profile_reset(out);
    }

    cJSON_Delete(root);
    return status;
}

wf_status wf_agent_parse_profile(const char *json, size_t json_len,
                                 wf_agent_profile *out) {
    wf_response response = {0};
    if (!json || !json_len || !out) return WF_ERR_INVALID_ARG;
    response.body = (char *)json;
    response.body_len = json_len;
    return wf_agent_profile_from_response(&response, out);
}

static wf_status wf_agent_put_record_call(wf_agent *agent,
                                          const char *collection,
                                          const char *rkey, cJSON *record,
                                          wf_agent_post_result *out) {
    if (!wf_agent_is_logged_in(agent) || !collection || !rkey || !record ||
        !out) {
        cJSON_Delete(record);
        return WF_ERR_INVALID_ARG;
    }

    if (wf_syntax_nsid_validate(collection) != WF_OK ||
        !wf_syntax_record_key_is_valid(rkey)) {
        cJSON_Delete(record);
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(root, "repo", agent->session->data.did) ||
        !cJSON_AddStringToObject(root, "collection", collection) ||
        !cJSON_AddStringToObject(root, "rkey", rkey) ||
        !cJSON_AddItemToObject(root, "record", record)) {
        cJSON_Delete(record);
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    wf_agent_sync_auth(agent);

    wf_response res = {0};
    wf_status status =
        wf_xrpc_procedure(agent->client, WF_AGENT_PUT_RECORD_NSID, json, &res);
    free(json);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    wf_agent_post_result_reset(out);

    cJSON *resp_root = cJSON_ParseWithLength(res.body, res.body_len);
    if (!resp_root) {
        wf_response_free(&res);
        return WF_ERR_PARSE;
    }

    cJSON *uri = cJSON_GetObjectItemCaseSensitive(resp_root, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(resp_root, "cid");
    if (!cJSON_IsString(uri) || !cJSON_IsString(cid) || !uri->valuestring ||
        !cid->valuestring) {
        cJSON_Delete(resp_root);
        wf_response_free(&res);
        return WF_ERR_PARSE;
    }

    status = wf_str_set(&out->uri, uri->valuestring);
    if (status == WF_OK) {
        status = wf_str_set(&out->cid, cid->valuestring);
    }
    if (status != WF_OK) {
        wf_agent_post_result_reset(out);
    }

    cJSON_Delete(resp_root);
    wf_response_free(&res);
    return status;
}

static int wf_agent_int_to_str(int value, char *buf, size_t buf_len) {
    return snprintf(buf, buf_len, "%d", value) > 0;
}

wf_agent *wf_agent_new(const char *service_url) {
    if (!service_url) {
        return NULL;
    }

    wf_agent *agent = calloc(1, sizeof(*agent));
    if (!agent) {
        return NULL;
    }

    agent->service_url = wf_str_dup(service_url);
    if (!agent->service_url) {
        free(agent);
        return NULL;
    }

    agent->client = wf_xrpc_client_new(service_url);
    if (!agent->client) {
        free(agent->service_url);
        free(agent);
        return NULL;
    }

    agent->chat_client = NULL;

    agent->session = wf_session_new(service_url);
    if (!agent->session) {
        wf_xrpc_client_free(agent->client);
        free(agent->service_url);
        free(agent);
        return NULL;
    }

    /* Enable one transparent refresh+retry on the data-plane client when an
     * access token expires mid-request. */
    wf_xrpc_client_set_refresh_handler(agent->client, wf_agent_refresh_cb,
                                       agent);

    return agent;
}

void wf_agent_apply_tls(wf_agent *agent, wf_xrpc_client *client) {
    if (!agent || !client) {
        return;
    }
    if (agent->ca_bundle) {
        wf_xrpc_client_set_ca_bundle(client, agent->ca_bundle);
    }
    if (agent->tls_rng) {
        /* Deliberately unchecked: an application that asked for its own RNG
         * has already been told by wf_agent_set_tls_rng whether this build can
         * honour it, and failing a chat-service call here would be a confusing
         * place to report it a second time. */
        (void)wf_xrpc_client_set_tls_rng(client, agent->tls_rng,
                                         agent->tls_rng_userdata);
    }
}

wf_status wf_agent_set_post_langs(wf_agent *agent, const char *langs) {
    if (!agent) {
        return WF_ERR_INVALID_ARG;
    }
    char *copy = NULL;
    if (langs && langs[0]) {
        /* At most three tags, each 2-35 chars of [A-Za-z0-9-]. */
        int tags = 1;
        size_t run = 0;
        for (const char *p = langs;; p++) {
            if (*p == ',' || *p == '\0') {
                if (run < 2 || run > 35) {
                    return WF_ERR_INVALID_ARG;
                }
                if (*p == '\0') {
                    break;
                }
                if (++tags > 3) {
                    return WF_ERR_INVALID_ARG;
                }
                run = 0;
            } else if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                       (*p >= '0' && *p <= '9') || *p == '-') {
                run++;
            } else {
                return WF_ERR_INVALID_ARG;
            }
        }
        copy = wf_str_dup(langs);
        if (!copy) {
            return WF_ERR_ALLOC;
        }
    }
    free(agent->post_langs);
    agent->post_langs = copy;
    return WF_OK;
}

wf_status wf_agent_set_ca_bundle(wf_agent *agent, const char *path) {
    if (!agent) {
        return WF_ERR_INVALID_ARG;
    }

    char *copy = NULL;
    if (path) {
        copy = wf_str_dup(path);
        if (!copy) {
            return WF_ERR_ALLOC;
        }
    }

    free(agent->ca_bundle);
    agent->ca_bundle = copy;

    /* Every client that already exists. The chat client picks it up when it is
     * created, via wf_agent_apply_tls. */
    wf_xrpc_client_set_ca_bundle(agent->client, path);
    if (agent->session) {
        wf_xrpc_client_set_ca_bundle(agent->session->client, path);
    }
    if (agent->chat_client) {
        wf_xrpc_client_set_ca_bundle(agent->chat_client, path);
    }
    return WF_OK;
}

wf_status wf_agent_set_tls_rng(wf_agent *agent, wf_tls_rng_fn fn,
                               void *userdata) {
    if (!agent) {
        return WF_ERR_INVALID_ARG;
    }

    wf_status status = wf_xrpc_client_set_tls_rng(agent->client, fn, userdata);
    if (status != WF_OK) {
        /* Report the first refusal and install nothing, so the agent never
         * ends up with some clients using the application RNG and others not.
         */
        return status;
    }

    agent->tls_rng = fn;
    agent->tls_rng_userdata = userdata;

    if (agent->session) {
        (void)wf_xrpc_client_set_tls_rng(agent->session->client, fn, userdata);
    }
    if (agent->chat_client) {
        (void)wf_xrpc_client_set_tls_rng(agent->chat_client, fn, userdata);
    }
    return WF_OK;
}

void wf_agent_free(wf_agent *agent) {
    if (!agent) {
        return;
    }

    wf_session_free(agent->session);
    wf_xrpc_client_free(agent->client);
    wf_xrpc_client_free(agent->chat_client);
    free(agent->service_url);
    free(agent->post_langs);
    free(agent->ca_bundle);
    free(agent->mirror_did);
    free(agent->mirror_signing_key);
    wf_car_free(&agent->mirror);
#ifdef WOLFRAM_BUILD_STORE
    if (agent->persisted_labels) {
        wf_mod_labels_free(agent->persisted_labels,
                           agent->persisted_label_count);
    }
#endif
    free(agent);
}

wf_status wf_agent_login(wf_agent *agent, const char *identifier,
                         const char *password) {
    if (!agent || !agent->session || !agent->client || !identifier ||
        !password) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_client_set_auth(agent->client, NULL);

    wf_status status = wf_session_login(agent->session, identifier, password);
    if (status != WF_OK) {
        return status;
    }

    wf_agent_sync_auth(agent);
    wf_agent_apply_session_pds(agent);
    return WF_OK;
}

wf_status wf_agent_login_discovered(wf_agent *agent, const char *identifier,
                                    const char *password, char **out_pds) {
    if (out_pds) {
        *out_pds = NULL;
    }
    if (!agent || !agent->session || !agent->client || !identifier ||
        !identifier[0] || !password || !out_pds) {
        return WF_ERR_INVALID_ARG;
    }

    char *did = NULL;
    char *pds = NULL;
    wf_status status;
    if (wf_did_method_of(identifier) != WF_DID_METHOD_UNKNOWN) {
        did = wf_str_dup(identifier);
        status = did ? WF_OK : WF_ERR_ALLOC;
    } else {
        status = wf_handle_resolve(agent->client, identifier, &did);
    }
    if (status == WF_OK) {
        status =
            wf_did_resolve_service_by_id(agent->client, did, "#atproto_pds",
                                         "AtprotoPersonalDataServer", &pds);
    }
    if (status != WF_OK) {
        /* Not resolvable (no DNS or HTTP record, or no PDS in the document):
         * sign in at the agent's current host, as before discovery existed. */
        free(did);
        free(pds);
        status = wf_agent_login(agent, identifier, password);
        return status;
    }

    wf_xrpc_client_set_base_url(agent->client, pds);
    status = wf_agent_login(agent, identifier, password);
    free(did);
    if (status != WF_OK) {
        free(pds);
        return status;
    }
    *out_pds = pds;
    return WF_OK;
}

wf_status wf_agent_resume(wf_agent *agent, const wf_session_data *data) {
    if (!agent || !agent->session || !agent->client || !data) {
        return WF_ERR_INVALID_ARG;
    }

    wf_status status = wf_session_resume(agent->session, data);
    wf_agent_sync_auth(agent);
    wf_agent_apply_session_pds(agent);
    return status;
}

wf_status wf_agent_set_bearer(wf_agent *agent, const char *access_token,
                              const char *handle, const char *did) {
    if (!agent || !agent->session || !agent->client || !access_token ||
        !access_token[0] || !handle || !handle[0] || !did || !did[0] ||
        /* The syntax validators return nonzero for valid input; they are
         * predicates, not wf_status. */
        !wf_syntax_did_is_valid(did) || !wf_syntax_handle_is_valid(handle)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_session_data_reset(&agent->session->data);

    wf_status status =
        wf_str_set(&agent->session->data.access_jwt, access_token);
    if (status == WF_OK)
        status = wf_str_set(&agent->session->data.handle, handle);
    if (status == WF_OK) status = wf_str_set(&agent->session->data.did, did);
    if (status == WF_OK)
        status = wf_str_set(&agent->session->data.pds_url, agent->service_url);
    if (status != WF_OK) {
        wf_agent_session_data_reset(&agent->session->data);
        agent->session->has_session = 0;
        wf_xrpc_client_set_auth(agent->client, NULL);
        return status;
    }

    agent->session->has_session = 1;
    wf_xrpc_client_set_auth(agent->client, access_token);
    return WF_OK;
}

wf_status wf_agent_get_session(wf_agent *agent) {
    if (!agent || !agent->session || !agent->client) {
        return WF_ERR_INVALID_ARG;
    }

    wf_status status = wf_session_get(agent->session);
    wf_agent_sync_auth(agent);
    return status;
}

wf_status wf_agent_logout(wf_agent *agent) {
    if (!agent || !agent->session || !agent->client) {
        return WF_ERR_INVALID_ARG;
    }

    wf_status status = wf_session_delete(agent->session);
    wf_agent_sync_auth(agent);
    return status;
}

wf_status wf_agent_get_session_data(wf_agent *agent, wf_session_data *out) {
    if (!agent || !out || !wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }

    return wf_agent_session_data_copy(out, &agent->session->data);
}

void wf_agent_session_data_free(wf_session_data *data) {
    wf_agent_session_data_reset(data);
}

const char *wf_agent_get_did(wf_agent *agent) {
    if (!wf_agent_is_logged_in(agent)) {
        return NULL;
    }

    return agent->session->data.did;
}

const char *wf_agent_get_handle(wf_agent *agent) {
    if (!wf_agent_is_logged_in(agent)) {
        return NULL;
    }

    return agent->session->data.handle;
}

/* Return the XRPC error message from the agent's most recent request, or NULL
 * if the last request succeeded or carried no error envelope. */
const char *wf_agent_last_error(const wf_agent *agent) {
    if (!agent || !agent->client) return NULL;
    /* Login and refresh go through the session's own client, so a failed
     * createSession leaves its envelope there rather than on the data-plane
     * client. */
    const char *err = wf_xrpc_last_error(agent->client);
    if (!err && agent->session && agent->session->client) {
        err = wf_xrpc_last_error(agent->session->client);
    }
    return err;
}

void wf_agent_post_result_free(wf_agent_post_result *result) {
    wf_agent_post_result_reset(result);
}

void wf_agent_profile_free(wf_agent_profile *profile) {
    wf_agent_profile_reset(profile);
}

/* ── getRecord ──────────────────────────────────────────────────────── */

wf_status wf_agent_get_record(wf_agent *agent, const char *collection,
                              const char *rkey, wf_response *out) {
    if (!agent || !collection || !rkey || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }

    if (wf_syntax_nsid_validate(collection) != WF_OK ||
        !wf_syntax_record_key_is_valid(rkey)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;

    if (!cJSON_AddStringToObject(root, "repo", agent->session->data.did) ||
        !cJSON_AddStringToObject(root, "collection", collection) ||
        !cJSON_AddStringToObject(root, "rkey", rkey)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_procedure(
        agent->client, "com.atproto.repo.getRecord", json, out);
    free(json);
    return status;
}

/* ── putRecord ──────────────────────────────────────────────────────── */

wf_status wf_agent_put_record(wf_agent *agent, const char *collection,
                              const char *rkey, const char *record_json,
                              wf_agent_post_result *out) {
    if (!agent || !collection || !rkey || !record_json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }

    if (wf_syntax_nsid_validate(collection) != WF_OK ||
        !wf_syntax_record_key_is_valid(rkey)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *value = cJSON_Parse(record_json);
    if (!value) return WF_ERR_PARSE;
    if (!cJSON_IsObject(value)) {
        cJSON_Delete(value);
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        cJSON_Delete(value);
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(root, "repo", agent->session->data.did) ||
        !cJSON_AddStringToObject(root, "collection", collection) ||
        !cJSON_AddStringToObject(root, "rkey", rkey) ||
        !cJSON_AddItemToObject(root, "record", value)) {
        cJSON_Delete(value);
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);

    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(
        agent->client, "com.atproto.repo.putRecord", json, &res);
    free(json);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    wf_agent_post_result_reset(out);

    cJSON *resp_root = cJSON_ParseWithLength(res.body, res.body_len);
    if (!resp_root) {
        wf_response_free(&res);
        return WF_ERR_PARSE;
    }

    cJSON *uri = cJSON_GetObjectItemCaseSensitive(resp_root, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(resp_root, "cid");
    if (!cJSON_IsString(uri) || !cJSON_IsString(cid) || !uri->valuestring ||
        !cid->valuestring) {
        cJSON_Delete(resp_root);
        wf_response_free(&res);
        return WF_ERR_PARSE;
    }

    status = wf_str_set(&out->uri, uri->valuestring);
    if (status == WF_OK) {
        status = wf_str_set(&out->cid, cid->valuestring);
    }
    if (status != WF_OK) {
        wf_agent_post_result_reset(out);
    }

    cJSON_Delete(resp_root);
    wf_response_free(&res);
    return status;
}

/* ── listRecords ───────────────────────────────────────────────────── */

wf_status wf_agent_list_records(wf_agent *agent, const char *collection,
                                int limit, const char *cursor,
                                wf_response *out) {
    if (!agent || !collection || !collection[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }
    if (wf_syntax_nsid_validate(collection) != WF_OK) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[4];
    size_t param_count = 0;
    char limit_buf[16];

    if (!agent->session->data.did) return WF_ERR_INVALID_ARG;
    params[param_count].name = "repo";
    params[param_count].value = agent->session->data.did;
    param_count++;

    params[param_count].name = "collection";
    params[param_count].value = collection;
    param_count++;

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

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "com.atproto.repo.listRecords",
                                params, param_count, out);
}

wf_status wf_agent_get_profile(wf_agent *agent, const char *actor,
                               wf_agent_profile *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[] = {
        {"actor", actor},
    };

    wf_response res = {0};
    wf_status status = wf_xrpc_query_params(
        agent->client, "app.bsky.actor.getProfile", params, 1, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_profile_from_response(&res, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_profile_raw(wf_agent *agent, const char *actor,
                                   wf_response *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[] = {
        {"actor", actor},
    };

    return wf_xrpc_query_params(agent->client, "app.bsky.actor.getProfile",
                                params, 1, out);
}

wf_status wf_agent_get_preferences(wf_agent *agent, char **out_json) {
    if (!agent || !out_json) {
        return WF_ERR_INVALID_ARG;
    }

    *out_json = NULL;

    wf_response res = {0};
    wf_status status = wf_xrpc_query_params(
        agent->client, "app.bsky.actor.getPreferences", NULL, 0, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    wf_status ret = WF_ERR_PARSE;
    cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
    if (root) {
        cJSON *prefs = cJSON_GetObjectItemCaseSensitive(root, "preferences");
        if (cJSON_IsArray(prefs)) {
            char *json = cJSON_PrintUnformatted(prefs);
            if (json) {
                *out_json = json;
                ret = WF_OK;
            } else {
                ret = WF_ERR_ALLOC;
            }
        }
        cJSON_Delete(root);
    }

    wf_response_free(&res);
    return ret;
}

wf_status wf_agent_get_preferences_typed(
    wf_agent *agent, wf_lex_app_bsky_actor_get_preferences_main_output **out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    *out = NULL;

    wf_lex_app_bsky_actor_get_preferences_main_params params = {0};
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_app_bsky_actor_get_preferences_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    wf_lex_app_bsky_actor_get_preferences_main_output *output = NULL;
    status = wf_lex_app_bsky_actor_get_preferences_main_output_decode_json(
        res.body, res.body_len, &output);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = output;
    }
    return status;
}

wf_status wf_agent_put_preferences_json(
    wf_agent *agent,
    const wf_lex_app_bsky_actor_put_preferences_main_input *input,
    wf_response *out) {
    if (!agent || !input || !out) return WF_ERR_INVALID_ARG;
    wf_agent_sync_auth(agent);
    return wf_lex_app_bsky_actor_put_preferences_main_call(agent->client, input,
                                                           out);
}

wf_status wf_agent_search_actors(wf_agent *agent, const char *query, int limit,
                                 const char *cursor, wf_response *out) {
    if (!agent || !query || !query[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "q";
    params[param_count].value = query;
    param_count++;

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

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.actor.searchActors",
                                params, param_count, out);
}

wf_status wf_agent_search_actors_typeahead(wf_agent *agent, const char *query,
                                           int limit, wf_response *out) {
    if (!agent || !query || !query[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
    wf_xrpc_param params[2];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "q";
    params[param_count].value = query;
    param_count++;

    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf))) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "limit";
        params[param_count].value = limit_buf;
        param_count++;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client,
                                "app.bsky.actor.searchActorsTypeahead", params,
                                param_count, out);
}

wf_status wf_agent_update_profile(wf_agent *agent,
                                  const wf_agent_profile_update *update) {
    if (!agent || !update) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }

    char created_at[32];
    if (!wf_agent_make_rfc3339_timestamp(created_at, sizeof(created_at)) ||
        !wf_syntax_datetime_is_valid(created_at)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *record = cJSON_CreateObject();
    if (!record) {
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(record, "$type",
                                 WF_AGENT_PROFILE_RECORD_TYPE) ||
        !cJSON_AddStringToObject(record, "createdAt", created_at)) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    if (update->display_name) {
        if (!cJSON_AddStringToObject(record, "displayName",
                                     update->display_name)) {
            cJSON_Delete(record);
            return WF_ERR_ALLOC;
        }
    }

    if (update->description) {
        if (!cJSON_AddStringToObject(record, "description",
                                     update->description)) {
            cJSON_Delete(record);
            return WF_ERR_ALLOC;
        }
    }

    if (update->avatar_cid) {
        cJSON *avatar = cJSON_CreateObject();
        if (!avatar) {
            cJSON_Delete(record);
            return WF_ERR_ALLOC;
        }
        cJSON *ref = cJSON_CreateObject();
        if (!ref) {
            cJSON_Delete(avatar);
            cJSON_Delete(record);
            return WF_ERR_ALLOC;
        }
        if (!cJSON_AddStringToObject(ref, "$link", update->avatar_cid) ||
            !cJSON_AddItemToObject(avatar, "ref", ref) ||
            !cJSON_AddStringToObject(avatar, "$type", "blob") ||
            !cJSON_AddItemToObject(record, "avatar", avatar)) {
            cJSON_Delete(ref);
            cJSON_Delete(avatar);
            cJSON_Delete(record);
            return WF_ERR_ALLOC;
        }
    }

    if (update->banner_cid) {
        cJSON *banner = cJSON_CreateObject();
        if (!banner) {
            cJSON_Delete(record);
            return WF_ERR_ALLOC;
        }
        cJSON *ref = cJSON_CreateObject();
        if (!ref) {
            cJSON_Delete(banner);
            cJSON_Delete(record);
            return WF_ERR_ALLOC;
        }
        if (!cJSON_AddStringToObject(ref, "$link", update->banner_cid) ||
            !cJSON_AddItemToObject(banner, "ref", ref) ||
            !cJSON_AddStringToObject(banner, "$type", "blob") ||
            !cJSON_AddItemToObject(record, "banner", banner)) {
            cJSON_Delete(ref);
            cJSON_Delete(banner);
            cJSON_Delete(record);
            return WF_ERR_ALLOC;
        }
    }

    wf_agent_post_result result = {0};
    wf_status status =
        wf_agent_put_record_call(agent, WF_AGENT_PROFILE_COLLECTION,
                                 WF_AGENT_PROFILE_RKEY, record, &result);
    wf_agent_post_result_free(&result);
    return status;
}

wf_status wf_agent_upload_blob(wf_agent *agent, const void *data,
                               size_t data_len, const char *content_type,
                               wf_response *out) {
    if (!agent || !data || data_len == 0 || !content_type || !out) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_upload_blob(agent->client, WF_AGENT_UPLOAD_BLOB_NSID, data,
                               data_len, content_type, out);
}

/* ── applyWrites ────────────────────────────────────────────────────── */

/* Unused: wf_agent_build_write_value removed */

static const char *wf_agent_write_type_str(wf_agent_write_type type) {
    switch (type) {
        case WF_AGENT_WRITE_CREATE:
            return "com.atproto.repo.applyWrites#create";
        case WF_AGENT_WRITE_UPDATE:
            return "com.atproto.repo.applyWrites#update";
        case WF_AGENT_WRITE_DELETE:
            return "com.atproto.repo.applyWrites#delete";
    }
    return NULL;
}

static wf_status wf_agent_add_write(cJSON *writes_array,
                                    const wf_agent_write *write) {
    cJSON *item = cJSON_CreateObject();
    if (!item) return WF_ERR_ALLOC;

    const char *type_str = wf_agent_write_type_str(write->type);
    if (!type_str || !cJSON_AddStringToObject(item, "$type", type_str) ||
        !cJSON_AddStringToObject(item, "collection", write->collection)) {
        cJSON_Delete(item);
        return WF_ERR_ALLOC;
    }

    if (write->rkey && write->rkey[0]) {
        if (!cJSON_AddStringToObject(item, "rkey", write->rkey)) {
            cJSON_Delete(item);
            return WF_ERR_ALLOC;
        }
    }

    if (write->type != WF_AGENT_WRITE_DELETE) {
        if (!write->value_json) {
            cJSON_Delete(item);
            return WF_ERR_INVALID_ARG;
        }
        cJSON *value = cJSON_Parse(write->value_json);
        if (!value) {
            cJSON_Delete(item);
            return WF_ERR_PARSE;
        }
        if (!cJSON_AddItemToObject(item, "value", value)) {
            cJSON_Delete(value);
            cJSON_Delete(item);
            return WF_ERR_ALLOC;
        }
    }

    if (!cJSON_AddItemToArray(writes_array, item)) {
        cJSON_Delete(item);
        return WF_ERR_ALLOC;
    }

    return WF_OK;
}

wf_status wf_agent_apply_writes(wf_agent *agent, const wf_agent_write *writes,
                                size_t write_count, wf_response *out) {
    size_t i;
    wf_status status;

    if (!agent || !writes || write_count == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }

    for (i = 0; i < write_count; i++) {
        if (!writes[i].collection) return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;

    if (!cJSON_AddStringToObject(root, "repo", agent->session->data.did)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    cJSON *writes_array = cJSON_AddArrayToObject(root, "writes");
    if (!writes_array) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    for (i = 0; i < write_count; i++) {
        status = wf_agent_add_write(writes_array, &writes[i]);
        if (status != WF_OK) {
            cJSON_Delete(root);
            return status;
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    status = wf_xrpc_procedure(agent->client, "com.atproto.repo.applyWrites",
                               json, out);
    free(json);
    return status;
}

wf_status wf_agent_resolve_handle(wf_agent *agent, const char *handle,
                                  char **out_did) {
    if (!agent || !handle || !handle[0] || !out_did) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_syntax_handle_is_valid(handle)) {
        return WF_ERR_INVALID_ARG;
    }

    *out_did = NULL;

    wf_xrpc_param params[] = {
        {"handle", handle},
    };

    wf_agent_sync_auth(agent);

    wf_response res = {0};
    wf_status status = wf_xrpc_query_params(
        agent->client, WF_AGENT_RESOLVE_HANDLE_NSID, params, 1, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
    if (!root) {
        wf_response_free(&res);
        return WF_ERR_PARSE;
    }

    cJSON *did = cJSON_GetObjectItemCaseSensitive(root, "did");
    if (!cJSON_IsString(did) || !did->valuestring ||
        !wf_syntax_did_is_valid(did->valuestring)) {
        cJSON_Delete(root);
        wf_response_free(&res);
        return WF_ERR_PARSE;
    }

    status = wf_str_set(out_did, did->valuestring);
    if (status != WF_OK) {
        free(*out_did);
        *out_did = NULL;
    }

    cJSON_Delete(root);
    wf_response_free(&res);
    return status;
}

/* Update handle – com.atproto.identity.updateHandle */
wf_status wf_agent_update_handle(wf_agent *agent, const char *new_handle) {
    if (!agent || !new_handle || !new_handle[0]) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_handle_is_valid(new_handle)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_com_atproto_identity_update_handle_main_input input = {
        .handle = new_handle};
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_com_atproto_identity_update_handle_main_call(
        agent->client, &input, &res);
    wf_response_free(&res);
    return status;
}

void wf_agent_server_description_free(wf_agent_server_description *desc) {
    wf_agent_server_description_reset(desc);
}

void wf_agent_app_password_free(wf_agent_app_password *pwd) {
    wf_agent_app_password_reset(pwd);
}

void wf_agent_app_password_list_free(wf_agent_app_password_list *list) {
    wf_agent_app_password_list_reset(list);
}

wf_status wf_agent_describe_server(wf_agent *agent,
                                   wf_agent_server_description *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_server_description_reset(out);

    wf_server_description sdesc = {0};
    wf_status status = wf_server_describe(agent->client, &sdesc);
    if (status != WF_OK) {
        return status;
    }

    status = wf_str_set(&out->did, sdesc.did);
    if (status == WF_OK) {
        out->invite_code_required = sdesc.invite_code_required;
        out->phone_verification_required = sdesc.phone_verification_required;
    }
    if (status == WF_OK && sdesc.available_user_domains_count > 0) {
        out->available_user_domains =
            calloc(sdesc.available_user_domains_count, sizeof(char *));
        if (!out->available_user_domains) {
            status = WF_ERR_ALLOC;
        }
    }
    if (status == WF_OK) {
        for (size_t i = 0; i < sdesc.available_user_domains_count; ++i) {
            out->available_user_domains[i] =
                wf_str_dup(sdesc.available_user_domains[i]);
            if (!out->available_user_domains[i]) {
                status = WF_ERR_ALLOC;
                break;
            }
            out->available_user_domain_count++;
        }
    }
    if (status == WF_OK) {
        status = wf_str_set(&out->privacy_policy, sdesc.links_privacy_policy);
    }
    if (status == WF_OK) {
        status =
            wf_str_set(&out->terms_of_service, sdesc.links_terms_of_service);
    }
    if (status == WF_OK) {
        status = wf_str_set(&out->contact_email, sdesc.contact_email);
    }

    if (status != WF_OK) {
        wf_agent_server_description_reset(out);
    }

    wf_server_describe_free(&sdesc);
    return status;
}

wf_status wf_agent_create_app_password(wf_agent *agent, const char *name,
                                       int privileged,
                                       wf_agent_app_password *out) {
    if (!agent || !agent->client || !out || !name || name[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_app_password_reset(out);

    wf_server_create_app_password_input input = {
        .name = name,
        .privileged = privileged,
    };

    wf_server_app_password spwd = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_server_create_app_password(agent->client, &input, &spwd);
    if (status != WF_OK) {
        return status;
    }

    status = wf_str_set(&out->name, spwd.name);
    if (status == WF_OK) {
        status = wf_str_set(&out->created_at, spwd.created_at);
    }
    if (status == WF_OK) {
        out->privileged = spwd.privileged;
    }

    if (status != WF_OK) {
        wf_agent_app_password_reset(out);
    }

    wf_server_app_password_free(&spwd);
    return status;
}

wf_status wf_agent_list_app_passwords(wf_agent *agent,
                                      wf_agent_app_password_list *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_app_password_list_reset(out);

    wf_server_app_password_list slist = {0};
    wf_agent_sync_auth(agent);
    wf_status status = wf_server_list_app_passwords(agent->client, &slist);
    if (status != WF_OK) {
        return status;
    }

    if (slist.password_count > 0) {
        out->passwords =
            calloc(slist.password_count, sizeof(wf_agent_app_password));
        if (!out->passwords) {
            status = WF_ERR_ALLOC;
        }
    }
    if (status == WF_OK) {
        for (size_t i = 0; i < slist.password_count; ++i) {
            wf_server_app_password *src = &slist.passwords[i];
            wf_agent_app_password *dst = &out->passwords[i];

            status = wf_str_set(&dst->name, src->name);
            if (status == WF_OK) {
                status = wf_str_set(&dst->created_at, src->created_at);
            }
            if (status == WF_OK) {
                dst->privileged = src->privileged;
            }
            if (status != WF_OK) {
                break;
            }
            out->password_count++;
        }
    }

    if (status != WF_OK) {
        wf_agent_app_password_list_reset(out);
    }

    wf_server_app_password_list_free(&slist);
    return status;
}

wf_status wf_agent_revoke_app_password(wf_agent *agent, const char *name) {
    if (!agent || !agent->client || !name || name[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_server_revoke_app_password_input input = {
        .name = name,
    };

    wf_agent_sync_auth(agent);
    return wf_server_revoke_app_password(agent->client, &input);
}

wf_status wf_agent_delete_account(wf_agent *agent, const char *did,
                                  const char *password, const char *token) {
    if (!agent || !agent->client || !did || !password || !token) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_server_delete_account_input input = {
        .did = did,
        .password = password,
        .token = token,
    };

    wf_agent_sync_auth(agent);
    return wf_server_delete_account(agent->client, &input);
}

/* ── actor status ───────────────────────────────────────────────────── */

wf_status wf_agent_put_actor_status(wf_agent *agent, const char *status,
                                    int duration_minutes,
                                    const char *embed_json,
                                    wf_agent_post_result *out) {
    if (!agent || !status || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *record = cJSON_CreateObject();
    if (!record) return WF_ERR_ALLOC;

    if (!cJSON_AddStringToObject(record, "status", status) ||
        !cJSON_AddStringToObject(record, "$type", "app.bsky.actor.status")) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    if (duration_minutes > 0) {
        if (!cJSON_AddNumberToObject(record, "durationMinutes",
                                     duration_minutes)) {
            cJSON_Delete(record);
            return WF_ERR_ALLOC;
        }
    }

    if (embed_json && embed_json[0]) {
        cJSON *embed = cJSON_Parse(embed_json);
        if (!embed) {
            cJSON_Delete(record);
            return WF_ERR_PARSE;
        }
        if (!cJSON_AddItemToObject(record, "embed", embed)) {
            cJSON_Delete(embed);
            cJSON_Delete(record);
            return WF_ERR_ALLOC;
        }
    }

    /* createdAt — use current time */
    time_t now = time(NULL);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    char ts_buf[32];
    strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    if (!cJSON_AddStringToObject(record, "createdAt", ts_buf)) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    char *json_str = cJSON_PrintUnformatted(record);
    cJSON_Delete(record);
    if (!json_str) return WF_ERR_ALLOC;

    wf_status status_ret = wf_agent_put_record(agent, "app.bsky.actor.status",
                                               "self", json_str, out);
    free(json_str);
    return status_ret;
}

wf_status wf_agent_get_video_job_status(wf_agent *agent, const char *job_id,
                                        wf_response *out) {
    if (!agent || !job_id || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }

    /* Use the generated marshalling rather than hand-building the query
     * param array: the NSID, param name ("jobId"), and encoding all come
     * from wf_lexgen, so this can't drift from the lexicon. */
    wf_lex_app_bsky_video_get_job_status_main_params params = {0};
    params.job_id = job_id;

    wf_agent_sync_auth(agent);
    return wf_lex_app_bsky_video_get_job_status_main_call(agent->client,
                                                          &params, out);
}

wf_status wf_agent_get_video_upload_limits(wf_agent *agent, wf_response *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_sync_auth(agent);
    return wf_lex_app_bsky_video_get_upload_limits_main_call(agent->client,
                                                             out);
}

/* ── server wrappers ─────────────────────────────────────────────────── */

wf_status wf_agent_create_invite_code(wf_agent *agent, int use_count,
                                      wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddNumberToObject(root, "useCount", use_count)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_procedure(
        agent->client, "com.atproto.server.createInviteCode", json, out);
    free(json);
    return status;
}

wf_status wf_agent_get_account_invite_codes(wf_agent *agent, int limit,
                                            const char *cursor,
                                            wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;

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

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client,
                                "com.atproto.server.getAccountInviteCodes",
                                params, param_count, out);
}

wf_status wf_agent_activate_account(wf_agent *agent, wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;
    wf_agent_sync_auth(agent);
    return wf_xrpc_procedure(agent->client,
                             "com.atproto.server.activateAccount", "{}", out);
}

wf_status wf_agent_deactivate_account(wf_agent *agent, wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;
    wf_agent_sync_auth(agent);
    return wf_xrpc_procedure(agent->client,
                             "com.atproto.server.deactivateAccount", "{}", out);
}

wf_status wf_agent_check_account_status(wf_agent *agent, wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;
    wf_agent_sync_auth(agent);
    return wf_xrpc_query(agent->client, "com.atproto.server.checkAccountStatus",
                         NULL, out);
}

wf_status wf_agent_confirm_email(wf_agent *agent, const char *email,
                                 const char *token, wf_response *out) {
    if (!agent || !email || !token || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "email", email) ||
        !cJSON_AddStringToObject(root, "token", token)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_procedure(
        agent->client, "com.atproto.server.confirmEmail", json, out);
    free(json);
    return status;
}

wf_status wf_agent_update_email(wf_agent *agent, const char *email,
                                wf_response *out) {
    if (!agent || !email || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "email", email)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_procedure(
        agent->client, "com.atproto.server.updateEmail", json, out);
    free(json);
    return status;
}

wf_status wf_agent_create_account(wf_agent *agent, const char *email,
                                  const char *handle, const char *did,
                                  const char *invite_code,
                                  const char *verification_phone,
                                  const char *password, wf_response *out) {
    if (!agent || !handle || !out) return WF_ERR_INVALID_ARG;

    cJSON *body = cJSON_CreateObject();
    if (!body) return WF_ERR_ALLOC;

    if (email && email[0]) cJSON_AddStringToObject(body, "email", email);
    if (!cJSON_AddStringToObject(body, "handle", handle)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }
    if (did && did[0]) cJSON_AddStringToObject(body, "did", did);
    if (invite_code && invite_code[0])
        cJSON_AddStringToObject(body, "inviteCode", invite_code);
    if (verification_phone && verification_phone[0])
        cJSON_AddStringToObject(body, "verificationPhone", verification_phone);
    if (password && password[0])
        cJSON_AddStringToObject(body, "password", password);

    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_procedure(
        agent->client, "com.atproto.server.createAccount", json, out);
    free(json);
    return status;
}

wf_status wf_agent_create_invite_codes(wf_agent *agent, int use_count,
                                       int code_count,
                                       const char *const *for_dids,
                                       size_t for_did_count, wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;

    cJSON *body = cJSON_CreateObject();
    if (!body) return WF_ERR_ALLOC;

    if (!cJSON_AddNumberToObject(body, "useCount", use_count) ||
        !cJSON_AddNumberToObject(body, "codeCount", code_count)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }

    if (for_dids && for_did_count > 0) {
        cJSON *arr = cJSON_AddArrayToObject(body, "forDids");
        if (!arr) {
            cJSON_Delete(body);
            return WF_ERR_ALLOC;
        }
        for (size_t i = 0; i < for_did_count; i++) {
            cJSON *item = cJSON_CreateString(for_dids[i]);
            if (!item || !cJSON_AddItemToArray(arr, item)) {
                cJSON_Delete(body);
                return WF_ERR_ALLOC;
            }
        }
    }

    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_procedure(
        agent->client, "com.atproto.server.createInviteCodes", json, out);
    free(json);
    return status;
}

wf_status wf_agent_request_account_delete(wf_agent *agent, wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;
    wf_agent_sync_auth(agent);
    return wf_xrpc_procedure(
        agent->client, "com.atproto.server.requestAccountDelete", "{}", out);
}

wf_status wf_agent_request_email_confirmation(wf_agent *agent,
                                              wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;
    wf_agent_sync_auth(agent);
    return wf_xrpc_procedure(agent->client,
                             "com.atproto.server.requestEmailConfirmation",
                             "{}", out);
}

wf_status wf_agent_request_email_update(wf_agent *agent, const char *email,
                                        wf_response *out) {
    if (!agent || !email || !out) return WF_ERR_INVALID_ARG;

    cJSON *body = cJSON_CreateObject();
    if (!body) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(body, "email", email)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_procedure(
        agent->client, "com.atproto.server.requestEmailUpdate", json, out);
    free(json);
    return status;
}

wf_status wf_agent_request_password_reset(wf_agent *agent, const char *email,
                                          wf_response *out) {
    if (!agent || !email || !out) return WF_ERR_INVALID_ARG;

    cJSON *body = cJSON_CreateObject();
    if (!body) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(body, "email", email)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_procedure(
        agent->client, "com.atproto.server.requestPasswordReset", json, out);
    free(json);
    return status;
}

wf_status wf_agent_reset_password(wf_agent *agent, const char *token,
                                  const char *password, wf_response *out) {
    if (!agent || !token || !password || !out) return WF_ERR_INVALID_ARG;

    cJSON *body = cJSON_CreateObject();
    if (!body) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(body, "token", token) ||
        !cJSON_AddStringToObject(body, "password", password)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_procedure(
        agent->client, "com.atproto.server.resetPassword", json, out);
    free(json);
    return status;
}

wf_status wf_agent_reserve_signing_key(wf_agent *agent, const char *did,
                                       wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;

    cJSON *body = cJSON_CreateObject();
    if (!body) return WF_ERR_ALLOC;
    if (did && did[0]) cJSON_AddStringToObject(body, "did", did);

    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_procedure(
        agent->client, "com.atproto.server.reserveSigningKey", json, out);
    free(json);
    return status;
}

wf_status wf_agent_get_service_auth(wf_agent *agent, const char *aud,
                                    const char *lhf, wf_response *out) {
    if (!agent || !aud || !out) return WF_ERR_INVALID_ARG;

    cJSON *body = cJSON_CreateObject();
    if (!body) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(body, "aud", aud)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }
    if (lhf && lhf[0]) cJSON_AddStringToObject(body, "lhf", lhf);

    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_procedure(
        agent->client, "com.atproto.server.getServiceAuth", json, out);
    free(json);
    return status;
}

/* ── identity wrappers ───────────────────────────────────────────────── */

wf_status wf_agent_resolve_did(wf_agent *agent, const char *did,
                               wf_response *out) {
    if (!agent || !did || !out) return WF_ERR_INVALID_ARG;
    if (!wf_syntax_did_is_valid(did)) return WF_ERR_INVALID_ARG;

    wf_xrpc_param params[] = {{"did", did}};
    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(
        agent->client, "com.atproto.identity.resolveDid", params, 1, out);
}

wf_status wf_agent_get_recommended_did_credentials(wf_agent *agent,
                                                   wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    wf_agent_sync_auth(agent);
    return wf_xrpc_query(agent->client,
                         "com.atproto.identity.getRecommendedDidCredentials",
                         NULL, out);
}

wf_status wf_agent_sign_plc_operation(wf_agent *agent, const char *token,
                                      const char *rotation_keys_json,
                                      const char *also_known_as_json,
                                      const char *verification_methods_json,
                                      const char *services_json,
                                      wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;

    cJSON *body = cJSON_CreateObject();
    if (!body) return WF_ERR_ALLOC;

    if (token && token[0]) cJSON_AddStringToObject(body, "token", token);

    if (rotation_keys_json) {
        cJSON *arr = cJSON_Parse(rotation_keys_json);
        if (!arr) {
            cJSON_Delete(body);
            return WF_ERR_PARSE;
        }
        cJSON_AddItemToObject(body, "rotationKeys", arr);
    }
    if (also_known_as_json) {
        cJSON *arr = cJSON_Parse(also_known_as_json);
        if (!arr) {
            cJSON_Delete(body);
            return WF_ERR_PARSE;
        }
        cJSON_AddItemToObject(body, "alsoKnownAs", arr);
    }
    if (verification_methods_json) {
        cJSON *obj = cJSON_Parse(verification_methods_json);
        if (!obj) {
            cJSON_Delete(body);
            return WF_ERR_PARSE;
        }
        cJSON_AddItemToObject(body, "verificationMethods", obj);
    }
    if (services_json) {
        cJSON *obj = cJSON_Parse(services_json);
        if (!obj) {
            cJSON_Delete(body);
            return WF_ERR_PARSE;
        }
        cJSON_AddItemToObject(body, "services", obj);
    }

    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_procedure(
        agent->client, "com.atproto.identity.signPlcOperation", json, out);
    free(json);
    return status;
}

wf_status wf_agent_submit_plc_operation(wf_agent *agent,
                                        const char *operation_json,
                                        wf_response *out) {
    if (!agent || !operation_json || !out) return WF_ERR_INVALID_ARG;

    cJSON *body = cJSON_Parse(operation_json);
    if (!body) return WF_ERR_PARSE;

    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_procedure(
        agent->client, "com.atproto.identity.submitPlcOperation", json, out);
    free(json);
    return status;
}

wf_status wf_agent_request_plc_operation_signature(wf_agent *agent,
                                                   wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    wf_agent_sync_auth(agent);
    return wf_xrpc_procedure(
        agent->client, "com.atproto.identity.requestPlcOperationSignature",
        "{}", out);
}

wf_status wf_agent_describe_repo(wf_agent *agent, const char *repo,
                                 wf_response *out) {
    if (!agent || !repo || !out) return WF_ERR_INVALID_ARG;
    if (!wf_syntax_at_identifier_is_valid(repo)) return WF_ERR_INVALID_ARG;

    wf_xrpc_param params[] = {{"repo", repo}};
    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "com.atproto.repo.describeRepo",
                                params, 1, out);
}
wf_status wf_agent_send_interactions(wf_agent *agent, const char *feed_uri,
                                     const char *interactions_json,
                                     wf_response *out) {
    if (!agent || !interactions_json || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_Parse(interactions_json);
    if (!root) return WF_ERR_PARSE;

    cJSON *body = cJSON_CreateObject();
    if (!body) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    if (feed_uri && feed_uri[0]) {
        if (!cJSON_AddStringToObject(body, "feed", feed_uri)) {
            cJSON_Delete(root);
            cJSON_Delete(body);
            return WF_ERR_ALLOC;
        }
    }
    if (!cJSON_AddItemToObject(body, "interactions", root)) {
        cJSON_Delete(root);
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_procedure(
        agent->client, "app.bsky.feed.sendInteractions", json, out);
    free(json);
    return status;
}
