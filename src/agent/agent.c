#include "wolfram/agent.h"

#include "wolfram/identity.h"
#include "wolfram/repo.h"
#include "wolfram/richtext.h"
#include "wolfram/server.h"
#include "wolfram/session.h"
#include "wolfram/syntax.h"

#include <cJSON.h>
#include "wolfram/atproto_lex.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WF_AGENT_CREATE_RECORD_NSID  "com.atproto.repo.createRecord"
#define WF_AGENT_DELETE_RECORD_NSID  "com.atproto.repo.deleteRecord"
#define WF_AGENT_PUT_RECORD_NSID     "com.atproto.repo.putRecord"
#define WF_AGENT_POST_COLLECTION      "app.bsky.feed.post"
#define WF_AGENT_POST_RECORD_TYPE     "app.bsky.feed.post"
#define WF_AGENT_FOLLOW_COLLECTION    "app.bsky.graph.follow"
#define WF_AGENT_FOLLOW_RECORD_TYPE   "app.bsky.graph.follow"
#define WF_AGENT_LIKE_COLLECTION      "app.bsky.feed.like"
#define WF_AGENT_LIKE_RECORD_TYPE     "app.bsky.feed.like"
#define WF_AGENT_REPOST_COLLECTION    "app.bsky.feed.repost"
#define WF_AGENT_REPOST_RECORD_TYPE   "app.bsky.feed.repost"
#define WF_AGENT_PROFILE_COLLECTION   "app.bsky.actor.profile"
#define WF_AGENT_PROFILE_RECORD_TYPE  "app.bsky.actor.profile"
#define WF_AGENT_PROFILE_RKEY         "self"
#define WF_AGENT_UPLOAD_BLOB_NSID     "com.atproto.repo.uploadBlob"
#define WF_AGENT_RESOLVE_HANDLE_NSID  "com.atproto.identity.resolveHandle"
#define WF_AGENT_FACET_MENTION_TYPE   "app.bsky.richtext.facet#mention"
#define WF_AGENT_FACET_LINK_TYPE      "app.bsky.richtext.facet#link"
#define WF_AGENT_FACET_TAG_TYPE       "app.bsky.richtext.facet#tag"

typedef struct wf_agent {
    wf_xrpc_client *client;
    wf_session *session;
    char *service_url;
    /* Offline identity (for local repo mirror without network login). */
    char *mirror_did;
    char *mirror_signing_key;
    /* Local repo mirror — a wf_car whose root is the latest verified commit. */
    wf_car mirror;
} wf_agent;

static char *wf_agent_strdup(const char *s) {
    if (!s) {
        return NULL;
    }

    size_t len = strlen(s) + 1;
    char *dup = malloc(len);
    if (dup) {
        memcpy(dup, s, len);
    }
    return dup;
}

static char *wf_agent_strndup(const char *s, size_t len) {
    char *dup = malloc(len + 1);
    if (!dup) {
        return NULL;
    }

    memcpy(dup, s, len);
    dup[len] = '\0';
    return dup;
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
    free(profile->avatar_cid);
    memset(profile, 0, sizeof(*profile));
}

static void wf_agent_server_description_reset(wf_agent_server_description *desc) {
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
    memset(data, 0, sizeof(*data));
    data->email_confirmed = -1;
    data->email_auth_factor = -1;
    data->active = -1;
}

static wf_status wf_agent_set_string(char **dst, const char *src);
static int wf_agent_is_logged_in(const wf_agent *agent);

