/**
 * test_plc_recovery.c — pre-registered did:plc rotation keys.
 *
 * Offline. The directory is replaced by wf_xrpc_set_handler. Recorded
 * public operations (test/vectors/plc/rotation_keys.json, from
 * plc.directory) check the CID, index and signature-authority logic
 * against what the real directory accepted; freshly generated keys check
 * the build/sign/validate path. No real key material is in this file.
 */

#include "wolfram/plc_recovery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include "wolfram/crypto.h"
#include "wolfram/plc.h"
#include "wolfram/xrpc.h"
#include "test.h"

#ifndef WF_TEST_VECTOR_DIR
#error "WF_TEST_VECTOR_DIR must be defined"
#endif

static const char B58[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    if (buf) buf[n] = '\0';
    fclose(f);
    return buf;
}

/* ── canned directory ───────────────────────────────────────── */

static const char *g_last_op;

static wf_status last_op_handler(void *userdata, const char *method,
                                 const char *url, const char *content_type,
                                 const char *body, size_t body_len,
                                 const wf_http_header *headers,
                                 size_t header_count, wf_response *out) {
    (void)userdata;
    (void)method;
    (void)url;
    (void)content_type;
    (void)body;
    (void)body_len;
    (void)headers;
    (void)header_count;
    size_t len = strlen(g_last_op);
    out->body = malloc(len + 1);
    if (!out->body) return WF_ERR_ALLOC;
    memcpy(out->body, g_last_op, len + 1);
    out->body_len = len;
    out->status = 200;
    return WF_OK;
}

static wf_xrpc_client *canned_client(const char *op_json) {
    wf_xrpc_client *c = wf_xrpc_client_new("https://plc.example.invalid");
    if (c) {
        g_last_op = op_json;
        wf_xrpc_set_handler(c, last_op_handler, NULL);
    }
    return c;
}

/* ── recorded public operations ─────────────────────────────── */

