/*
 * upload_image.c — upload an image blob and create a post embedding it.
 *
 * Demonstrates:
 *   1. PDS session login with wf_session
 *   2. Loading a binary image from disk with standard C file I/O
 *   3. Uploading the image with com.atproto.repo.uploadBlob
 *   4. Parsing the returned blob reference with cJSON
 *   5. Building a com.atproto.repo.createRecord request body with an
 *      app.bsky.embed.images embed
 *   6. Posting the record through the XRPC transport
 *
 * Usage:
 *   upload_image <service-url> <handle-or-email> <password> <image-path> [alt text...]
 */

#include <cJSON.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "wolfram/session.h"
#include "wolfram/syntax.h"
#include "wolfram/xrpc.h"

#define WF_UPLOAD_BLOB_NSID   "com.atproto.repo.uploadBlob"
#define WF_CREATE_RECORD_NSID "com.atproto.repo.createRecord"
#define WF_POST_COLLECTION    "app.bsky.feed.post"
#define WF_POST_RECORD_TYPE   "app.bsky.feed.post"
#define WF_IMAGE_EMBED_TYPE   "app.bsky.embed.images"
#define WF_BLOB_TYPE          "blob"
#define WF_POST_TEXT          "Check out this image!"

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

static int wf_ascii_iequals(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        ++a;
        ++b;
    }

    return *a == '\0' && *b == '\0';
}

static const char *wf_image_mime_type_from_path(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot || !dot[1]) {
        return NULL;
    }

    const char *ext = dot + 1;
    if (wf_ascii_iequals(ext, "jpg") || wf_ascii_iequals(ext, "jpeg")) {
        return "image/jpeg";
    }
    if (wf_ascii_iequals(ext, "png")) {
        return "image/png";
    }
    if (wf_ascii_iequals(ext, "gif")) {
        return "image/gif";
    }
    if (wf_ascii_iequals(ext, "webp")) {
        return "image/webp";
    }

    return NULL;
}

static wf_status wf_read_file(const char *path, unsigned char **out_data, size_t *out_len) {
    if (!path || !out_data || !out_len) {
        return WF_ERR_INVALID_ARG;
    }

    *out_data = NULL;
    *out_len = 0;

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "failed to open image file '%s': %s\n", path, strerror(errno));
        return WF_ERR_NOT_FOUND;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "failed to seek image file '%s'\n", path);
        fclose(f);
        return WF_ERR_PARSE;
    }

    long len = ftell(f);
    if (len < 0) {
        fprintf(stderr, "failed to determine image file size for '%s'\n", path);
        fclose(f);
        return WF_ERR_PARSE;
    }

    if (len == 0) {
        fprintf(stderr, "image file '%s' is empty\n", path);
        fclose(f);
        return WF_ERR_INVALID_ARG;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "failed to rewind image file '%s'\n", path);
        fclose(f);
        return WF_ERR_PARSE;
    }

    unsigned char *data = malloc((size_t)len);
    if (!data) {
        fclose(f);
        return WF_ERR_ALLOC;
    }

    size_t n = fread(data, 1, (size_t)len, f);
    if (n != (size_t)len) {
        fprintf(stderr, "failed to read image file '%s'\n", path);
        free(data);
        fclose(f);
        return WF_ERR_PARSE;
    }

    fclose(f);
    *out_data = data;
    *out_len = (size_t)len;
    return WF_OK;
}

typedef struct wf_uploaded_blob {
    char *cid;
    char *mime_type;
    size_t size;
} wf_uploaded_blob;

static void wf_uploaded_blob_free(wf_uploaded_blob *blob) {
    if (!blob) {
        return;
    }

    free(blob->cid);
    free(blob->mime_type);
    blob->cid = NULL;
    blob->mime_type = NULL;
    blob->size = 0;
}