static wf_status wf_agent_session_data_copy(wf_session_data *dst, const wf_session_data *src) {
    if (!dst || !src) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_session_data_reset(dst);

    wf_status status = wf_agent_set_string(&dst->access_jwt, src->access_jwt);
    if (status == WF_OK) {
        status = wf_agent_set_string(&dst->refresh_jwt, src->refresh_jwt);
    }
    if (status == WF_OK) {
        status = wf_agent_set_string(&dst->handle, src->handle);
    }
    if (status == WF_OK) {
        status = wf_agent_set_string(&dst->did, src->did);
    }
    if (status == WF_OK) {
        status = wf_agent_set_string(&dst->email, src->email);
    }
    if (status == WF_OK) {
        status = wf_agent_set_string(&dst->status, src->status);
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

    wf_xrpc_client_set_auth(agent->client,
                            wf_agent_is_logged_in(agent) ? agent->session->data.access_jwt : NULL);
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

static int wf_agent_authority_matches_session(const wf_agent *agent, const char *authority) {
    if (!agent || !agent->session || !authority) {
        return 0;
    }

    const char *did = agent->session->data.did;
    const char *handle = agent->session->data.handle;

    if (did && strcmp(authority, did) == 0) {
        return 1;
    }
    if (handle && strcmp(authority, handle) == 0) {
        return 1;
    }
    return 0;
}

static wf_status wf_agent_set_string(char **dst, const char *src) {
    char *copy = wf_agent_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }

    free(*dst);
    *dst = copy;
    return WF_OK;
}

static wf_status wf_agent_add_feature_json(cJSON *features,
                                           const wf_richtext_segment *segment,
                                           const wf_richtext_feature *feature,
                                           wf_xrpc_client *client) {
    if (!features || !segment || !feature || !client) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *feature_json = cJSON_CreateObject();
    if (!feature_json) {
        return WF_ERR_ALLOC;
    }

    wf_status status = WF_OK;

    switch (feature->type) {
    case WF_RICHTEXT_FEATURE_MENTION: {
        if (!segment->text || segment->text_len < 2 || segment->text[0] != '@') {
            status = WF_ERR_PARSE;
            break;
        }

        char *handle = wf_agent_strndup(segment->text + 1, segment->text_len - 1);
        if (!handle) {
            status = WF_ERR_ALLOC;
            break;
        }

        if (!wf_syntax_handle_is_valid(handle)) {
            free(handle);
            status = WF_ERR_PARSE;
            break;
        }

        char *did = NULL;
        status = wf_handle_resolve(client, handle, &did);
        free(handle);
        if (status != WF_OK) {
            break;
        }

        if (!wf_syntax_did_is_valid(did)) {
            free(did);
            status = WF_ERR_PARSE;
            break;
        }

        if (!cJSON_AddStringToObject(feature_json, "$type", WF_AGENT_FACET_MENTION_TYPE) ||
            !cJSON_AddStringToObject(feature_json, "did", did)) {
            free(did);
            status = WF_ERR_ALLOC;
            break;
        }

        free(did);
        break;
    }

    case WF_RICHTEXT_FEATURE_LINK:
        if (!cJSON_AddStringToObject(feature_json, "$type", WF_AGENT_FACET_LINK_TYPE) ||
            !cJSON_AddStringToObject(feature_json, "uri", feature->uri)) {
            status = WF_ERR_ALLOC;
        }
        break;

    case WF_RICHTEXT_FEATURE_TAG:
        if (!cJSON_AddStringToObject(feature_json, "$type", WF_AGENT_FACET_TAG_TYPE) ||
            !cJSON_AddStringToObject(feature_json, "tag", feature->tag)) {
            status = WF_ERR_ALLOC;
        }
        break;

    default:
        status = WF_ERR_INVALID_ARG;
        break;
    }

    if (status != WF_OK) {
        cJSON_Delete(feature_json);
        return status;
    }

    if (!cJSON_AddItemToArray(features, feature_json)) {
        cJSON_Delete(feature_json);
        return WF_ERR_ALLOC;
    }

    return WF_OK;
}

static wf_status wf_agent_add_segment_facet_json(cJSON *facets,
                                                const wf_richtext_segment *segment,
                                                wf_xrpc_client *client) {
    if (!facets || !segment || !segment->facet || !segment->facet->features ||
        segment->facet->feature_count == 0) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *facet_json = cJSON_CreateObject();
    if (!facet_json) {
        return WF_ERR_ALLOC;
    }

    cJSON *index_json = cJSON_CreateObject();
    if (!index_json) {
        cJSON_Delete(facet_json);
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddNumberToObject(index_json, "byteStart", (double)segment->facet->byte_start) ||
        !cJSON_AddNumberToObject(index_json, "byteEnd", (double)segment->facet->byte_end) ||
        !cJSON_AddItemToObject(facet_json, "index", index_json)) {
        cJSON_Delete(index_json);
        cJSON_Delete(facet_json);
        return WF_ERR_ALLOC;
    }

    cJSON *features_json = cJSON_CreateArray();
    if (!features_json) {
        cJSON_Delete(facet_json);
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddItemToObject(facet_json, "features", features_json)) {
        cJSON_Delete(features_json);
        cJSON_Delete(facet_json);
        return WF_ERR_ALLOC;
    }

    for (size_t i = 0; i < segment->facet->feature_count; ++i) {
        wf_status status = wf_agent_add_feature_json(features_json, segment,
                                                     &segment->facet->features[i], client);
        if (status != WF_OK) {
            cJSON_Delete(facet_json);
            return status;
        }
    }

    if (!cJSON_AddItemToArray(facets, facet_json)) {
        cJSON_Delete(facet_json);
        return WF_ERR_ALLOC;
    }

    return WF_OK;
}

static wf_status wf_agent_build_post_record(wf_agent *agent,
                                            const char *text,
                                            cJSON *facets,
                                            cJSON **out_record) {
    if (!agent || !text || !out_record || !agent->session || !agent->session->data.did) {
        cJSON_Delete(facets);
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_syntax_did_is_valid(agent->session->data.did)) {
        cJSON_Delete(facets);
        return WF_ERR_PARSE;
    }

    char created_at[32];
    if (!wf_agent_make_rfc3339_timestamp(created_at, sizeof(created_at)) ||
        !wf_syntax_datetime_is_valid(created_at)) {
        cJSON_Delete(facets);
        return WF_ERR_INVALID_ARG;
    }

    cJSON *record = cJSON_CreateObject();
    if (!record) {
        cJSON_Delete(facets);
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(record, "$type", WF_AGENT_POST_RECORD_TYPE) ||
        !cJSON_AddStringToObject(record, "text", text) ||
        !cJSON_AddStringToObject(record, "createdAt", created_at)) {
        cJSON_Delete(facets);
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    if (facets) {
        if (!cJSON_AddItemToObject(record, "facets", facets)) {
            cJSON_Delete(facets);
            cJSON_Delete(record);
            return WF_ERR_ALLOC;
        }
    }

    *out_record = record;
    return WF_OK;
}

static wf_status wf_agent_create_record_call(wf_agent *agent,
                                             const char *collection,
                                             cJSON *record,
                                             wf_agent_post_result *out) {
    if (!wf_agent_is_logged_in(agent) || !collection || !record || !out) {
        cJSON_Delete(record);
        return WF_ERR_INVALID_ARG;
    }

    if (wf_syntax_nsid_validate(collection) != WF_OK) {
        cJSON_Delete(record);
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(root, "repo", agent->session->data.did) ||
        !cJSON_AddStringToObject(root, "collection", collection)) {
        cJSON_Delete(record);
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddItemToObject(root, "record", record)) {
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
    wf_status status = wf_xrpc_procedure(agent->client, WF_AGENT_CREATE_RECORD_NSID,
                                         json, &res);
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
    if (!cJSON_IsString(uri) || !cJSON_IsString(cid) ||
        !uri->valuestring || !cid->valuestring) {
        cJSON_Delete(resp_root);
        wf_response_free(&res);
        return WF_ERR_PARSE;
    }

    status = wf_agent_set_string(&out->uri, uri->valuestring);
    if (status == WF_OK) {
        status = wf_agent_set_string(&out->cid, cid->valuestring);
    }
    if (status != WF_OK) {
        wf_agent_post_result_reset(out);
    }

    cJSON_Delete(resp_root);
    wf_response_free(&res);
    return status;
}

static wf_status wf_agent_delete_record_call(wf_agent *agent,
                                             const char *collection,
                                             const char *rkey) {
    if (!wf_agent_is_logged_in(agent) || !collection || !rkey) {
        return WF_ERR_INVALID_ARG;
    }

    if (wf_syntax_nsid_validate(collection) != WF_OK ||
        !wf_syntax_record_key_is_valid(rkey)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(root, "repo", agent->session->data.did) ||
        !cJSON_AddStringToObject(root, "collection", collection) ||
        !cJSON_AddStringToObject(root, "rkey", rkey)) {
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
    wf_status status = wf_xrpc_procedure(agent->client, WF_AGENT_DELETE_RECORD_NSID,
                                         json, &res);
    free(json);
    wf_response_free(&res);
    return status;
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
    if (!cJSON_IsString(did) || !cJSON_IsString(handle) ||
        !did->valuestring || !handle->valuestring ||
        !wf_syntax_did_is_valid(did->valuestring) ||
        !wf_syntax_handle_is_valid(handle->valuestring)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = wf_agent_set_string(&out->did, did->valuestring);
    if (status == WF_OK) {
        status = wf_agent_set_string(&out->handle, handle->valuestring);
    }

    cJSON *display_name = cJSON_GetObjectItemCaseSensitive(root, "displayName");
    if (status == WF_OK && cJSON_IsString(display_name) && display_name->valuestring) {
        status = wf_agent_set_string(&out->display_name, display_name->valuestring);
    }

    cJSON *description = cJSON_GetObjectItemCaseSensitive(root, "description");
    if (status == WF_OK && cJSON_IsString(description) && description->valuestring) {
        status = wf_agent_set_string(&out->description, description->valuestring);
    }

    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(root, "avatar");
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        /* The profile view exposes an avatar URL, not a CID; keep the
         * field populated with the server value for convenience. */
        status = wf_agent_set_string(&out->avatar_cid, avatar->valuestring);
    }

    cJSON *followers_count = cJSON_GetObjectItemCaseSensitive(root, "followersCount");
    cJSON *follows_count = cJSON_GetObjectItemCaseSensitive(root, "followsCount");
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

static wf_status wf_agent_build_follow_record(wf_agent *agent,
                                              const char *subject_did,
                                              cJSON **out_record) {
    if (!wf_agent_is_logged_in(agent) || !subject_did || !out_record) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_syntax_did_is_valid(subject_did)) {
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

    if (!cJSON_AddStringToObject(record, "$type", WF_AGENT_FOLLOW_RECORD_TYPE) ||
        !cJSON_AddStringToObject(record, "subject", subject_did) ||
        !cJSON_AddStringToObject(record, "createdAt", created_at)) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    *out_record = record;
    return WF_OK;
}

static wf_status wf_agent_build_like_record(wf_agent *agent,
                                            const char *post_uri,
                                            const char *post_cid,
                                            cJSON **out_record) {
    if (!wf_agent_is_logged_in(agent) || !post_uri || !post_cid || !*post_cid || !out_record) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi wf_uri = {0};
    if (!wf_syntax_aturi_parse(post_uri, &wf_uri)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi_free(&wf_uri);

    char created_at[32];
    if (!wf_agent_make_rfc3339_timestamp(created_at, sizeof(created_at)) ||
        !wf_syntax_datetime_is_valid(created_at)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *record = cJSON_CreateObject();
    if (!record) {
        return WF_ERR_ALLOC;
    }

    cJSON *subject = cJSON_CreateObject();
    if (!subject) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(subject, "uri", post_uri) ||
        !cJSON_AddStringToObject(subject, "cid", post_cid) ||
        !cJSON_AddStringToObject(record, "$type", WF_AGENT_LIKE_RECORD_TYPE) ||
        !cJSON_AddItemToObject(record, "subject", subject) ||
        !cJSON_AddStringToObject(record, "createdAt", created_at)) {
        cJSON_Delete(subject);
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    *out_record = record;
    return WF_OK;
}

static wf_status wf_agent_build_repost_record(wf_agent *agent,
                                               const char *post_uri,
                                               const char *post_cid,
                                               cJSON **out_record) {
    if (!wf_agent_is_logged_in(agent) || !post_uri || !post_cid || !*post_cid || !out_record) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi wf_uri = {0};
    if (!wf_syntax_aturi_parse(post_uri, &wf_uri)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_syntax_aturi_free(&wf_uri);

    char created_at[32];
    if (!wf_agent_make_rfc3339_timestamp(created_at, sizeof(created_at)) ||
        !wf_syntax_datetime_is_valid(created_at)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *record = cJSON_CreateObject();
    if (!record) {
        return WF_ERR_ALLOC;
    }

    cJSON *subject = cJSON_CreateObject();
    if (!subject) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(subject, "uri", post_uri) ||
        !cJSON_AddStringToObject(subject, "cid", post_cid) ||
        !cJSON_AddStringToObject(record, "$type", WF_AGENT_REPOST_RECORD_TYPE) ||
        !cJSON_AddItemToObject(record, "subject", subject) ||
        !cJSON_AddStringToObject(record, "createdAt", created_at)) {
        cJSON_Delete(subject);
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    *out_record = record;
    return WF_OK;
}

static wf_status wf_agent_put_record_call(wf_agent *agent,
                                           const char *collection,
                                           const char *rkey,
                                           cJSON *record,
                                           wf_agent_post_result *out) {
    if (!wf_agent_is_logged_in(agent) || !collection || !rkey || !record || !out) {
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
    wf_status status = wf_xrpc_procedure(agent->client, WF_AGENT_PUT_RECORD_NSID,
                                         json, &res);
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
    if (!cJSON_IsString(uri) || !cJSON_IsString(cid) ||
        !uri->valuestring || !cid->valuestring) {
        cJSON_Delete(resp_root);
        wf_response_free(&res);
        return WF_ERR_PARSE;
    }

    status = wf_agent_set_string(&out->uri, uri->valuestring);
    if (status == WF_OK) {
        status = wf_agent_set_string(&out->cid, cid->valuestring);
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

    agent->service_url = wf_agent_strdup(service_url);
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

    agent->session = wf_session_new(service_url);
    if (!agent->session) {
        wf_xrpc_client_free(agent->client);
        free(agent->service_url);
        free(agent);
        return NULL;
    }

    return agent;
}

void wf_agent_free(wf_agent *agent) {
    if (!agent) {
        return;
    }

    wf_session_free(agent->session);
    wf_xrpc_client_free(agent->client);
    free(agent->service_url);
    free(agent->mirror_did);
    free(agent->mirror_signing_key);
    wf_car_free(&agent->mirror);
    free(agent);
}

wf_status wf_agent_login(wf_agent *agent, const char *identifier, const char *password) {
    if (!agent || !agent->session || !agent->client || !identifier || !password) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_client_set_auth(agent->client, NULL);

    wf_status status = wf_session_login(agent->session, identifier, password);
    if (status != WF_OK) {
        return status;
    }

    wf_agent_sync_auth(agent);
    return WF_OK;
}

wf_status wf_agent_resume(wf_agent *agent, const wf_session_data *data) {
    if (!agent || !agent->session || !agent->client || !data) {
        return WF_ERR_INVALID_ARG;
    }

    wf_status status = wf_session_resume(agent->session, data);
    wf_agent_sync_auth(agent);
    return status;
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
    wf_status status = wf_xrpc_procedure(agent->client,
                                          "com.atproto.repo.getRecord",
                                          json, out);
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
    wf_status status = wf_xrpc_procedure(agent->client,
                                          "com.atproto.repo.putRecord",
                                          json, &res);
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
    if (!cJSON_IsString(uri) || !cJSON_IsString(cid) ||
        !uri->valuestring || !cid->valuestring) {
        cJSON_Delete(resp_root);
        wf_response_free(&res);
        return WF_ERR_PARSE;
    }

    status = wf_agent_set_string(&out->uri, uri->valuestring);
    if (status == WF_OK) {
        status = wf_agent_set_string(&out->cid, cid->valuestring);
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

#if 0 // post functions moved to post.c
wf_status wf_agent_post_with_facets(wf_agent *agent, const char *text,
                                    const char *facets_json, wf_agent_post_result *out) {
    if (!agent || !text || !out) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *facets = NULL;
    if (facets_json && facets_json[0] != '\0') {
        facets = cJSON_Parse(facets_json);
        if (!facets) {
            return WF_ERR_PARSE;
        }
        if (!cJSON_IsArray(facets)) {
            cJSON_Delete(facets);
            return WF_ERR_PARSE;
        }
    }

    cJSON *record = NULL;
    wf_status status = wf_agent_build_post_record(agent, text, facets, &record);
    if (status != WF_OK) {
        return status;
    }

    return wf_agent_create_record_call(agent, WF_AGENT_POST_COLLECTION, record, out);
}

wf_status wf_agent_post(wf_agent *agent, const char *text, wf_agent_post_result *out) {
    if (!agent || !text || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_richtext rt = {0};
    wf_status status = wf_richtext_init(&rt, text);
    if (status != WF_OK) {
        return status;
    }

    status = wf_richtext_detect_facets(&rt);
    if (status != WF_OK) {
        wf_richtext_free(&rt);
        return status;
    }

    cJSON *facets = NULL;
    size_t segment_count = wf_richtext_segment_count(&rt);
    for (size_t i = 0; i < segment_count; ++i) {
        wf_richtext_segment segment = wf_richtext_get_segment(&rt, i);
        if (!segment.facet || !segment.facet->features || segment.facet->feature_count == 0) {
            continue;
        }

        if (!facets) {
            facets = cJSON_CreateArray();
            if (!facets) {
                wf_richtext_free(&rt);
                return WF_ERR_ALLOC;
            }
        }

        status = wf_agent_add_segment_facet_json(facets, &segment, agent->client);
        if (status != WF_OK) {
            cJSON_Delete(facets);
            wf_richtext_free(&rt);
            return status;
        }
    }

    wf_richtext_free(&rt);

    cJSON *record = NULL;
    status = wf_agent_build_post_record(agent, text, facets, &record);
    if (status != WF_OK) {
        return status;
    }

    return wf_agent_create_record_call(agent, WF_AGENT_POST_COLLECTION, record, out);
}

wf_status wf_agent_delete_post(wf_agent *agent, const char *uri) {
    if (!agent || !uri) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(uri, &parsed)) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    if (!parsed.authority || !wf_agent_authority_matches_session(agent, parsed.authority) ||
        !parsed.collection || !parsed.record_key ||
        strcmp(parsed.collection, WF_AGENT_POST_COLLECTION) != 0) {
        status = WF_ERR_INVALID_ARG;
        goto done;
    }

    status = wf_agent_delete_record_call(agent, parsed.collection, parsed.record_key);

done:
    wf_syntax_aturi_free(&parsed);
    return status;
}

#endif // post functions moved to post.c

wf_status wf_agent_get_profile(wf_agent *agent, const char *actor, wf_agent_profile *out) {
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
    wf_status status = wf_xrpc_query_params(agent->client,
                                             "app.bsky.actor.getProfile",
                                             params, 1, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_profile_from_response(&res, out);
    wf_response_free(&res);
    return status;
}

#if 0 // social functions moved to post.c

wf_status wf_agent_follow(wf_agent *agent, const char *subject_did, wf_agent_post_result *out) {
    if (!agent || !subject_did || !out) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *record = NULL;
    wf_status status = wf_agent_build_follow_record(agent, subject_did, &record);
    if (status != WF_OK) {
        return status;
    }

    return wf_agent_create_record_call(agent, WF_AGENT_FOLLOW_COLLECTION, record, out);
}

wf_status wf_agent_unfollow(wf_agent *agent, const char *follow_uri) {
    if (!agent || !follow_uri) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(follow_uri, &parsed)) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    if (!parsed.authority || !wf_agent_authority_matches_session(agent, parsed.authority) ||
        !parsed.collection || !parsed.record_key ||
        strcmp(parsed.collection, WF_AGENT_FOLLOW_COLLECTION) != 0) {
        status = WF_ERR_INVALID_ARG;
        goto done;
    }

    status = wf_agent_delete_record_call(agent, parsed.collection, parsed.record_key);

done:
    wf_syntax_aturi_free(&parsed);
    return status;
}

wf_status wf_agent_like(wf_agent *agent, const char *post_uri, const char *post_cid, wf_agent_post_result *out) {
    if (!agent || !post_uri || !post_cid || !out) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *record = NULL;
    wf_status status = wf_agent_build_like_record(agent, post_uri, post_cid, &record);
    if (status != WF_OK) {
        return status;
    }

    return wf_agent_create_record_call(agent, WF_AGENT_LIKE_COLLECTION, record, out);
}

wf_status wf_agent_unlike(wf_agent *agent, const char *like_uri) {
    if (!agent || !like_uri) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(like_uri, &parsed)) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    if (!parsed.authority || !wf_agent_authority_matches_session(agent, parsed.authority) ||
        !parsed.collection || !parsed.record_key ||
        strcmp(parsed.collection, WF_AGENT_LIKE_COLLECTION) != 0) {
        status = WF_ERR_INVALID_ARG;
        goto done;
    }

    status = wf_agent_delete_record_call(agent, parsed.collection, parsed.record_key);

done:
    wf_syntax_aturi_free(&parsed);
    return status;
}

/* ── mute / unmute ─────────────────────────────────────────────────── */

wf_status wf_agent_mute(wf_agent *agent, const char *actor) {
    if (!agent || !actor || !actor[0]) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "actor", actor)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(agent->client,
                                          "app.bsky.graph.muteActor",
                                          json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_unmute(wf_agent *agent, const char *actor) {
    if (!agent || !actor || !actor[0]) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(root, "actor", actor)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_xrpc_procedure(agent->client,
                                          "app.bsky.graph.unmuteActor",
                                          json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_repost(wf_agent *agent, const char *post_uri, const char *post_cid,
                          wf_agent_post_result *out) {
    if (!agent || !post_uri || !post_cid || !out) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *record = NULL;
    wf_status status = wf_agent_build_repost_record(agent, post_uri, post_cid, &record);
    if (status != WF_OK) {
        return status;
    }

    return wf_agent_create_record_call(agent, WF_AGENT_REPOST_COLLECTION, record, out);
}

wf_status wf_agent_delete_repost(wf_agent *agent, const char *repost_uri) {
    if (!agent || !repost_uri) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(repost_uri, &parsed)) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    if (!parsed.authority || !wf_agent_authority_matches_session(agent, parsed.authority) ||
        !parsed.collection || !parsed.record_key ||
        strcmp(parsed.collection, WF_AGENT_REPOST_COLLECTION) != 0) {
        status = WF_ERR_INVALID_ARG;
        goto done;
    }

    status = wf_agent_delete_record_call(agent, parsed.collection, parsed.record_key);

done:
    wf_syntax_aturi_free(&parsed);
    return status;
}


#endif // social functions moved to post.c

#if 0 // feed functions moved to feed.c

wf_status wf_agent_get_timeline(wf_agent *agent, int limit, const char *cursor,
                                wf_response *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[3];
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

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getTimeline",
                                 params, param_count, out);
}

wf_status wf_agent_get_timeline_lex(wf_agent *agent, int limit, const char *cursor,
                                    wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    wf_lex_app_bsky_feed_get_timeline_main_params params = {0};
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }
    wf_agent_sync_auth(agent);
    return wf_lex_app_bsky_feed_get_timeline_main_call(agent->client, &params, out);
}

wf_status wf_agent_get_author_feed(wf_agent *agent, const char *actor,
                                   int limit, const char *cursor, const char *filter,
                                   wf_response *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[4];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "actor";
    params[param_count].value = actor;
    param_count++;

    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf))) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "limit";
        params[param_count].value = limit_buf;
        param_count++;
    }
    if (filter && filter[0]) {
        params[param_count].name = "filter";
        params[param_count].value = filter;
        param_count++;
    }
    if (cursor && cursor[0]) {
        params[param_count].name = "cursor";
        params[param_count].value = cursor;
        param_count++;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getAuthorFeed",
                                  params, param_count, out);
}

wf_status wf_agent_get_author_feed_lex(wf_agent *agent, const char *actor,
                                        int limit, const char *cursor, const char *filter,
                                        wf_response *out) {
    if (!agent || !actor || !out) return WF_ERR_INVALID_ARG;
    if (!wf_syntax_at_identifier_is_valid(actor)) return WF_ERR_INVALID_ARG;
    wf_lex_app_bsky_feed_get_author_feed_main_params params = {0};
    params.actor = actor;
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (filter && filter[0]) {
        params.has_filter = true;
        params.filter = filter;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }
    wf_agent_sync_auth(agent);
    return wf_lex_app_bsky_feed_get_author_feed_main_call(agent->client, &params, out);
}


wf_status wf_agent_get_post_thread(wf_agent *agent, const char *uri, int depth,
                                   wf_response *out) {
    if (!agent || !uri || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(uri, &parsed)) {
        return WF_ERR_PARSE;
    }
    wf_syntax_aturi_free(&parsed);

    wf_xrpc_param params[2];
    size_t param_count = 0;
    char depth_buf[16];

    params[param_count].name = "uri";
    params[param_count].value = uri;
    param_count++;

    if (depth > 0) {
        if (!wf_agent_int_to_str(depth, depth_buf, sizeof(depth_buf))) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "depth";
        params[param_count].value = depth_buf;
        param_count++;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getPostThread",
                                 params, param_count, out);
}

wf_status wf_agent_get_posts(wf_agent *agent, const char *const *uris, size_t uri_count,
                             wf_response *out) {
    if (!agent || !uris || uri_count == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }

    /* Validate all URIs first */
    for (size_t i = 0; i < uri_count; ++i) {
        if (!uris[i]) {
            return WF_ERR_INVALID_ARG;
        }
        wf_syntax_aturi parsed = {0};
        if (!wf_syntax_aturi_parse(uris[i], &parsed)) {
            return WF_ERR_PARSE;
        }
        wf_syntax_aturi_free(&parsed);
    }

    /* Build query string: uris=at://...&uris=at://... */
    /* wf_xrpc_query_params handles repeated params by passing the same name */
    wf_xrpc_param *params = calloc(uri_count, sizeof(*params));
    if (!params) {
        return WF_ERR_ALLOC;
    }

    for (size_t i = 0; i < uri_count; ++i) {
        params[i].name = "uris";
        params[i].value = uris[i];
    }

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_query_params(agent->client, "app.bsky.feed.getPosts",
                                             params, uri_count, out);
    free(params);
    return status;
}

/* ── searchPosts ───────────────────────────────────────────────────── */
#if 0 // searchPosts moved to feed.c

wf_status wf_agent_search_posts(wf_agent *agent, const char *query,
                                int limit, const char *cursor, const char *sort,
                                const char *since, const char *until,
                                const char *author, const char *lang,
                                wf_response *out) {
    if (!agent || !query || !query[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[9];
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
    if (sort && sort[0]) {
        params[param_count].name = "sort";
        params[param_count].value = sort;
        param_count++;
    }
    if (since && since[0]) {
        params[param_count].name = "since";
        params[param_count].value = since;
        param_count++;
    }
    if (until && until[0]) {
        params[param_count].name = "until";
        params[param_count].value = until;
        param_count++;
    }
    if (author && author[0]) {
        if (!wf_syntax_at_identifier_is_valid(author)) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "author";
        params[param_count].value = author;
        param_count++;
    }
    if (lang && lang[0]) {
        if (!wf_syntax_language_is_valid(lang)) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "lang";
        params[param_count].value = lang;
        param_count++;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.searchPosts",
                                 params, param_count, out);
}

wf_status wf_agent_search_posts_lex(wf_agent *agent, const char *query,
                                 int limit, const char *cursor, const char *sort,
                                 const char *since, const char *until,
                                 const char *author, const char *lang,
                                 wf_response *out) {
    if (!agent || !query || !query[0] || !out) return WF_ERR_INVALID_ARG;
    wf_lex_app_bsky_feed_search_posts_main_params params = {0};
    params.q = query;
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }
    if (sort && sort[0]) {
        params.has_sort = true;
        params.sort = sort;
    }
    if (since && since[0]) {
        params.has_since = true;
        params.since = since;
    }
    if (until && until[0]) {
        params.has_until = true;
        params.until = until;
    }
    if (author && author[0]) {
        if (!wf_syntax_at_identifier_is_valid(author)) return WF_ERR_INVALID_ARG;
        params.has_author = true;
        params.author = author;
    }
    if (lang && lang[0]) {
        if (!wf_syntax_language_is_valid(lang)) return WF_ERR_INVALID_ARG;
        params.has_lang = true;
        params.lang = lang;
    }
    wf_agent_sync_auth(agent);
    return wf_lex_app_bsky_feed_search_posts_main_call(agent->client, &params, out);
}
#endif // searchPosts moved to feed.c


/* ── getActorLikes ─────────────────────────────────────────────────── */

wf_status wf_agent_get_actor_likes(wf_agent *agent, const char *actor,
                                   int limit, const char *cursor,
                                   wf_response *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "actor";
    params[param_count].value = actor;
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
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getActorLikes",
                                params, param_count, out);
}

/* ── getLikes ──────────────────────────────────────────────────────── */

wf_status wf_agent_get_likes(wf_agent *agent, const char *uri,
                             int limit, const char *cursor,
                             wf_response *out) {
    if (!agent || !uri || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(uri, &parsed)) {
        return WF_ERR_PARSE;
    }
    wf_syntax_aturi_free(&parsed);

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "uri";
    params[param_count].value = uri;
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
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getLikes",
                                params, param_count, out);
}

wf_status wf_agent_get_likes_lex(wf_agent *agent, const char *uri,
                                 int limit, const char *cursor,
                                 wf_response *out) {
    if (!agent || !uri || !out) return WF_ERR_INVALID_ARG;
    // Validate AT-URI syntax
    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(uri, &parsed)) return WF_ERR_PARSE;
    wf_syntax_aturi_free(&parsed);

    wf_lex_app_bsky_feed_get_likes_main_params params = {0};
    params.uri = uri;
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }
    // Ensure auth
    wf_agent_sync_auth(agent);
    return wf_lex_app_bsky_feed_get_likes_main_call(agent->client, &params, out);
}

/* ── lexicon wrappers for feed endpoints ─────────────────────────────────────── */

wf_status wf_agent_get_quotes_lex(wf_agent *agent, const char *uri,
                                 int limit, const char *cursor,
                                 wf_response *out) {
    if (!agent || !uri || !out) return WF_ERR_INVALID_ARG;
    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(uri, &parsed)) return WF_ERR_PARSE;
    wf_syntax_aturi_free(&parsed);
    wf_lex_app_bsky_feed_get_quotes_main_params params = {0};
    params.uri = uri;
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }
    wf_agent_sync_auth(agent);
    return wf_lex_app_bsky_feed_get_quotes_main_call(agent->client, &params, out);
}

wf_status wf_agent_get_list_feed_lex(wf_agent *agent, const char *list_uri,
                                    int limit, const char *cursor,
                                    wf_response *out) {
    if (!agent || !list_uri || !out) return WF_ERR_INVALID_ARG;
    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(list_uri, &parsed)) return WF_ERR_PARSE;
    wf_syntax_aturi_free(&parsed);
    wf_lex_app_bsky_feed_get_list_feed_main_params params = {0};
    params.list = list_uri;
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }
    wf_agent_sync_auth(agent);
    return wf_lex_app_bsky_feed_get_list_feed_main_call(agent->client, &params, out);
}

wf_status wf_agent_get_feed_lex(wf_agent *agent, const char *feed_uri,
                               int limit, const char *cursor,
                               wf_response *out) {
    if (!agent || !feed_uri || !out) return WF_ERR_INVALID_ARG;
    wf_lex_app_bsky_feed_get_feed_main_params params = {0};
    params.feed = feed_uri;
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }
    wf_agent_sync_auth(agent);
    return wf_lex_app_bsky_feed_get_feed_main_call(agent->client, &params, out);
}