static void test_recorded(void) {
    char *raw = read_file(WF_TEST_VECTOR_DIR "/plc/rotation_keys.json");
    WF_CHECK(raw != NULL);
    if (!raw) return;
    cJSON *root = cJSON_Parse(raw);
    free(raw);
    cJSON *entries = cJSON_GetObjectItemCaseSensitive(root, "entries");
    WF_CHECK(cJSON_GetArraySize(entries) == 3);
    if (cJSON_GetArraySize(entries) != 3) {
        cJSON_Delete(root);
        return;
    }
    char *op[3];
    const char *cid[3];
    for (int i = 0; i < 3; i++) {
        cJSON *e = cJSON_GetArrayItem(entries, i);
        op[i] = cJSON_PrintUnformatted(
            cJSON_GetObjectItemCaseSensitive(e, "operation"));
        cid[i] = cJSON_GetObjectItemCaseSensitive(e, "cid")->valuestring;
    }
    const char *recovery_p256 =
        "did:key:zDnaemZN1taothXb3MVNZhALxzsu3YzAo8c7axZEpaVGhqzkZ";
    const char *old_first =
        "did:key:zQ3shhCGUqDKjStzuDxPkTxN6ujddP4RkEKJJouJGRRkaLGbg";

    /* The CID wf_plc_get_last_op derives is the one the directory lists. */
    for (int i = 0; i < 3; i++) {
        wf_xrpc_client *c = canned_client(op[i]);
        char *got_cid = NULL, *got_op = NULL;
        WF_CHECK(wf_plc_get_last_op(c, "https://plc.example.invalid",
                                    "did:plc:44ybard66vv44zksje25o7dz",
                                    &got_cid, &got_op) == WF_OK);
        WF_CHECK(got_cid && strcmp(got_cid, cid[i]) == 0);
        free(got_cid);
        free(got_op);
        wf_xrpc_client_free(c);
    }

    /* Registration: absent before, first after. */
    int idx = 7;
    WF_CHECK(wf_plc_rotation_key_index(op[0], recovery_p256, &idx) == WF_OK);
    WF_CHECK(idx == -1);
    WF_CHECK(wf_plc_rotation_key_index(op[1], recovery_p256, &idx) == WF_OK);
    WF_CHECK(idx == 0);
    WF_CHECK(wf_plc_rotation_key_index(op[1], old_first, &idx) == WF_OK);
    WF_CHECK(idx == 1);
    {
        wf_xrpc_client *c = canned_client(op[1]);
        WF_CHECK(wf_plc_rotation_key_check(c, "https://plc.example.invalid",
                                           "did:plc:44ybard66vv44zksje25o7dz",
                                           recovery_p256, &idx) == WF_OK);
        WF_CHECK(idx == 0);
        wf_xrpc_client_free(c);
    }

    /* Applying "insert at 0" to the earlier operation gives exactly the
     * rotationKeys the directory accepted next. */
    wf_plc_rotation_key_change first = {recovery_p256, NULL, 0};
    wf_plc_rotation_keys keys = {0};
    WF_CHECK(wf_plc_rotation_keys_apply(op[0], &first, &keys) == WF_OK);
    {
        cJSON *next = cJSON_Parse(op[1]);
        cJSON *rk = cJSON_GetObjectItemCaseSensitive(next, "rotationKeys");
        WF_CHECK((size_t)cJSON_GetArraySize(rk) == keys.count);
        for (size_t i = 0; i < keys.count; i++)
            WF_CHECK(strcmp(cJSON_GetArrayItem(rk, (int)i)->valuestring,
                            keys.keys[i]) == 0);
        cJSON_Delete(next);
    }
    wf_plc_rotation_keys_free(&keys);

    /* That operation also added a verification method, so as a pure
     * rotation-key update it is refused. */
    WF_CHECK(wf_plc_rotation_key_update_validate(op[0], cid[0], op[1], &first,
                                                 NULL) == WF_ERR_VALIDATION);
    WF_CHECK(wf_plc_rotation_key_update_validate(op[1], cid[1], op[2], NULL,
                                                 NULL) == WF_ERR_VALIDATION);

#ifdef HAVE_LIBSECP256K1
    /* The directory accepted it, so a key of the previous operation signed
     * it; the new P-256 key did not. */
    char *signer = NULL;
    WF_CHECK(wf_plc_operation_verify_with_keys(op[1], op[0], &signer) == WF_OK);
    WF_CHECK(signer != NULL && strcmp(signer, recovery_p256) != 0);
    idx = -1;
    if (signer) wf_plc_rotation_key_index(op[0], signer, &idx);
    WF_CHECK(idx >= 0);
    free(signer);
    signer = NULL;
    WF_CHECK(wf_plc_operation_verify_with_keys(op[2], op[1], &signer) == WF_OK);
    free(signer);
#endif

    for (int i = 0; i < 3; i++) free(op[i]);
    cJSON_Delete(root);
}

/* ── generated keys ─────────────────────────────────────────── */

typedef struct fixture {
    wf_signing_key pds;
    char *pds_did;
    char *other_did;
    char *recovery_did;
    wf_plc_recovery_key recovery;
    char prev[2048];
    const char *prev_cid;
} fixture;

static char *new_didkey(void) {
    wf_signing_key k;
    char *d = NULL;
    if (wf_signing_key_generate(WF_KEY_TYPE_P256, &k) == WF_OK)
        wf_signing_key_public_didkey(&k, &d);
    wf_secure_zero(&k, sizeof(k));
    return d;
}

static int fixture_init(fixture *f) {
    memset(f, 0, sizeof(*f));
    if (wf_signing_key_generate(WF_KEY_TYPE_P256, &f->pds) != WF_OK ||
        wf_signing_key_public_didkey(&f->pds, &f->pds_did) != WF_OK)
        return 0;
    f->other_did = new_didkey();
    if (!f->other_did) return 0;
    if (wf_plc_recovery_key_generate(WF_PLC_RECOVERY_SCHEME_ROTATION_KEY,
                                     WF_KEY_TYPE_P256, &f->recovery) != WF_OK ||
        wf_plc_recovery_key_public(&f->recovery, &f->recovery_did) != WF_OK)
        return 0;
    /* An extra verification method stands in for anything a future scheme
     * (an atproto_kem entry, say) adds: it must survive the update. */
    snprintf(f->prev, sizeof(f->prev),
             "{\"type\":\"plc_operation\",\"rotationKeys\":[\"%s\",\"%s\"],"
             "\"verificationMethods\":{\"atproto\":\"%s\",\"example\":\"%s\"},"
             "\"services\":{\"atproto_pds\":{\"type\":"
             "\"AtprotoPersonalDataServer\",\"endpoint\":\"https://"
             "pds.example.invalid\"}},\"alsoKnownAs\":[\"at://alice.example."
             "invalid\"],\"prev\":\"bafyreiprevprevprev\",\"sig\":\"AAAA\"}",
             f->pds_did, f->other_did, f->other_did, f->pds_did);
    f->prev_cid = "bafyreigh2akiscaildcqabsyg3dfr6chu3fgpregiymsck7e7aqa4s52zy";
    return 1;
}

