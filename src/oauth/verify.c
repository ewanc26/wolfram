/**
 * verify.c — OAuth resource-server token verification (Bearer + DPoP).
 *
 * Implements RFC 9449 DPoP proof verification and JWT access-token validation
 * for atproto resource servers, reusing the P-256/ES256, SHA-256, base64url
 * and JWK primitives from wolfram/crypto.c. No crypto is hand-rolled here.
 */

#include "wolfram/crypto.h"
#include "wolfram/oauth/verify.h"

#include <cJSON.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static char *wf_strdup(const char *s) {
    size_t n;
    char *out;
    if (!s) return NULL;
    n = strlen(s) + 1;
    out = malloc(n);
    if (!out) return NULL;
    memcpy(out, s, n);
    return out;
}

/* Parse a positive (or zero) integer claim. Returns WF_OK and sets *out (and
 * *present=1) when present; *present=0 when absent. */
static wf_status json_int(const cJSON *root, const char *name,
                          int64_t *out, int *present) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsNumber(item)) {
        if (present) *present = 0;
        return WF_OK;
    }
    if (present) *present = 1;
    *out = (int64_t)item->valuedouble;
    return WF_OK;
}

static wf_status json_string_dup(const cJSON *root, const char *name,
                                 char **out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsString(item) || !item->valuestring) {
        *out = NULL;
        return WF_OK;
    }
    *out = wf_strdup(item->valuestring);
    return *out ? WF_OK : WF_ERR_ALLOC;
}

/* ------------------------------------------------------------------ */
/* Trusted keys                                                        */
/* ------------------------------------------------------------------ */

struct wf_oauth_trusted_keys {
    char **jwks;
    size_t count;
    size_t cap;
};

wf_status wf_oauth_trusted_keys_new(wf_oauth_trusted_keys **out) {
    wf_oauth_trusted_keys *keys;
    if (!out) return WF_ERR_INVALID_ARG;
    *out = NULL;
    keys = calloc(1, sizeof(*keys));
    if (!keys) return WF_ERR_ALLOC;
    *out = keys;
    return WF_OK;
}

wf_status wf_oauth_trusted_keys_add_jwk(wf_oauth_trusted_keys *keys,
                                        const char *jwk_json) {
    char *copy;
    if (!keys || !jwk_json) return WF_ERR_INVALID_ARG;
    if (keys->count == keys->cap) {
        size_t newcap = keys->cap ? keys->cap * 2 : 4;
        char **grown = realloc(keys->jwks, newcap * sizeof(*grown));
        if (!grown) return WF_ERR_ALLOC;
        keys->jwks = grown;
        keys->cap = newcap;
    }
    copy = wf_strdup(jwk_json);
    if (!copy) return WF_ERR_ALLOC;
    keys->jwks[keys->count++] = copy;
    return WF_OK;
}

void wf_oauth_trusted_keys_free(wf_oauth_trusted_keys *keys) {
    if (!keys) return;
    for (size_t i = 0; i < keys->count; i++) free(keys->jwks[i]);
    free(keys->jwks);
    free(keys);
}

/* ------------------------------------------------------------------ */
/* Verified token                                                      */
/* ------------------------------------------------------------------ */

void wf_oauth_verified_token_free(wf_oauth_verified_token *token) {
    if (!token) return;
    free(token->sub);
    free(token->iss);
    free(token->aud);
    free(token->scope);
    free(token->dpop_jkt);
    free(token);
}

static wf_oauth_verified_token *verified_new(void) {
    wf_oauth_verified_token *t = calloc(1, sizeof(*t));
    return t; /* NULL on alloc failure; callers check */
}

/* ------------------------------------------------------------------ */
/* Replay cache                                                        */
/* ------------------------------------------------------------------ */

typedef struct wf_oauth_replay_entry {
    char *jti;
    int64_t expires; /* unix seconds; <= 0 means never expires */
} wf_oauth_replay_entry;

struct wf_oauth_dpop_replay_cache {
    wf_oauth_replay_entry *entries;
    size_t count;
    size_t cap;
};

