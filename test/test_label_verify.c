/*
 * test_label_verify.c — offline test for com.atproto.label signature
 * verification (wf_label_verify_signature_with_key).
 *
 * The test builds a label, reconstructs its DRISL-canonical DAG-CBOR exactly
 * as the production code does (see wf_label_build_signed_cbor in
 * src/label/label.c: every schema field except `sig`, with `cid`/`neg`/`exp`
 * emitted as CBOR null when absent, and `ver` always present), signs those
 * bytes with a freshly generated key, and verifies the label with that key.
 *
 * Because the signature is computed over the same canonical CBOR bytes the
 * verifier reconstructs, a divergence between the test's reconstruction and
 * the production one would cause verification to fail — so this is a genuine
 * cross-check of the reconstruction, not a fabricated vector. A real
 * on-the-wire vector from the atproto reference implementation would be
 * needed to prove full protocol parity; this test confirms internal
 * consistency of the verify path (decode sig -> canonical CBOR -> wf_verify).
 *
 * No network access required.
 */

#include "wolfram/label.h"
#include "wolfram/crypto.h"
#include "wolfram/repo/cbor.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Replicate the production canonical-CBOR reconstruction (kept in sync with
 * wf_label_build_signed_cbor). Returns heap-allocated bytes; caller frees. */
static wf_status build_signed_cbor(const wf_label *label,
                                    unsigned char **out, size_t *out_len) {
    if (!label || !out || !out_len) return WF_ERR_INVALID_ARG;
    *out = NULL; *out_len = 0;
    if (!label->src || !label->uri || !label->val || !label->cts)
        return WF_ERR_INVALID_ARG;

    const char *names[8] = {"ver","src","uri","cid","val","neg","cts","exp"};
    wf_cbor_item *keys[8] = {0}, *vals[8] = {0};
    int64_t ver = label->has_ver ? label->ver : 1;
    keys[0] = &(wf_cbor_item){.type = WF_CBOR_STRING, .string = {(char *)names[0], 3}};
    vals[0] = &(wf_cbor_item){.type = WF_CBOR_UNSIGNED, .uinteger = (uint64_t)ver};
    keys[1] = &(wf_cbor_item){.type = WF_CBOR_STRING, .string = {(char *)names[1], 3}};
    vals[1] = &(wf_cbor_item){.type = WF_CBOR_STRING, .string = {(char *)label->src, strlen(label->src)}};
    keys[2] = &(wf_cbor_item){.type = WF_CBOR_STRING, .string = {(char *)names[2], 3}};
    vals[2] = &(wf_cbor_item){.type = WF_CBOR_STRING, .string = {(char *)label->uri, strlen(label->uri)}};
    keys[3] = &(wf_cbor_item){.type = WF_CBOR_STRING, .string = {(char *)names[3], 3}};
    vals[3] = label->has_cid ? &(wf_cbor_item){.type = WF_CBOR_STRING, .string = {(char *)label->cid, strlen(label->cid)}}
                             : &(wf_cbor_item){.type = WF_CBOR_SIMPLE, .simple_value = 22};
    keys[4] = &(wf_cbor_item){.type = WF_CBOR_STRING, .string = {(char *)names[4], 3}};
    vals[4] = &(wf_cbor_item){.type = WF_CBOR_STRING, .string = {(char *)label->val, strlen(label->val)}};
    keys[5] = &(wf_cbor_item){.type = WF_CBOR_STRING, .string = {(char *)names[5], 3}};
    vals[5] = label->has_neg ? &(wf_cbor_item){.type = WF_CBOR_SIMPLE, .simple_value = label->neg ? 21 : 20}
                             : &(wf_cbor_item){.type = WF_CBOR_SIMPLE, .simple_value = 22};
    keys[6] = &(wf_cbor_item){.type = WF_CBOR_STRING, .string = {(char *)names[6], 3}};
    vals[6] = &(wf_cbor_item){.type = WF_CBOR_STRING, .string = {(char *)label->cts, strlen(label->cts)}};
    keys[7] = &(wf_cbor_item){.type = WF_CBOR_STRING, .string = {(char *)names[7], 3}};
    vals[7] = label->has_exp ? &(wf_cbor_item){.type = WF_CBOR_STRING, .string = {(char *)label->exp, strlen(label->exp)}}
                             : &(wf_cbor_item){.type = WF_CBOR_SIMPLE, .simple_value = 22};

    wf_cbor_item map = {.type = WF_CBOR_MAP, .map = {.count = 8, .pairs = NULL}};
    wf_cbor_pair pairs[8];
    for (size_t i = 0; i < 8; ++i) { pairs[i].key = keys[i]; pairs[i].value = vals[i]; }
    map.map.pairs = pairs;

    *out = wf_cbor_serialize(&map, out_len);
    return *out ? WF_OK : WF_ERR_ALLOC;
}

