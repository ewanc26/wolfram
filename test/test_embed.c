#include "test.h"
#include "wolfram/blob.h"
#include "wolfram/embed.h"
#include "wolfram/util.h"
#include <cJSON.h>
#include <string.h>

int main(void) {
    wf_uploaded_blob blob = {0};
    const char *dummy_cid = "bafkreihdwdcefgh4dqkjv67uzcmw7ojee6xedzdetojuzjev8e";
    const char *dummy_mime = "image/png";
    blob.cid = wf_dup_span(dummy_cid, strlen(dummy_cid));
    blob.mime_type = wf_dup_span(dummy_mime, strlen(dummy_mime));
    blob.size = 12345;

    // Image embed
    cJSON *img_embed = wf_embed_images_new();
    WF_CHECK(img_embed != NULL);
    wf_status s = wf_embed_images_add_image(img_embed, &blob, "Alt text");
    WF_CHECK(s == WF_OK);

    cJSON *type = cJSON_GetObjectItem(img_embed, "$type");
    WF_CHECK(type && cJSON_IsString(type) && strcmp(type->valuestring, "app.bsky.embed.images") == 0);

    cJSON *images = cJSON_GetObjectItem(img_embed, "images");
    WF_CHECK(images && cJSON_IsArray(images) && cJSON_GetArraySize(images) == 1);

    cJSON_Delete(img_embed);
    wf_uploaded_blob_free(&blob);

    WF_TEST_SUMMARY();
}
