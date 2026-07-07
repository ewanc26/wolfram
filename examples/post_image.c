#include <stdio.h>
#include <stdlib.h>
#include "wolfram/agent.h"
#include <cJSON.h>

static const char *wf_image_mime_type_from_path(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot || !dot[1]) return NULL;
    const char *ext = dot + 1;
    if (wf_ascii_iequals(ext, "jpg") || wf_ascii_iequals(ext, "jpeg")) return "image/jpeg";
    if (wf_ascii_iequals(ext, "png")) return "image/png";
    if (wf_ascii_iequals(ext, "gif")) return "image/gif";
    if (wf_ascii_iequals(ext, "webp")) return "image/webp";
    return NULL;
}

/* Helper to read a file into memory */
static wf_status wf_read_file(const char *path, unsigned char **out_data, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return WF_ERR_INVALID_ARG;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return WF_ERR_INVALID_ARG; }
    long len = ftell(f);
    if (len < 0) { fclose(f); return WF_ERR_INVALID_ARG; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return WF_ERR_INVALID_ARG; }
    unsigned char *data = malloc((size_t)len);
    if (!data) { fclose(f); return WF_ERR_ALLOC; }
    size_t read = fread(data, 1, (size_t)len, f);
    fclose(f);
    if (read != (size_t)len) { free(data); return WF_ERR_IO; }
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
    if (!blob) return;
    free(blob->cid);
    free(blob->mime_type);
    blob->cid = NULL;
    blob->mime_type = NULL;
    blob->size = 0;
}

static wf_status wf_parse_uploaded_blob_response(const wf_response *res, wf_uploaded_blob *out) {
    if (!res || !out || !res->body || res->body_len == 0) return WF_ERR_INVALID_ARG;
    out->cid = NULL; out->mime_type = NULL; out->size = 0;
    cJSON *root = cJSON_ParseWithLength(res->body, res->body_len);
    if (!root) return WF_ERR_PARSE;
    cJSON *blob = cJSON_GetObjectItemCaseSensitive(root, "blob");
    cJSON *type = blob ? cJSON_GetObjectItemCaseSensitive(blob, "$type") : NULL;
    cJSON *ref = blob ? cJSON_GetObjectItemCaseSensitive(blob, "ref") : NULL;
    cJSON *link = ref ? cJSON_GetObjectItemCaseSensitive(ref, "$link") : NULL;
    cJSON *mime_type = blob ? cJSON_GetObjectItemCaseSensitive(blob, "mimeType") : NULL;
    cJSON *size = blob ? cJSON_GetObjectItemCaseSensitive(blob, "size") : NULL;
    if (!cJSON_IsObject(blob) || !cJSON_IsString(type) || strcmp(type->valuestring, "blob") != 0 ||
        !cJSON_IsObject(ref) || !cJSON_IsString(link) ||
        !cJSON_IsString(mime_type) || !cJSON_IsNumber(size) || size->valuedouble < 0.0) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    out->cid = wf_dup_span(link->valuestring, strlen(link->valuestring));
    out->mime_type = wf_dup_span(mime_type->valuestring, strlen(mime_type->valuestring));
    out->size = (size_t)size->valuedouble;
    cJSON_Delete(root);
    if (!out->cid || !out->mime_type) { wf_uploaded_blob_free(out); return WF_ERR_ALLOC; }
    return WF_OK;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <service-url> <handle-or-email> <password> <image-path> [alt text...]\n", argv[0]);
        return 1;
    }
    const char *service_url = argv[1];
    const char *identifier = argv[2];
    const char *password = argv[3];
    const char *image_path = argv[4];
    char *alt_text = wf_join_args(argc, argv, 5);
    if (!alt_text) { fprintf(stderr, "failed to assemble alt text\n"); return 1; }
    const char *content_type = wf_image_mime_type_from_path(image_path);
    if (!content_type) {
        fprintf(stderr, "unsupported image type for '%s'\n", image_path);
        free(alt_text);
        return 1;
    }
    unsigned char *image_data = NULL; size_t image_len = 0;
    wf_status status = wf_read_file(image_path, &image_data, &image_len);
    if (status != WF_OK) { free(alt_text); return 1; }
    wf_agent *agent = wf_agent_new(service_url);
    if (!agent) { fprintf(stderr, "failed to create agent\n"); free(image_data); free(alt_text); return 1; }
    status = wf_agent_login(agent, identifier, password);
    if (status != WF_OK) { fprintf(stderr, "login failed: %d\n", (int)status); wf_agent_free(agent); free(image_data); free(alt_text); return 1; }
    printf("Logged in as %s (%s)\n", agent->session->data.handle ? agent->session->data.handle : identifier, agent->session->data.did);
    printf("Uploading %s as %s (%zu bytes)\n", image_path, content_type, image_len);
    wf_response upload_res = {0};
    status = wf_agent_upload_blob(agent, image_data, image_len, content_type, &upload_res);
    free(image_data);
    if (status != WF_OK) {
        fprintf(stderr, "blob upload failed: %d\n", (int)status);
        wf_response_free(&upload_res);
        wf_agent_free(agent);
        free(alt_text);
        return 1;
    }
    wf_uploaded_blob blob = {0};
    status = wf_parse_uploaded_blob_response(&upload_res, &blob);
    wf_response_free(&upload_res);
    if (status != WF_OK) {
        fprintf(stderr, "failed to parse blob response: %d\n", (int)status);
        wf_agent_free(agent);
        free(alt_text);
        return 1;
    }
    printf("Uploaded blob: cid=%s mime=%s size=%zu\n", blob.cid, blob.mime_type, blob.size);
    /* Build embed JSON */
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
    cJSON_AddStringToObject(image, "alt", alt_text);
    cJSON_AddItemToArray(images, image);
    cJSON_AddStringToObject(embed, "$type", "app.bsky.embed.images");
    cJSON_AddItemToObject(embed, "images", images);
    char *embed_str = cJSON_PrintUnformatted(embed);
    cJSON_Delete(embed);
    if (!embed_str) {
        fprintf(stderr, "failed to serialize embed JSON\n");
        wf_uploaded_blob_free(&blob);
        wf_agent_free(agent);
        free(alt_text);
        return 1;
    }
    wf_agent_post_result post_res = {0};
    const char *post_text = "Post with image embed";
    status = wf_agent_post_with_embed(agent, post_text, embed_str, &post_res);
    free(embed_str);
    if (status != WF_OK) {
        fprintf(stderr, "post with embed failed: %d\n", (int)status);
        wf_uploaded_blob_free(&blob);
        wf_agent_free(agent);
        free(alt_text);
        return 1;
    }
    printf("Created post: uri=%s cid=%s\n", post_res.uri, post_res.cid);
    wf_agent_post_result_free(&post_res);
    wf_uploaded_blob_free(&blob);
    wf_agent_free(agent);
    free(alt_text);
    return 0;
}