wf_status wf_oauth_dpop_replay_cache_new(wf_oauth_dpop_replay_cache **out) {
    wf_oauth_dpop_replay_cache *c;
    if (!out) return WF_ERR_INVALID_ARG;
    *out = NULL;
    c = calloc(1, sizeof(*c));
    if (!c) return WF_ERR_ALLOC;
    *out = c;
    return WF_OK;
}

/* Drop expired entries (best-effort; called on reads and writes). */
static void replay_evict(wf_oauth_dpop_replay_cache *c, int64_t now) {
    size_t i = 0;
    while (i < c->count) {
        wf_oauth_replay_entry *e = &c->entries[i];
        if (e->expires > 0 && e->expires <= now) {
            free(e->jti);
            c->entries[i] = c->entries[--c->count];
        } else {
            i++;
        }
    }
}

int wf_oauth_dpop_replay_cache_is_seen(const wf_oauth_dpop_replay_cache *cache,
                                       const char *jti) {
    if (!cache || !jti) return 0;
    for (size_t i = 0; i < cache->count; i++) {
        if (strcmp(cache->entries[i].jti, jti) == 0) return 1;
    }
    return 0;
}

wf_status wf_oauth_dpop_replay_cache_mark_seen(wf_oauth_dpop_replay_cache *cache,
                                               const char *jti, int64_t ttl) {
    int64_t now = (int64_t)time(NULL);
    if (!cache || !jti) return WF_ERR_INVALID_ARG;
    replay_evict(cache, now);
    for (size_t i = 0; i < cache->count; i++) {
        if (strcmp(cache->entries[i].jti, jti) == 0) {
            cache->entries[i].expires = ttl > 0 ? now + ttl : 0;
            return WF_OK;
        }
    }
    if (cache->count == cache->cap) {
        size_t newcap = cache->cap ? cache->cap * 2 : 16;
        wf_oauth_replay_entry *grown =
            realloc(cache->entries, newcap * sizeof(*grown));
        if (!grown) return WF_ERR_ALLOC;
        cache->entries = grown;
        cache->cap = newcap;
    }
    cache->entries[cache->count].jti = wf_strdup(jti);
    if (!cache->entries[cache->count].jti) return WF_ERR_ALLOC;
    cache->entries[cache->count].expires = ttl > 0 ? now + ttl : 0;
    cache->count++;
    return WF_OK;
}

void wf_oauth_dpop_replay_cache_free(wf_oauth_dpop_replay_cache *cache) {
    if (!cache) return;
    for (size_t i = 0; i < cache->count; i++) free(cache->entries[i].jti);
    free(cache->entries);
    free(cache);
}

/* ------------------------------------------------------------------ */
/* JWT parsing                                                         */
/* ------------------------------------------------------------------ */

typedef struct wf_jwt {
    char *header_b64;  /* owned */
    char *payload_b64; /* owned */
    char *sig_b64;     /* owned */
    unsigned char *sig; /* raw signature bytes (owned) */
    size_t sig_len;
    cJSON *header;   /* owned */
    cJSON *payload;  /* owned */
} wf_jwt;

static void wf_jwt_free(wf_jwt *j) {
    if (!j) return;
    free(j->header_b64);
    free(j->payload_b64);
    free(j->sig_b64);
    free(j->sig);
    cJSON_Delete(j->header);
    cJSON_Delete(j->payload);
    free(j);
}

/* Split `jwt` into its three dot-separated parts, base64url-decode the
 * header and payload to JSON, and decode the signature to raw bytes. */
