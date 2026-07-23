/**
 * plc.c — DID PLC operation build / sign / submit helpers.
 *
 * See include/wolfram/plc.h for the public contract and protocol notes.
 */

#include "wolfram/plc.h"

#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include "wolfram/identity.h"
#include "wolfram/log.h"
#include "wolfram/repo/cbor.h"

/* ── small utilities ────────────────────────────────────────── */

static char *wf_plc_strdup(const char *value) {
    size_t len;
    char *copy;

    if (!value) return NULL;
    len = strlen(value);
    copy = malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, value, len + 1);
    return copy;
}

static void *wf_plc_alloc(size_t n) { return calloc(1, n); }

/* ── base64url (RFC 4648 §5, no padding) ───────────────────── */

static const char wf_b64url_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static char *wf_plc_base64url_encode(const unsigned char *in, size_t len) {
    size_t out_len = (len + 2) / 3 * 4;
    char *out = malloc(out_len + 1);
    size_t o = 0, i = 0;

    if (!out) return NULL;

    while (i + 3 <= len) {
        unsigned int n = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
        out[o++] = wf_b64url_alphabet[(n >> 18) & 0x3f];
        out[o++] = wf_b64url_alphabet[(n >> 12) & 0x3f];
        out[o++] = wf_b64url_alphabet[(n >> 6) & 0x3f];
        out[o++] = wf_b64url_alphabet[n & 0x3f];
        i += 3;
    }
    if (len - i == 1) {
        unsigned int n = in[i] << 16;
        out[o++] = wf_b64url_alphabet[(n >> 18) & 0x3f];
        out[o++] = wf_b64url_alphabet[(n >> 12) & 0x3f];
    } else if (len - i == 2) {
        unsigned int n = (in[i] << 16) | (in[i + 1] << 8);
        out[o++] = wf_b64url_alphabet[(n >> 18) & 0x3f];
        out[o++] = wf_b64url_alphabet[(n >> 12) & 0x3f];
        out[o++] = wf_b64url_alphabet[(n >> 6) & 0x3f];
    }
    out[o] = '\0';
    return out;
}

static int wf_plc_base64url_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

static unsigned char *wf_plc_base64url_decode(const char *in, size_t *out_len) {
    size_t in_len = strlen(in);
    size_t pad = 0;
    size_t i = 0;
    size_t o = 0;
    unsigned char *out;

    while (i < in_len && in[i] != '=') i++;
    pad = in_len - i; /* trailing '=' not expected but tolerated */
    if (in_len < pad) { *out_len = 0; return NULL; }
    {
        size_t data_len = in_len - pad;
        out = malloc(data_len * 3 / 4 + 1);
        if (!out) { *out_len = 0; return NULL; }
        i = 0;
        while (i + 4 <= data_len) {
            int a = wf_plc_base64url_value(in[i]);
            int b = wf_plc_base64url_value(in[i + 1]);
            int c = wf_plc_base64url_value(in[i + 2]);
            int d = wf_plc_base64url_value(in[i + 3]);
            if (a < 0 || b < 0 || c < 0 || d < 0) { free(out); *out_len = 0; return NULL; }
            unsigned int n = (a << 18) | (b << 12) | (c << 6) | d;
            out[o++] = (unsigned char)((n >> 16) & 0xff);
            out[o++] = (unsigned char)((n >> 8) & 0xff);
            out[o++] = (unsigned char)(n & 0xff);
            i += 4;
        }
        if (data_len - i == 2) {
            int a = wf_plc_base64url_value(in[i]);
            int b = wf_plc_base64url_value(in[i + 1]);
            if (a < 0 || b < 0) { free(out); *out_len = 0; return NULL; }
            unsigned int n = (a << 18) | (b << 12);
            out[o++] = (unsigned char)((n >> 16) & 0xff);
        } else if (data_len - i == 3) {
            int a = wf_plc_base64url_value(in[i]);
            int b = wf_plc_base64url_value(in[i + 1]);
            int c = wf_plc_base64url_value(in[i + 2]);
            if (a < 0 || b < 0 || c < 0) { free(out); *out_len = 0; return NULL; }
            unsigned int n = (a << 18) | (b << 12) | (c << 6);
            out[o++] = (unsigned char)((n >> 16) & 0xff);
            out[o++] = (unsigned char)((n >> 8) & 0xff);
        }
    }
    *out_len = o;
    return out;
}

