#ifndef WOLFRAM_REPO_CAR_H
#define WOLFRAM_REPO_CAR_H

#include <stddef.h>
#include "wolfram/xrpc.h"
#include "wolfram/repo/cid.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wf_car_block {
    wf_cid cid;
    unsigned char *data;
    size_t data_len;
} wf_car_block;

typedef struct wf_car {
    wf_cid *roots;
    size_t root_count;
    wf_car_block *blocks;
    size_t block_count;
} wf_car;

wf_status wf_car_parse(const unsigned char *data, size_t len, wf_car *out);
void wf_car_free(wf_car *car);
wf_car_block *wf_car_find_block(wf_car *car, const wf_cid *cid);
wf_status wf_car_write(const wf_car *car, unsigned char **out, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_REPO_CAR_H */