static wf_status wf_jwt_parse(const char *jwt, wf_jwt **out) {
    wf_jwt *j = NULL;
    const char *p1, *p2;
    unsigned char *raw = NULL;
    size_t raw_len = 0;
    wf_status status;

    if (!jwt || !out) return WF_ERR_INVALID_ARG;
    *out = NULL;
    p1 = strchr(jwt, '.');
    if (!p1) return WF_ERR_PARSE;
    p2 = strchr(p1 + 1, '.');
    if (!p2) return WF_ERR_PARSE;

    j = calloc(1, sizeof(*j));
    if (!j) return WF_ERR_ALLOC;

    /* Header. */
    {
        size_t hlen = (size_t)(p1 - jwt);
        char *hb = malloc(hlen + 1);
        if (!hb) { status = WF_ERR_ALLOC; goto done; }
        memcpy(hb, jwt, hlen);
        hb[hlen] = '\0';
        j->header_b64 = hb;
    }
    status = wf_crypto_base64url_decode(j->header_b64, &raw, &raw_len);
    if (status != WF_OK) goto done;
    j->header = cJSON_ParseWithLength((const char *)raw, raw_len);
    free(raw);
    raw = NULL;
    if (!j->header) { status = WF_ERR_PARSE; goto done; }

    /* Payload. */
    {
        size_t plen = (size_t)(p2 - (p1 + 1));
        char *pb = malloc(plen + 1);
        if (!pb) { status = WF_ERR_ALLOC; goto done; }
        memcpy(pb, p1 + 1, plen);
        pb[plen] = '\0';
        j->payload_b64 = pb;
    }
    status = wf_crypto_base64url_decode(j->payload_b64, &raw, &raw_len);
    if (status != WF_OK) goto done;
    j->payload = cJSON_ParseWithLength((const char *)raw, raw_len);
    free(raw);
    raw = NULL;
    if (!j->payload) { status = WF_ERR_PARSE; goto done; }

    /* Signature. */
    {
        size_t slen = strlen(p2 + 1);
        char *sb = malloc(slen + 1);
        if (!sb) { status = WF_ERR_ALLOC; goto done; }
        memcpy(sb, p2 + 1, slen);
        sb[slen] = '\0';
        j->sig_b64 = sb;
    }
    status = wf_crypto_base64url_decode(j->sig_b64, &j->sig, &j->sig_len);
    if (status != WF_OK) goto done;

    *out = j;
    return WF_OK;
done:
    wf_jwt_free(j);
    free(raw);
    return status;
}

/* ------------------------------------------------------------------ */
/* htu normalization (mirrors the client's proof htu construction)     */
/* ------------------------------------------------------------------ */

/* Returns an owned normalized htu (scheme://host/path, no query/fragment,
 * no userinfo) or NULL on malformed input. */
static char *normalize_htu(const char *uri) {
    /* Minimal parser: require http/https scheme, strip userinfo, query, frag. */
    const char *scheme_end, *host_start, *host_end, *path, *end;
    size_t scheme_len, host_len, path_len;
    char *out;
    int https;
    if (!uri || !*uri) return NULL;
    scheme_end = strstr(uri, "://");
    if (!scheme_end) return NULL;
    scheme_len = (size_t)(scheme_end - uri);
    if (scheme_len == 5 && strncmp(uri, "https", 5) == 0) https = 1;
    else if (scheme_len == 4 && strncmp(uri, "http", 4) == 0) https = 0;
    else return NULL;
    host_start = scheme_end + 3;
    /* Strip userinfo. */
    {
        const char *at = strchr(host_start, '@');
        const char *slash = strchr(host_start, '/');
        if (at && (!slash || at < slash)) host_start = at + 1;
    }
    host_end = strchr(host_start, '/');
    if (host_end) {
        host_len = (size_t)(host_end - host_start);
        path = host_end;
    } else {
        host_len = strlen(host_start);
        path = host_start + host_len;
    }
    if (host_len == 0) return NULL;
    /* Path up to query/fragment. */
    end = path + strlen(path);
    {
        const char *q = strchr(path, '?');
        const char *f = strchr(path, '#');
        if (q && q < end) end = q;
        if (f && f < end) end = f;
    }
    path_len = (size_t)(end - path);
    out = malloc(scheme_len + 3 + host_len + path_len + 1);
    if (!out) return NULL;
    {
        size_t o = 0;
        memcpy(out + o, uri, scheme_len);
        o += scheme_len;
        memcpy(out + o, "://", 3);
        o += 3;
        memcpy(out + o, host_start, host_len);
        o += host_len;
        memcpy(out + o, path, path_len);
        o += path_len;
        out[o] = '\0';
    }
    (void)https;
    return out;
}

/* ------------------------------------------------------------------ */
/* DPoP proof thumbprint (RFC 7638) from a JWK object in the header     */
/* ------------------------------------------------------------------ */

