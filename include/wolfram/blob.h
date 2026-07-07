#ifndef WOLFRAM_BLOB_H
#define WOLFRAM_BLOB_H

#include "wolfram/agent.h"
#include "wolfram/util.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wf_uploaded_blob {
    char *cid;
    char *mime_type;
    size_t size;
} wf_uploaded_blob;

/* Upload a blob and parse the response into a wf_uploaded_blob structure. */
wf_status wf_agent_upload_blob_ex(wf_agent *agent, const void *data, size_t data_len,
                                 const char *content_type, wf_uploaded_blob *out);

/* Upload a video blob using the dedicated video endpoint. */
wf_status wf_agent_upload_video(wf_agent *agent, const void *data, size_t data_len,
                               const char *content_type, wf_uploaded_blob *out);

void wf_uploaded_blob_free(wf_uploaded_blob *blob);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_BLOB_H */