wf_status wf_agent_get_actor_feeds_lex(wf_agent *agent, const char *actor,
                                        int limit, const char *cursor,
                                        wf_response *out) {
    if (!agent || !actor || !out) return WF_ERR_INVALID_ARG;
    if (!wf_syntax_at_identifier_is_valid(actor)) return WF_ERR_INVALID_ARG;
    wf_lex_app_bsky_feed_get_actor_feeds_main_params params = {0};
    params.actor = actor;
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }
    wf_agent_sync_auth(agent);
    return wf_lex_app_bsky_feed_get_actor_feeds_main_call(agent->client, &params, out);
}


/* ── getRepostedBy ─────────────────────────────────────────────────── */

wf_status wf_agent_get_reposted_by(wf_agent *agent, const char *uri,
                                   int limit, const char *cursor,
                                   wf_response *out) {
    if (!agent || !uri || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(uri, &parsed)) {
        return WF_ERR_PARSE;
    }
    wf_syntax_aturi_free(&parsed);

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "uri";
    params[param_count].value = uri;
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
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getRepostedBy",
                                params, param_count, out);
}

wf_status wf_agent_get_quotes(wf_agent *agent, const char *uri,
                               int limit, const char *cursor,
                               wf_response *out) {
    if (!agent || !uri || !out) return WF_ERR_INVALID_ARG;
    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(uri, &parsed)) return WF_ERR_PARSE;
    wf_syntax_aturi_free(&parsed);

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];
    params[param_count].name = "uri";
    params[param_count].value = uri;
    param_count++;
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
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getQuotes",
                                params, param_count, out);
}

