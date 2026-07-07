/*
 * feed_generator.c — create and read an app.bsky.feed.generator record.
 *
 * Demonstrates:
 *   1. High-level agent login with wf_agent
 *   2. Building an app.bsky.feed.generator record value with cJSON
 *      (displayName, description, did, optional avatar blob, createdAt)
 *   3. Record creation via wf_agent_create_record
 *   4. Reading the record back with wf_agent_get_record
 *   5. Listing the actor's feeds with wf_agent_get_actor_feeds_typed
 *
 * Usage (all network gated behind argv — no args prints usage, exit 0):
 *   feed_generator <service-url> <handle> <password> <generator-did> \
 *                  <display-name> <description> [avatar-image-path]
 *
 * The <generator-did> is the DID of the feed generator service (the account
 * that will serve the feed). If an avatar-image-path is supplied, the image
 * is uploaded as a blob and attached as the generator's avatar.
 */

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wolfram/agent.h"
#include "wolfram/blob.h"

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

/* Extract the record key (rkey) from an at:// URI of the form
 * at://<did>/<collection>/<rkey>. Caller must free the result. */
static char *wf_rkey_from_aturi(const char *uri) {
    if (!uri) {
        return NULL;
    }

    size_t len = strlen(uri);
    const char *last_slash = NULL;
    for (size_t i = 0; i < len; ++i) {
        if (uri[i] == '/') {
            last_slash = uri + i;
        }
    }

    if (!last_slash || last_slash == uri + len - 1) {
        return NULL;
    }

    const char *rkey = last_slash + 1;
    size_t rkey_len = strlen(rkey);
    char *out = malloc(rkey_len + 1);
    if (!out) {
        return NULL;
    }

    memcpy(out, rkey, rkey_len);
    out[rkey_len] = '\0';
    return out;
}

static char *wf_read_file(const char *path, unsigned char **out_data,
                           size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    unsigned char *data = malloc((size_t)len);
    if (!data) {
        fclose(f);
        return NULL;
    }
    size_t read = fread(data, 1, (size_t)len, f);
    fclose(f);
    if (read != (size_t)len) {
        free(data);
        return NULL;
    }
    *out_data = data;
    *out_len = (size_t)len;
    return (char *)data;
}

static const char *wf_image_mime_type_from_path(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot || !dot[1]) {
        return NULL;
    }
    const char *ext = dot + 1;
    if (!strcasecmp(ext, "jpg") || !strcasecmp(ext, "jpeg")) {
        return "image/jpeg";
    }
    if (!strcasecmp(ext, "png")) {
        return "image/png";
    }
    if (!strcasecmp(ext, "gif")) {
        return "image/gif";
    }
    if (!strcasecmp(ext, "webp")) {
        return "image/webp";
    }
    return NULL;
}

static void print_usage(const char *prog) {
    printf("usage: %s <service-url> <handle> <password> <generator-did> "
           "<display-name> <description> [avatar-image-path]\n",
           prog);
    printf("  (no arguments: print usage and exit 0 — offline safe)\n");
}

