#include "wolfram/agent.h"

#include "wolfram/identity.h"
#include "wolfram/repo.h"
#include "wolfram/richtext.h"
#include "wolfram/server.h"
#include "wolfram/session.h"
#include "wolfram/syntax.h"

#include <cJSON.h>
#include "wolfram/atproto_lex.h"
#include "wolfram/util.h"
#include "wolfram/embed.h"

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
#define WF_AGENT_BLOCK_COLLECTION     "app.bsky.graph.block"
#define WF_AGENT_BLOCK_RECORD_TYPE    "app.bsky.graph.block"
#define WF_AGENT_PROFILE_COLLECTION   "app.bsky.actor.profile"
#define WF_AGENT_PROFILE_RECORD_TYPE  "app.bsky.actor.profile"
#define WF_AGENT_PROFILE_RKEY         "self"
#define WF_AGENT_RESOLVE_HANDLE_NSID  "com.atproto.identity.resolveHandle"
#define WF_AGENT_UPLOAD_BLOB_NSID     "com.atproto.repo.uploadBlob"
#define WF_AGENT_RESOLVE_HANDLE_NSID  "com.atproto.identity.resolveHandle"
#define WF_AGENT_FACET_MENTION_TYPE   "app.bsky.richtext.facet#mention"
#define WF_AGENT_FACET_LINK_TYPE      "app.bsky.richtext.facet#link"
#define WF_AGENT_FACET_TAG_TYPE       "app.bsky.richtext.facet#tag"

#include "_internal.h"

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

#if 0
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

#endif // unused helper functions

static wf_status wf_agent_set_string(char **dst, const char *src);
static int wf_agent_is_logged_in(const wf_agent *agent);



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

#if 0
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

#endif // unused profile parsing

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

/* Post with embed */
wf_status wf_agent_post_with_embed(wf_agent *agent, const char *text,
                                 const char *embed_json, wf_agent_post_result *out) {
    if (!agent || !text || !out) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *embed = NULL;
    if (embed_json && embed_json[0] != '\0') {
        embed = cJSON_Parse(embed_json);
        if (!embed) {
            return WF_ERR_PARSE;
        }
        if (!cJSON_IsObject(embed)) {
            cJSON_Delete(embed);
            return WF_ERR_INVALID_ARG;
        }
    } else {
        return wf_agent_post(agent, text, out);
    }

    wf_richtext rt = {0};
    wf_status status = wf_richtext_init(&rt, text);
    if (status != WF_OK) {
        cJSON_Delete(embed);
        return status;
    }
    status = wf_richtext_detect_facets(&rt);
    if (status != WF_OK) {
        wf_richtext_free(&rt);
        cJSON_Delete(embed);
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
                cJSON_Delete(embed);
                return WF_ERR_ALLOC;
            }
        }
        status = wf_agent_add_segment_facet_json(facets, &segment, agent->client);
        if (status != WF_OK) {
            cJSON_Delete(facets);
            wf_richtext_free(&rt);
            cJSON_Delete(embed);
            return status;
        }
    }
    wf_richtext_free(&rt);
    cJSON *record = NULL;
    status = wf_agent_build_post_record(agent, text, facets, &record);
    if (status != WF_OK) {
        cJSON_Delete(embed);
        return status;
    }

    if (!cJSON_AddItemToObject(record, "embed", embed)) {
        cJSON_Delete(embed);
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    return wf_agent_create_record_call(agent, WF_AGENT_POST_COLLECTION, record, out);
}

/* Reply with separate root and parent strong references. */
wf_status wf_agent_reply_refs(wf_agent *agent, const char *text,
                             const char *root_uri, const char *root_cid,
                             const char *parent_uri, const char *parent_cid,
                             wf_agent_post_result *out) {
    if (!agent || !text || !root_uri || !root_cid ||
        !parent_uri || !parent_cid || !out) {
        return WF_ERR_INVALID_ARG;
    }

    // Build post record with facets
    wf_richtext rt = {0};
    wf_status status = wf_richtext_init(&rt, text);
    if (status != WF_OK) return status;
    status = wf_richtext_detect_facets(&rt);
    if (status != WF_OK) { wf_richtext_free(&rt); return status; }
    cJSON *facets = NULL;
    size_t seg_cnt = wf_richtext_segment_count(&rt);
    for (size_t i = 0; i < seg_cnt; ++i) {
        wf_richtext_segment seg = wf_richtext_get_segment(&rt, i);
        if (!seg.facet || !seg.facet->features || seg.facet->feature_count == 0) continue;
        if (!facets) {
            facets = cJSON_CreateArray();
            if (!facets) { wf_richtext_free(&rt); return WF_ERR_ALLOC; }
        }
        status = wf_agent_add_segment_facet_json(facets, &seg, agent->client);
        if (status != WF_OK) { cJSON_Delete(facets); wf_richtext_free(&rt); return status; }
    }
    wf_richtext_free(&rt);

    cJSON *record = NULL;
    status = wf_agent_build_post_record(agent, text, facets, &record);
    if (status != WF_OK) return status;

    // Build reply object
    cJSON *reply = cJSON_CreateObject();
    if (!reply) { cJSON_Delete(record); return WF_ERR_ALLOC; }
    cJSON *root = cJSON_CreateObject();
    cJSON *parent = cJSON_CreateObject();
    if (!root || !parent) { cJSON_Delete(reply); cJSON_Delete(record); return WF_ERR_ALLOC; }
    cJSON_AddStringToObject(root, "uri", root_uri);
    cJSON_AddStringToObject(root, "cid", root_cid);
    cJSON_AddStringToObject(parent, "uri", parent_uri);
    cJSON_AddStringToObject(parent, "cid", parent_cid);
    cJSON_AddItemToObject(reply, "root", root);
    cJSON_AddItemToObject(reply, "parent", parent);
    if (!cJSON_AddItemToObject(record, "reply", reply)) {
        cJSON_Delete(reply);
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }
    return wf_agent_create_record_call(agent, WF_AGENT_POST_COLLECTION, record, out);
}

