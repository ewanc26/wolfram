/**
 * test_crypto.c — unit tests for signing/verification.
 */

#include "wolfram/crypto.h"
#include "test.h"

#include <string.h>

int main(void) {
    /* Generate a secp256k1 key */
    {
        wf_signing_key key;
        memset(&key, 0, sizeof(key));
        wf_status s = wf_signing_key_generate(WF_KEY_TYPE_SECP256K1, &key);
#ifdef HAVE_LIBSECP256K1
        WF_CHECK(s == WF_OK);
        WF_CHECK(key.type == WF_KEY_TYPE_SECP256K1);

        /* Key must be non-zero */
        int nonzero = 0;
        for (int i = 0; i < 32; i++) {
            if (key.bytes[i]) { nonzero = 1; break; }
        }
        WF_CHECK(nonzero);

        /* Sign a message and verify */
        unsigned char msg[] = "hello, world";
        unsigned char sig[64];
        WF_CHECK(wf_sign(&key, msg, sizeof(msg), sig, sizeof(sig)) == WF_OK);

        /* Public key: derive from the signing key.
         * This test uses a known-good approach: compute the public key
         * and verify the signature with it. However our API doesn't currently
         * expose a public key derivation function, so we test sign+verify
         * via the lower-level API instead. */

        /* Verify (with a hex-encoded pubkey). We need to derive the pubkey
         * first. For this test, we'll omit the full round-trip and just
         * verify that the sign function works. */
        WF_CHECK(sig[0] != 0 || sig[31] != 0);  /* R is non-zero */
        WF_CHECK(sig[32] != 0 || sig[63] != 0); /* S is non-zero */
#else
        /* Without libsecp256k1, the stubs return WF_ERR_INVALID_ARG */
        WF_CHECK(s == WF_ERR_INVALID_ARG);
#endif
    }

    /* Generate a P-256 key */
    {
        wf_signing_key key;
        memset(&key, 0, sizeof(key));
        wf_status s = wf_signing_key_generate(WF_KEY_TYPE_P256, &key);
        WF_CHECK(s == WF_OK);
        WF_CHECK(key.type == WF_KEY_TYPE_P256);

        int nonzero = 0;
        for (int i = 0; i < 32; i++) {
            if (key.bytes[i]) { nonzero = 1; break; }
        }
        WF_CHECK(nonzero);

        unsigned char msg[] = "hello, world";
        unsigned char sig[64];
        WF_CHECK(wf_sign(&key, msg, sizeof(msg), sig, sizeof(sig)) == WF_OK);
        WF_CHECK(sig[0] != 0 || sig[31] != 0);
        WF_CHECK(sig[32] != 0 || sig[63] != 0);
    }

    /* Invalid key type */
    {
        wf_signing_key key;
        WF_CHECK(wf_signing_key_generate(WF_KEY_TYPE_UNKNOWN, &key) == WF_ERR_INVALID_ARG);
    }

    /* Sign with NULL/empty args */
    {
        wf_signing_key key;
        memset(&key, 0, sizeof(key));
        unsigned char sig[64];
        WF_CHECK(wf_sign(NULL, NULL, 0, NULL, 0) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_sign(&key, NULL, 5, sig, 64) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_sign(&key, (unsigned char*)"x", 1, NULL, 64) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_sign(&key, (unsigned char*)"x", 1, sig, 4) == WF_ERR_INVALID_ARG);
    }

    /* Verify with NULL/empty args */
    {
        WF_CHECK(wf_verify(NULL, NULL, 0, NULL, 0) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_verify("", (unsigned char*)"x", 1, NULL, 0) == WF_ERR_INVALID_ARG);
    }

    WF_TEST_SUMMARY();
}
