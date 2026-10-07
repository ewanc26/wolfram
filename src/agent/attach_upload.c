#include "wolfram/attach.h"

#include "wolfram/blob.h"
#include "wolfram/embed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

wf_status wf_agent_upload_image_file(wf_agent *agent, const char *path,
                                     const char *alt, cJSON **embed) {
    if (embed) *embed = NULL;
    const char *mime = wf_attach_mime(path);
    if (!agent || !path || !embed || !mime) return WF_ERR_INVALID_ARG;

    FILE *f = fopen(path, "rb");
    if (!f) return WF_ERR_NOT_FOUND;
    unsigned char *buf = malloc((size_t)WF_ATTACH_MAX_BYTES + 1);
    if (!buf) {
        fclose(f);
        return WF_ERR_ALLOC;
    }
    size_t n = fread(buf, 1, (size_t)WF_ATTACH_MAX_BYTES + 1, f);
    fclose(f);
    if (n == 0 || n > (size_t)WF_ATTACH_MAX_BYTES) {
        free(buf);
        return WF_ERR_INVALID_ARG;
    }

    wf_uploaded_blob blob;
    memset(&blob, 0, sizeof blob);
    wf_status st = wf_agent_upload_blob_ex(agent, buf, n, mime, &blob);
    free(buf);
    if (st != WF_OK) return st;

    cJSON *out = wf_embed_images_new();
    if (!out) {
        wf_uploaded_blob_free(&blob);
        return WF_ERR_ALLOC;
    }
    st = wf_embed_images_add_image(out, &blob, alt ? alt : "");
    wf_uploaded_blob_free(&blob);
    if (st != WF_OK) {
        cJSON_Delete(out);
        return st;
    }
    *embed = out;
    return WF_OK;
}