wf_status wf_agent_get_list_feed(wf_agent *agent, const char *list_uri,
                                  int limit, const char *cursor,
                                  wf_response *out) {
    if (!agent || !list_uri || !out) return WF_ERR_INVALID_ARG;
    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];
    params[param_count].name = "list";
    params[param_count].value = list_uri;
    param_count++;
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
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getListFeed",
                                params, param_count, out);
}

wf_status wf_agent_get_feed(wf_agent *agent, const char *feed_uri,
                             int limit, const char *cursor,
                             wf_response *out) {
    if (!agent || !feed_uri || !out) return WF_ERR_INVALID_ARG;
    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];
    params[param_count].name = "feed";
    params[param_count].value = feed_uri;
    param_count++;
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
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getFeed",
                                params, param_count, out);
}

wf_status wf_agent_get_actor_feeds(wf_agent *agent, const char *actor,
                                    int limit, const char *cursor,
                                    wf_response *out) {
    if (!agent || !actor || !out) return WF_ERR_INVALID_ARG;
    if (!wf_syntax_at_identifier_is_valid(actor))
        return WF_ERR_INVALID_ARG;

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];
    params[param_count].name = "actor";
    params[param_count].value = actor;
    param_count++;
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
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getActorFeeds",
                                params, param_count, out);
}

