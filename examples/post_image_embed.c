/*
 * post_image_embed.c — create a post whose text carries a rich-text facet and
 * an app.bsky.embed.images image embed.
 *
 * Demonstrates:
 *   1. High-level agent login with wf_agent
 *   2. Uploading an image blob via wf_agent_upload_blob_ex
 *   3. Building an app.bsky.embed.images embed value with cJSON
 *   4. Building a link facet for the post text
 *   5. Creating the post via wf_agent_post_with_embed
 *
 * Usage (all network gated behind argv — no args prints usage, exit 0):
 *   post_image_embed <service-url> <handle> <password> <image-path> [text...]
 */

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "wolfram/agent.h"
#include "wolfram/blob.h"
#include "wolfram/util.h"

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

static wf_status wf_read_file(const char *path, unsigned char **out_data,
                              size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return WF_ERR_INVALID_ARG;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return WF_ERR_INVALID_ARG;
    }
    long len = ftell(f);
    if (len < 0) {
        fclose(f);
        return WF_ERR_INVALID_ARG;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return WF_ERR_INVALID_ARG;
    }
    unsigned char *data = malloc((size_t)len);
    if (!data) {
        fclose(f);
        return WF_ERR_ALLOC;
    }
    size_t read = fread(data, 1, (size_t)len, f);
    fclose(f);
    if (read != (size_t)len) {
        free(data);
        return WF_ERR_ALLOC;
    }
    *out_data = data;
    *out_len = (size_t)len;
    return WF_OK;
}

static void print_usage(const char *prog) {
    printf("usage: %s <service-url> <handle> <password> <image-path> [text...]\n",
           prog);
    printf("  (no arguments: print usage and exit 0 — offline safe)\n");
}

