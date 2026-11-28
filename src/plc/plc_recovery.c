/**
 * plc_recovery.c — pre-registered did:plc rotation keys.
 *
 * See include/wolfram/plc_recovery.h for the contract. Secrets are never
 * logged; every wf_signing_key copy made here is wiped before it goes out
 * of scope.
 */

#include "wolfram/plc_recovery.h"

#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include "wolfram/plc.h"
#include "wolfram/util.h"

/* ── keys ───────────────────────────────────────────────────── */

wf_status wf_plc_recovery_key_generate(wf_plc_recovery_scheme scheme,
                                       wf_key_type type,
                                       wf_plc_recovery_key *out) {
    if (!out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (scheme != WF_PLC_RECOVERY_SCHEME_ROTATION_KEY)
        return WF_ERR_UNSUPPORTED;
    wf_status s = wf_signing_key_generate(type, &out->signing);
    if (s != WF_OK) {
        wf_plc_recovery_key_clear(out);
        return s;
    }
    out->scheme = scheme;
    return WF_OK;
}

wf_status wf_plc_recovery_key_encode(const wf_plc_recovery_key *key,
                                     char **out_secret) {
    if (!out_secret) return WF_ERR_INVALID_ARG;
    *out_secret = NULL;
    if (!key) return WF_ERR_INVALID_ARG;
    if (key->scheme != WF_PLC_RECOVERY_SCHEME_ROTATION_KEY)
        return WF_ERR_UNSUPPORTED;
    return wf_signing_key_to_multibase(&key->signing, out_secret);
}

wf_status wf_plc_recovery_key_public(const wf_plc_recovery_key *key,
                                     char **out_didkey) {
    if (!out_didkey) return WF_ERR_INVALID_ARG;
    *out_didkey = NULL;
    if (!key) return WF_ERR_INVALID_ARG;
    if (key->scheme != WF_PLC_RECOVERY_SCHEME_ROTATION_KEY)
        return WF_ERR_UNSUPPORTED;
    return wf_signing_key_public_didkey(&key->signing, out_didkey);
}

void wf_plc_recovery_key_clear(wf_plc_recovery_key *key) {
    if (key) wf_secure_zero(key, sizeof(*key));
}

/* 1 when both did:keys name the same curve and point. Falls back to a
 * string comparison when either does not decode. */
static int plc_didkey_same(const char *a, const char *b) {
    wf_key_type ta = WF_KEY_TYPE_UNKNOWN, tb = WF_KEY_TYPE_UNKNOWN;
    unsigned char *ra = NULL, *rb = NULL;
    size_t la = 0, lb = 0;
    int same;
    if (wf_didkey_decode(a, &ta, &ra, &la) != WF_OK ||
        wf_didkey_decode(b, &tb, &rb, &lb) != WF_OK) {
        free(ra);
        free(rb);
        return strcmp(a, b) == 0;
    }
    same = ta == tb && la == lb && memcmp(ra, rb, la) == 0;
    free(ra);
    free(rb);
    return same;
}

/* 1 when `didkey` decodes as a secp256k1 or P-256 did:key. */
static int plc_didkey_usable(const char *didkey) {
    wf_key_type t = WF_KEY_TYPE_UNKNOWN;
    unsigned char *raw = NULL;
    size_t len = 0;
    if (!didkey || strncmp(didkey, "did:key:z", 9) != 0) return 0;
    if (wf_didkey_decode(didkey, &t, &raw, &len) != WF_OK) return 0;
    free(raw);
    return t == WF_KEY_TYPE_SECP256K1 || t == WF_KEY_TYPE_P256;
}

wf_status wf_plc_recovery_key_decode(const char *secret,
                                     const char *expected_didkey,
                                     wf_plc_recovery_key *out) {
    if (!out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (!secret) return WF_ERR_INVALID_ARG;

    wf_plc_recovery_key key;
    memset(&key, 0, sizeof(key));
    wf_status s = wf_signing_key_from_multibase(secret, &key.signing);
    if (s != WF_OK) return s;
    key.scheme = WF_PLC_RECOVERY_SCHEME_ROTATION_KEY;

    if (expected_didkey) {
        char *didkey = NULL;
        s = wf_signing_key_public_didkey(&key.signing, &didkey);
        if (s == WF_OK && !plc_didkey_same(didkey, expected_didkey))
            s = WF_ERR_VALIDATION;
        free(didkey);
        if (s != WF_OK) {
            wf_plc_recovery_key_clear(&key);
            return s;
        }
    }

    *out = key;
    wf_plc_recovery_key_clear(&key);
    return WF_OK;
}

/* ── registration ───────────────────────────────────────────── */

/* The operation's type, or a status for one that cannot carry rotation
 * keys this module understands. */
static wf_status plc_require_operation(const cJSON *op) {
    if (!op || !cJSON_IsObject(op)) return WF_ERR_PARSE;
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(op, "type");
    if (!cJSON_IsString(type)) return WF_ERR_PARSE;
    if (strcmp(type->valuestring, "plc_operation") != 0)
        return WF_ERR_UNSUPPORTED; /* plc_tombstone, legacy create */
    const cJSON *keys = cJSON_GetObjectItemCaseSensitive(op, "rotationKeys");
    if (!cJSON_IsArray(keys)) return WF_ERR_PARSE;
    return WF_OK;
}

static int plc_index_in(const cJSON *keys, const char *didkey) {
    int i = 0;
    const cJSON *k = NULL;
    cJSON_ArrayForEach(k, keys) {
        if (cJSON_IsString(k) && plc_didkey_same(k->valuestring, didkey))
            return i;
        i++;
    }
    return -1;
}

wf_status wf_plc_rotation_key_index(const char *op_json, const char *didkey,
                                    int *out_index) {
    if (!out_index) return WF_ERR_INVALID_ARG;
    *out_index = -1;
    if (!op_json || !plc_didkey_usable(didkey)) return WF_ERR_INVALID_ARG;

    cJSON *op = cJSON_Parse(op_json);
    wf_status s = plc_require_operation(op);
    if (s == WF_OK)
        *out_index = plc_index_in(
            cJSON_GetObjectItemCaseSensitive(op, "rotationKeys"), didkey);
    cJSON_Delete(op);
    return s;
}

wf_status wf_plc_rotation_key_check(wf_xrpc_client *client,
                                    const char *plc_directory_url,
                                    const char *did, const char *didkey,
                                    int *out_index) {
    if (!out_index) return WF_ERR_INVALID_ARG;
    *out_index = -1;
    if (!client || !plc_directory_url || !did || !plc_didkey_usable(didkey))
        return WF_ERR_INVALID_ARG;

    char *cid = NULL, *op_json = NULL;
    wf_status s =
        wf_plc_get_last_op(client, plc_directory_url, did, &cid, &op_json);
    if (s == WF_OK) s = wf_plc_rotation_key_index(op_json, didkey, out_index);
    free(cid);
    free(op_json);
    return s;
}

/* ── update ─────────────────────────────────────────────────── */

void wf_plc_rotation_keys_free(wf_plc_rotation_keys *keys) {
    if (!keys) return;
    for (size_t i = 0; i < keys->count; i++) free(keys->keys[i]);
    free(keys->keys);
    keys->keys = NULL;
    keys->count = 0;
}

static wf_status plc_apply_to(const cJSON *prev,
                              const wf_plc_rotation_key_change *change,
                              wf_plc_rotation_keys *out) {
    out->keys = NULL;
    out->count = 0;
    if (!change || !plc_didkey_usable(change->add_didkey))
        return WF_ERR_INVALID_ARG;
    if (change->replace_didkey && !change->replace_didkey[0])
        return WF_ERR_INVALID_ARG;

    wf_status s = plc_require_operation(prev);
    if (s != WF_OK) return s;
    const cJSON *keys = cJSON_GetObjectItemCaseSensitive(prev, "rotationKeys");
    size_t n = (size_t)cJSON_GetArraySize(keys);

    int existing = plc_index_in(keys, change->add_didkey);
    int replace_at = -1;
    if (change->replace_didkey) {
        replace_at = plc_index_in(keys, change->replace_didkey);
        if (replace_at < 0) return WF_ERR_NOT_FOUND;
        if (existing >= 0) return WF_ERR_DUPLICATE;
    } else {
        if (existing >= 0) return WF_ERR_DUPLICATE;
        if (change->position > n) return WF_ERR_INVALID_ARG;
    }

    size_t count = change->replace_didkey ? n : n + 1;
    if (count > WF_PLC_MAX_ROTATION_KEYS) return WF_ERR_VALIDATION;

    char **list = calloc(count, sizeof(*list));
    if (!list) return WF_ERR_ALLOC;

    size_t o = 0, i = 0;
    const cJSON *k = NULL;
    s = WF_OK;
    cJSON_ArrayForEach(k, keys) {
        if (!cJSON_IsString(k)) {
            s = WF_ERR_PARSE;
            break;
        }
        if (!change->replace_didkey && i == change->position)
            list[o++] = wf_str_dup(change->add_didkey);
        if ((int)i == replace_at)
            list[o++] = wf_str_dup(change->add_didkey);
        else
            list[o++] = wf_str_dup(k->valuestring);
        i++;
    }
    if (s == WF_OK && !change->replace_didkey && change->position == n)
        list[o++] = wf_str_dup(change->add_didkey);

    for (size_t j = 0; s == WF_OK && j < o; j++)
        if (!list[j]) s = WF_ERR_ALLOC;
    if (s != WF_OK) {
        for (size_t j = 0; j < o; j++) free(list[j]);
        free(list);
        return s;
    }
    out->keys = list;
    out->count = o;
    return WF_OK;
}

wf_status wf_plc_rotation_keys_apply(const char *prev_op_json,
                                     const wf_plc_rotation_key_change *change,
                                     wf_plc_rotation_keys *out) {
    if (!out) return WF_ERR_INVALID_ARG;
    out->keys = NULL;
    out->count = 0;
    if (!prev_op_json || !change) return WF_ERR_INVALID_ARG;
    cJSON *prev = cJSON_Parse(prev_op_json);
    wf_status s = plc_apply_to(prev, change, out);
    cJSON_Delete(prev);
    return s;
}

wf_status
wf_plc_rotation_key_update_build(const char *prev_op_json, const char *prev_cid,
                                 const wf_plc_rotation_key_change *change,
                                 char **out_unsigned_op_json) {
    if (!out_unsigned_op_json) return WF_ERR_INVALID_ARG;
    *out_unsigned_op_json = NULL;
    if (!prev_op_json || !prev_cid || !prev_cid[0] || !change)
        return WF_ERR_INVALID_ARG;

    cJSON *op = cJSON_Parse(prev_op_json);
    wf_plc_rotation_keys keys = {0};
    wf_status s = plc_apply_to(op, change, &keys);
    if (s != WF_OK) {
        cJSON_Delete(op);
        return s;
    }

    /* Copy every field forward: drop only the old signature and replace
     * prev and rotationKeys. */
    cJSON_DeleteItemFromObjectCaseSensitive(op, "sig");
    cJSON *arr = cJSON_CreateStringArray((const char *const *)keys.keys,
                                         (int)keys.count);
    cJSON *prev = cJSON_CreateString(prev_cid);
    wf_plc_rotation_keys_free(&keys);
    if (!arr || !prev ||
        !cJSON_ReplaceItemInObjectCaseSensitive(op, "rotationKeys", arr)) {
        cJSON_Delete(arr);
        cJSON_Delete(prev);
        cJSON_Delete(op);
        return WF_ERR_ALLOC;
    }
    if (cJSON_GetObjectItemCaseSensitive(op, "prev")) {
        if (!cJSON_ReplaceItemInObjectCaseSensitive(op, "prev", prev)) {
            cJSON_Delete(prev);
            cJSON_Delete(op);
            return WF_ERR_ALLOC;
        }
    } else if (!cJSON_AddItemToObject(op, "prev", prev)) {
        cJSON_Delete(prev);
        cJSON_Delete(op);
        return WF_ERR_ALLOC;
    }

    char *json = cJSON_PrintUnformatted(op);
    cJSON_Delete(op);
    if (!json) return WF_ERR_ALLOC;
    *out_unsigned_op_json = json;
    return WF_OK;
}

/* 1 when `keys` is 1..WF_PLC_MAX_ROTATION_KEYS usable, distinct did:keys. */
static int plc_rotation_keys_valid(const cJSON *keys) {
    if (!cJSON_IsArray(keys)) return 0;
    int n = cJSON_GetArraySize(keys);
    if (n < 1 || n > WF_PLC_MAX_ROTATION_KEYS) return 0;
    for (int i = 0; i < n; i++) {
        const cJSON *a = cJSON_GetArrayItem(keys, i);
        if (!cJSON_IsString(a) || !plc_didkey_usable(a->valuestring)) return 0;
        for (int j = 0; j < i; j++) {
            const cJSON *b = cJSON_GetArrayItem(keys, j);
            if (plc_didkey_same(a->valuestring, b->valuestring)) return 0;
        }
    }
    return 1;
}

/* 1 when every field but rotationKeys, prev and sig is the same in both. */
static int plc_rest_unchanged(const cJSON *prev, const cJSON *next) {
    static const char *const skip[] = {"rotationKeys", "prev", "sig"};
    const cJSON *pair[2][2] = {{prev, next}, {next, prev}};
    for (int dir = 0; dir < 2; dir++) {
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, pair[dir][0]) {
            int skipped = 0;
            for (size_t k = 0; k < sizeof(skip) / sizeof(skip[0]); k++)
                if (strcmp(item->string, skip[k]) == 0) skipped = 1;
            if (skipped) continue;
            const cJSON *other =
                cJSON_GetObjectItemCaseSensitive(pair[dir][1], item->string);
            if (!other || !cJSON_Compare(item, other, 1)) return 0;
        }
    }
    return 1;
}

wf_status wf_plc_rotation_key_update_validate(
    const char *prev_op_json, const char *prev_cid, const char *signed_op_json,
    const wf_plc_rotation_key_change *expected, char **out_signer_didkey) {
    if (out_signer_didkey) *out_signer_didkey = NULL;
    if (!prev_op_json || !prev_cid || !signed_op_json)
        return WF_ERR_INVALID_ARG;

    cJSON *prev = cJSON_Parse(prev_op_json);
    cJSON *next = cJSON_Parse(signed_op_json);
    wf_plc_rotation_keys want = {0};
    char *signer = NULL;
    wf_status s = plc_require_operation(prev);
    if (s != WF_OK) goto done;

    if (!next || !cJSON_IsObject(next)) {
        s = WF_ERR_PARSE;
        goto done;
    }
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(next, "type");
    const cJSON *link = cJSON_GetObjectItemCaseSensitive(next, "prev");
    const cJSON *keys = cJSON_GetObjectItemCaseSensitive(next, "rotationKeys");
    if (!cJSON_IsString(type) ||
        strcmp(type->valuestring, "plc_operation") != 0 ||
        !cJSON_IsString(link) || strcmp(link->valuestring, prev_cid) != 0 ||
        !plc_rotation_keys_valid(keys) || !plc_rest_unchanged(prev, next)) {
        s = WF_ERR_VALIDATION;
        goto done;
    }

    if (expected) {
        s = plc_apply_to(prev, expected, &want);
        if (s != WF_OK) goto done;
        if ((size_t)cJSON_GetArraySize(keys) != want.count) {
            s = WF_ERR_VALIDATION;
            goto done;
        }
        for (size_t i = 0; i < want.count; i++) {
            const cJSON *k = cJSON_GetArrayItem(keys, (int)i);
            if (!plc_didkey_same(k->valuestring, want.keys[i])) {
                s = WF_ERR_VALIDATION;
                goto done;
            }
        }
    }

    s = wf_plc_operation_verify_with_keys(signed_op_json, prev_op_json,
                                          &signer);
    if (s == WF_ERR_INVALID_ARG) s = WF_ERR_CRYPTO;
    if (s == WF_OK && out_signer_didkey) {
        *out_signer_didkey = signer;
        signer = NULL;
    }

done:
    free(signer);
    wf_plc_rotation_keys_free(&want);
    cJSON_Delete(prev);
    cJSON_Delete(next);
    return s;
}

wf_status wf_plc_build_rotation_key_update(
    wf_xrpc_client *client, const char *plc_directory_url, const char *did,
    const wf_plc_rotation_key_change *change, const wf_signing_key *signer,
    char **out_signed_op_json) {
    if (!out_signed_op_json) return WF_ERR_INVALID_ARG;
    *out_signed_op_json = NULL;
    if (!client || !plc_directory_url || !did || !change || !signer)
        return WF_ERR_INVALID_ARG;

    char *cid = NULL, *prev_json = NULL, *unsigned_op = NULL, *signed_op = NULL;
    wf_status s =
        wf_plc_get_last_op(client, plc_directory_url, did, &cid, &prev_json);
    if (s == WF_OK)
        s = wf_plc_rotation_key_update_build(prev_json, cid, change,
                                             &unsigned_op);
    if (s == WF_OK) s = wf_plc_operation_sign(unsigned_op, signer, &signed_op);
    if (s == WF_OK)
        s = wf_plc_rotation_key_update_validate(prev_json, cid, signed_op,
                                                change, NULL);
    if (s == WF_OK) {
        *out_signed_op_json = signed_op;
        signed_op = NULL;
    }
    wf_plc_operation_free(signed_op);
    wf_plc_operation_free(unsigned_op);
    free(prev_json);
    free(cid);
    return s;
}