wf_status wf_agent_get_follows(wf_agent *agent, const char *actor,
                               int limit, const char *cursor, wf_response *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "actor";
    params[param_count].value = actor;
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
    return wf_xrpc_query_params(agent->client, "app.bsky.graph.getFollows",
                                 params, param_count, out);
}

wf_status wf_agent_get_followers(wf_agent *agent, const char *actor,
                                 int limit, const char *cursor, wf_response *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "actor";
    params[param_count].value = actor;
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
    return wf_xrpc_query_params(agent->client, "app.bsky.graph.getFollowers",
                                 params, param_count, out);
}

/* ── getBlocks ─────────────────────────────────────────────────────── */

wf_status wf_agent_get_blocks(wf_agent *agent, int limit, const char *cursor,
                               wf_response *out) {
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

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.graph.getBlocks",
                                params, param_count, out);
}

/* ── getMutes ──────────────────────────────────────────────────────── */

wf_status wf_agent_get_mutes(wf_agent *agent, int limit, const char *cursor,
                              wf_response *out) {
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

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.graph.getMutes",
                                params, param_count, out);
}

/* ── getKnownFollowers ─────────────────────────────────────────────── */

wf_status wf_agent_get_known_followers(wf_agent *agent, const char *actor,
                                       int limit, const char *cursor,
                                       wf_response *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "actor";
    params[param_count].value = actor;
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
    return wf_xrpc_query_params(agent->client, "app.bsky.graph.getKnownFollowers",
                                params, param_count, out);
}