/* `jwk` is a cJSON public JWK with kty/EC, crv/P-256, x, y strings. */
static wf_status dpop_thumbprint(const cJSON *jwk, char out_jkt[44]) {
    const cJSON *x, *y;
    char *canonical = NULL, *encoded = NULL;
    unsigned char digest[SHA256_DIGEST_LENGTH];
    wf_status status;

    if (!jwk || !out_jkt) return WF_ERR_INVALID_ARG;
    x = cJSON_GetObjectItemCaseSensitive(jwk, "x");
    y = cJSON_GetObjectItemCaseSensitive(jwk, "y");
    if (!cJSON_IsString(x) || !cJSON_IsString(y)) return WF_ERR_PARSE;
    /* Canonical member order per RFC 7638: crv, kty, x, y. */
    size_t need = strlen(x->valuestring) + strlen(y->valuestring) +
                  strlen("{\"crv\":\"P-256\",\"kty\":\"EC\",\"x\":\"\",\"y\":\"\"}") + 1;
    canonical = malloc(need);
    if (!canonical) return WF_ERR_ALLOC;
    snprintf(canonical, need,
             "{\"crv\":\"P-256\",\"kty\":\"EC\",\"x\":\"%s\",\"y\":\"%s\"}",
             x->valuestring, y->valuestring);
    SHA256((const unsigned char *)canonical, strlen(canonical), digest);
    status = wf_crypto_base64url_encode(digest, sizeof(digest), &encoded);
    if (status == WF_OK) {
        if (strlen(encoded) != 43) status = WF_ERR_PARSE;
        else memcpy(out_jkt, encoded, 44);
    }
    free(canonical);
    free(encoded);
    return status;
}

/* ------------------------------------------------------------------ */
/* Bearer (access token) verification                                 */
/* ------------------------------------------------------------------ */