int main(int argc, char **argv) {
    if (argc < 5) {
        print_usage(argv[0]);
        return 0;
    }

    const char *service_url = argv[1];
    const char *identifier  = argv[2];
    const char *password    = argv[3];
    const char *image_path  = argv[4];
    char *post_text = wf_join_args(argc, argv, 5);
    if (!post_text) {
        fprintf(stderr, "failed to assemble post text\n");
        return 1;
    }

    const char *content_type = wf_image_mime_type_from_path(image_path);
    if (!content_type) {
        fprintf(stderr, "unsupported image type for '%s'\n", image_path);
        free(post_text);
        return 1;
    }

    unsigned char *image_data = NULL;
    size_t image_len = 0;
    wf_status status = wf_read_file(image_path, &image_data, &image_len);
    if (status != WF_OK) {
        fprintf(stderr, "failed to read image '%s'\n", image_path);
        free(post_text);
        return 1;
    }

    wf_agent *agent = wf_agent_new(service_url);
    if (!agent) {
        fprintf(stderr, "failed to create agent\n");
        free(image_data);
        free(post_text);
        return 1;
    }

    status = wf_agent_login(agent, identifier, password);
    if (status != WF_OK) {
        fprintf(stderr, "login failed: %d\n", (int)status);
        wf_agent_free(agent);
        free(image_data);
        free(post_text);
        return 1;
    }

    const char *handle = wf_agent_get_handle(agent);
    const char *did    = wf_agent_get_did(agent);
    printf("Logged in as %s (%s)\n", handle ? handle : identifier, did);

    printf("Uploading %s as %s (%zu bytes)\n", image_path, content_type,
           image_len);

    wf_uploaded_blob blob = {0};
    status = wf_agent_upload_blob_ex(agent, image_data, image_len,
                                     content_type, &blob);
    free(image_data);
    if (status != WF_OK) {
        fprintf(stderr, "blob upload failed: %d\n", (int)status);
        wf_uploaded_blob_free(&blob);
        wf_agent_free(agent);
        free(post_text);
        return 1;
    }
    printf("Uploaded blob: cid=%s mime=%s size=%zu\n", blob.cid,
           blob.mime_type, blob.size);

    /* Build an app.bsky.embed.images embed. */
    cJSON *embed = cJSON_CreateObject();
    cJSON *images = cJSON_CreateArray();
    cJSON *image = cJSON_CreateObject();

    cJSON *blob_json = cJSON_CreateObject();
    cJSON *ref = cJSON_CreateObject();
    cJSON_AddStringToObject(ref, "$link", blob.cid);
    cJSON_AddItemToObject(blob_json, "ref", ref);
    cJSON_AddStringToObject(blob_json, "$type", "blob");
    cJSON_AddStringToObject(blob_json, "mimeType", blob.mime_type);
    cJSON_AddNumberToObject(blob_json, "size", (double)blob.size);

    cJSON_AddItemToObject(image, "image", blob_json);
    cJSON_AddStringToObject(image, "alt", "uploaded image");
    cJSON_AddItemToArray(images, image);
    cJSON_AddStringToObject(embed, "$type", "app.bsky.embed.images");
    cJSON_AddItemToObject(embed, "images", images);

    char *embed_str = cJSON_PrintUnformatted(embed);
    cJSON_Delete(embed);
    wf_uploaded_blob_free(&blob);
    if (!embed_str) {
        fprintf(stderr, "failed to serialize embed JSON\n");
        wf_agent_free(agent);
        free(post_text);
        return 1;
    }

    /* Build a link facet over the first URL-looking span in the text. We build
     * the full post record ourselves (text + createdAt + facets + embed) so the
     * embed and the facet can coexist, then create it via the generic
     * createRecord helper for the app.bsky.feed.post collection. */
    cJSON *facets = NULL;
    const char *url = strstr(post_text, "http://");
    if (!url) {
        url = strstr(post_text, "https://");
    }
    if (url) {
        size_t byte_start = (size_t)(url - post_text);
        size_t byte_end = byte_start + strlen(url);
        cJSON *facet = cJSON_CreateObject();
        cJSON *index = cJSON_CreateObject();
        cJSON_AddNumberToObject(index, "byteStart", (double)byte_start);
        cJSON_AddNumberToObject(index, "byteEnd", (double)byte_end);
        cJSON_AddItemToObject(facet, "index", index);

        cJSON *features = cJSON_CreateArray();
        cJSON *feature = cJSON_CreateObject();
        cJSON_AddStringToObject(feature, "$type",
                                "app.bsky.richtext.facet#link");
        cJSON_AddStringToObject(feature, "uri", url);
        cJSON_AddItemToArray(features, feature);
        cJSON_AddItemToObject(facet, "features", features);

        facets = cJSON_CreateArray();
        cJSON_AddItemToArray(facets, facet);
    }

    char created_at[32];
    if (!wf_make_rfc3339_timestamp(created_at, sizeof(created_at))) {
        fprintf(stderr, "failed to create timestamp\n");
        if (facets) {
            cJSON_Delete(facets);
        }
        free(embed_str);
        free(post_text);
        wf_agent_free(agent);
        return 1;
    }

    cJSON *record = cJSON_CreateObject();
    cJSON_AddStringToObject(record, "$type", "app.bsky.feed.post");
    cJSON_AddStringToObject(record, "text", post_text);
    cJSON_AddStringToObject(record, "createdAt", created_at);
    if (facets) {
        cJSON_AddItemToObject(record, "facets", facets);
    }
    cJSON_AddItemToObject(record, "embed", cJSON_Parse(embed_str));

    char *record_json = cJSON_PrintUnformatted(record);
    cJSON_Delete(record);
    free(embed_str);
    free(post_text);

    if (!record_json) {
        fprintf(stderr, "failed to serialize post JSON\n");
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result post_res = {0};
    status = wf_agent_create_record(agent, "app.bsky.feed.post", record_json,
                                    &post_res);
    free(record_json);

    if (status != WF_OK) {
        fprintf(stderr, "createRecord (post) failed: %d\n", (int)status);
        wf_agent_post_result_free(&post_res);
        wf_agent_free(agent);
        return 1;
    }

    printf("Created post: uri=%s cid=%s\n", post_res.uri, post_res.cid);
    wf_agent_post_result_free(&post_res);
    wf_agent_free(agent);
    return 0;
}
