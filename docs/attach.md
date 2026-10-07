# Attaching an image from a file

A client with an SD card or a disk needs the same four things to let someone attach a picture to a post, and Cobalt and Indigo each wrote them: which files in a folder are postable, the MIME type to upload one as, the size the network will take, and the upload that ends in an image embed. [`wolfram/attach.h`](../include/wolfram/attach.h) does them once.

```c
char names[32][64];
int too_large = 0;
int n = wf_attach_scan_images("sdmc:/images", &names[0][0], sizeof names[0], 32,
                              &too_large);
/* show names[0..n-1]; if n == 0 and too_large > 0, say the files are too big */

cJSON *embed = NULL;
if (wf_agent_upload_image_file(agent, path, alt, &embed) == WF_OK) {
    /* pass `embed` to wf_agent_reply_refs_with_embed, wf_agent_post_with_embed
       or wf_agent_quote_with_media; delete it yourself if you do not */
}
```

- **Types.** `.jpg`, `.jpeg` and `.png`, by extension, in any case (`wf_attach_mime`). The file is not opened to check, so a text file renamed to `.png` is uploaded and the server decides.
- **Size.** `WF_ATTACH_MAX_BYTES` is 950,000. Bluesky's blob limit is 1,000,000; the headroom is for clients that count differently. A file of exactly that size is allowed.
- **Scan.** `wf_attach_scan_images` lists regular, non-empty files that match, skipping dotfiles and names too long for a row, sorted with `strcmp`, up to `max`. `too_large` counts the images skipped for size, so a screen can say why a folder looks empty. It returns 0 for a folder it cannot open.
- **Upload.** `wf_agent_upload_image_file` reads the file, uploads it with `wf_agent_upload_blob_ex` and returns an `app.bsky.embed.images` embed with the alt text. `WF_ERR_INVALID_ARG` for a bad argument, an extension it does not take or an empty or oversized file; `WF_ERR_NOT_FOUND` when the file cannot be opened; otherwise the upload's own status.

The file helpers (`src/attach.c`) need only `dirent.h` and `stat`; the upload (`src/agent/attach_upload.c`) needs the agent, so a client can link one without the other. Neither is in the Mac OS 9 target. [`test/test_attach.c`](../test/test_attach.c) checks the types and the scan on a scratch folder; the upload is checked in `test/test_graph_writes.c` against the mock PDS. Nothing here has run on a console.