wf_status wf_oauth_verify_bearer(const char *access_token,
                                 const wf_oauth_trusted_keys *keys,
                                 wf_oauth_verified_token **out) {
    wf_jwt *j = NULL;
    const cJSON *alg, *kid, *cnf, *jkt, *aud;
    const char *alg_s, *kid_s = NULL;
    unsigned char x[32], y[32];
    char *signing_input = NULL;
    size_t silen;
    int64_t now = (int64_t)time(NULL);
    int64_t exp = 0;
    int exp_present = 0, has_cnf = 0;
    wf_status status;
    wf_oauth_verified_token *tok = NULL;

    if (!access_token || !keys || !out) return WF_ERR_INVALID_ARG;
    *out = NULL;
    if (strchr(access_token, '.') == NULL) return WF_ERR_PARSE;

    status = wf_jwt_parse(access_token, &j);
    if (status != WF_OK) return status;

    /* alg must be ES256. */
    alg = cJSON_GetObjectItemCaseSensitive(j->header, "alg");
    if (!cJSON_IsString(alg) || (alg_s = alg->valuestring) == NULL) {
        status = WF_ERR_PARSE;
        goto done;
    }
    if (strcmp(alg_s, "ES256") != 0) {
        status = WF_ERR_INVALID_ARG; /* reject "none" and other algs */
        goto done;
    }
    kid = cJSON_GetObjectItemCaseSensitive(j->header, "kid");
    if (cJSON_IsString(kid)) kid_s = kid->valuestring;

    /* Verify signature against one of the trusted keys. */
    silen = strlen(j->header_b64) + 1 + strlen(j->payload_b64);
    signing_input = malloc(silen + 1);
    if (!signing_input) { status = WF_ERR_ALLOC; goto done; }
    snprintf(signing_input, silen + 1, "%s.%s", j->header_b64, j->payload_b64);

    status = WF_ERR_PARSE;
    for (size_t i = 0; i < keys->count; i++) {
        const char *kj = keys->jwks[i];
        if (kid_s) {
            /* Only filter by kid when the trusted key advertises one; a
             * kid-less key is an unconditional candidate. */
            cJSON *k = cJSON_Parse(kj);
            const cJSON *kk;
            int key_has_kid = 0, match = 1;
            if (k && (kk = cJSON_GetObjectItemCaseSensitive(k, "kid")) &&
                cJSON_IsString(kk) && *kk->valuestring) {
                key_has_kid = 1;
                match = strcmp(kk->valuestring, kid_s) == 0 ? 1 : 0;
            }
            cJSON_Delete(k);
            if (key_has_kid && !match) continue;
        }
        if (wf_crypto_p256_jwk_coords(kj, x, y) != WF_OK) continue;
        if (wf_crypto_p256_verify(x, y, (const unsigned char *)signing_input,
                                  silen, j->sig, j->sig_len) == WF_OK) {
            status = WF_OK;
            break;
        }
    }
    if (status != WF_OK) goto done;

    /* exp check. */
    status = json_int(j->payload, "exp", &exp, &exp_present);
    if (status != WF_OK) goto done;
    if (exp_present && exp < now - WF_OAUTH_CLOCK_SKEW) {
        status = WF_ERR_PARSE; /* expired */
        goto done;
    }

    /* Build the principal. */
    tok = verified_new();
    if (!tok) { status = WF_ERR_ALLOC; goto done; }
    status = json_string_dup(j->payload, "sub", &tok->sub);
    if (status == WF_OK) status = json_string_dup(j->payload, "iss", &tok->iss);
    if (status != WF_OK) goto done;

    aud = cJSON_GetObjectItemCaseSensitive(j->payload, "aud");
    if (cJSON_IsString(aud)) {
        tok->aud = wf_strdup(aud->valuestring);
    } else if (cJSON_IsArray(aud) && cJSON_GetArraySize(aud) > 0) {
        cJSON *first = cJSON_GetArrayItem(aud, 0);
        if (cJSON_IsString(first)) tok->aud = wf_strdup(first->valuestring);
    }
    if (status == WF_OK) {
        status = json_string_dup(j->payload, "scope", &tok->scope);
        if (status == WF_OK && !tok->scope)
            status = json_string_dup(j->payload, "scopes", &tok->scope);
    }
    if (status != WF_OK) goto done;

    tok->exp = exp;
    tok->dpop_bound = 0;

    /* cnf.jkt confirmation claim (if present). */
    cnf = cJSON_GetObjectItemCaseSensitive(j->payload, "cnf");
    if (cJSON_IsObject(cnf)) {
        has_cnf = 1;
        jkt = cJSON_GetObjectItemCaseSensitive(cnf, "jkt");
        if (cJSON_IsString(jkt)) tok->dpop_jkt = wf_strdup(jkt->valuestring);
    }
    (void)has_cnf;

    *out = tok;
    tok = NULL;
    status = WF_OK;
done:
    wf_jwt_free(j);
    free(signing_input);
    wf_oauth_verified_token_free(tok);
    return status;
}

/* ------------------------------------------------------------------ */
/* DPoP proof verification                                             */
/* ------------------------------------------------------------------ */

