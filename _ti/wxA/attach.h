/*
 * attach.h -- picking and uploading a local image to attach to a post.
 *
 * A client with an SD card or a disk wants the same four things: which files in
 * a folder are postable images, the MIME type to upload one as, the size the
 * network will take, and the upload that ends in an image embed. They are here
 * so Cobalt, Indigo and the rest do not each carry their own copy.
 *
 * Only JPEG and PNG are accepted, by extension, case-insensitively. The size
 * limit is conservative: Bluesky's blob limit is 1,000,000 bytes and this
 * leaves headroom for clients that count differently.
 */

#ifndef WOLFRAM_ATTACH_H
#define WOLFRAM_ATTACH_H

#include "wolfram/agent.h"

#include <cJSON.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The largest image file, in bytes, that wf_attach_scan_images lists and
 * wf_agent_upload_image_file will read. */
#define WF_ATTACH_MAX_BYTES 950000

/* "image/jpeg" for .jpg and .jpeg, "image/png" for .png (any case), otherwise
 * NULL. Only the extension is looked at; the file is not opened. */
const char *wf_attach_mime(const char *path);

/*
 * List the postable images in `dir` (a regular file, not empty, at most
 * WF_ATTACH_MAX_BYTES, a name wf_attach_mime accepts and shorter than
 * `name_cap`), sorted by name with strcmp, at most `max` of them. `names` is
 * `max` rows of `name_cap` bytes each. Dotfiles are skipped. Returns how many
 * were stored, 0 if `dir` cannot be opened. `*too_large` (optional) receives
 * how many otherwise-fine images were skipped for size, so a screen can say
 * why a folder looks empty.
 */
int wf_attach_scan_images(const char *dir, char *names, size_t name_cap,
                          int max, int *too_large);

/*
 * As wf_attach_scan_images, plus the images one folder down in `dir`: the
 * camera layout (DCIM/<folder>/<image>). Entries from `dir` itself are plain
 * names; entries from a subfolder are "folder/name", so the caller joins
 * dir + "/" + entry to get the file. Subfolders are read one level only, and
 * dotfolders are skipped. Sorted by strcmp, at most `max` in all. Returns the
 * count, 0 if `dir` cannot be opened.
 */
int wf_attach_scan_images_tree(const char *dir, char *names, size_t name_cap,
                               int max, int *too_large);

/*
 * Read the image at `path`, upload it with wf_agent_upload_blob_ex and build an
 * app.bsky.embed.images embed holding it with `alt` (NULL for none). On WF_OK
 * *embed is a new cJSON object the caller deletes, or passes on to a record
 * builder that takes it. WF_ERR_INVALID_ARG for a bad argument, an extension
 * wf_attach_mime does not accept, or an empty or oversized file;
 * WF_ERR_NOT_FOUND when the file cannot be opened; WF_ERR_ALLOC; otherwise the
 * upload's own status. On error *embed is NULL.
 */
wf_status wf_agent_upload_image_file(wf_agent *agent, const char *path,
                                     const char *alt, cJSON **embed);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_ATTACH_H */