static void fixture_free(fixture *f) {
    wf_secure_zero(&f->pds, sizeof(f->pds));
    wf_plc_recovery_key_clear(&f->recovery);
    free(f->pds_did);
    free(f->other_did);
    free(f->recovery_did);
}

static char *sign_update(const fixture *f, const wf_plc_rotation_key_change *c,
                         const wf_signing_key *by) {
    char *u = NULL, *s = NULL;
    if (wf_plc_rotation_key_update_build(f->prev, f->prev_cid, c, &u) == WF_OK)
        wf_plc_operation_sign(u, by, &s);
    wf_plc_operation_free(u);
    return s;
}

static void test_update_insert_and_replace(void) {
    fixture f;
    WF_CHECK(fixture_init(&f));
    if (!f.recovery_did) {
        fixture_free(&f);
        return;
    }

    wf_plc_rotation_key_change first = {f.recovery_did, NULL, 0};
    char *signed_op = sign_update(&f, &first, &f.pds);
    WF_CHECK(signed_op != NULL);
    char *signer = NULL;
    WF_CHECK(wf_plc_rotation_key_update_validate(f.prev, f.prev_cid, signed_op,
                                                 &first, &signer) == WF_OK);
    WF_CHECK(signer && strcmp(signer, f.pds_did) == 0);
    free(signer);

    cJSON *prev = cJSON_Parse(f.prev);
    cJSON *next = cJSON_Parse(signed_op);
    cJSON *rk = cJSON_GetObjectItemCaseSensitive(next, "rotationKeys");
    WF_CHECK(cJSON_GetArraySize(rk) == 3);
    WF_CHECK(strcmp(cJSON_GetArrayItem(rk, 0)->valuestring, f.recovery_did) ==
             0);
    WF_CHECK(strcmp(cJSON_GetArrayItem(rk, 1)->valuestring, f.pds_did) == 0);
    WF_CHECK(strcmp(cJSON_GetArrayItem(rk, 2)->valuestring, f.other_did) == 0);
    WF_CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(next, "prev")->valuestring,
                    f.prev_cid) == 0);
    const char *same[] = {"verificationMethods", "services", "alsoKnownAs",
                          "type"};
    for (size_t i = 0; i < sizeof(same) / sizeof(same[0]); i++)
        WF_CHECK(cJSON_Compare(cJSON_GetObjectItemCaseSensitive(prev, same[i]),
                               cJSON_GetObjectItemCaseSensitive(next, same[i]),
                               1));
    WF_CHECK(strcmp(cJSON_GetObjectItemCaseSensitive(next, "sig")->valuestring,
                    "AAAA") != 0);
    cJSON_Delete(prev);

    /* The new key is now registered first. */
    int idx = -1;
    WF_CHECK(wf_plc_rotation_key_index(signed_op, f.recovery_did, &idx) ==
             WF_OK);
    WF_CHECK(idx == 0);

    /* Validated against a different expected change, it is refused. */
    wf_plc_rotation_key_change last = {f.recovery_did, NULL, 2};
    WF_CHECK(wf_plc_rotation_key_update_validate(f.prev, f.prev_cid, signed_op,
                                                 &last,
                                                 NULL) == WF_ERR_VALIDATION);
    /* Against the wrong prev CID, refused. */
    WF_CHECK(wf_plc_rotation_key_update_validate(
                 f.prev, "bafyreiwrongwrongwrong", signed_op, &first, NULL) ==
             WF_ERR_VALIDATION);

    /* Tampering after signing: the shape check fires first for a changed
     * field, the signature check for a changed key order. */
    cJSON_ReplaceItemInObjectCaseSensitive(
        next, "alsoKnownAs",
        cJSON_CreateStringArray((const char *[]){"at://mallory.invalid"}, 1));
    char *tampered = cJSON_PrintUnformatted(next);
    WF_CHECK(wf_plc_rotation_key_update_validate(f.prev, f.prev_cid, tampered,
                                                 NULL,
                                                 NULL) == WF_ERR_VALIDATION);
    free(tampered);
    cJSON_Delete(next);
    next = cJSON_Parse(signed_op);
    rk = cJSON_GetObjectItemCaseSensitive(next, "rotationKeys");
    cJSON *moved = cJSON_DetachItemFromArray(rk, 0);
    cJSON_AddItemToArray(rk, moved);
    tampered = cJSON_PrintUnformatted(next);
    WF_CHECK(wf_plc_rotation_key_update_validate(f.prev, f.prev_cid, tampered,
                                                 NULL, NULL) == WF_ERR_CRYPTO);
    free(tampered);
    /* A duplicated key is refused before the signature is looked at. */
    cJSON_AddItemToArray(rk, cJSON_CreateString(f.pds_did));
    tampered = cJSON_PrintUnformatted(next);
    WF_CHECK(wf_plc_rotation_key_update_validate(f.prev, f.prev_cid, tampered,
                                                 NULL,
                                                 NULL) == WF_ERR_VALIDATION);
    free(tampered);
    cJSON_Delete(next);
    wf_plc_operation_free(signed_op);

    /* Signed by the key being added, which the previous op does not list:
     * the update cannot vouch for itself. */
    signed_op = sign_update(&f, &first, &f.recovery.signing);
    WF_CHECK(signed_op != NULL);
    WF_CHECK(wf_plc_rotation_key_update_validate(
                 f.prev, f.prev_cid, signed_op, &first, NULL) == WF_ERR_CRYPTO);
    wf_plc_operation_free(signed_op);

    /* Replace keeps the replaced key's priority. */
    wf_plc_rotation_key_change swap = {f.recovery_did, f.other_did, 0};
    signed_op = sign_update(&f, &swap, &f.pds);
    WF_CHECK(wf_plc_rotation_key_update_validate(f.prev, f.prev_cid, signed_op,
                                                 &swap, NULL) == WF_OK);
    WF_CHECK(wf_plc_rotation_key_index(signed_op, f.recovery_did, &idx) ==
             WF_OK);
    WF_CHECK(idx == 1);
    WF_CHECK(wf_plc_rotation_key_index(signed_op, f.other_did, &idx) == WF_OK);
    WF_CHECK(idx == -1);
    wf_plc_operation_free(signed_op);

    fixture_free(&f);
}