wf_status wf_oauth_verify_dpop(const char *dpop_proof,
                               const char *access_token,
                               const char *http_method,
                               const char *http_uri,
                               wf_oauth_dpop_replay_cache *replay,
                               wf_oauth_verified_token **out) {
    wf_jwt *j = NULL;
    const cJSON *alg, *typ, *jwk, *htm, *htu, *jti, *ath, *nonce;
    const char *alg_s, *typ_s, *htm_s, *htu_s, *jti_s, *ath_s;
    unsigned char x[32], y[32];
    char *signing_input = NULL, *norm_htu = NULL, *calc_ath = NULL;
    unsigned char digest[SHA256_DIGEST_LENGTH];
    char proof_jkt[44];
    size_t silen;
    int64_t now = (int64_t)time(NULL);
    int64_t iat = 0, exp = 0;
    int iat_present = 0, exp_present = 0;
    wf_status status;
    wf_oauth_verified_token *tok = NULL;

    if (!dpop_proof || !http_method || !http_uri || !out)
        return WF_ERR_INVALID_ARG;
    *out = NULL;
    if (strchr(dpop_proof, '.') == NULL) return WF_ERR_PARSE;

    status = wf_jwt_parse(dpop_proof, &j);
    if (status != WF_OK) return status;

    /* Header checks. */
    alg = cJSON_GetObjectItemCaseSensitive(j->header, "alg");
    if (!cJSON_IsString(alg) || (alg_s = alg->valuestring) == NULL ||
        strcmp(alg_s, "ES256") != 0) {
        status = WF_ERR_INVALID_ARG;
        goto done;
    }
    typ = cJSON_GetObjectItemCaseSensitive(j->header, "typ");
    if (!cJSON_IsString(typ) || (typ_s = typ->valuestring) == NULL ||
        strcmp(typ_s, "dpop+jwt") != 0) {
        status = WF_ERR_INVALID_ARG;
        goto done;
    }
    jwk = cJSON_GetObjectItemCaseSensitive(j->header, "jwk");
    if (!cJSON_IsObject(jwk)) {
        status = WF_ERR_INVALID_ARG; /* missing proof public key */
        goto done;
    }
    if (wf_crypto_p256_jwk_coords(cJSON_PrintUnformatted(jwk), x, y) != WF_OK) {
        status = WF_ERR_PARSE;
        goto done;
    }

    /* Verify the proof signature with its own JWK. */
    silen = strlen(j->header_b64) + 1 + strlen(j->payload_b64);
    signing_input = malloc(silen + 1);
    if (!signing_input) { status = WF_ERR_ALLOC; goto done; }
    snprintf(signing_input, silen + 1, "%s.%s", j->header_b64, j->payload_b64);
    if (wf_crypto_p256_verify(x, y, (const unsigned char *)signing_input,
                              silen, j->sig, j->sig_len) != WF_OK) {
        status = WF_ERR_PARSE;
        goto done;
    }

    /* Payload claim checks. */
    htm = cJSON_GetObjectItemCaseSensitive(j->payload, "htm");
    if (!cJSON_IsString(htm) || (htm_s = htm->valuestring) == NULL ||
        strcmp(htm_s, http_method) != 0) {
        status = WF_ERR_INVALID_ARG; /* wrong htm */
        goto done;
    }
    htu = cJSON_GetObjectItemCaseSensitive(j->payload, "htu");
    if (!cJSON_IsString(htu) || (htu_s = htu->valuestring) == NULL) {
        status = WF_ERR_INVALID_ARG;
        goto done;
    }
    norm_htu = normalize_htu(http_uri);
    if (!norm_htu || strcmp(norm_htu, htu_s) != 0) {
        status = WF_ERR_INVALID_ARG; /* htu mismatch */
        goto done;
    }
    jti = cJSON_GetObjectItemCaseSensitive(j->payload, "jti");
    if (!cJSON_IsString(jti) || (jti_s = jti->valuestring) == NULL ||
        !*jti_s) {
        status = WF_ERR_INVALID_ARG;
        goto done;
    }

    status = json_int(j->payload, "iat", &iat, &iat_present);
    if (status != WF_OK) goto done;
    if (!iat_present) { status = WF_ERR_INVALID_ARG; goto done; }
    if (iat > now + WF_OAUTH_CLOCK_SKEW ||
        now - iat > WF_OAUTH_DPOP_MAX_AGE) {
        status = WF_ERR_INVALID_ARG; /* iat outside freshness window */
        goto done;
    }
    status = json_int(j->payload, "exp", &exp, &exp_present);
    if (status != WF_OK) goto done;
    if (exp_present && exp < now - WF_OAUTH_CLOCK_SKEW) {
        status = WF_ERR_INVALID_ARG; /* expired */
        goto done;
    }

    /* ath check (when an access token is bound). */
    ath = cJSON_GetObjectItemCaseSensitive(j->payload, "ath");
    if (access_token) {
        if (!cJSON_IsString(ath) || (ath_s = ath->valuestring) == NULL) {
            status = WF_ERR_INVALID_ARG; /* missing ath */
            goto done;
        }
        SHA256((const unsigned char *)access_token, strlen(access_token),
               digest);
        status = wf_crypto_base64url_encode(digest, sizeof(digest), &calc_ath);
        if (status != WF_OK) goto done;
        if (strcmp(calc_ath, ath_s) != 0) {
            status = WF_ERR_INVALID_ARG; /* ath mismatch */
            goto done;
        }
    } else if (cJSON_IsString(ath)) {
        /* DPoP-only proof must not carry an ath binding. */
        status = WF_ERR_INVALID_ARG;
        goto done;
    }

    /* Server nonce, if present in the proof, is accepted (resource servers that
     * enforce a nonce would compare it here; we do not reject its presence). */
    (void)nonce;

    /* Replay protection. */
    if (replay) {
        if (wf_oauth_dpop_replay_cache_is_seen(replay, jti_s)) {
            status = WF_ERR_DUPLICATE;
            goto done;
        }
    }

    /* Compute the proof key thumbprint and build the principal. */
    status = dpop_thumbprint(jwk, proof_jkt);
    if (status != WF_OK) goto done;

    tok = verified_new();
    if (!tok) { status = WF_ERR_ALLOC; goto done; }
    tok->dpop_jkt = wf_strdup(proof_jkt);
    if (!tok->dpop_jkt) { status = WF_ERR_ALLOC; goto done; }
    tok->dpop_bound = 1;
    tok->exp = exp;
    *out = tok;
    tok = NULL;

    /* Only record the jti after a fully successful verification. */
    if (replay) {
        int64_t ttl = exp_present ? (exp - now) : 0;
        if (ttl < WF_OAUTH_DPOP_MAX_AGE) ttl = WF_OAUTH_DPOP_MAX_AGE;
        wf_oauth_dpop_replay_cache_mark_seen(replay, jti_s, ttl);
    }
    status = WF_OK;
done:
    wf_jwt_free(j);
    free(signing_input);
    free(norm_htu);
    free(calc_ath);
    wf_oauth_verified_token_free(tok);
    return status;
}