/* ── cJSON → canonical DAG-CBOR ─────────────────────────────── */

static wf_cbor_item *wf_plc_cbor_string(const char *s) {
    wf_cbor_item *item = wf_plc_alloc(sizeof(*item));
    if (!item) return NULL;
    item->type = WF_CBOR_STRING;
    item->string.len = strlen(s);
    item->string.str = wf_plc_alloc(item->string.len + 1);
    if (!item->string.str) { free(item); return NULL; }
    memcpy(item->string.str, s, item->string.len + 1);
    return item;
}

static wf_cbor_item *wf_plc_cbor_simple(int value) {
    wf_cbor_item *item = wf_plc_alloc(sizeof(*item));
    if (!item) return NULL;
    item->type = WF_CBOR_SIMPLE;
    item->simple_value = value;
    return item;
}

static wf_cbor_item *wf_plc_cbor_from_json(const cJSON *node, const char *skip);

static wf_cbor_item *wf_plc_cbor_object(const cJSON *node, const char *skip) {
    const cJSON *child;
    size_t count = 0;
    wf_cbor_item *item;
    size_t idx = 0;

    cJSON_ArrayForEach(child, node) {
        if (skip && strcmp(child->string, skip) == 0) continue;
        count++;
    }

    item = wf_plc_alloc(sizeof(*item));
    if (!item) return NULL;
    item->type = WF_CBOR_MAP;
    item->map.count = 0;
    if (count == 0) return item;
    item->map.pairs = wf_plc_alloc(count * sizeof(wf_cbor_pair));
    if (!item->map.pairs) { free(item); return NULL; }

    cJSON_ArrayForEach(child, node) {
        wf_cbor_item *key;
        wf_cbor_item *val;
        if (skip && strcmp(child->string, skip) == 0) continue;
        key = wf_plc_cbor_string(child->string);
        val = wf_plc_cbor_from_json(child, NULL);
        if (!key || !val) {
            wf_cbor_free(key);
            wf_cbor_free(val);
            for (size_t k = 0; k < idx; k++) {
                wf_cbor_free(item->map.pairs[k].key);
                wf_cbor_free(item->map.pairs[k].value);
            }
            free(item->map.pairs);
            free(item);
            return NULL;
        }
        item->map.pairs[idx].key = key;
        item->map.pairs[idx].value = val;
        idx++;
    }
    item->map.count = idx;
    return item;
}

static wf_cbor_item *wf_plc_cbor_array(const cJSON *node) {
    const cJSON *child;
    size_t count = 0;
    wf_cbor_item *item;
    size_t idx = 0;

    cJSON_ArrayForEach(child, node) count++;

    item = wf_plc_alloc(sizeof(*item));
    if (!item) return NULL;
    item->type = WF_CBOR_ARRAY;
    item->children.count = 0;
    if (count == 0) return item;
    item->children.items = wf_plc_alloc(count * sizeof(wf_cbor_item *));
    if (!item->children.items) { free(item); return NULL; }

    cJSON_ArrayForEach(child, node) {
        item->children.items[idx] = wf_plc_cbor_from_json(child, NULL);
        if (!item->children.items[idx]) {
            for (size_t k = 0; k < idx; k++) wf_cbor_free(item->children.items[k]);
            free(item->children.items);
            free(item);
            return NULL;
        }
        idx++;
    }
    item->children.count = idx;
    return item;
}