static void test_apply_errors(void) {
    fixture f;
    WF_CHECK(fixture_init(&f));
    if (!f.recovery_did) {
        fixture_free(&f);
        return;
    }
    wf_plc_rotation_keys keys = {0};
    wf_plc_rotation_key_change c = {f.pds_did, NULL, 0};
    WF_CHECK(wf_plc_rotation_keys_apply(f.prev, &c, &keys) == WF_ERR_DUPLICATE);
    c.add_didkey = f.recovery_did;
    c.position = 3;
    WF_CHECK(wf_plc_rotation_keys_apply(f.prev, &c, &keys) ==
             WF_ERR_INVALID_ARG);
    c.position = 2; /* append is allowed */
    WF_CHECK(wf_plc_rotation_keys_apply(f.prev, &c, &keys) == WF_OK);
    WF_CHECK(keys.count == 3 && strcmp(keys.keys[2], f.recovery_did) == 0);
    wf_plc_rotation_keys_free(&keys);
    WF_CHECK(keys.keys == NULL && keys.count == 0);

    c.replace_didkey = f.recovery_did; /* not registered */
    c.add_didkey = f.other_did;
    WF_CHECK(wf_plc_rotation_keys_apply(f.prev, &c, &keys) == WF_ERR_NOT_FOUND);
    c.replace_didkey = f.pds_did; /* the new key is already present */
    WF_CHECK(wf_plc_rotation_keys_apply(f.prev, &c, &keys) == WF_ERR_DUPLICATE);

    c.replace_didkey = NULL;
    c.position = 0;
    c.add_didkey = "did:key:zNotAKey";
    WF_CHECK(wf_plc_rotation_keys_apply(f.prev, &c, &keys) ==
             WF_ERR_INVALID_ARG);
    c.add_didkey = "did:web:example.invalid";
    WF_CHECK(wf_plc_rotation_keys_apply(f.prev, &c, &keys) ==
             WF_ERR_INVALID_ARG);
    /* An ed25519 did:key is a did:key, but not a rotation key. */
    c.add_didkey = "did:key:z6MkhaXgBZDvotDkL5257faiztiGiC2QtKLGpbnnEGta2doK";
    WF_CHECK(wf_plc_rotation_keys_apply(f.prev, &c, &keys) ==
             WF_ERR_INVALID_ARG);

    /* Five keys is the spec's limit. */
    char *k[3] = {new_didkey(), new_didkey(), new_didkey()};
    char full[2048];
    snprintf(full, sizeof(full),
             "{\"type\":\"plc_operation\",\"rotationKeys\":[\"%s\",\"%s\","
             "\"%s\",\"%s\",\"%s\"]}",
             f.pds_did, f.other_did, k[0], k[1], k[2]);
    c.add_didkey = f.recovery_did;
    WF_CHECK(wf_plc_rotation_keys_apply(full, &c, &keys) == WF_ERR_VALIDATION);
    c.replace_didkey = k[2]; /* a replace at the limit is fine */
    WF_CHECK(wf_plc_rotation_keys_apply(full, &c, &keys) == WF_OK);
    WF_CHECK(keys.count == 5);
    wf_plc_rotation_keys_free(&keys);
    for (int i = 0; i < 3; i++) free(k[i]);
    c.replace_didkey = NULL;

    /* Operations that do not carry rotation keys this module handles. */
    int idx = 0;
    const char *tomb = "{\"type\":\"plc_tombstone\",\"prev\":\"bafy\"}";
    const char *legacy =
        "{\"type\":\"create\",\"signingKey\":\"did:key:zQ3sh\","
        "\"recoveryKey\":\"did:key:zQ3sh\",\"handle\":\"a\",\"service\":\"b\"}";
    WF_CHECK(wf_plc_rotation_keys_apply(tomb, &c, &keys) == WF_ERR_UNSUPPORTED);
    WF_CHECK(wf_plc_rotation_keys_apply(legacy, &c, &keys) ==
             WF_ERR_UNSUPPORTED);
    WF_CHECK(wf_plc_rotation_key_index(tomb, f.pds_did, &idx) ==
             WF_ERR_UNSUPPORTED);
    WF_CHECK(idx == -1);
    WF_CHECK(wf_plc_rotation_key_index("not json", f.pds_did, &idx) ==
             WF_ERR_PARSE);
    WF_CHECK(wf_plc_rotation_key_index(f.prev, "did:key:zNope", &idx) ==
             WF_ERR_INVALID_ARG);
    char *u = (char *)1;
    WF_CHECK(wf_plc_rotation_key_update_build(tomb, f.prev_cid, &c, &u) ==
             WF_ERR_UNSUPPORTED);
    WF_CHECK(u == NULL);
    WF_CHECK(wf_plc_rotation_key_update_build(f.prev, "", &c, &u) ==
             WF_ERR_INVALID_ARG);

    fixture_free(&f);
}

