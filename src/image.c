#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "wolfram/image.h"

wf_status wf_image_dimensions(const void *data, size_t len, int *out_w, int *out_h) {
    if (!data || len == 0 || !out_w || !out_h) return WF_ERR_INVALID_ARG;
    int w = 0, h = 0, comp = 0;
    if (!stbi_info_from_memory((const unsigned char *)data, (int)len, &w, &h, &comp))
        return WF_ERR_PARSE;
    *out_w = w;
    *out_h = h;
    return WF_OK;
}