static wf_cbor_item *wf_plc_cbor_from_json(const cJSON *node, const char *skip) {
    if (!node) return NULL;
    if (cJSON_IsNull(node)) return wf_plc_cbor_simple(22);
    if (cJSON_IsBool(node)) return wf_plc_cbor_simple(cJSON_IsTrue(node) ? 21 : 20);
    if (cJSON_IsString(node)) return wf_plc_cbor_string(node->valuestring);
    if (cJSON_IsArray(node)) return wf_plc_cbor_array(node);
    if (cJSON_IsObject(node)) return wf_plc_cbor_object(node, skip);
    if (cJSON_IsNumber(node)) {
        wf_cbor_item *item = wf_plc_alloc(sizeof(*item));
        if (!item) return NULL;
        if (node->valueint < 0) {
            item->type = WF_CBOR_NEGATIVE;
            item->neginteger = (uint64_t)(-node->valueint);
        } else {
            item->type = WF_CBOR_UNSIGNED;
            item->uinteger = (uint64_t)node->valueint;
        }
        return item;
    }
    return NULL; /* unsupported type (also matches skipped arrays) */
}

/* Canonical DAG-CBOR map ordering: by encoded CBOR key (shorter first, then
 * bytewise). We sort the in-memory pairs so wf_cbor_serialize emits them in
 * the order the PLC registry expects. */
static int wf_plc_cbor_pair_cmp(const void *a, const void *b) {
    const wf_cbor_pair *pa = (const wf_cbor_pair *)a;
    const wf_cbor_pair *pb = (const wf_cbor_pair *)b;
    size_t la, lb;
    unsigned char *ea = wf_cbor_serialize(pa->key, &la);
    unsigned char *eb = wf_cbor_serialize(pb->key, &lb);
    int r;

    if (!ea || !eb) r = 0;
    else if (la != lb) r = (la < lb) ? -1 : 1;
    else r = (int)memcmp(ea, eb, la);
    free(ea);
    free(eb);
    return r;
}

static void wf_plc_cbor_canonicalize(wf_cbor_item *item) {
    if (!item) return;
    if (item->type == WF_CBOR_MAP && item->map.count > 1) {
        qsort(item->map.pairs, item->map.count, sizeof(wf_cbor_pair),
              wf_plc_cbor_pair_cmp);
    }
    if (item->type == WF_CBOR_MAP) {
        for (size_t i = 0; i < item->map.count; i++) {
            wf_plc_cbor_canonicalize(item->map.pairs[i].key);
            wf_plc_cbor_canonicalize(item->map.pairs[i].value);
        }
    } else if (item->type == WF_CBOR_ARRAY) {
        for (size_t i = 0; i < item->children.count; i++) {
            wf_plc_cbor_canonicalize(item->children.items[i]);
        }
    }
}

/* Serialize a cJSON operation (with `skip` key omitted) to its canonical
 * DAG-CBOR bytes. Caller frees *out via free(). */
static wf_status wf_plc_canonical_cbor(const cJSON *root, const char *skip,
                                       unsigned char **out, size_t *out_len) {
    wf_cbor_item *item = wf_plc_cbor_from_json(root, skip);
    unsigned char *buf;

    WF_LOG_DEBUG("plc", "wf_plc_canonical_cbor: enter (skip=%s)", skip ? skip : "null");

    if (!item) {
        WF_LOG_ERROR("plc", "wf_plc_canonical_cbor: failed to convert JSON to CBOR");
        return WF_ERR_ALLOC;
    }
    wf_plc_cbor_canonicalize(item);
    buf = wf_cbor_serialize(item, out_len);
    wf_cbor_free(item);
    if (!buf) {
        WF_LOG_ERROR("plc", "wf_plc_canonical_cbor: serialization failed");
        *out_len = 0;
        return WF_ERR_ALLOC;
    }
    WF_LOG_DEBUG("plc", "wf_plc_canonical_cbor: success, output length=%zu", *out_len);
    *out = buf;
    return WF_OK;
}

/* ── public API ─────────────────────────────────────────────── */

void wf_plc_operation_free(char *json) { free(json); }

