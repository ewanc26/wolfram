#include "test.h"
#include "wolfram/image.h"
#include "wolfram/blob.h"
#include "wolfram/embed.h"
#include "wolfram/util.h"
#include <cJSON.h>
#include <string.h>

/* A well-known valid 1x1 PNG (67 bytes). */
static const unsigned char kPng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
    0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x64, 0xf8, 0xff, 0xbf,
    0x1e, 0x00, 0x05, 0x84, 0x02, 0x7f, 0xc2, 0x5b, 0x1e, 0x2a, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
};

int main(void) {
    /* --- wf_image_dimensions --- */
    int w = 0, h = 0;
    wf_status s = wf_image_dimensions(kPng, sizeof(kPng), &w, &h);
    WF_CHECK(s == WF_OK);
    WF_CHECK(w == 1 && h == 1);

    w = h = -1;
    s = wf_image_dimensions(kPng, 0, &w, &h);
    WF_CHECK(s == WF_ERR_PARSE || s == WF_ERR_INVALID_ARG);
    if (s == WF_ERR_INVALID_ARG) {
        /* zero-length buffer is rejected as invalid arg */
    } else {
        WF_CHECK(w == -1 && h == -1);
    }

    /* --- embed with aspectRatio --- */
    wf_uploaded_blob blob = {0};
    const char *dummy_cid = "bafkreihdwdcefgh4dqkjv67uzcmw7ojee6xedzdetojuzjev8e";
    const char *dummy_mime = "image/png";
    blob.cid = wf_dup_span(dummy_cid, strlen(dummy_cid));
    blob.mime_type = wf_dup_span(dummy_mime, strlen(dummy_mime));
    blob.size = (size_t)sizeof(kPng);

    wf_embed_images_t imgs;
    s = wf_embed_images_init(&imgs);
    WF_CHECK(s == WF_OK);
    s = wf_embed_images_add_with_aspect(&imgs, &blob, "alt", 4, 3);
    WF_CHECK(s == WF_OK);

    cJSON *embed = wf_embed_images_build(&imgs);
    WF_CHECK(embed != NULL);

    if (embed) {
        cJSON *images = cJSON_GetObjectItem(embed, "images");
        WF_CHECK(images && cJSON_IsArray(images) && cJSON_GetArraySize(images) == 1);
        cJSON *img = cJSON_GetArrayItem(images, 0);
        WF_CHECK(img != NULL);
        cJSON *ar = img ? cJSON_GetObjectItem(img, "aspectRatio") : NULL;
        WF_CHECK(ar != NULL);
        if (ar) {
            cJSON *aw = cJSON_GetObjectItem(ar, "width");
            cJSON *ah = cJSON_GetObjectItem(ar, "height");
            WF_CHECK(aw && cJSON_IsNumber(aw) && aw->valueint == 4);
            WF_CHECK(ah && cJSON_IsNumber(ah) && ah->valueint == 3);
        }
        cJSON_Delete(embed);
    }

    wf_embed_images_free(&imgs);
    wf_uploaded_blob_free(&blob);

    WF_TEST_SUMMARY();
}
