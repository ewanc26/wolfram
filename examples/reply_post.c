/*
 * reply_post.c — create a reply to an existing Bluesky post.
 *
 * Demonstrates:
 *   1. PDS session login with wf_session
 *   2. AT URI validation with wf_syntax_aturi_parse
 *   3. Resolving the parent post CID via app.bsky.feed.getPosts
 *   4. Building a com.atproto.repo.createRecord JSON body with cJSON
 *   5. Posting the reply through the XRPC transport
 *
 * Usage:
 *   reply_post <service-url> <handle-or-email> <password> <parent-post-uri> <reply text>
 */

#include <cJSON.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wolfram/session.h"
#include "wolfram/syntax.h"
#include "wolfram/xrpc.h"

#define WF_CREATE_RECORD_NSID "com.atproto.repo.createRecord"
#define WF_GET_POSTS_NSID      "app.bsky.feed.getPosts"
#define WF_POST_COLLECTION     "app.bsky.feed.post"
#define WF_POST_RECORD_TYPE    "app.bsky.feed.post"

static char *wf_strdup(const char *s) {
    size_t len;
    char *copy;

    if (!s) {
        return NULL;
    }

    len = strlen(s) + 1;
    copy = malloc(len);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, s, len);
    return copy;
}

static int wf_is_unreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '-' || c == '.' || c == '_' || c == '~';
}

static char *wf_url_encode(const char *input) {
    static const char hex[] = "0123456789ABCDEF";
    char *out;
    char *dst;
    const unsigned char *src;
    size_t len;

    if (!input) {
        return NULL;
    }

    len = strlen(input);
    if (len > (SIZE_MAX - 1) / 3) {
        return NULL;
    }

    out = malloc(len * 3 + 1);
    if (!out) {
        return NULL;
    }

    dst = out;
    for (src = (const unsigned char *)input; *src; ++src) {
        if (wf_is_unreserved(*src)) {
            *dst++ = (char)*src;
        } else {
            *dst++ = '%';
            *dst++ = hex[*src >> 4];
            *dst++ = hex[*src & 0x0F];
        }
    }
    *dst = '\0';

    return out;
}

static char *wf_build_query_1(const char *name, const char *value) {
    char *encoded;
    char *query;
    size_t name_len;
    size_t value_len;
    char *dst;

    if (!name || !value) {
        return NULL;
    }

    encoded = wf_url_encode(value);
    if (!encoded) {
        return NULL;
    }

    name_len = strlen(name);
    value_len = strlen(encoded);
    if (name_len > SIZE_MAX - value_len - 2) {
        free(encoded);
        return NULL;
    }

    query = malloc(name_len + value_len + 2);
    if (!query) {
        free(encoded);
        return NULL;
    }

    dst = query;
    memcpy(dst, name, name_len);
    dst += name_len;
    *dst++ = '=';
    memcpy(dst, encoded, value_len);
    dst += value_len;
    *dst = '\0';

    free(encoded);
    return query;
}

static char *wf_build_query_2(const char *name1, const char *value1,
                              const char *name2, const char *value2) {
    char *encoded1;
    char *encoded2;
    char *query;
    size_t name1_len;
    size_t name2_len;
    size_t value1_len;
    size_t value2_len;
    char *dst;

    if (!name1 || !value1) {
        return NULL;
    }

    encoded1 = wf_url_encode(value1);
    if (!encoded1) {
        return NULL;
    }

    encoded2 = NULL;
    if (name2 && value2) {
        encoded2 = wf_url_encode(value2);
        if (!encoded2) {
            free(encoded1);
            return NULL;
        }
    }

    name1_len = strlen(name1);
    value1_len = strlen(encoded1);
    if (name1_len > SIZE_MAX - value1_len - 2) {
        free(encoded1);
        free(encoded2);
        return NULL;
    }

    if (name2 && encoded2) {
        name2_len = strlen(name2);
        value2_len = strlen(encoded2);
        if (name2_len > SIZE_MAX - value2_len - 2 ||
            name1_len + value1_len + 2 > SIZE_MAX - (name2_len + value2_len + 2)) {
            free(encoded1);
            free(encoded2);
            return NULL;
        }
        query = malloc(name1_len + value1_len + name2_len + value2_len + 3);
    } else {
        query = malloc(name1_len + value1_len + 2);
    }

    if (!query) {
        free(encoded1);
        free(encoded2);
        return NULL;
    }

    dst = query;
    memcpy(dst, name1, name1_len);
    dst += name1_len;
    *dst++ = '=';
    memcpy(dst, encoded1, value1_len);
    dst += value1_len;

    if (name2 && encoded2) {
        *dst++ = '&';
        memcpy(dst, name2, name2_len);
        dst += name2_len;
        *dst++ = '=';
        memcpy(dst, encoded2, value2_len);
        dst += value2_len;
    }

    *dst = '\0';

    free(encoded1);
    free(encoded2);
    return query;
}