wf_status wf_plc_operation_build(const wf_plc_operation_update *update,
                                 char **out_json) {
    cJSON *op = NULL;
    cJSON *arr = NULL;
    cJSON *sub = NULL;
    char *json = NULL;
    wf_status status = WF_OK;

    WF_LOG_DEBUG("plc", "wf_plc_operation_build: enter");

    if (!update || !out_json) {
        WF_LOG_ERROR("plc", "wf_plc_operation_build: invalid args");
        return WF_ERR_INVALID_ARG;
    }
    *out_json = NULL;

    WF_LOG_DEBUG("plc", "wf_plc_operation_build: rotation_keys_count=%zu, also_known_as_count=%zu",
            update->rotation_keys_count, update->also_known_as_count);

    op = cJSON_CreateObject();
    if (!op) {
        WF_LOG_ERROR("plc", "wf_plc_operation_build: failed to create JSON object");
        return WF_ERR_ALLOC;
    }

    if (!cJSON_AddStringToObject(op, "type", "plc_operation")) {
        WF_LOG_ERROR("plc", "wf_plc_operation_build: failed to add type");
        status = WF_ERR_ALLOC; goto cleanup;
    }

    arr = cJSON_AddArrayToObject(op, "rotationKeys");
    if (!arr) { status = WF_ERR_ALLOC; goto cleanup; }
    for (size_t i = 0; i < update->rotation_keys_count; i++) {
        WF_LOG_DEBUG("plc", "wf_plc_operation_build: adding rotation key [%zu]=%s", i, update->rotation_keys[i]);
        if (!cJSON_AddItemToArray(arr,
                cJSON_CreateString(update->rotation_keys[i]))) {
            status = WF_ERR_ALLOC; goto cleanup;
        }
    }

    if (update->verification_methods_json && *update->verification_methods_json) {
        sub = cJSON_Parse(update->verification_methods_json);
        if (!sub) {
            WF_LOG_ERROR("plc", "wf_plc_operation_build: failed to parse verification_methods_json");
            status = WF_ERR_PARSE; goto cleanup;
        }
    } else {
        sub = cJSON_CreateObject();
    }
    if (!sub) { status = WF_ERR_ALLOC; goto cleanup; }
    cJSON_AddItemToObject(op, "verificationMethods", sub);
    sub = NULL;

    if (update->services_json && *update->services_json) {
        WF_LOG_DEBUG("plc", "wf_plc_operation_build: parsing services_json: %s", update->services_json);
        sub = cJSON_Parse(update->services_json);
        if (!sub) {
            WF_LOG_ERROR("plc", "wf_plc_operation_build: failed to parse services_json");
            status = WF_ERR_PARSE; goto cleanup;
        }
    } else {
        sub = cJSON_CreateObject();
    }
    if (!sub) { status = WF_ERR_ALLOC; goto cleanup; }
    cJSON_AddItemToObject(op, "services", sub);
    sub = NULL;

    arr = cJSON_AddArrayToObject(op, "alsoKnownAs");
    if (!arr) { status = WF_ERR_ALLOC; goto cleanup; }
    for (size_t i = 0; i < update->also_known_as_count; i++) {
        WF_LOG_DEBUG("plc", "wf_plc_operation_build: adding alsoKnownAs [%zu]=%s", i, update->also_known_as[i]);
        if (!cJSON_AddItemToArray(arr,
                cJSON_CreateString(update->also_known_as[i]))) {
            status = WF_ERR_ALLOC; goto cleanup;
        }
    }

    if (update->prev && *update->prev) {
        WF_LOG_DEBUG("plc", "wf_plc_operation_build: adding prev=%s", update->prev);
        if (!cJSON_AddStringToObject(op, "prev", update->prev)) {
            status = WF_ERR_ALLOC; goto cleanup;
        }
    }

    json = cJSON_PrintUnformatted(op);
    if (!json) {
        WF_LOG_ERROR("plc", "wf_plc_operation_build: failed to serialize JSON");
        status = WF_ERR_ALLOC; goto cleanup;
    }

    WF_LOG_DEBUG("plc", "wf_plc_operation_build: success, output length=%zu", strlen(json));
    *out_json = json;
    json = NULL; /* ownership transferred to caller */

cleanup:
    cJSON_Delete(op);
    wf_plc_operation_free(json); /* no-op when NULL (success transferred) */
    if (status != WF_OK) {
        WF_LOG_ERROR("plc", "wf_plc_operation_build: failed with status=%d", status);
    }
    return status;
}

