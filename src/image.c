/* stb_image keeps its last failure reason in a thread-local variable. On the 3DS
 * that variable, touched from a thread made with libctru's threadCreate, corrupts
 * memory the first time a JPEG is decoded there: Indigo's avatar loader crashed
 * in the C library's printf a moment after a successful decode, with text from
 * the decoded data where a FILE pointer should be. Without thread-local storage
 * the reason is one shared pointer to a string literal; nothing here reads it. */
#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "wolfram/image.h"
#include <limits.h>
#include <stdlib.h>

wf_status wf_image_dimensions(const void *data, size_t len, int *out_w,
                              int *out_h) {
    if (!data || len == 0 || !out_w || !out_h) return WF_ERR_INVALID_ARG;
    int w = 0, h = 0, comp = 0;
    if (!stbi_info_from_memory((const unsigned char *)data, (int)len, &w, &h,
                               &comp))
        return WF_ERR_PARSE;
    *out_w = w;
    *out_h = h;
    return WF_OK;
}

/* Box-filter downscale from sw*sh to dw*dh RGBA.
 *
 * Averaging is done premultiplied: a fully transparent pixel must contribute
 * its alpha and nothing else, or a black background behind a logo would bleed
 * into the logo's edges and give them a dark fringe. That is invisible against
 * an opaque image and very visible against a transparent one. */
static unsigned char *box_downscale_rgba(const unsigned char *src, int sw,
                                         int sh, int dw, int dh) {
    unsigned char *out = (unsigned char *)malloc((size_t)dw * (size_t)dh * 4u);
    if (!out) return NULL;
    for (int dy = 0; dy < dh; dy++) {
        const int sy0 = (int)((long)dy * sh / dh);
        int sy1 = (int)((long)(dy + 1) * sh / dh);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        unsigned char *dst = out + (size_t)dy * (size_t)dw * 4u;
        for (int dx = 0; dx < dw; dx++) {
            const int sx0 = (int)((long)dx * sw / dw);
            int sx1 = (int)((long)(dx + 1) * sw / dw);
            if (sx1 <= sx0) sx1 = sx0 + 1;
            unsigned int sa = 0, sr = 0, sg = 0, sb = 0;
            for (int sy = sy0; sy < sy1 && sy < sh; sy++) {
                const unsigned char *row = src + (size_t)sy * (size_t)sw * 4u;
                for (int sx = sx0; sx < sx1 && sx < sw; sx++) {
                    const unsigned char *p = row + (size_t)sx * 4u;
                    sa += p[3];
                    sr += (unsigned int)p[0] * p[3];
                    sg += (unsigned int)p[1] * p[3];
                    sb += (unsigned int)p[2] * p[3];
                }
            }
            dst[dx * 4 + 0] = sa ? (unsigned char)(sr / sa) : 0;
            dst[dx * 4 + 1] = sa ? (unsigned char)(sg / sa) : 0;
            dst[dx * 4 + 2] = sa ? (unsigned char)(sb / sa) : 0;
            dst[dx * 4 + 3] =
                (unsigned char)(sa / (unsigned int)((sy1 - sy0) * (sx1 - sx0)));
        }
    }
    return out;
}

wf_status wf_image_decode_rgba(const void *data, size_t len, int max_dim,
                               wf_image_rgba *out) {
    if (!out) return WF_ERR_INVALID_ARG;
    out->pixels = NULL;
    out->width = 0;
    out->height = 0;
    if (!data || len == 0) return WF_ERR_INVALID_ARG;
    /* stb takes the buffer length as an int, and a length that does not fit
     * one is not something this can decode anyway. */
    if (len > (size_t)INT_MAX) return WF_ERR_PARSE;

    /* Probe the header before decoding anything: the decode allocates the
     * full-size image, so an image past the source cap has to be refused
     * while it is still only a header. */
    int sw = 0, sh = 0, comp = 0;
    if (!stbi_info_from_memory((const unsigned char *)data, (int)len, &sw, &sh,
                               &comp))
        return WF_ERR_PARSE;
    if (sw <= 0 || sh <= 0) return WF_ERR_PARSE;
    if (sw > WF_IMAGE_MAX_SOURCE_DIM || sh > WF_IMAGE_MAX_SOURCE_DIM)
        return WF_ERR_UNSUPPORTED;

    int w = 0, h = 0;
    unsigned char *pixels = stbi_load_from_memory((const unsigned char *)data,
                                                  (int)len, &w, &h, &comp, 4);
    if (!pixels) return WF_ERR_PARSE;

    int dw = w, dh = h;
    if (max_dim > 0 && (w > max_dim || h > max_dim)) {
        /* Fit inside max_dim without changing the aspect ratio, and never
         * round a dimension down to zero. */
        if (w >= h) {
            dw = max_dim;
            dh = (int)((long)h * max_dim / w);
            if (dh < 1) dh = 1;
        } else {
            dh = max_dim;
            dw = (int)((long)w * max_dim / h);
            if (dw < 1) dw = 1;
        }
        if (dw == w && dh == h) {
            out->pixels = pixels;
            out->width = w;
            out->height = h;
            return WF_OK;
        }
        unsigned char *scaled = box_downscale_rgba(pixels, w, h, dw, dh);
        stbi_image_free(pixels);
        if (!scaled) return WF_ERR_ALLOC;
        out->pixels = scaled;
        out->width = dw;
        out->height = dh;
        return WF_OK;
    }

    out->pixels = pixels;
    out->width = w;
    out->height = h;
    return WF_OK;
}

void wf_image_rgba_free(wf_image_rgba *img) {
    if (!img) return;
    stbi_image_free(img->pixels);
    img->pixels = NULL;
    img->width = 0;
    img->height = 0;
}