static void test_build_through_directory(void) {
    fixture f;
    WF_CHECK(fixture_init(&f));
    if (!f.recovery_did) {
        fixture_free(&f);
        return;
    }
    wf_xrpc_client *c = canned_client(f.prev);
    char *cid = NULL, *op = NULL;
    WF_CHECK(wf_plc_get_last_op(c, "https://plc.example.invalid",
                                "did:plc:example", &cid, &op) == WF_OK);

    wf_plc_rotation_key_change first = {f.recovery_did, NULL, 0};
    char *signed_op = NULL;
    WF_CHECK(wf_plc_build_rotation_key_update(c, "https://plc.example.invalid",
                                              "did:plc:example", &first, &f.pds,
                                              &signed_op) == WF_OK);
    WF_CHECK(signed_op != NULL);
    if (signed_op && cid)
        WF_CHECK(wf_plc_rotation_key_update_validate(op, cid, signed_op, &first,
                                                     NULL) == WF_OK);
    wf_plc_operation_free(signed_op);

    /* A signer that is not a current rotation key is caught here, before
     * anything reaches the directory. */
    signed_op = (char *)1;
    WF_CHECK(wf_plc_build_rotation_key_update(
                 c, "https://plc.example.invalid", "did:plc:example", &first,
                 &f.recovery.signing, &signed_op) == WF_ERR_CRYPTO);
    WF_CHECK(signed_op == NULL);

    int idx = 9;
    WF_CHECK(wf_plc_rotation_key_check(c, "https://plc.example.invalid",
                                       "did:plc:example", f.recovery_did,
                                       &idx) == WF_OK);
    WF_CHECK(idx == -1);
    WF_CHECK(wf_plc_rotation_key_check(c, "https://plc.example.invalid",
                                       "did:plc:example", f.other_did,
                                       &idx) == WF_OK);
    WF_CHECK(idx == 1);

    free(cid);
    free(op);
    wf_xrpc_client_free(c);
    fixture_free(&f);
}

