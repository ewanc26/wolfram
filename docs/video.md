# Video upload (`blob.h`, `agent.h`, `embed_typed.h`)

wolfram uploads video with a dedicated endpoint (`app.bsky.video.uploadVideo`)
and exposes a polling model for the server-side transcoding job. The flow is:

1. **Upload** the raw video bytes via `wf_agent_upload_video` (returns a
   `wf_uploaded_blob` with a CID).
2. **Poll** the job status with `wf_agent_get_video_job_status` until the job
   reports success and yields a `blob` reference.
3. **Embed** the resulting blob in a post using `wf_embed_video_*`.

These complement the generic blob upload in `agent.h`
(`wf_agent_upload_blob` / `wf_agent_upload_blob_ex`) and the typed embed helpers
in `embed_typed.h`.

## Upload

Unlike the generic `wf_agent_upload_blob`, `wf_agent_upload_video` targets the
video endpoint. It returns a `wf_uploaded_blob` (owned `cid`, `mime_type`,
`size`):

```c
#include "wolfram/blob.h"
#include <stdio.h>

unsigned char *mp4 = /* ... */;   /* raw video bytes */
size_t mp4_len = /* ... */;

wf_uploaded_blob blob = {0};
wf_status s = wf_agent_upload_video(agent, mp4, mp4_len, "video/mp4", &blob);
if (s != WF_OK) {
    fprintf(stderr, "upload failed: %d\n", (int)s);
    return 1;
}
printf("uploaded cid=%s (%zu bytes)\n", blob.cid, blob.size);
```

After upload you receive a *job*, not a final embeddable blob. The upload
response (raw JSON via `wf_response`, or the `wf_uploaded_blob` convenience) is
what you poll.

## Poll job status

`wf_agent_get_video_job_status` returns the raw job-status JSON; free it with
`wf_response_free`. Poll until the job succeeds (the JSON contains a `blob`
reference you use for the embed):

```c
const char *job_id = /* from the upload response */;
wf_response job = {0};
wf_status s = wf_agent_get_video_job_status(agent, job_id, &job);
/* job.body is the raw job-status JSON, e.g. {"jobStatus":{...,"blob":{...}}} */
wf_response_free(&job);
```

You can also query the account's upload limits up front:

```c
wf_response limits = {0};
wf_status s = wf_agent_get_video_upload_limits(agent, &limits);
/* limits.body is the raw limits JSON */
wf_response_free(&limits);
```

## Embed in a post

Build a video embed from the uploaded blob with `wf_embed_video_*`, then attach
it when posting. `embed_typed.h` keeps an owned copy of the blob's `cid`,
`mime_type`, `size`, and an optional `alt` string; `wf_embed_video_build` returns
a `cJSON *` embed object you pass to the post call. The `cJSON *` is owned by you
— `cJSON_Delete` it after use; `wf_embed_video_free` only frees the typed
struct's copies.

```c
#include "wolfram/embed_typed.h"

wf_embed_video_t vid;
wf_status s = wf_embed_video_init(&vid, &blob, "a short clip");  /* copies cid/mime/size */
if (s != WF_OK) { /* handle error */ }

cJSON *embed = wf_embed_video_build(&vid);   /* owned by caller */
wf_embed_video_free(&vid);

/* Post with the embed (pass the built cJSON*; free it yourself). */
wf_agent_post_result out = {0};
wf_status s2 = wf_agent_post_with_embed(agent, "check out this video", embed, &out);
if (s2 == WF_OK) printf("uri=%s\n", out.uri);
wf_agent_post_result_free(&out);

cJSON_Delete(embed);       /* you own the built embed */
wf_uploaded_blob_free(&blob);  /* free the upload result */
```

## Notes

- `wf_agent_upload_video` and the generic `wf_agent_upload_blob_ex` both fill a
  `wf_uploaded_blob`; always release it with `wf_uploaded_blob_free`.
- The job-status and upload-limits calls return raw JSON in a `wf_response`; free
  them with `wf_response_free`. Parse the `blob` reference out of the job-status
  body to obtain the final CID for embedding.
- There is no typed job-status struct; the embed step only needs the final
  `wf_uploaded_blob` (CID + MIME type + size), which `wf_agent_upload_video`
  already provides.

See [agent.md](agent.md) for `wf_agent_post` / `wf_agent_post_with_embed`. The
remaining embed helpers (images, record, external) are declared in
`include/wolfram/embed_typed.h`.