/* ── getRelationships ──────────────────────────────────────────────── */

wf_status wf_agent_get_relationships(wf_agent *agent, const char *actor,
                                     const char *const *others, size_t others_count,
                                     wf_response *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_did_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param *params = NULL;
    size_t param_count = 0;

    params = calloc(1 + others_count, sizeof(*params));
    if (!params) return WF_ERR_ALLOC;

    params[param_count].name = "actor";
    params[param_count].value = actor;
    param_count++;

    for (size_t i = 0; i < others_count; i++) {
        if (!others[i] || !others[i][0] || !wf_syntax_did_is_valid(others[i])) {
            free(params);
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "others";
        params[param_count].value = others[i];
        param_count++;
    }

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_query_params(agent->client,
                                             "app.bsky.graph.getRelationships",
                                             params, param_count, out);
    free(params);
    return status;
}

/* ── getList ───────────────────────────────────────────────────────── */

wf_status wf_agent_get_list(wf_agent *agent, const char *list_uri,
                            int limit, const char *cursor,
                            wf_response *out) {
    if (!agent || !list_uri || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(list_uri, &parsed)) {
        return WF_ERR_PARSE;
    }
    wf_syntax_aturi_free(&parsed);

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "list";
    params[param_count].value = list_uri;
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
    return wf_xrpc_query_params(agent->client, "app.bsky.graph.getList",
                                params, param_count, out);
}

/* ── getLists ──────────────────────────────────────────────────────── */

wf_status wf_agent_get_lists(wf_agent *agent, const char *actor,
                              int limit, const char *cursor,
                              wf_response *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "actor";
    params[param_count].value = actor;
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
    return wf_xrpc_query_params(agent->client, "app.bsky.graph.getLists",
                                params, param_count, out);
}

#endif // feed functions moved to feed.c

#if 0 // graph functions moved to graph.c

wf_status wf_agent_get_suggested_follows_by_actor(wf_agent *agent,
                                                   const char *actor,
                                                   wf_response *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_xrpc_param params[1];
    size_t param_count = 0;
    params[param_count].name = "actor";
    params[param_count].value = actor;
    param_count++;
    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.graph.getSuggestedFollowsByActor",
                                 params, param_count, out);
}

#endif // graph functions moved to graph.c