static int wf_make_rfc3339_timestamp(char *buf, size_t buf_len) {
    time_t now = time(NULL);
    struct tm tm_utc;
    struct tm *tmp = gmtime(&now);

    if (!tmp) {
        return 0;
    }

    tm_utc = *tmp;
    return strftime(buf, buf_len, "%Y-%m-%dT%H:%M:%SZ", &tm_utc) != 0;
}

static int wf_extract_parent_refs(const cJSON *post_view,
                                  const char **out_parent_uri,
                                  const char **out_parent_cid,
                                  const char **out_root_uri,
                                  const char **out_root_cid) {
    const cJSON *uri;
    const cJSON *cid;
    const cJSON *record;
    const cJSON *reply;
    const cJSON *root;
    const cJSON *root_uri;
    const cJSON *root_cid;

    if (!post_view || !out_parent_uri || !out_parent_cid || !out_root_uri || !out_root_cid) {
        return 0;
    }

    uri = cJSON_GetObjectItemCaseSensitive((cJSON *)post_view, "uri");
    cid = cJSON_GetObjectItemCaseSensitive((cJSON *)post_view, "cid");
    record = cJSON_GetObjectItemCaseSensitive((cJSON *)post_view, "record");
    if (!cJSON_IsString(uri) || !cJSON_IsString(cid) || !cJSON_IsObject(record)) {
        return 0;
    }

    *out_parent_uri = uri->valuestring;
    *out_parent_cid = cid->valuestring;
    *out_root_uri = uri->valuestring;
    *out_root_cid = cid->valuestring;

    reply = cJSON_GetObjectItemCaseSensitive((cJSON *)record, "reply");
    if (!cJSON_IsObject(reply)) {
        return 1;
    }

    root = cJSON_GetObjectItemCaseSensitive((cJSON *)reply, "root");
    if (!cJSON_IsObject(root)) {
        return 1;
    }

    root_uri = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "uri");
    root_cid = cJSON_GetObjectItemCaseSensitive((cJSON *)root, "cid");
    if (!cJSON_IsString(root_uri) || !cJSON_IsString(root_cid)) {
        return 0;
    }

    *out_root_uri = root_uri->valuestring;
    *out_root_cid = root_cid->valuestring;
    return 1;
}