wf_status wf_plc_operation_sign(const char *op_json,
                                const wf_signing_key *key,
                                  char **out_signed_json) {
    cJSON *root = NULL;
    unsigned char *cbor = NULL;
    size_t cbor_len = 0;
    unsigned char sig[64];
    char *sig_b64 = NULL;
    char *didkey = NULL;
    char *json = NULL;
    wf_status status = WF_OK;

    WF_LOG_DEBUG("plc", "wf_plc_operation_sign: enter");

    if (!op_json || !key || !out_signed_json) {
        WF_LOG_ERROR("plc", "wf_plc_operation_sign: invalid args");
        return WF_ERR_INVALID_ARG;
    }
    *out_signed_json = NULL;

    WF_LOG_DEBUG("plc", "wf_plc_operation_sign: parsing op_json (len=%zu)", strlen(op_json));
    root = cJSON_Parse(op_json);
    if (!root || !cJSON_IsObject(root)) {
        WF_LOG_ERROR("plc", "wf_plc_operation_sign: failed to parse op_json");
        status = WF_ERR_PARSE; goto cleanup;
    }

    WF_LOG_DEBUG("plc", "wf_plc_operation_sign: computing canonical CBOR");
    status = wf_plc_canonical_cbor(root, "sig", &cbor, &cbor_len);
    if (status != WF_OK) {
        WF_LOG_ERROR("plc", "wf_plc_operation_sign: canonical CBOR failed status=%d", status);
        goto cleanup;
    }
    WF_LOG_DEBUG("plc", "wf_plc_operation_sign: CBOR length=%zu", cbor_len);

    WF_LOG_DEBUG("plc", "wf_plc_operation_sign: signing with key type=%d", key->type);
    status = wf_sign(key, cbor, cbor_len, sig, sizeof(sig));
    if (status != WF_OK) {
        WF_LOG_ERROR("plc", "wf_plc_operation_sign: signing failed status=%d", status);
        goto cleanup;
    }
    WF_LOG_DEBUG("plc", "wf_plc_operation_sign: signature created (64 bytes)");

    sig_b64 = wf_plc_base64url_encode(sig, sizeof(sig));
    if (!sig_b64) {
        WF_LOG_ERROR("plc", "wf_plc_operation_sign: base64 encoding failed");
        status = WF_ERR_ALLOC; goto cleanup;
    }
    WF_LOG_DEBUG("plc", "wf_plc_operation_sign: signature base64 len=%zu", strlen(sig_b64));

    status = wf_signing_key_public_didkey(key, &didkey);
    if (status != WF_OK) {
        WF_LOG_ERROR("plc", "wf_plc_operation_sign: failed to get public didkey status=%d", status);
        goto cleanup;
    }
    WF_LOG_DEBUG("plc", "wf_plc_operation_sign: signer didkey=%s", didkey);

    /* The PLC directory expects sig as an object mapping the signer's
     * did:key to the base64url signature, matching the @did-plc/codec
     * reference format. */
    cJSON *sig_obj = cJSON_CreateObject();
    if (!sig_obj || !cJSON_AddItemToObject(sig_obj, didkey,
            cJSON_CreateString(sig_b64))) {
        free(sig_obj);
        WF_LOG_ERROR("plc", "wf_plc_operation_sign: failed to add sig to JSON");
        status = WF_ERR_ALLOC; goto cleanup;
    }
    cJSON_AddItemToObject(root, "sig", sig_obj);

    json = cJSON_PrintUnformatted(root);
    if (!json) {
        WF_LOG_ERROR("plc", "wf_plc_operation_sign: failed to serialize signed JSON");
        status = WF_ERR_ALLOC; goto cleanup;
    }

    WF_LOG_DEBUG("plc", "wf_plc_operation_sign: success, output length=%zu", strlen(json));
    *out_signed_json = json;

cleanup:
    free(cbor);
    free(sig_b64);
    free(didkey);
    cJSON_Delete(root);
    if (status != WF_OK) {
        free(json);
        WF_LOG_ERROR("plc", "wf_plc_operation_sign: failed with status=%d", status);
    }
    return status;
}

