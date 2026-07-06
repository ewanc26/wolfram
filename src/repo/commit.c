#include "wolfram/repo/commit.h"
#include "wolfram/repo/cbor.h"
#include "wolfram/repo/cid.h"
#include "wolfram/repo/car.h"
#include "cbor_build.h"
#include "cbor_map_find.h"

#include <string.h>
#include <stdlib.h>

wf_status wf_commit_parse(const unsigned char *cbor, size_t len,
                          wf_commit *out) {
    if (!cbor || len == 0 || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    wf_cbor_item *obj = wf_cbor_parse(cbor, len);
    if (!obj) return WF_ERR_PARSE;
    if (obj->type != WF_CBOR_MAP) { wf_cbor_free(obj); return WF_ERR_PARSE; }

    wf_cbor_item *did_item = wf_cbor_map_find(obj, "did", 3);
    if (!did_item || did_item->type != WF_CBOR_STRING ||
        did_item->string.len >= sizeof(out->did)) {
        wf_cbor_free(obj);
        return WF_ERR_PARSE;
    }
    memcpy(out->did, did_item->string.str, did_item->string.len);
    out->did[did_item->string.len] = '\0';

    wf_cbor_item *ver_item = wf_cbor_map_find(obj, "version", 7);
    if (!ver_item || ver_item->type != WF_CBOR_UNSIGNED) {
        wf_cbor_free(obj);
        return WF_ERR_PARSE;
    }
    out->version = (int)ver_item->uinteger;

    wf_cbor_item *data_item = wf_cbor_map_find(obj, "data", 4);
    if (!data_item || data_item->type != WF_CBOR_LINK ||
        data_item->bytes.len != 36) {
        wf_cbor_free(obj);
        return WF_ERR_PARSE;
    }
    memcpy(out->data.bytes, data_item->bytes.data, 36);
    out->data.len = 36;

    wf_cbor_item *rev_item = wf_cbor_map_find(obj, "rev", 3);
    if (!rev_item || rev_item->type != WF_CBOR_STRING ||
        rev_item->string.len >= sizeof(out->rev)) {
        wf_cbor_free(obj);
        return WF_ERR_PARSE;
    }
    memcpy(out->rev, rev_item->string.str, rev_item->string.len);
    out->rev[rev_item->string.len] = '\0';

    wf_cbor_item *prev_item = wf_cbor_map_find(obj, "prev", 4);
    if (prev_item && prev_item->type == WF_CBOR_LINK &&
        prev_item->bytes.len == 36) {
        memcpy(out->prev.bytes, prev_item->bytes.data, 36);
        out->prev.len = 36;
        out->has_prev = 1;
    } else if (prev_item && !(prev_item->type == WF_CBOR_SIMPLE &&
                               prev_item->simple_value == 22)) {
        wf_cbor_free(obj);
        return WF_ERR_PARSE;
    }

    wf_cbor_item *sig_item = wf_cbor_map_find(obj, "sig", 3);
    if (sig_item) {
        if (sig_item->type != WF_CBOR_BYTES || sig_item->bytes.len != 64) {
            wf_cbor_free(obj);
            return WF_ERR_PARSE;
        }
        memcpy(out->sig, sig_item->bytes.data, 64);
        out->sig_len = 64;
    }

    wf_cbor_free(obj);
    return WF_OK;
}

wf_status wf_commit_create(const char *did_str, const char *rev_str,
                           const wf_cid *data_cid,
                           const wf_cid *prev_cid,
                           const wf_signing_key *key,
                           wf_car *car,
                           wf_commit *out) {
    if (!did_str || !rev_str || !data_cid || !key || !car || !out)
        return WF_ERR_INVALID_ARG;

    memset(out, 0, sizeof(*out));
    size_t dlen = strlen(did_str);
    if (dlen >= sizeof(out->did)) return WF_ERR_INVALID_ARG;
    memcpy(out->did, did_str, dlen + 1);
    size_t rlen = strlen(rev_str);
    if (rlen >= sizeof(out->rev)) return WF_ERR_INVALID_ARG;
    memcpy(out->rev, rev_str, rlen + 1);
    out->data = *data_cid;
    if (prev_cid && prev_cid->len > 0) {
        out->prev = *prev_cid;
        out->has_prev = 1;
    }
    out->version = 3;

    wf_cbor_item *map = calloc(1, sizeof(*map));
    if (!map) return WF_ERR_ALLOC;
    map->type = WF_CBOR_MAP;
    map->map.count = 5;
    map->map.pairs = calloc(5, sizeof(wf_cbor_pair));
    if (!map->map.pairs) { free(map); return WF_ERR_ALLOC; }

    map->map.pairs[0].key = cbor_str("did");
    map->map.pairs[0].value = cbor_str(did_str);
    map->map.pairs[1].key = cbor_str("rev");
    map->map.pairs[1].value = cbor_str(rev_str);
    map->map.pairs[2].key = cbor_str("data");
    map->map.pairs[2].value = cbor_cid(data_cid);
    map->map.pairs[3].key = cbor_str("prev");
    map->map.pairs[3].value = (prev_cid && prev_cid->len > 0)
                              ? cbor_cid(prev_cid) : cbor_null();
    map->map.pairs[4].key = cbor_str("version");
    map->map.pairs[4].value = cbor_uint(3);

    size_t cbor_len;
    unsigned char *cbor = wf_cbor_serialize(map, &cbor_len);
    if (!cbor) { wf_cbor_free(map); return WF_ERR_ALLOC; }

    wf_status s = wf_sign(key, cbor, cbor_len, out->sig, sizeof(out->sig));
    free(cbor);
    if (s != WF_OK) { wf_cbor_free(map); return s; }
    out->sig_len = 64;

    map->map.count = 6;
    wf_cbor_pair *np = realloc(map->map.pairs, 6 * sizeof(wf_cbor_pair));
    if (!np) { wf_cbor_free(map); return WF_ERR_ALLOC; }
    map->map.pairs = np;
    memmove(&map->map.pairs[3], &map->map.pairs[2], 3 * sizeof(wf_cbor_pair));
    map->map.pairs[2].key = cbor_str("sig");
    map->map.pairs[2].value = cbor_bytes(out->sig, out->sig_len);

    unsigned char *final_cbor = wf_cbor_serialize(map, &cbor_len);
    wf_cbor_free(map);
    if (!final_cbor) return WF_ERR_ALLOC;

    wf_cid final_cid;
    s = wf_cid_of_block(final_cbor, cbor_len, &final_cid);
    if (s != WF_OK) { free(final_cbor); return s; }

    wf_car_block *blk = realloc(car->blocks,
        (car->block_count + 1) * sizeof(wf_car_block));
    if (!blk) { free(final_cbor); return WF_ERR_ALLOC; }
    car->blocks = blk;
    car->blocks[car->block_count].cid = final_cid;
    car->blocks[car->block_count].data = final_cbor;
    car->blocks[car->block_count].data_len = cbor_len;
    car->block_count++;

    out->cid = final_cid;
    return WF_OK;
}