static wf_status wf_build_reply_record_body(const char *repo_did,
                                            const char *text,
                                            const char *created_at,
                                            const char *parent_uri,
                                            const char *parent_cid,
                                            const char *root_uri,
                                            const char *root_cid,
                                            char **out_json) {
    cJSON *root;
    cJSON *record;
    cJSON *reply;
    cJSON *parent_ref;
    cJSON *root_ref;
    char *json;

    if (!repo_did || !text || !created_at || !parent_uri || !parent_cid ||
        !root_uri || !root_cid || !out_json) {
        return WF_ERR_INVALID_ARG;
    }

    *out_json = NULL;

    if (wf_syntax_nsid_validate(WF_POST_COLLECTION) != WF_OK ||
        !wf_syntax_did_is_valid(repo_did) ||
        !wf_syntax_datetime_is_valid(created_at)) {
        return WF_ERR_INVALID_ARG;
    }

    root = cJSON_CreateObject();
    record = cJSON_CreateObject();
    reply = cJSON_CreateObject();
    parent_ref = cJSON_CreateObject();
    root_ref = cJSON_CreateObject();
    if (!root || !record || !reply || !parent_ref || !root_ref) {
        cJSON_Delete(root_ref);
        cJSON_Delete(parent_ref);
        cJSON_Delete(reply);
        cJSON_Delete(record);
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(root_ref, "uri", root_uri) ||
        !cJSON_AddStringToObject(root_ref, "cid", root_cid) ||
        !cJSON_AddStringToObject(parent_ref, "uri", parent_uri) ||
        !cJSON_AddStringToObject(parent_ref, "cid", parent_cid) ||
        !cJSON_AddItemToObject(reply, "root", root_ref) ||
        !cJSON_AddItemToObject(reply, "parent", parent_ref) ||
        !cJSON_AddStringToObject(root, "repo", repo_did) ||
        !cJSON_AddStringToObject(root, "collection", WF_POST_COLLECTION) ||
        !cJSON_AddItemToObject(root, "record", record) ||
        !cJSON_AddStringToObject(record, "$type", WF_POST_RECORD_TYPE) ||
        !cJSON_AddStringToObject(record, "text", text) ||
        !cJSON_AddStringToObject(record, "createdAt", created_at) ||
        !cJSON_AddItemToObject(record, "reply", reply)) {
        cJSON_Delete(root_ref);
        cJSON_Delete(parent_ref);
        cJSON_Delete(reply);
        cJSON_Delete(record);
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    *out_json = json;
    return WF_OK;
}

static int wf_print_create_record_uri(const wf_response *res) {
    cJSON *root;
    cJSON *uri;

    if (!res || !res->body || res->body_len == 0) {
        fprintf(stderr, "createRecord returned an empty response\n");
        return 0;
    }

    root = cJSON_ParseWithLength(res->body, res->body_len);
    if (!root) {
        fprintf(stderr, "createRecord returned invalid JSON\n");
        return 0;
    }

    uri = cJSON_GetObjectItemCaseSensitive(root, "uri");
    if (!cJSON_IsString(uri)) {
        cJSON_Delete(root);
        fprintf(stderr, "createRecord response did not include a uri\n");
        return 0;
    }

    printf("%s\n", uri->valuestring);
    cJSON_Delete(root);
    return 1;
}

int main(int argc, char **argv) {
    const char *service_url;
    const char *identifier;
    const char *password;
    const char *parent_post_uri;
    const char *reply_text;
    wf_session *session;
    wf_status status;
    wf_syntax_aturi parent_uri;
    wf_response res = {0};
    char *get_posts_query = NULL;
    cJSON *root;
    cJSON *posts;
    cJSON *post_view;
    const char *parent_uri_str = NULL;
    const char *parent_cid = NULL;
    const char *root_uri_str = NULL;
    const char *root_cid = NULL;
    char created_at[32];
    char *body_json = NULL;

    if (argc != 6) {
        fprintf(stderr,
                "usage: %s <service-url> <handle-or-email> <password> <parent-post-uri> <reply text>\n",
                argv[0]);
        return 1;
    }

    service_url = argv[1];
    identifier = argv[2];
    password = argv[3];
    parent_post_uri = argv[4];
    reply_text = argv[5];

    if (!wf_syntax_aturi_parse(parent_post_uri, &parent_uri)) {
        fprintf(stderr, "invalid parent post URI: %s\n", parent_post_uri);
        return 1;
    }
    if (!parent_uri.collection || strcmp(parent_uri.collection, WF_POST_COLLECTION) != 0 ||
        !parent_uri.record_key) {
        fprintf(stderr, "parent URI must reference an app.bsky.feed.post record\n");
        wf_syntax_aturi_free(&parent_uri);
        return 1;
    }
    wf_syntax_aturi_free(&parent_uri);

    session = wf_session_new(service_url);
    if (!session) {
        fprintf(stderr, "failed to create session\n");
        return 1;
    }

    status = wf_session_login(session, identifier, password);
    if (status != WF_OK) {
        fprintf(stderr, "login failed: %d\n", (int)status);
        wf_session_free(session);
        return 1;
    }

    if (!session->data.did || !wf_syntax_did_is_valid(session->data.did)) {
        fprintf(stderr, "session did is invalid or missing\n");
        wf_session_free(session);
        return 1;
    }

    get_posts_query = wf_build_query_1("uris", parent_post_uri);
    if (!get_posts_query) {
        fprintf(stderr, "failed to build getPosts query\n");
        wf_session_free(session);
        return 1;
    }

    status = wf_xrpc_query(session->client, WF_GET_POSTS_NSID, get_posts_query, &res);
    free(get_posts_query);
    get_posts_query = NULL;
    if (status != WF_OK && status != WF_ERR_HTTP) {
        fprintf(stderr, "getPosts failed: %d\n", (int)status);
        wf_response_free(&res);
        wf_session_free(session);
        return 1;
    }
    if (status == WF_ERR_HTTP) {
        fprintf(stderr, "getPosts returned HTTP %ld\n", res.status);
        if (res.body && res.body_len > 0) {
            fprintf(stderr, "%s\n", res.body);
        }
        wf_response_free(&res);
        wf_session_free(session);
        return 1;
    }

    root = cJSON_ParseWithLength(res.body, res.body_len);
    wf_response_free(&res);
    if (!root) {
        fprintf(stderr, "failed to parse getPosts response\n");
        wf_session_free(session);
        return 1;
    }

    posts = cJSON_GetObjectItemCaseSensitive(root, "posts");
    if (!cJSON_IsArray(posts)) {
        fprintf(stderr, "getPosts response did not include a posts array\n");
        cJSON_Delete(root);
        wf_session_free(session);
        return 1;
    }

    post_view = cJSON_GetArrayItem(posts, 0);
    if (!post_view || !wf_extract_parent_refs(post_view,
                                              &parent_uri_str,
                                              &parent_cid,
                                              &root_uri_str,
                                              &root_cid)) {
        fprintf(stderr, "failed to resolve parent post reference\n");
        cJSON_Delete(root);
        wf_session_free(session);
        return 1;
    }

    cJSON_Delete(root);

    if (!wf_make_rfc3339_timestamp(created_at, sizeof(created_at))) {
        fprintf(stderr, "failed to create timestamp\n");
        wf_session_free(session);
        return 1;
    }

    status = wf_build_reply_record_body(session->data.did,
                                        reply_text,
                                        created_at,
                                        parent_uri_str,
                                        parent_cid,
                                        root_uri_str,
                                        root_cid,
                                        &body_json);
    if (status != WF_OK) {
        fprintf(stderr, "failed to build reply record body: %d\n", (int)status);
        wf_session_free(session);
        return 1;
    }

    status = wf_xrpc_procedure(session->client, WF_CREATE_RECORD_NSID, body_json, &res);
    free(body_json);
    body_json = NULL;
    if (status != WF_OK && status != WF_ERR_HTTP) {
        fprintf(stderr, "createRecord failed: %d\n", (int)status);
        wf_response_free(&res);
        wf_session_free(session);
        return 1;
    }
    if (status == WF_ERR_HTTP) {
        fprintf(stderr, "createRecord returned HTTP %ld\n", res.status);
        if (res.body && res.body_len > 0) {
            fprintf(stderr, "%s\n", res.body);
        }
        wf_response_free(&res);
        wf_session_free(session);
        return 1;
    }

    if (!wf_print_create_record_uri(&res)) {
        wf_response_free(&res);
        wf_session_free(session);
        return 1;
    }

    wf_response_free(&res);
    wf_session_free(session);
    return 0;
}