int main(int argc, char **argv) {
    if (argc < 7) {
        print_usage(argv[0]);
        return 0;
    }

    const char *service_url = argv[1];
    const char *identifier  = argv[2];
    const char *password    = argv[3];
    const char *gen_did     = argv[4];
    const char *display     = argv[5];
    const char *desc        = argv[6];
    const char *avatar_path = (argc >= 8) ? argv[7] : NULL;

    wf_agent *agent = wf_agent_new(service_url);
    if (!agent) {
        fprintf(stderr, "failed to create agent\n");
        return 1;
    }

    wf_status status = wf_agent_login(agent, identifier, password);
    if (status != WF_OK) {
        fprintf(stderr, "login failed: %d\n", (int)status);
        wf_agent_free(agent);
        return 1;
    }

    const char *handle = wf_agent_get_handle(agent);
    const char *did    = wf_agent_get_did(agent);
    printf("Logged in as %s (%s)\n", handle ? handle : identifier, did);

    /* Build the app.bsky.feed.generator record value. */
    cJSON *record = cJSON_CreateObject();
    if (!record) {
        fprintf(stderr, "failed to allocate record JSON\n");
        wf_agent_free(agent);
        return 1;
    }

    cJSON_AddStringToObject(record, "$type", "app.bsky.feed.generator");
    cJSON_AddStringToObject(record, "did", gen_did);
    cJSON_AddStringToObject(record, "displayName", display);
    cJSON_AddStringToObject(record, "description", desc);

    char created_at[32];
    if (!wf_make_rfc3339_timestamp(created_at, sizeof(created_at))) {
        fprintf(stderr, "failed to create timestamp\n");
        cJSON_Delete(record);
        wf_agent_free(agent);
        return 1;
    }
    cJSON_AddStringToObject(record, "createdAt", created_at);

    /* Optional avatar blob upload. */
    if (avatar_path) {
        const char *content_type = wf_image_mime_type_from_path(avatar_path);
        if (!content_type) {
            fprintf(stderr, "unsupported avatar type for '%s'\n", avatar_path);
            cJSON_Delete(record);
            wf_agent_free(agent);
            return 1;
        }

        unsigned char *avatar_data = NULL;
        size_t avatar_len = 0;
        if (!wf_read_file(avatar_path, &avatar_data, &avatar_len)) {
            fprintf(stderr, "failed to read avatar '%s'\n", avatar_path);
            cJSON_Delete(record);
            wf_agent_free(agent);
            return 1;
        }

        wf_uploaded_blob blob = {0};
        status = wf_agent_upload_blob_ex(agent, avatar_data, avatar_len,
                                         content_type, &blob);
        free(avatar_data);
        if (status != WF_OK) {
            fprintf(stderr, "avatar blob upload failed: %d\n", (int)status);
            wf_uploaded_blob_free(&blob);
            cJSON_Delete(record);
            wf_agent_free(agent);
            return 1;
        }

        cJSON *avatar = cJSON_CreateObject();
        cJSON *ref = cJSON_CreateObject();
        cJSON_AddStringToObject(ref, "$link", blob.cid);
        cJSON_AddItemToObject(avatar, "ref", ref);
        cJSON_AddStringToObject(avatar, "$type", "blob");
        cJSON_AddStringToObject(avatar, "mimeType", blob.mime_type);
        cJSON_AddNumberToObject(avatar, "size", (double)blob.size);
        cJSON_AddItemToObject(record, "avatar", avatar);
        wf_uploaded_blob_free(&blob);
        printf("Uploaded avatar blob\n");
    }

    char *record_json = cJSON_PrintUnformatted(record);
    cJSON_Delete(record);
    if (!record_json) {
        fprintf(stderr, "failed to serialize record JSON\n");
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result res = {0};
    status = wf_agent_create_record(agent, "app.bsky.feed.generator",
                                    record_json, &res);
    free(record_json);

    if (status != WF_OK) {
        fprintf(stderr, "createRecord failed: %d\n", (int)status);
        wf_agent_free(agent);
        return 1;
    }

    printf("Created app.bsky.feed.generator record:\n");
    printf("  uri: %s\n", res.uri);
    printf("  cid: %s\n", res.cid);

    /* Read it back with getRecord. */
    char *rkey = wf_rkey_from_aturi(res.uri);
    wf_agent_post_result_free(&res);
    if (!rkey) {
        fprintf(stderr, "failed to parse rkey from created uri\n");
        wf_agent_free(agent);
        return 1;
    }

    wf_response get_res = {0};
    status = wf_agent_get_record(agent, "app.bsky.feed.generator", rkey,
                                 &get_res);
    if (status != WF_OK) {
        fprintf(stderr, "getRecord failed: %d\n", (int)status);
        free(rkey);
        wf_response_free(&get_res);
        wf_agent_free(agent);
        return 1;
    }

    printf("Read back record:\n");
    if (get_res.body && get_res.body_len) {
        cJSON *root = cJSON_ParseWithLength(get_res.body, get_res.body_len);
        if (root) {
            cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "value");
            cJSON *v = value ? value : root;
            cJSON *dn = cJSON_GetObjectItemCaseSensitive(v, "displayName");
            cJSON *ds = cJSON_GetObjectItemCaseSensitive(v, "description");
            cJSON *gd = cJSON_GetObjectItemCaseSensitive(v, "did");
            if (cJSON_IsString(dn)) {
                printf("  displayName: %s\n", dn->valuestring);
            }
            if (cJSON_IsString(ds)) {
                printf("  description: %s\n", ds->valuestring);
            }
            if (cJSON_IsString(gd)) {
                printf("  did: %s\n", gd->valuestring);
            }
            cJSON_Delete(root);
        } else {
            printf("  (could not parse response body)\n");
        }
    }
    wf_response_free(&get_res);
    free(rkey);

    /* List the actor's feeds. */
    wf_agent_generator_view_list feeds = {0};
    status = wf_agent_get_actor_feeds_typed(agent, did, 50, NULL, &feeds);
    if (status != WF_OK) {
        fprintf(stderr, "getActorFeeds failed: %d (continuing)\n", (int)status);
    } else {
        printf("Actor feeds (%zu):\n", feeds.generator_count);
        for (size_t i = 0; i < feeds.generator_count; ++i) {
            printf("  [%zu] %s — %s\n", i,
                   feeds.generators[i].display_name
                       ? feeds.generators[i].display_name : "?",
                   feeds.generators[i].uri ? feeds.generators[i].uri : "?");
        }
        wf_agent_generator_view_list_free(&feeds);
    }

    wf_agent_free(agent);
    return 0;
}
