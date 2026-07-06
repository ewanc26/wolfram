/*
 * create_post.c — end-to-end Bluesky post creation example.
 *
 * Demonstrates:
 *   1. PDS session login with wf_session
 *   2. Rich text facet detection with wf_richtext
 *   3. Handle resolution for mention facets via the identity API
 *   4. Building a com.atproto.repo.createRecord JSON body with cJSON
 *   5. Posting the record through the XRPC transport
 *
 * Usage:
 *   create_post <service-url> <handle-or-email> <password> <post text...>
 */

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "wolfram/identity.h"
#include "wolfram/richtext.h"
#include "wolfram/session.h"
#include "wolfram/syntax.h"
#include "wolfram/xrpc.h"

#define WF_CREATE_RECORD_NSID "com.atproto.repo.createRecord"
#define WF_POST_COLLECTION     "app.bsky.feed.post"
#define WF_POST_RECORD_TYPE    "app.bsky.feed.post"
#define WF_FACET_MENTION_TYPE  "app.bsky.richtext.facet#mention"
#define WF_FACET_LINK_TYPE     "app.bsky.richtext.facet#link"
#define WF_FACET_TAG_TYPE      "app.bsky.richtext.facet#tag"

static char *wf_dup_span(const char *s, size_t len) {
    char *out = malloc(len + 1);
    if (!out) {
        return NULL;
    }

    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

static char *wf_join_args(int argc, char **argv, int first) {
    size_t total = 1;

    for (int i = first; i < argc; ++i) {
        size_t part = strlen(argv[i]);
        if (part > SIZE_MAX - total - 1) {
            return NULL;
        }
        total += part;
        if (i + 1 < argc) {
            ++total;
        }
    }

    char *out = malloc(total);
    if (!out) {
        return NULL;
    }

    char *dst = out;
    for (int i = first; i < argc; ++i) {
        size_t part = strlen(argv[i]);
        memcpy(dst, argv[i], part);
        dst += part;
        if (i + 1 < argc) {
            *dst++ = ' ';
        }
    }
    *dst = '\0';

    return out;
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

static wf_status wf_add_feature_json(cJSON *features,
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

        char *handle = wf_dup_span(segment->text + 1, segment->text_len - 1);
        if (!handle) {
            status = WF_ERR_ALLOC;
            break;
        }

        char *did = NULL;
        status = wf_handle_resolve(client, handle, &did);
        free(handle);
        if (status != WF_OK) {
            break;
        }
        if (!did || !wf_syntax_did_is_valid(did)) {
            free(did);
            status = WF_ERR_PARSE;
            break;
        }

        if (!cJSON_AddStringToObject(feature_json, "$type", WF_FACET_MENTION_TYPE) ||
            !cJSON_AddStringToObject(feature_json, "did", did)) {
            free(did);
            status = WF_ERR_ALLOC;
            break;
        }

        free(did);
        break;
    }

    case WF_RICHTEXT_FEATURE_LINK:
        if (!cJSON_AddStringToObject(feature_json, "$type", WF_FACET_LINK_TYPE) ||
            !cJSON_AddStringToObject(feature_json, "uri", feature->uri)) {
            status = WF_ERR_ALLOC;
        }
        break;

    case WF_RICHTEXT_FEATURE_TAG:
        if (!cJSON_AddStringToObject(feature_json, "$type", WF_FACET_TAG_TYPE) ||
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

static wf_status wf_add_segment_facet_json(cJSON *facets,
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
        wf_status status = wf_add_feature_json(features_json, segment,
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

static wf_status wf_build_create_record_body(const wf_richtext *rt,
                                             const char *repo_did,
                                             const char *created_at,
                                             wf_xrpc_client *client,
                                             char **out_json) {
    if (!rt || !repo_did || !created_at || !client || !out_json) {
        return WF_ERR_INVALID_ARG;
    }

    *out_json = NULL;

    if (wf_syntax_nsid_validate(WF_POST_COLLECTION) != WF_OK) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_syntax_did_is_valid(repo_did) || !wf_syntax_datetime_is_valid(created_at)) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return WF_ERR_ALLOC;
    }

    cJSON *record = cJSON_CreateObject();
    if (!record) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(root, "repo", repo_did) ||
        !cJSON_AddStringToObject(root, "collection", WF_POST_COLLECTION) ||
        !cJSON_AddItemToObject(root, "record", record)) {
        cJSON_Delete(record);
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(record, "$type", WF_POST_RECORD_TYPE) ||
        !cJSON_AddStringToObject(record, "text", rt->text) ||
        !cJSON_AddStringToObject(record, "createdAt", created_at)) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    size_t segment_count = wf_richtext_segment_count(rt);
    cJSON *facets = NULL;

    for (size_t i = 0; i < segment_count; ++i) {
        wf_richtext_segment segment = wf_richtext_get_segment(rt, i);
        if (!segment.facet) {
            continue;
        }

        if (!facets) {
            facets = cJSON_CreateArray();
            if (!facets) {
                cJSON_Delete(root);
                return WF_ERR_ALLOC;
            }
        }

        wf_status status = wf_add_segment_facet_json(facets, &segment, client);
        if (status != WF_OK) {
            cJSON_Delete(facets);
            cJSON_Delete(root);
            return status;
        }
    }

    if (facets) {
        if (!cJSON_AddItemToObject(record, "facets", facets)) {
            cJSON_Delete(facets);
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return WF_ERR_ALLOC;
    }

    *out_json = json;
    return WF_OK;
}

static void wf_print_create_record_response(const wf_response *res) {
    if (!res) {
        return;
    }

    if (!res->body || res->body_len == 0) {
        printf("HTTP %ld\n(empty body)\n", res->status);
        return;
    }

    cJSON *root = cJSON_ParseWithLength(res->body, res->body_len);
    if (!root) {
        printf("HTTP %ld\n%s\n", res->status, res->body);
        return;
    }

    cJSON *uri = cJSON_GetObjectItemCaseSensitive(root, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(root, "cid");
    if (cJSON_IsString(uri) && cJSON_IsString(cid)) {
        printf("HTTP %ld\nCreated record:\n  uri: %s\n  cid: %s\n",
               res->status, uri->valuestring, cid->valuestring);
    } else {
        printf("HTTP %ld\n%s\n", res->status, res->body);
    }

    cJSON_Delete(root);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr,
                "usage: %s <service-url> <handle-or-email> <password> <post text...>\n",
                argv[0]);
        return 1;
    }

    const char *service_url = argv[1];
    const char *identifier = argv[2];
    const char *password = argv[3];

    char *post_text = wf_join_args(argc, argv, 4);
    if (!post_text) {
        fprintf(stderr, "failed to assemble post text\n");
        return 1;
    }

    wf_session *session = wf_session_new(service_url);
    if (!session) {
        fprintf(stderr, "failed to create session\n");
        free(post_text);
        return 1;
    }

    wf_status status = wf_session_login(session, identifier, password);
    if (status != WF_OK) {
        fprintf(stderr, "login failed: %d\n", (int)status);
        wf_session_free(session);
        free(post_text);
        return 1;
    }

    if (!session->data.did || !wf_syntax_did_is_valid(session->data.did)) {
        fprintf(stderr, "session did is invalid or missing\n");
        wf_session_free(session);
        free(post_text);
        return 1;
    }

    printf("Logged in as %s (%s)\n", session->data.handle, session->data.did);

    wf_richtext rt = {0};
    status = wf_richtext_init(&rt, post_text);
    free(post_text);
    post_text = NULL;
    if (status != WF_OK) {
        fprintf(stderr, "failed to initialize rich text: %d\n", (int)status);
        wf_session_free(session);
        return 1;
    }

    status = wf_richtext_detect_facets(&rt);
    if (status != WF_OK) {
        fprintf(stderr, "failed to detect facets: %d\n", (int)status);
        wf_richtext_free(&rt);
        wf_session_free(session);
        return 1;
    }

    char created_at[32];
    if (!wf_make_rfc3339_timestamp(created_at, sizeof(created_at))) {
        fprintf(stderr, "failed to create timestamp\n");
        wf_richtext_free(&rt);
        wf_session_free(session);
        return 1;
    }

    char *body_json = NULL;
    status = wf_build_create_record_body(&rt, session->data.did, created_at,
                                         session->client, &body_json);
    if (status != WF_OK) {
        fprintf(stderr, "failed to build request body: %d\n", (int)status);
        wf_richtext_free(&rt);
        wf_session_free(session);
        return 1;
    }

    wf_response res = {0};
    status = wf_xrpc_procedure(session->client, WF_CREATE_RECORD_NSID, body_json, &res);
    free(body_json);

    if (status != WF_OK && status != WF_ERR_HTTP) {
        fprintf(stderr, "createRecord failed: %d\n", (int)status);
        wf_response_free(&res);
        wf_richtext_free(&rt);
        wf_session_free(session);
        return 1;
    }

    wf_print_create_record_response(&res);

    wf_response_free(&res);
    wf_richtext_free(&rt);
    wf_session_free(session);
    return status == WF_OK ? 0 : 1;
}
