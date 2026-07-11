/*
 * test_service_auth.c — offline field-level tests for service-auth (service
 * JWT) token issuance + verification (wf_server_create_service_auth /
 * wf_server_verify_service_auth). No network.
 *
 * Deterministic: a fresh P-256 signing key is generated, a token is minted and
 * round-tripped through the verifier, and the signature/expiry checks are
 * exercised with tampered / wrong-key / expired inputs.
 */

#include "wolfram/server.h"
#include "wolfram/crypto.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *resign_with_header(const char *token, const char *header_json,
                                const wf_signing_key *key) {
    const char *dot1 = strchr(token, '.');
    const char *dot2 = dot1 ? strchr(dot1 + 1, '.') : NULL;
    char *header_b64 = NULL, *sig_b64 = NULL, *signing_input = NULL;
    char *forged = NULL;
    unsigned char sig[64];
    size_t payload_len, signing_len, forged_len;

    if (!dot1 || !dot2 ||
        wf_crypto_base64url_encode((const unsigned char *)header_json,
                                   strlen(header_json), &header_b64) != WF_OK) {
        return NULL;
    }
    payload_len = (size_t)(dot2 - (dot1 + 1));
    signing_len = strlen(header_b64) + 1 + payload_len;
    signing_input = malloc(signing_len + 1);
    if (!signing_input) goto done;
    snprintf(signing_input, signing_len + 1, "%s.%.*s", header_b64,
             (int)payload_len, dot1 + 1);
    if (wf_sign(key, (const unsigned char *)signing_input, signing_len,
                sig, sizeof(sig)) != WF_OK ||
        wf_crypto_base64url_encode(sig, sizeof(sig), &sig_b64) != WF_OK) {
        goto done;
    }
    forged_len = signing_len + 1 + strlen(sig_b64);
    forged = malloc(forged_len + 1);
    if (forged) {
        snprintf(forged, forged_len + 1, "%s.%s", signing_input, sig_b64);
    }

done:
    free(header_b64);
    free(sig_b64);
    free(signing_input);
    return forged;
}