wf_status wf_plc_operation_verify(const char *signed_json,
                                  char **out_signer_didkey) {
    cJSON *root = NULL;
    const cJSON *sig_item = NULL;
    const cJSON *sig_b64_item = NULL;
    const char *didkey = NULL;
    const char *sig_b64 = NULL;
    const cJSON *rotation_keys = NULL;
    unsigned char *cbor = NULL;
    size_t cbor_len = 0;
    unsigned char *sig = NULL;
    size_t sig_len = 0;
    wf_status status = WF_OK;

    if (!signed_json || !out_signer_didkey) return WF_ERR_INVALID_ARG;
    *out_signer_didkey = NULL;

    root = cJSON_Parse(signed_json);
    if (!root || !cJSON_IsObject(root)) {
        status = WF_ERR_PARSE; goto cleanup;
    }

    sig_item = cJSON_GetObjectItemCaseSensitive(root, "sig");
    if (!sig_item || !cJSON_IsObject(sig_item)) {
        status = WF_ERR_PARSE; goto cleanup;
    }
    cJSON_ArrayForEach(sig_b64_item, sig_item) {
        if (cJSON_IsString(sig_b64_item) && sig_b64_item->valuestring[0]) {
            sig_b64 = sig_b64_item->valuestring;
            didkey = sig_b64_item->string;
            break;
        }
    }
    if (!sig_b64 || !didkey) {
        status = WF_ERR_PARSE; goto cleanup;
    }

    sig = wf_plc_base64url_decode(sig_b64, &sig_len);
    if (!sig || sig_len != 64) { status = WF_ERR_PARSE; goto cleanup; }

    status = wf_plc_canonical_cbor(root, "sig", &cbor, &cbor_len);
    if (status != WF_OK) goto cleanup;

    /* wf_verify hashes the message internally (SHA256), so pass the raw
     * CBOR bytes — not a pre-computed hash — to avoid double-hashing. */
    rotation_keys = cJSON_GetObjectItemCaseSensitive(root, "rotationKeys");
    if (!rotation_keys || !cJSON_IsArray(rotation_keys)) {
        status = WF_ERR_PARSE; goto cleanup;
    }

    for (int i = 0; i < (int)cJSON_GetArraySize(rotation_keys); i++) {
        const cJSON *rk = cJSON_GetArrayItem(rotation_keys, i);
        if (!cJSON_IsString(rk) || !rk->valuestring[0]) continue;
        didkey = rk->valuestring;
        if (wf_verify(didkey, cbor, cbor_len, sig, sig_len) == WF_OK) {
            *out_signer_didkey = wf_plc_strdup(didkey);
            if (!*out_signer_didkey) { status = WF_ERR_ALLOC; goto cleanup; }
            status = WF_OK;
            goto cleanup;
        }
    }

    status = WF_ERR_INVALID_ARG;

cleanup:
    free(cbor);
    free(sig);
    cJSON_Delete(root);
    return status;
}

wf_status wf_plc_sign_operation(wf_xrpc_client *client,
                                const char *did,
                                const wf_plc_operation_update *update,
                                const wf_signing_key *key,
                                char **out_signed_json) {
    wf_did_document doc;
    char *op = NULL;
    wf_status status;

    if (!client || !did || !update || !key || !out_signed_json) {
        return WF_ERR_INVALID_ARG;
    }
    *out_signed_json = NULL;

    memset(&doc, 0, sizeof(doc));
    status = wf_did_resolve(client, did, &doc);
    if (status != WF_OK) return status;

    if (doc.method != WF_DID_METHOD_PLC) {
        wf_did_document_free(&doc);
        return WF_ERR_INVALID_ARG; /* TODO: did:web has no PLC operation */
    }
    wf_did_document_free(&doc);

    status = wf_plc_operation_build(update, &op);
    if (status != WF_OK) return status;

    status = wf_plc_operation_sign(op, key, out_signed_json);
    wf_plc_operation_free(op);
    return status;
}