/* ------------------------------------------------------------------ */
/* Request-level convenience                                           */
/* ------------------------------------------------------------------ */

wf_status wf_oauth_verify_request(const char *authorization,
                                  const char *dpop_proof,
                                  const char *http_method,
                                  const char *http_uri,
                                  const wf_oauth_trusted_keys *keys,
                                  wf_oauth_dpop_replay_cache *replay,
                                  wf_oauth_verified_token **out) {
    wf_oauth_verified_token *bearer = NULL, *dpop = NULL;
    const char *token;
    wf_status status;

    if (!out) return WF_ERR_INVALID_ARG;
    *out = NULL;

    if (authorization && dpop_proof) {
        /* Full DPoP-bound access token + proof. atproto clients carry the
         * access token with the "DPoP" scheme (RFC 9449); accept "Bearer"
         * too for compatibility. The token is the same in both cases. */
        if (strncasecmp(authorization, "DPoP ", 5) == 0) {
            token = authorization + 5;
        } else if (strncasecmp(authorization, "Bearer ", 7) == 0) {
            token = authorization + 7;
        } else {
            return WF_ERR_INVALID_ARG;
        }
        status = wf_oauth_verify_bearer(token, keys, &bearer);
        if (status != WF_OK) return status;
        status = wf_oauth_verify_dpop(dpop_proof, token, http_method,
                                      http_uri, replay, &dpop);
        if (status != WF_OK) {
            wf_oauth_verified_token_free(bearer);
            return status;
        }
        /* Confirmation: if the access token carried a cnf.jkt, it must match
         * the DPoP proof key thumbprint. */
        if (bearer->dpop_jkt &&
            strcmp(bearer->dpop_jkt, dpop->dpop_jkt) != 0) {
            wf_oauth_verified_token_free(bearer);
            wf_oauth_verified_token_free(dpop);
            return WF_ERR_INVALID_ARG;
        }
        free(bearer->dpop_jkt);
        bearer->dpop_jkt = dpop->dpop_jkt;
        dpop->dpop_jkt = NULL;
        bearer->dpop_bound = 1;
        wf_oauth_verified_token_free(dpop);
        *out = bearer;
        return WF_OK;
    }

    if (authorization && !dpop_proof) {
        /* Bearer-only. */
        if (strncasecmp(authorization, "Bearer ", 7) != 0)
            return WF_ERR_INVALID_ARG;
        token = authorization + 7;
        return wf_oauth_verify_bearer(token, keys, out);
    }

    if (!authorization && dpop_proof) {
        /* DPoP-only (no identity token). */
        status = wf_oauth_verify_dpop(dpop_proof, NULL, http_method,
                                      http_uri, replay, out);
        return status;
    }

    return WF_ERR_INVALID_ARG; /* neither present */
}