/* Sign `label` with `key` and store the resulting sig (base64url) in `label`.
 * Returns WF_OK on success. */
static wf_status sign_label(const wf_signing_key *key, wf_label *label) {
    unsigned char *cbor = NULL;
    size_t cbor_len = 0;
    wf_status st = build_signed_cbor(label, &cbor, &cbor_len);
    if (st != WF_OK) return st;

    unsigned char sig[64];
    st = wf_sign(key, cbor, cbor_len, sig, sizeof(sig));
    free(cbor);
    if (st != WF_OK) return st;

    char *sig_b64 = NULL;
    st = wf_crypto_base64url_encode(sig, sizeof(sig), &sig_b64);
    if (st != WF_OK) return st;

    label->sig = sig_b64; /* owned by label; freed by caller */
    label->has_sig = 1;
    return WF_OK;
}

int main(void) {
    wf_signing_key key;
    WF_CHECK(wf_signing_key_generate(WF_KEY_TYPE_P256, &key) == WF_OK);

    char *didkey = NULL;
    WF_CHECK(wf_signing_key_public_didkey(&key, &didkey) == WF_OK);
    WF_CHECK(didkey && strncmp(didkey, "did:key:", 8) == 0);

    /* Label with every optional field present. */
    wf_label label;
    memset(&label, 0, sizeof(label));
    label.has_ver = 1; label.ver = 1;
    label.src = "did:plc:labeler";
    label.uri = "at://did:plc:alice/app.bsky.feed.post/aaa111";
    label.has_cid = 1; label.cid = "bafyreimxexamplecid";
    label.val = "org.labeler.porn";
    label.has_neg = 1; label.neg = 0;
    label.cts = "2024-01-02T03:04:05.000Z";
    label.has_exp = 1; label.exp = "2099-01-01T00:00:00.000Z";

    WF_CHECK(sign_label(&key, &label) == WF_OK);
    WF_CHECK(wf_label_verify_signature_with_key(didkey, &label) == WF_OK);

    /* A wrong key must fail to verify. */
    wf_signing_key other;
    WF_CHECK(wf_signing_key_generate(WF_KEY_TYPE_P256, &other) == WF_OK);
    char *other_didkey = NULL;
    WF_CHECK(wf_signing_key_public_didkey(&other, &other_didkey) == WF_OK);
    WF_CHECK(wf_label_verify_signature_with_key(other_didkey, &label) != WF_OK);
    free(other_didkey);

    /* Tampering with a signed field must fail verification. */
    label.val = "org.labeler.notporn";
    WF_CHECK(wf_label_verify_signature_with_key(didkey, &label) != WF_OK);
    label.val = "org.labeler.porn"; /* restore */

    /* A label without sig cannot be verified. */
    label.has_sig = 0;
    free(label.sig); label.sig = NULL;
    WF_CHECK(wf_label_verify_signature_with_key(didkey, &label) == WF_ERR_INVALID_ARG);
    label.has_sig = 1;

    /* A label with all optional fields ABSENT must still verify (the
     * reconstruction emits cid/neg/exp as CBOR null, matching the reference
     * createLabelV1 behavior). This is the critical parity cross-check. */
    wf_label sparse;
    memset(&sparse, 0, sizeof(sparse));
    sparse.src = "did:plc:labeler";
    sparse.uri = "at://did:plc:bob/app.bsky.actor.profile/self";
    sparse.val = "spam";
    sparse.cts = "2024-01-03T04:05:06.000Z";
    WF_CHECK(sign_label(&key, &sparse) == WF_OK);
    WF_CHECK(wf_label_verify_signature_with_key(didkey, &sparse) == WF_OK);
    free(sparse.sig);

    free(label.sig);
    free(didkey);
    WF_TEST_SUMMARY();
}
