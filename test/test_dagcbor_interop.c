/**
 * test_dagcbor_interop.c — validates wolfram's DAG-CBOR (DRISL) canonical
 * encoder and CID computation against the official atproto reference
 * vectors (packages/lex/lex-cbor/tests/vectors.ts, copied verbatim as
 * test/fixtures/dag-cbor-vectors.json).
 *
 * Each vector carries a plain JSON value, the expected canonical DAG-CBOR
 * bytes (hex), and the expected CIDv1 (dag-cbor, sha2-256). We build a
 * wf_cbor_item tree from the JSON (mirroring the reference jsonToLex
 * semantics: a single-key {"$link": cid} becomes a tag-42 link, a
 * single-key {"$bytes": b64} becomes a byte string, and any other object —
 * including {"$link": ..., "other": ...} — keeps $link/$bytes as plain
 * string keys), then serialize with wf_cbor_serialize and check both the
 * bytes and the recomputed CID.
 */

#include "wolfram/repo/cbor.h"
#include "wolfram/repo/cid.h"
#include "wolfram/crypto.h"

#include "test.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef WF_TEST_FIXTURE_DIR
#define WF_TEST_FIXTURE_DIR "test/fixtures"
#endif

/* Reference jsonToLex: a link/bytes node is only produced when $link/$bytes
 * is the sole key of the object. Otherwise they are ordinary string keys. */
static wf_cbor_item *cbor_from_json(const cJSON *j);

static wf_cbor_item *new_item(void) {
    return calloc(1, sizeof(wf_cbor_item));
}

static wf_cbor_item *make_link(const char *cid_str) {
    wf_cid cid;
    wf_cbor_item *item = new_item();
    if (!item) return NULL;
    if (wf_cid_from_string(cid_str, &cid) != WF_OK) {
        free(item);
        return NULL;
    }
    item->type = WF_CBOR_LINK;
    item->bytes.len = cid.len;
    item->bytes.data = malloc(cid.len ? cid.len : 1);
    if (!item->bytes.data) { free(item); return NULL; }
    memcpy(item->bytes.data, cid.bytes, cid.len);
    return item;
}

static wf_cbor_item *make_bytes(const char *b64) {
    unsigned char *raw = NULL;
    size_t raw_len = 0;
    wf_cbor_item *item = new_item();
    if (!item) return NULL;
    if (wf_crypto_base64url_decode(b64, &raw, &raw_len) != WF_OK) {
        free(item);
        return NULL;
    }
    item->type = WF_CBOR_BYTES;
    item->bytes.len = raw_len;
    item->bytes.data = raw;
    return item;
}

static wf_cbor_item *cbor_from_json(const cJSON *j) {
    wf_cbor_item *item;
    if (cJSON_IsNull(j)) {
        item = new_item();
        if (item) { item->type = WF_CBOR_SIMPLE; item->simple_value = 22; }
        return item;
    }
    if (cJSON_IsBool(j)) {
        item = new_item();
        if (item) { item->type = WF_CBOR_SIMPLE;
                    item->simple_value = cJSON_IsTrue(j) ? 21 : 20; }
        return item;
    }
    if (cJSON_IsNumber(j)) {
        double d = j->valuedouble;
        if (d < 0) {
            item = new_item();
            if (item) { item->type = WF_CBOR_NEGATIVE;
                        item->neginteger = (uint64_t)(-d); }
            return item;
        }
        item = new_item();
        if (item) { item->type = WF_CBOR_UNSIGNED;
                    item->uinteger = (uint64_t)d; }
        return item;
    }
    if (cJSON_IsString(j)) {
        item = new_item();
        if (item) {
            item->type = WF_CBOR_STRING;
            item->string.len = strlen(j->valuestring);
            item->string.str = malloc(item->string.len + 1);
            if (!item->string.str) { free(item); return NULL; }
            memcpy(item->string.str, j->valuestring, item->string.len);
            item->string.str[item->string.len] = '\0';
        }
        return item;
    }
    if (cJSON_IsArray(j)) {
        size_t n = cJSON_GetArraySize(j), k;
        item = new_item();
        if (!item) return NULL;
        item->type = WF_CBOR_ARRAY;
        item->children.count = n;
        item->children.items = calloc(n ? n : 1, sizeof(*item->children.items));
        if (!item->children.items) { free(item); return NULL; }
        for (k = 0; k < n; k++)
            item->children.items[k] = cbor_from_json(cJSON_GetArrayItem(j, k));
        return item;
    }
    if (cJSON_IsObject(j)) {
        size_t n = cJSON_GetArraySize(j);
        cJSON *child = j->child;
        if (n == 1 && child->string) {
            if (strcmp(child->string, "$link") == 0 && cJSON_IsString(child))
                return make_link(child->valuestring);
            if (strcmp(child->string, "$bytes") == 0 && cJSON_IsString(child))
                return make_bytes(child->valuestring);
        }
        item = new_item();
        if (!item) return NULL;
        item->type = WF_CBOR_MAP;
        item->map.count = n;
        item->map.pairs = calloc(n ? n : 1, sizeof(*item->map.pairs));
        if (!item->map.pairs) { free(item); return NULL; }
        for (size_t k = 0; k < n; k++) {
            cJSON *pair = cJSON_GetArrayItem(j, k);
            wf_cbor_item *key = new_item();
            if (!key) return item; /* partial; serialize copes */
            key->type = WF_CBOR_STRING;
            key->string.len = strlen(pair->string);
            key->string.str = malloc(key->string.len + 1);
            if (!key->string.str) { free(key); return item; }
            memcpy(key->string.str, pair->string, key->string.len);
            key->string.str[key->string.len] = '\0';
            item->map.pairs[k].key = key;
            item->map.pairs[k].value = cbor_from_json(pair);
        }
        return item;
    }
    return NULL;
}