wf_status wf_agent_reply(wf_agent *agent, const char *text,
                        const char *parent_uri, const char *parent_cid,
                        wf_agent_post_result *out) {
    return wf_agent_reply_refs(agent, text, parent_uri, parent_cid,
                               parent_uri, parent_cid, out);
}

/* Quote a post (record embed) */
wf_status wf_agent_quote(wf_agent *agent, const char *text,
                         const char *quote_uri, const char *quote_cid,
                         wf_agent_post_result *out) {
    if (!agent || !text || !quote_uri || !quote_cid || !out) {
        return WF_ERR_INVALID_ARG;
    }
    cJSON *embed = wf_embed_record_new(quote_uri, quote_cid);
    if (!embed) return WF_ERR_ALLOC;
    char *embed_str = cJSON_PrintUnformatted(embed);
    cJSON_Delete(embed);
    if (!embed_str) return WF_ERR_ALLOC;
    wf_status status = wf_agent_post_with_embed(agent, text, embed_str, out);
    free(embed_str);
    return status;
}

/* Quote a post with media (recordWithMedia embed) */
wf_status wf_agent_quote_with_media(wf_agent *agent, const char *text,
                                    const char *quote_uri, const char *quote_cid,
                                    cJSON *media_embed,
                                    wf_agent_post_result *out) {
    if (!agent || !text || !quote_uri || !quote_cid || !media_embed || !out) {
        return WF_ERR_INVALID_ARG;
    }
    cJSON *record_embed = wf_embed_record_new(quote_uri, quote_cid);
    if (!record_embed) return WF_ERR_ALLOC;
    cJSON *combined = wf_embed_record_with_media_new(record_embed, media_embed);
    // wf_embed_record_with_media_new takes ownership of its arguments
    if (!combined) {
        // Record embed already transferred ownership if combined succeeded, otherwise free it
        cJSON_Delete(record_embed);
        return WF_ERR_ALLOC;
    }
    char *embed_str = cJSON_PrintUnformatted(combined);
    cJSON_Delete(combined);
    if (!embed_str) return WF_ERR_ALLOC;
    wf_status status = wf_agent_post_with_embed(agent, text, embed_str, out);
    free(embed_str);
    return status;
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

wf_status wf_agent_block(wf_agent *agent, const char *subject_did,
                          wf_agent_post_result *out) {
    if (!agent || !subject_did || !out) {
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
    if (!record) return WF_ERR_ALLOC;

    if (!cJSON_AddStringToObject(record, "$type", WF_AGENT_BLOCK_RECORD_TYPE) ||
        !cJSON_AddStringToObject(record, "subject", subject_did) ||
        !cJSON_AddStringToObject(record, "createdAt", created_at)) {
        cJSON_Delete(record);
        return WF_ERR_ALLOC;
    }

    return wf_agent_create_record_call(agent, WF_AGENT_BLOCK_COLLECTION, record, out);
}

wf_status wf_agent_unblock(wf_agent *agent, const char *block_uri) {
    if (!agent || !block_uri) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(block_uri, &parsed)) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    if (!parsed.authority || !wf_agent_authority_matches_session(agent, parsed.authority) ||
        !parsed.collection || !parsed.record_key ||
        strcmp(parsed.collection, WF_AGENT_BLOCK_COLLECTION) != 0) {
        status = WF_ERR_INVALID_ARG;
        goto done;
    }

    status = wf_agent_delete_record_call(agent, parsed.collection, parsed.record_key);

    done:
        wf_syntax_aturi_free(&parsed);
        return status;
}

wf_status wf_agent_delete_record(wf_agent *agent, const char *collection,
                                  const char *rkey) {
    return wf_agent_delete_record_call(agent, collection, rkey);
}
