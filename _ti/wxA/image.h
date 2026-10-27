#ifndef WOLFRAM_IMAGE_H
#define WOLFRAM_IMAGE_H

#include "wolfram/util.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Refuses to decode anything larger than this on either axis, whatever
 * max_dim asks for. The decoder has to hold the full-size pixels before it can
 * box-filter them down, so this cap is what bounds peak memory: at 2048 it
 * caps the transient full-size buffer at 16 MB. Console callers want this far
 * lower and should ask for it with max_dim plus a smaller source. */
#define WF_IMAGE_MAX_SOURCE_DIM 2048

/* Decoded 8-bit RGBA, tightly packed and row-major with the first row at the
 * top. `pixels` is owned by the caller: free it with wf_image_rgba_free, which
 * is safe on a zeroed struct. */
typedef struct wf_image_rgba {
    unsigned char *pixels;
    int width;
    int height;
} wf_image_rgba;

/* Reads image dimensions from an in-memory encoded image without decoding
 * pixels, using vendored stb_image (stbi_info_from_memory).
 *
 * Returns WF_OK and writes *out_w / *out_h on success. Returns WF_ERR_PARSE
 * if the buffer is not a recognized image format, or WF_ERR_INVALID_ARG if any
 * argument is NULL or the length is zero. */
wf_status wf_image_dimensions(const void *data, size_t len, int *out_w,
                              int *out_h);

/* Decodes an encoded image (JPEG, PNG, and the other formats vendored stb
 * reads) to RGBA8. `max_dim` is the longest side the caller will draw: the
 * result is downscaled with a box filter to fit it while preserving aspect
 * ratio, so a caller wanting a 64px avatar asks for 64 and gets 64x64 for a
 * square source, 64x48 for a 4:3 one. 0 or negative means no downscaling.
 *
 * Scaling happens after decode, not during it: vendored stb_image has no
 * scaled-decode entry point, so peak memory is the full-size image (bounded by
 * WF_IMAGE_MAX_SOURCE_DIM) plus the smaller result. Alpha is averaged
 * premultiplied, so transparent pixels do not bleed their colour into the
 * edges of a resized image.
 *
 * On success returns WF_OK with *out filled, and the caller releases it with
 * wf_image_rgba_free. On failure *out is left zeroed — no half-built result to
 * leak — and the status is WF_ERR_INVALID_ARG for a NULL argument or zero
 * length, WF_ERR_PARSE for bytes that are not a readable image, WF_ERR_ALLOC
 * when the pixel buffer cannot be allocated, or WF_ERR_UNSUPPORTED when the
 * image's own dimensions exceed WF_IMAGE_MAX_SOURCE_DIM. */
wf_status wf_image_decode_rgba(const void *data, size_t len, int max_dim,
                               wf_image_rgba *out);

void wf_image_rgba_free(wf_image_rgba *img);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_IMAGE_H */
