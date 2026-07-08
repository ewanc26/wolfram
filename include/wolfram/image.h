#ifndef WOLFRAM_IMAGE_H
#define WOLFRAM_IMAGE_H

#include "wolfram/util.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reads image dimensions from an in-memory encoded image without decoding
 * pixels, using vendored stb_image (stbi_info_from_memory).
 *
 * Returns WF_OK and writes *out_w / *out_h on success. Returns WF_ERR_PARSE
 * if the buffer is not a recognized image format, or WF_ERR_INVALID_ARG if any
 * argument is NULL or the length is zero. */
wf_status wf_image_dimensions(const void *data, size_t len, int *out_w, int *out_h);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_IMAGE_H */