wf_status wf_plc_submit_operation(wf_xrpc_client *client,
                                  const char *signed_op_json) {
    wf_response response = {0};
    cJSON *op = NULL;
    cJSON *body = NULL;
    char *json = NULL;
    wf_status status;

    if (!client || !signed_op_json) return WF_ERR_INVALID_ARG;

    op = cJSON_Parse(signed_op_json);
    if (!op || !cJSON_IsObject(op)) {
        cJSON_Delete(op);
        return WF_ERR_PARSE;
    }

    body = cJSON_CreateObject();
    if (!body) {
        cJSON_Delete(op);
        return WF_ERR_ALLOC;
    }
    cJSON_AddItemToObject(body, "operation", op); /* transfers ownership */

    json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return WF_ERR_ALLOC;

    status = wf_xrpc_procedure(client, "com.atproto.identity.submitPlcOperation",
                               json, &response);
    free(json);
    wf_response_free(&response);
    return status;
}

wf_status wf_plc_request_signature(wf_xrpc_client *client, const char *did) {
    wf_response response = {0};
    cJSON *body = NULL;
    char *json = NULL;
    wf_status status;

    if (!client || !did || did[0] == '\0') return WF_ERR_INVALID_ARG;

    body = cJSON_CreateObject();
    if (!body) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(body, "did", did)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }
    json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return WF_ERR_ALLOC;

    status = wf_xrpc_procedure(client,
                               "com.atproto.identity.requestPlcOperationSignature",
                               json, &response);
    free(json);
    wf_response_free(&response);
    return status;
}

wf_status wf_plc_update_handle(wf_xrpc_client *client, const char *handle) {
    wf_response response = {0};
    cJSON *body = NULL;
    char *json = NULL;
    wf_status status;

    if (!client || !handle || handle[0] == '\0') return WF_ERR_INVALID_ARG;

    body = cJSON_CreateObject();
    if (!body) return WF_ERR_ALLOC;
    if (!cJSON_AddStringToObject(body, "handle", handle)) {
        cJSON_Delete(body);
        return WF_ERR_ALLOC;
    }
    json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return WF_ERR_ALLOC;

    status = wf_xrpc_procedure(client, "com.atproto.identity.updateHandle",
                               json, &response);
    free(json);
    wf_response_free(&response);
    return status;
}

/* ── PLC DID computation and raw directory submission ─────────────────── */

static void wf_plc_base32_encode(const unsigned char *in, size_t in_len,
                                 char *out) {
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz234567";
    size_t i = 0, o = 0;
    while (i < in_len) {
        uint64_t buf = 0;
        int bits = 0;
        for (int n = 0; n < 5 && i < in_len; n++, i++) {
            buf = (buf << 8) | in[i];
            bits += 8;
        }
        int need = (bits + 4) / 5;
        for (int c = 0; c < need; c++) {
            int shift = bits - 5;
            if (shift >= 0) {
                out[o++] = alphabet[(buf >> shift) & 0x1f];
            } else {
                out[o++] = alphabet[(buf << (-shift)) & 0x1f];
            }
            bits -= 5;
        }
    }
    out[o] = '\0';
}