#if 0 // notification functions moved to notification.c
wf_status wf_agent_list_notifications(wf_agent *agent, int limit, const char *cursor,
                                       wf_response *out) {
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

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.notification.listNotifications",
                                 params, param_count, out);
}

wf_status wf_agent_update_seen_notifications(wf_agent *agent, const char *seen_at) {
    if (!agent || !seen_at) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_syntax_datetime_is_valid(seen_at)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(root, "seenAt", seen_at)) {
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
    wf_status status = wf_xrpc_procedure(agent->client,
                                         "app.bsky.notification.updateSeen",
                                         json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

/* ── getUnreadCount ────────────────────────────────────────────────── */

wf_status wf_agent_get_unread_count(wf_agent *agent, wf_response *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client,
                                "app.bsky.notification.getUnreadCount",
                                NULL, 0, out);
}

#endif // notification functions moved to notification.c

wf_status wf_agent_search_actors(wf_agent *agent, const char *query,
                                 int limit, const char *cursor, wf_response *out) {
    if (!agent || !query || !query[0] || !out) {
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
    return wf_xrpc_query_params(agent->client, "app.bsky.actor.searchActorsTypeahead",
                                 params, param_count, out);
}

wf_status wf_agent_update_profile(wf_agent *agent, const wf_agent_profile_update *update) {
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

    if (!cJSON_AddStringToObject(record, "$type", WF_AGENT_PROFILE_RECORD_TYPE) ||
        !cJSON_AddStringToObject(record, "createdAt", created_at)) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    if (update->display_name) {
        if (!cJSON_AddStringToObject(record, "displayName", update->display_name)) {
            cJSON_Delete(record);
            return WF_ERR_ALLOC;
        }
    }

    if (update->description) {
        if (!cJSON_AddStringToObject(record, "description", update->description)) {
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
    wf_status status = wf_agent_put_record_call(agent, WF_AGENT_PROFILE_COLLECTION,
                                                WF_AGENT_PROFILE_RKEY, record, &result);
    wf_agent_post_result_free(&result);
    return status;
}

wf_status wf_agent_upload_blob(wf_agent *agent, const void *data, size_t data_len,
                               const char *content_type, wf_response *out) {
    if (!agent || !data || data_len == 0 || !content_type || !out) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_agent_is_logged_in(agent)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_upload_blob(agent->client, WF_AGENT_UPLOAD_BLOB_NSID,
                               data, data_len, content_type, out);
}

/* ── applyWrites ────────────────────────────────────────────────────── */

/* Unused: wf_agent_build_write_value removed */

static const char *wf_agent_write_type_str(wf_agent_write_type type) {
    switch (type) {
        case WF_AGENT_WRITE_CREATE: return "com.atproto.repo.applyWrites#create";
        case WF_AGENT_WRITE_UPDATE: return "com.atproto.repo.applyWrites#update";
        case WF_AGENT_WRITE_DELETE: return "com.atproto.repo.applyWrites#delete";
    }
    return NULL;
}

static wf_status wf_agent_add_write(cJSON *writes_array, const wf_agent_write *write) {
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

wf_status wf_agent_apply_writes(wf_agent *agent,
                                 const wf_agent_write *writes, size_t write_count,
                                 wf_response *out) {
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
    status = wf_xrpc_procedure(agent->client,
                                "com.atproto.repo.applyWrites",
                                json, out);
    free(json);
    return status;
}

wf_status wf_agent_resolve_handle(wf_agent *agent, const char *handle, char **out_did) {
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
    wf_status status = wf_xrpc_query_params(agent->client, WF_AGENT_RESOLVE_HANDLE_NSID,
                                             params, 1, &res);
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

    status = wf_agent_set_string(out_did, did->valuestring);
    if (status != WF_OK) {
        free(*out_did);
        *out_did = NULL;
    }

    cJSON_Delete(root);
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

wf_status wf_agent_describe_server(wf_agent *agent, wf_agent_server_description *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_server_description_reset(out);

    wf_server_description sdesc = {0};
    wf_status status = wf_server_describe(agent->client, &sdesc);
    if (status != WF_OK) {
        return status;
    }

    status = wf_agent_set_string(&out->did, sdesc.did);
    if (status == WF_OK) {
        out->invite_code_required = sdesc.invite_code_required;
        out->phone_verification_required = sdesc.phone_verification_required;
    }
    if (status == WF_OK && sdesc.available_user_domains_count > 0) {
        out->available_user_domains = calloc(sdesc.available_user_domains_count,
                                             sizeof(char *));
        if (!out->available_user_domains) {
            status = WF_ERR_ALLOC;
        }
    }
    if (status == WF_OK) {
        for (size_t i = 0; i < sdesc.available_user_domains_count; ++i) {
            out->available_user_domains[i] =
                wf_agent_strdup(sdesc.available_user_domains[i]);
            if (!out->available_user_domains[i]) {
                status = WF_ERR_ALLOC;
                break;
            }
            out->available_user_domain_count++;
        }
    }
    if (status == WF_OK) {
        status = wf_agent_set_string(&out->privacy_policy, sdesc.links_privacy_policy);
    }
    if (status == WF_OK) {
        status = wf_agent_set_string(&out->terms_of_service, sdesc.links_terms_of_service);
    }
    if (status == WF_OK) {
        status = wf_agent_set_string(&out->contact_email, sdesc.contact_email);
    }

    if (status != WF_OK) {
        wf_agent_server_description_reset(out);
    }

    wf_server_describe_free(&sdesc);
    return status;
}

wf_status wf_agent_create_app_password(wf_agent *agent, const char *name, int privileged,
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
    wf_status status = wf_server_create_app_password(agent->client, &input, &spwd);
    if (status != WF_OK) {
        return status;
    }

    status = wf_agent_set_string(&out->name, spwd.name);
    if (status == WF_OK) {
        status = wf_agent_set_string(&out->created_at, spwd.created_at);
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

wf_status wf_agent_list_app_passwords(wf_agent *agent, wf_agent_app_password_list *out) {
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
        out->passwords = calloc(slist.password_count, sizeof(wf_agent_app_password));
        if (!out->passwords) {
            status = WF_ERR_ALLOC;
        }
    }
    if (status == WF_OK) {
        for (size_t i = 0; i < slist.password_count; ++i) {
            wf_server_app_password *src = &slist.passwords[i];
            wf_agent_app_password *dst = &out->passwords[i];

            status = wf_agent_set_string(&dst->name, src->name);
            if (status == WF_OK) {
                status = wf_agent_set_string(&dst->created_at, src->created_at);
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

wf_status wf_agent_delete_account(wf_agent *agent, const char *did, const char *password,
                                  const char *token) {
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

/* ── repo sync: set_did / set_signing_key ──────────────────────────── */

wf_status wf_agent_set_did(wf_agent *agent, const char *did) {
    if (!agent || !did || !wf_syntax_did_is_valid(did))
        return WF_ERR_INVALID_ARG;
    char *copy = wf_agent_strdup(did);
    if (!copy) return WF_ERR_ALLOC;
    free(agent->mirror_did);
    agent->mirror_did = copy;
    return WF_OK;
}

wf_status wf_agent_set_signing_key(wf_agent *agent, const char *key) {
    if (!agent || !key || !key[0])
        return WF_ERR_INVALID_ARG;
    char *copy = wf_agent_strdup(key);
    if (!copy) return WF_ERR_ALLOC;
    free(agent->mirror_signing_key);
    agent->mirror_signing_key = copy;
    return WF_OK;
}

/* ── repo sync: seed_repo ──────────────────────────────────────────── */

wf_status wf_agent_seed_repo(wf_agent *agent, const wf_car *car) {
    if (!agent || !car || car->root_count == 0 || !car->roots)
        return WF_ERR_INVALID_ARG;

    wf_car existing = {0};
    /* wf_status status = WF_OK; */

    /* Deep-copy the CAR into agent->mirror. */
    existing.roots = malloc(car->root_count * sizeof(wf_cid));
    if (!existing.roots) return WF_ERR_ALLOC;
    existing.root_count = car->root_count;
    for (size_t i = 0; i < car->root_count; i++)
        existing.roots[i] = car->roots[i];

    if (car->block_count > 0) {
        existing.blocks = calloc(car->block_count, sizeof(wf_car_block));
        if (!existing.blocks) {
            free(existing.roots);
            return WF_ERR_ALLOC;
        }
        for (size_t i = 0; i < car->block_count; i++) {
            existing.blocks[i].data = malloc(car->blocks[i].data_len);
            if (!existing.blocks[i].data) {
                for (size_t j = 0; j < i; j++)
                    free(existing.blocks[j].data);
                free(existing.blocks);
                free(existing.roots);
                return WF_ERR_ALLOC;
            }
            memcpy(existing.blocks[i].data, car->blocks[i].data,
                   car->blocks[i].data_len);
            existing.blocks[i].data_len = car->blocks[i].data_len;
            existing.blocks[i].cid = car->blocks[i].cid;
        }
        existing.block_count = car->block_count;
    }

    wf_car_free(&agent->mirror);
    agent->mirror = existing;
    return WF_OK;
}

/* ── repo sync: apply_repo_diff ────────────────────────────────────── */

wf_status wf_agent_apply_repo_diff(wf_agent *agent,
                                   const unsigned char *car_bytes,
                                   size_t car_len) {
    if (!agent || !car_bytes || car_len == 0)
        return WF_ERR_INVALID_ARG;
    if (agent->mirror.root_count != 1 || !agent->mirror.roots ||
        !agent->mirror_did || !agent->mirror_signing_key)
        return WF_ERR_INVALID_ARG;

    wf_car update = {0};
    wf_status status = wf_car_parse(car_bytes, car_len, &update);
    if (status != WF_OK) return status;

    wf_repo_verify_options opts = {
        .expected_did = agent->mirror_did,
        .signing_key = agent->mirror_signing_key,
    };

    wf_repo_diff diff = {0};
    status = wf_repo_diff_verify(&agent->mirror, &agent->mirror.roots[0],
                                 &update, &opts, &diff);
    wf_car_free(&update);
    if (status != WF_OK) return status;

    status = wf_repo_diff_apply(&agent->mirror, &diff);
    wf_repo_diff_free(&diff);
    return status;
}

/* ── repo sync: repo_head ──────────────────────────────────────────── */

wf_status wf_agent_repo_head(wf_agent *agent, char **out_head) {
    if (!agent || !out_head)
        return WF_ERR_INVALID_ARG;
    if (agent->mirror.root_count != 1 || !agent->mirror.roots)
        return WF_ERR_INVALID_ARG;

    *out_head = wf_cid_to_string(&agent->mirror.roots[0]);
    return *out_head ? WF_OK : WF_ERR_ALLOC;
}

/* ── repo sync: invert_repo_operations ─────────────────────────────── */

wf_status wf_agent_invert_repo_operations(wf_agent *agent,
                                          const wf_repo_operation *operations,
                                          size_t count,
                                          wf_repo_operation **out_inverse) {
    (void)agent;
    return wf_repo_operations_invert(operations, count, out_inverse);
}

/* ── repo sync: mirror_get_record ──────────────────────────────────── */

wf_status wf_agent_mirror_get_record(wf_agent *agent, const char *collection,
                                     const char *rkey,
                                     unsigned char **out_data, size_t *out_len) {
    if (!agent || !collection || !rkey || !out_data || !out_len)
        return WF_ERR_INVALID_ARG;
    if (agent->mirror.root_count == 0)
        return WF_ERR_INVALID_ARG;

    wf_cid record_cid = {0};
    return wf_repo_get_record(&agent->mirror, &agent->mirror.roots[0],
                              collection, rkey, out_data, out_len, &record_cid);
}

/* ── sync.getBlob ──────────────────────────────────────────────────── */

wf_status wf_agent_sync_get_blob(wf_agent *agent, const char *did, const char *cid,
                                 wf_response *out) {
    if (!agent || !did || !cid || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_did_is_valid(did)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[] = {
        {"did", did},
        {"cid", cid},
    };
    return wf_xrpc_query_params(agent->client, "com.atproto.sync.getBlob",
                                params, 2, out);
}

/* ── sync.getBlocks ────────────────────────────────────────────────── */

wf_status wf_agent_sync_get_blocks(wf_agent *agent, const char *did,
                                   const char *const *cids, size_t cid_count,
                                   wf_response *out) {
    if (!agent || !did || !cids || cid_count == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_did_is_valid(did)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param *params = calloc(cid_count + 1, sizeof(*params));
    if (!params) return WF_ERR_ALLOC;

    params[0].name = "did";
    params[0].value = did;
    for (size_t i = 0; i < cid_count; i++) {
        if (!cids[i] || !cids[i][0]) {
            free(params);
            return WF_ERR_INVALID_ARG;
        }
        params[i + 1].name = "cids";
        params[i + 1].value = cids[i];
    }

    wf_status status = wf_xrpc_query_params(agent->client, "com.atproto.sync.getBlocks",
                                             params, cid_count + 1, out);
    free(params);
    return status;
}

/* ── sync.getRecord ────────────────────────────────────────────────── */

wf_status wf_agent_sync_get_record(wf_agent *agent, const char *did,
                                   const char *collection, const char *rkey,
                                   wf_response *out) {
    if (!agent || !did || !collection || !rkey || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_did_is_valid(did) ||
        wf_syntax_nsid_validate(collection) != WF_OK ||
        !wf_syntax_record_key_is_valid(rkey)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[] = {
        {"did", did},
        {"collection", collection},
        {"rkey", rkey},
    };
    return wf_xrpc_query_params(agent->client, "com.atproto.sync.getRecord",
                                params, 3, out);
}

/* ── sync.listBlobs ────────────────────────────────────────────────── */

wf_status wf_agent_sync_list_blobs(wf_agent *agent, const char *did,
                                   int limit, const char *cursor,
                                   const char *since, wf_response *out) {
    if (!agent || !did || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_did_is_valid(did)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[5];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "did";
    params[param_count].value = did;
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
    if (since && since[0]) {
        if (!wf_syntax_tid_is_valid(since)) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "since";
        params[param_count].value = since;
        param_count++;
    }

    return wf_xrpc_query_params(agent->client, "com.atproto.sync.listBlobs",
                                params, param_count, out);
}