/* ── recovery keys ──────────────────────────────────────────── */

static void test_recovery_key(wf_key_type type) {
    wf_plc_recovery_key key, back;
    char *secret = NULL, *didkey = NULL, *other = NULL;
    WF_CHECK(wf_plc_recovery_key_generate(WF_PLC_RECOVERY_SCHEME_ROTATION_KEY,
                                          type, &key) == WF_OK);
    WF_CHECK(key.scheme == WF_PLC_RECOVERY_SCHEME_ROTATION_KEY);
    WF_CHECK(wf_plc_recovery_key_encode(&key, &secret) == WF_OK);
    WF_CHECK(wf_plc_recovery_key_public(&key, &didkey) == WF_OK);
    if (!secret || !didkey) goto done;

    WF_CHECK(wf_plc_recovery_key_decode(secret, didkey, &back) == WF_OK);
    WF_CHECK(back.scheme == key.scheme);
    WF_CHECK(memcmp(&back.signing, &key.signing, sizeof(key.signing)) == 0);
    WF_CHECK(wf_plc_recovery_key_decode(secret, NULL, &back) == WF_OK);

    other = new_didkey();
    WF_CHECK(wf_plc_recovery_key_decode(secret, other, &back) ==
             WF_ERR_VALIDATION);
    WF_CHECK(back.scheme == WF_PLC_RECOVERY_SCHEME_UNKNOWN);

    /* Every single-character substitution, at every position, is refused
     * once the expected public key is known: as structural damage or as a
     * key that does not match. None is partially accepted. */
    size_t n = strlen(secret), accepted = 0, tried = 0;
    char *copy = malloc(n + 1);
    for (size_t i = 0; copy && i < n; i++) {
        for (const char *a = B58; *a; a += 7) {
            memcpy(copy, secret, n + 1);
            if (copy[i] == *a) continue;
            copy[i] = *a;
            tried++;
            wf_status s = wf_plc_recovery_key_decode(copy, didkey, &back);
            if (s == WF_OK) accepted++;
            if (s != WF_OK)
                WF_CHECK(back.scheme == 0 && back.signing.type == 0);
        }
    }
    WF_CHECK(tried > 100);
    WF_CHECK(accepted == 0);
    if (copy) wf_secure_zero(copy, n);
    free(copy);

done:
    wf_signing_key_secret_free(secret);
    free(didkey);
    free(other);
    wf_plc_recovery_key_clear(&key);
    wf_plc_recovery_key_clear(&back);
    WF_CHECK(key.signing.type == WF_KEY_TYPE_UNKNOWN);
}

static void test_recovery_scheme(void) {
    wf_plc_recovery_key key;
    char *s = (char *)1;
    WF_CHECK(wf_plc_recovery_key_generate(WF_PLC_RECOVERY_SCHEME_UNKNOWN,
                                          WF_KEY_TYPE_P256,
                                          &key) == WF_ERR_UNSUPPORTED);
    WF_CHECK(wf_plc_recovery_key_encode(&key, &s) == WF_ERR_UNSUPPORTED);
    WF_CHECK(s == NULL);
    WF_CHECK(wf_plc_recovery_key_decode("zzzz", NULL, &key) == WF_ERR_PARSE);
    WF_CHECK(wf_plc_recovery_key_decode(NULL, NULL, &key) ==
             WF_ERR_INVALID_ARG);
}

int main(void) {
    test_recorded();
    test_update_insert_and_replace();
    test_apply_errors();
    test_build_through_directory();
    test_recovery_key(WF_KEY_TYPE_P256);
#ifdef HAVE_LIBSECP256K1
    test_recovery_key(WF_KEY_TYPE_SECP256K1);
#endif
    test_recovery_scheme();
    WF_TEST_SUMMARY();
}