wf_status wf_plc_operation_compute_did(const char *signed_op_json,
                                       char **out_did) {
    cJSON *root = NULL;
    unsigned char *cbor = NULL;
    size_t cbor_len = 0;
    unsigned char hash[SHA256_DIGEST_LENGTH];
    char b32[54];
    char *did = NULL;

    WF_LOG_DEBUG("plc", "wf_plc_operation_compute_did: enter");

    if (!signed_op_json || !out_did) {
        WF_LOG_ERROR("plc", "wf_plc_operation_compute_did: invalid args");
        return WF_ERR_INVALID_ARG;
    }
    *out_did = NULL;

    WF_LOG_DEBUG("plc", "wf_plc_operation_compute_did: parsing JSON (len=%zu)", strlen(signed_op_json));
    root = cJSON_Parse(signed_op_json);
    if (!root || !cJSON_IsObject(root)) {
        WF_LOG_ERROR("plc", "wf_plc_operation_compute_did: failed to parse JSON");
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    WF_LOG_DEBUG("plc", "wf_plc_operation_compute_did: computing canonical CBOR");
    if (wf_plc_canonical_cbor(root, NULL, &cbor, &cbor_len) != WF_OK) {
        WF_LOG_ERROR("plc", "wf_plc_operation_compute_did: CBOR encoding failed");
        cJSON_Delete(root);
        return WF_ERR_INTERNAL;
    }
    cJSON_Delete(root);

    WF_LOG_DEBUG("plc", "wf_plc_operation_compute_did: CBOR length=%zu", cbor_len);
    SHA256(cbor, cbor_len, hash);
    free(cbor);

    wf_plc_base32_encode(hash, sizeof(hash), b32);
    WF_LOG_DEBUG("plc", "wf_plc_operation_compute_did: base32 hash=%.24s", b32);

    did = malloc(32);
    if (!did) {
        WF_LOG_ERROR("plc", "wf_plc_operation_compute_did: allocation failed");
        return WF_ERR_ALLOC;
    }
    snprintf(did, 32, "did:plc:%.24s", b32);

    WF_LOG_DEBUG("plc", "wf_plc_operation_compute_did: success, did=%s", did);
    *out_did = did;
    return WF_OK;
}

wf_status wf_plc_submit_operation_raw(const char *plc_directory_url,
                                      const char *did,
                                      const char *signed_op_json) {
    wf_xrpc_client *client = NULL;
    wf_response response = {0};
    char operation_url[1024];
    wf_status status;

    WF_LOG_DEBUG("plc", "wf_plc_submit_operation_raw: enter");
    WF_LOG_DEBUG("plc", "wf_plc_submit_operation_raw: plc_url=%s, did=%s", plc_directory_url ? plc_directory_url : "(null)", did ? did : "(null)");

    if (!plc_directory_url || !did || !signed_op_json) {
        WF_LOG_ERROR("plc", "wf_plc_submit_operation_raw: invalid args");
        return WF_ERR_INVALID_ARG;
    }

    /* Build URL: plc_directory_url/<did> */
    size_t base_len = strlen(plc_directory_url);
    size_t did_len = strlen(did);
    if (base_len + 1 + did_len + 1 >= sizeof(operation_url)) {
        WF_LOG_ERROR("plc", "wf_plc_submit_operation_raw: URL too long");
        return WF_ERR_INVALID_ARG;
    }
    memcpy(operation_url, plc_directory_url, base_len);
    operation_url[base_len] = '/';
    memcpy(operation_url + base_len + 1, did, did_len + 1);

    WF_LOG_DEBUG("plc", "wf_plc_submit_operation_raw: operation URL=%s", operation_url);
    WF_LOG_DEBUG("plc", "wf_plc_submit_operation_raw: signed_op_json: %s", signed_op_json);

    client = wf_xrpc_client_new(operation_url);
    if (!client) {
        WF_LOG_ERROR("plc", "wf_plc_submit_operation_raw: failed to create HTTP client");
        return WF_ERR_ALLOC;
    }

    WF_LOG_DEBUG("plc", "wf_plc_submit_operation_raw: sending POST request");
    status = wf_http_post(client, operation_url,
                          "application/json", signed_op_json,
                          NULL, 0, &response);

    WF_LOG_DEBUG("plc", "wf_plc_submit_operation_raw: POST status=%d, response code=%ld", status, response.status);
    if (response.body && response.body_len > 0) {
        WF_LOG_DEBUG("plc", "wf_plc_submit_operation_raw: response body: %.*s", (int)(response.body_len > 1024 ? 1024 : response.body_len), response.body);
    }

    wf_xrpc_client_free(client);
    if (status == WF_OK && response.body) {
        free(response.body);
    }
    if (status != WF_OK) {
        WF_LOG_ERROR("plc", "wf_plc_submit_operation_raw: failed with status=%d", status);
    } else {
        WF_LOG_DEBUG("plc", "wf_plc_submit_operation_raw: success");
    }
    return status;
}