static char *to_hex(const unsigned char *data, size_t len) {
    static const char *hx = "0123456789abcdef";
    char *out = malloc(len * 2 + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < len; i++) {
        out[2 * i] = hx[data[i] >> 4];
        out[2 * i + 1] = hx[data[i] & 0xf];
    }
    out[len * 2] = '\0';
    return out;
}

static char *read_entire_file(const char *path, size_t *len_out) {
    FILE *fp = fopen(path, "rb");
    long size_long;
    size_t size;
    char *buffer;
    if (len_out) *len_out = 0;
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    size_long = ftell(fp);
    if (size_long < 0) { fclose(fp); return NULL; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    size = (size_t)size_long;
    buffer = malloc(size + 1);
    if (!buffer) { fclose(fp); return NULL; }
    if (size > 0 && fread(buffer, 1, size, fp) != size) {
        free(buffer); fclose(fp); return NULL;
    }
    buffer[size] = '\0';
    fclose(fp);
    if (len_out) *len_out = size;
    return buffer;
}

int main(void) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/dag-cbor-vectors.json", WF_TEST_FIXTURE_DIR);
    char *json = read_entire_file(path, NULL);
    WF_CHECK(json != NULL);
    if (!json) WF_TEST_SUMMARY();

    cJSON *vectors = cJSON_Parse(json);
    free(json);
    WF_CHECK(vectors != NULL && cJSON_IsArray(vectors));
    if (!vectors || !cJSON_IsArray(vectors)) WF_TEST_SUMMARY();

    int n = cJSON_GetArraySize(vectors);
    for (int i = 0; i < n; i++) {
        cJSON *v = cJSON_GetArrayItem(vectors, i);
        cJSON *name = cJSON_GetObjectItemCaseSensitive(v, "name");
        cJSON *jv = cJSON_GetObjectItemCaseSensitive(v, "json");
        cJSON *exp_cbor = cJSON_GetObjectItemCaseSensitive(v, "cbor");
        cJSON *exp_cid = cJSON_GetObjectItemCaseSensitive(v, "cid");
        if (!name || !jv || !exp_cbor || !exp_cid) { WF_CHECK(0); continue; }

        wf_cbor_item *tree = cbor_from_json(jv);
        WF_CHECK(tree != NULL);
        if (!tree) continue;

        size_t len = 0;
        unsigned char *ser = wf_cbor_serialize(tree, &len);
        WF_CHECK(ser != NULL);
        if (ser) {
            char *got_hex = to_hex(ser, len);
            WF_CHECK(got_hex && strcmp(got_hex, exp_cbor->valuestring) == 0);
            free(got_hex);

            wf_cid cid;
            WF_CHECK(wf_cid_of_block(ser, len, &cid) == WF_OK);
            char *cid_str = wf_cid_to_string(&cid);
            WF_CHECK(cid_str && strcmp(cid_str, exp_cid->valuestring) == 0);
            free(cid_str);
            free(ser);
        }
        wf_cbor_free(tree);
    }

    cJSON_Delete(vectors);
    WF_TEST_SUMMARY();
}