int main(void) {
    /* ---- 1. Generate a P-256 signing key + its did:key ---- */
    wf_signing_key key = {0};
    WF_CHECK(wf_signing_key_generate(WF_KEY_TYPE_P256, &key) == WF_OK);
    char *didkey = NULL;
    WF_CHECK(wf_signing_key_public_didkey(&key, &didkey) == WF_OK);
    WF_CHECK(didkey != NULL);

    const char *iss = "did:plc:issuerpds";
    const char *aud = "did:web:labeler.example.com";
    const char *lxm = "tools.ozone.moderation.emitEvent";

    /* ---- 2. Issue with known aud + lxm; verify + assert every field ---- */
    {
        wf_service_auth_request req = {0};
        req.iss = iss;
        req.aud = aud;
        req.exp = 0; /* default now+60 */
        req.lxm = lxm;

        char *token = NULL;
        int64_t before = (int64_t)time(NULL);
        WF_CHECK(wf_server_create_service_auth(&req, &key, &token) == WF_OK);
        WF_CHECK(token != NULL);

        /* Structurally a compact JWT: exactly two dots. */
        if (token) {
            const char *d1 = strchr(token, '.');
            WF_CHECK(d1 != NULL);
            const char *d2 = d1 ? strchr(d1 + 1, '.') : NULL;
            WF_CHECK(d2 != NULL);
            WF_CHECK(d2 && strchr(d2 + 1, '.') == NULL);
        }

        wf_service_auth_claims claims = {0};
        WF_CHECK(wf_server_verify_service_auth(token, didkey, 0, &claims) ==
                 WF_OK);
        WF_CHECK(claims.alg && strcmp(claims.alg, "ES256") == 0);
        WF_CHECK(claims.iss && strcmp(claims.iss, iss) == 0);
        WF_CHECK(claims.aud && strcmp(claims.aud, aud) == 0);
        WF_CHECK(claims.lxm && strcmp(claims.lxm, lxm) == 0);
        WF_CHECK(claims.jti && strlen(claims.jti) == 32); /* 16 bytes hex */
        WF_CHECK(claims.iat >= before);
        WF_CHECK(claims.exp == claims.iat + 60);
        WF_CHECK(claims.nuance == NULL);

        wf_service_auth_claims_free(&claims);
        free(token);
    }

    /* ---- 3a. Negative: verify with a DIFFERENT key must fail ---- */
    {
        wf_signing_key other = {0};
        WF_CHECK(wf_signing_key_generate(WF_KEY_TYPE_P256, &other) == WF_OK);
        char *other_didkey = NULL;
        WF_CHECK(wf_signing_key_public_didkey(&other, &other_didkey) == WF_OK);

        wf_service_auth_request req = {0};
        req.iss = iss;
        req.aud = aud;
        char *token = NULL;
        WF_CHECK(wf_server_create_service_auth(&req, &key, &token) == WF_OK);

        wf_service_auth_claims claims = {0};
        WF_CHECK(wf_server_verify_service_auth(token, other_didkey, 0,
                                               &claims) != WF_OK);
        /* out left reset on failure */
        WF_CHECK(claims.iss == NULL && claims.aud == NULL);

        wf_service_auth_claims_free(&claims);
        free(token);
        free(other_didkey);
    }

    /* ---- 3b. Negative: a tampered payload must fail signature check ---- */
    {
        wf_service_auth_request req = {0};
        req.iss = iss;
        req.aud = aud;
        char *token = NULL;
        WF_CHECK(wf_server_create_service_auth(&req, &key, &token) == WF_OK);

        /* Flip one character inside the payload segment (between the dots). */
        if (token) {
            char *d1 = strchr(token, '.');
            char *d2 = d1 ? strchr(d1 + 1, '.') : NULL;
            if (d1 && d2 && d2 > d1 + 1) {
                char *target = d1 + 1;
                *target = (*target == 'A') ? 'B' : 'A';
            }
        }

        wf_service_auth_claims claims = {0};
        WF_CHECK(wf_server_verify_service_auth(token, didkey, 0, &claims) !=
                 WF_OK);
        wf_service_auth_claims_free(&claims);
        free(token);
    }

    /* ---- 3c. Negative: an expired exp must be rejected ---- */
    {
        wf_service_auth_request req = {0};
        req.iss = iss;
        req.aud = aud;
        req.exp = 1000000000; /* 2001-09-09, safely in the past */
        char *token = NULL;
        WF_CHECK(wf_server_create_service_auth(&req, &key, &token) == WF_OK);

        wf_service_auth_claims claims = {0};
        /* now_unix=0 => use current wall clock, which is well past exp */
        WF_CHECK(wf_server_verify_service_auth(token, didkey, 0, &claims) !=
                 WF_OK);
        wf_service_auth_claims_free(&claims);

        /* But verifying with an explicit `now` before the exp succeeds — proves
         * the failure above was expiry, not a bad signature. */
        wf_service_auth_claims claims2 = {0};
        WF_CHECK(wf_server_verify_service_auth(token, didkey, 999999999,
                                               &claims2) == WF_OK);
        WF_CHECK(claims2.exp == 1000000000);
        wf_service_auth_claims_free(&claims2);
        free(token);
    }

    /* ---- 4. Round-trip with optional nuance + defaulted exp ---- */
    {
        wf_service_auth_request req = {0};
        req.iss = iss;
        req.aud = aud;
        req.nuance = "migration"; /* no lxm, no explicit exp */
        char *token = NULL;
        WF_CHECK(wf_server_create_service_auth(&req, &key, &token) == WF_OK);

        wf_service_auth_claims claims = {0};
        WF_CHECK(wf_server_verify_service_auth(token, didkey, 0, &claims) ==
                 WF_OK);
        WF_CHECK(claims.lxm == NULL);
        WF_CHECK(claims.nuance && strcmp(claims.nuance, "migration") == 0);
        WF_CHECK(claims.exp == claims.iat + 60);
        WF_CHECK(claims.jti && strlen(claims.jti) == 32);
        wf_service_auth_claims_free(&claims);
        free(token);
    }

    /* ---- 4b. Negative: JWT alg must match the signing-key curve ---- */
    {
        wf_service_auth_request req = {0};
        req.iss = iss;
        req.aud = aud;
        char *token = NULL;
        WF_CHECK(wf_server_create_service_auth(&req, &key, &token) == WF_OK);
        char *forged = resign_with_header(
            token, "{\"typ\":\"JWT\",\"alg\":\"ES256K\"}", &key);
        WF_CHECK(forged != NULL);

        wf_service_auth_claims claims = {0};
        WF_CHECK(wf_server_verify_service_auth(forged, didkey, 0, &claims) ==
                 WF_ERR_PARSE);
        wf_service_auth_claims_free(&claims);
        free(forged);
        free(token);
    }

    /* ---- 5. Uniqueness: two tokens have distinct jti ---- */
    {
        wf_service_auth_request req = {0};
        req.iss = iss;
        req.aud = aud;
        char *t1 = NULL, *t2 = NULL;
        WF_CHECK(wf_server_create_service_auth(&req, &key, &t1) == WF_OK);
        WF_CHECK(wf_server_create_service_auth(&req, &key, &t2) == WF_OK);

        wf_service_auth_claims c1 = {0}, c2 = {0};
        WF_CHECK(wf_server_verify_service_auth(t1, didkey, 0, &c1) == WF_OK);
        WF_CHECK(wf_server_verify_service_auth(t2, didkey, 0, &c2) == WF_OK);
        WF_CHECK(c1.jti && c2.jti && strcmp(c1.jti, c2.jti) != 0);
        wf_service_auth_claims_free(&c1);
        wf_service_auth_claims_free(&c2);
        free(t1);
        free(t2);
    }

    /* ---- 6. Argument validation ---- */
    {
        wf_service_auth_request req = {0};
        req.iss = iss;
        req.aud = aud;
        char *token = NULL;
        WF_CHECK(wf_server_create_service_auth(NULL, &key, &token) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_server_create_service_auth(&req, NULL, &token) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_server_create_service_auth(&req, &key, NULL) ==
                 WF_ERR_INVALID_ARG);
        wf_service_auth_request bad = {0};
        bad.aud = aud; /* missing iss */
        WF_CHECK(wf_server_create_service_auth(&bad, &key, &token) ==
                 WF_ERR_INVALID_ARG);

        wf_service_auth_claims claims = {0};
        WF_CHECK(wf_server_verify_service_auth(NULL, didkey, 0, &claims) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_server_verify_service_auth("a.b.c", NULL, 0, &claims) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_server_verify_service_auth("not-a-jwt", didkey, 0,
                                               &claims) == WF_ERR_PARSE);
    }

    free(didkey);
    WF_TEST_SUMMARY();
}