static wf_status wf_parse_uploaded_blob_response(const wf_response *res,
                                                 wf_uploaded_blob *out) {
    if (!res || !out || !res->body || res->body_len == 0) {
        return WF_ERR_INVALID_ARG;
    }

    out->cid = NULL;
    out->mime_type = NULL;
    out->size = 0;

    cJSON *root = cJSON_ParseWithLength(res->body, res->body_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_ERR_PARSE;

    cJSON *blob = cJSON_GetObjectItemCaseSensitive(root, "blob");
    cJSON *type = blob ? cJSON_GetObjectItemCaseSensitive(blob, "$type") : NULL;
    cJSON *ref = blob ? cJSON_GetObjectItemCaseSensitive(blob, "ref") : NULL;
    cJSON *link = ref ? cJSON_GetObjectItemCaseSensitive(ref, "$link") : NULL;
    cJSON *mime_type = blob ? cJSON_GetObjectItemCaseSensitive(blob, "mimeType") : NULL;
    cJSON *size = blob ? cJSON_GetObjectItemCaseSensitive(blob, "size") : NULL;

    if (!cJSON_IsObject(blob) || !cJSON_IsString(type) || strcmp(type->valuestring, WF_BLOB_TYPE) != 0 ||
        !cJSON_IsObject(ref) || !cJSON_IsString(link) ||
        !cJSON_IsString(mime_type) || !cJSON_IsNumber(size) ||
        size->valuedouble < 0.0 || size->valuedouble > (double)SIZE_MAX) {
        goto done;
    }

    out->cid = wf_dup_span(link->valuestring, strlen(link->valuestring));
    out->mime_type = wf_dup_span(mime_type->valuestring, strlen(mime_type->valuestring));
    if (!out->cid || !out->mime_type) {
        wf_uploaded_blob_free(out);
        status = WF_ERR_ALLOC;
        goto done;
    }

    out->size = (size_t)size->valuedouble;
    status = WF_OK;

 done:
    cJSON_Delete(root);
    return status;
}

static wf_status wf_build_create_record_body(const char *repo_did,
                                             const char *created_at,
                                             const char *alt_text,
                                             const wf_uploaded_blob *blob,
                                             char **out_json) {
    if (!repo_did || !created_at || !alt_text || !blob || !out_json) {
        return WF_ERR_INVALID_ARG;
    }

    *out_json = NULL;

    if (!wf_syntax_did_is_valid(repo_did) || !wf_syntax_datetime_is_valid(created_at) ||
        !blob->cid || !blob->mime_type || blob->size == 0) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *record = NULL;
    cJSON *embed = NULL;
    cJSON *images = NULL;
    cJSON *image = NULL;
    cJSON *blob_json = NULL;
    cJSON *ref = NULL;

    if (!root) {
        return WF_ERR_ALLOC;
    }

    wf_status status = WF_ERR_ALLOC;

    record = cJSON_CreateObject();
    embed = cJSON_CreateObject();
    images = cJSON_CreateArray();
    image = cJSON_CreateObject();
    blob_json = cJSON_CreateObject();
    ref = cJSON_CreateObject();
    if (!record || !embed || !images || !image || !blob_json || !ref) {
        goto done;
    }

    if (!cJSON_AddStringToObject(root, "repo", repo_did) ||
        !cJSON_AddStringToObject(root, "collection", WF_POST_COLLECTION)) {
        goto done;
    }

    if (!cJSON_AddStringToObject(record, "$type", WF_POST_RECORD_TYPE) ||
        !cJSON_AddStringToObject(record, "text", WF_POST_TEXT) ||
        !cJSON_AddStringToObject(record, "createdAt", created_at)) {
        goto done;
    }

    if (!cJSON_AddStringToObject(ref, "$link", blob->cid) ||
        !cJSON_AddItemToObject(blob_json, "ref", ref)) {
        goto done;
    }
    ref = NULL;

    if (!cJSON_AddStringToObject(blob_json, "$type", WF_BLOB_TYPE) ||
        !cJSON_AddStringToObject(blob_json, "mimeType", blob->mime_type) ||
        !cJSON_AddNumberToObject(blob_json, "size", (double)blob->size)) {
        goto done;
    }

    if (!cJSON_AddStringToObject(image, "alt", alt_text) ||
        !cJSON_AddItemToObject(image, "image", blob_json)) {
        goto done;
    }
    blob_json = NULL;

    if (!cJSON_AddItemToArray(images, image)) {
        goto done;
    }
    image = NULL;

    if (!cJSON_AddStringToObject(embed, "$type", WF_IMAGE_EMBED_TYPE) ||
        !cJSON_AddItemToObject(embed, "images", images)) {
        goto done;
    }
    images = NULL;

    if (!cJSON_AddItemToObject(record, "embed", embed)) {
        goto done;
    }
    embed = NULL;

    if (!cJSON_AddItemToObject(root, "record", record)) {
        goto done;
    }
    record = NULL;

    char *json = cJSON_PrintUnformatted(root);
    if (!json) {
        goto done;
    }

    *out_json = json;
    status = WF_OK;

 done:
    cJSON_Delete(ref);
    cJSON_Delete(blob_json);
    cJSON_Delete(image);
    cJSON_Delete(images);
    cJSON_Delete(embed);
    cJSON_Delete(record);
    cJSON_Delete(root);
    return status;
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
                "usage: %s <service-url> <handle-or-email> <password> <image-path> [alt text...]\n",
                argv[0]);
        return 1;
    }

    const char *service_url = argv[1];
    const char *identifier = argv[2];
    const char *password = argv[3];
    const char *image_path = argv[4];

    char *alt_text = wf_join_args(argc, argv, 5);
    if (!alt_text) {
        fprintf(stderr, "failed to assemble alt text\n");
        return 1;
    }

    const char *content_type = wf_image_mime_type_from_path(image_path);
    if (!content_type) {
        fprintf(stderr,
                "unsupported image type for '%s' (use jpg, jpeg, png, gif, or webp)\n",
                image_path);
        free(alt_text);
        return 1;
    }

    unsigned char *image_data = NULL;
    size_t image_len = 0;
    wf_status status = wf_read_file(image_path, &image_data, &image_len);
    if (status != WF_OK) {
        free(alt_text);
        return 1;
    }

    wf_session *session = wf_session_new(service_url);
    if (!session) {
        fprintf(stderr, "failed to create session\n");
        free(image_data);
        free(alt_text);
        return 1;
    }

    status = wf_session_login(session, identifier, password);
    if (status != WF_OK) {
        fprintf(stderr, "login failed: %d\n", (int)status);
        wf_session_free(session);
        free(image_data);
        free(alt_text);
        return 1;
    }

    if (!session->data.did || !wf_syntax_did_is_valid(session->data.did)) {
        fprintf(stderr, "session did is invalid or missing\n");
        wf_session_free(session);
        free(image_data);
        free(alt_text);
        return 1;
    }

    printf("Logged in as %s (%s)\n",
           session->data.handle ? session->data.handle : identifier,
           session->data.did);
    printf("Uploading %s as %s (%zu bytes)\n", image_path, content_type, image_len);

    wf_response upload_res = {0};
    status = wf_xrpc_upload_blob(session->client,
                                 WF_UPLOAD_BLOB_NSID,
                                 image_data,
                                 image_len,
                                 content_type,
                                 &upload_res);
    free(image_data);
    image_data = NULL;

    if (status != WF_OK) {
        fprintf(stderr, "blob upload failed: %d\n", (int)status);
        if (status == WF_ERR_HTTP) {
            printf("HTTP %ld\n%s\n", upload_res.status,
                   upload_res.body ? upload_res.body : "(empty body)");
        }
        wf_response_free(&upload_res);
        wf_session_free(session);
        free(alt_text);
        return 1;
    }

    wf_uploaded_blob blob = {0};
    status = wf_parse_uploaded_blob_response(&upload_res, &blob);
    if (status != WF_OK) {
        fprintf(stderr, "failed to parse blob upload response: %d\n", (int)status);
        if (upload_res.body) {
            printf("HTTP %ld\n%s\n", upload_res.status, upload_res.body);
        }
        wf_response_free(&upload_res);
        wf_uploaded_blob_free(&blob);
        wf_session_free(session);
        free(alt_text);
        return 1;
    }
    wf_response_free(&upload_res);

    printf("Uploaded blob: cid=%s, mimeType=%s, size=%zu\n",
           blob.cid, blob.mime_type, blob.size);

    char created_at[32];
    if (!wf_make_rfc3339_timestamp(created_at, sizeof(created_at))) {
        fprintf(stderr, "failed to create timestamp\n");
        wf_uploaded_blob_free(&blob);
        wf_session_free(session);
        free(alt_text);
        return 1;
    }

    if (!wf_syntax_datetime_is_valid(created_at)) {
        fprintf(stderr, "generated timestamp is invalid: %s\n", created_at);
        wf_uploaded_blob_free(&blob);
        wf_session_free(session);
        free(alt_text);
        return 1;
    }

    char *body_json = NULL;
    status = wf_build_create_record_body(session->data.did,
                                         created_at,
                                         alt_text,
                                         &blob,
                                         &body_json);
    if (status != WF_OK) {
        fprintf(stderr, "failed to build createRecord body: %d\n", (int)status);
        wf_uploaded_blob_free(&blob);
        wf_session_free(session);
        free(alt_text);
        return 1;
    }

    wf_response create_res = {0};
    status = wf_xrpc_procedure(session->client, WF_CREATE_RECORD_NSID, body_json, &create_res);
    free(body_json);

    if (status != WF_OK && status != WF_ERR_HTTP) {
        fprintf(stderr, "createRecord failed: %d\n", (int)status);
        wf_response_free(&create_res);
        wf_uploaded_blob_free(&blob);
        wf_session_free(session);
        free(alt_text);
        return 1;
    }

    wf_print_create_record_response(&create_res);

    wf_response_free(&create_res);
    wf_uploaded_blob_free(&blob);
    wf_session_free(session);
    free(alt_text);
    return status == WF_OK ? 0 : 1;
}
