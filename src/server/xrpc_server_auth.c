/*
 * xrpc_server_auth.c — service-token / Bearer auth middleware for the XRPC
 * server.
 *
 * Wires wf_server_verify_service_auth (app.bsky service JWT) and
 * wf_oauth_verify_* (OAuth access token) into the server as an auth callback
 * installed via wf_xrpc_server_set_auth_middleware. For each protected request
 * it extracts the Bearer token, resolves the issuer's verification key (did:key
 * is resolved locally, no network), verifies the signature + expiry, checks the
 * `aud`/`lxm` bindings, and — on success — stashes the authenticated DID on the
 * request for the handler to read.
 *
 * Built only when WOLFRAM_BUILD_SERVER=ON.
 */

#include "wolfram/xrpc_server_auth.h"

#include "wolfram/crypto.h"
#include "wolfram/identity.h"
#include "wolfram/oauth/verify.h"
#include "wolfram/server.h"

#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ------------------------------------------------------------------ */
/* Internal context (deep copy of the user config)                     */
/* ------------------------------------------------------------------ */

typedef struct mw_ctx {
    wf_xrpc_server_auth_config cfg; /* owned copy */
} mw_ctx;

/* ------------------------------------------------------------------ */
/* Config lifecycle                                                     */
/* ------------------------------------------------------------------ */

wf_status wf_xrpc_server_auth_config_new(wf_xrpc_server_auth_config **out) {
    wf_xrpc_server_auth_config *cfg;

    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    cfg = (wf_xrpc_server_auth_config *)calloc(1, sizeof(*cfg));
    if (!cfg) {
        return WF_ERR_ALLOC;
    }
    /* Aud/lxm binding checks are on by default; everything else borrowed/NULL. */
    cfg->require_aud = 1;
    cfg->require_lxm = 1;
    *out = cfg;
    return WF_OK;
}

void wf_xrpc_server_auth_config_free(wf_xrpc_server_auth_config *cfg) {
    if (!cfg) {
        return;
    }
    free(cfg->server_did);
    for (size_t i = 0; i < cfg->protected_nsid_count; i++) {
        free(cfg->protected_nsids[i]);
    }
    free(cfg->protected_nsids);
    /* resolver_client, resolver_ctx and trusted_keys are borrowed: not freed. */
    free(cfg);
}

wf_status wf_xrpc_server_auth_config_set_server_did(wf_xrpc_server_auth_config *cfg,
                                                    const char *server_did) {
    char *copy;
    if (!cfg) {
        return WF_ERR_INVALID_ARG;
    }
    copy = server_did ? strdup(server_did) : NULL;
    if (server_did && !copy) {
        return WF_ERR_ALLOC;
    }
    free(cfg->server_did);
    cfg->server_did = copy;
    return WF_OK;
}

wf_status wf_xrpc_server_auth_config_protect(wf_xrpc_server_auth_config *cfg,
                                             const char *nsid_prefix) {
    char **grown;
    char *copy;

    if (!cfg || !nsid_prefix) {
        return WF_ERR_INVALID_ARG;
    }
    copy = strdup(nsid_prefix);
    if (!copy) {
        return WF_ERR_ALLOC;
    }
    grown = (char **)realloc(cfg->protected_nsids,
                             (cfg->protected_nsid_count + 1) * sizeof(char *));
    if (!grown) {
        free(copy);
        return WF_ERR_ALLOC;
    }
    cfg->protected_nsids = grown;
    cfg->protected_nsids[cfg->protected_nsid_count++] = copy;
    return WF_OK;
}

wf_status wf_xrpc_server_auth_config_set_require_aud(wf_xrpc_server_auth_config *cfg,
                                                     int require) {
    if (!cfg) {
        return WF_ERR_INVALID_ARG;
    }
    cfg->require_aud = require;
    return WF_OK;
}

wf_status wf_xrpc_server_auth_config_set_require_lxm(wf_xrpc_server_auth_config *cfg,
                                                     int require) {
    if (!cfg) {
        return WF_ERR_INVALID_ARG;
    }
    cfg->require_lxm = require;
    return WF_OK;
}

wf_status wf_xrpc_server_auth_config_set_resolver(wf_xrpc_server_auth_config *cfg,
                                                  wf_xrpc_server_auth_resolve_fn fn,
                                                  void *ctx) {
    if (!cfg) {
        return WF_ERR_INVALID_ARG;
    }
    cfg->resolver = fn;
    cfg->resolver_ctx = ctx;
    return WF_OK;
}

wf_status wf_xrpc_server_auth_config_set_resolver_client(
    wf_xrpc_server_auth_config *cfg, wf_xrpc_client *client) {
    if (!cfg) {
        return WF_ERR_INVALID_ARG;
    }
    cfg->resolver_client = client;
    return WF_OK;
}

wf_status wf_xrpc_server_auth_config_set_trusted_keys(
    wf_xrpc_server_auth_config *cfg, struct wf_oauth_trusted_keys *keys) {
    if (!cfg) {
        return WF_ERR_INVALID_ARG;
    }
    cfg->trusted_keys = keys;
    return WF_OK;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* True when `nsid` is covered by any protected prefix (exact or prefix+"."). */
static int nsid_protected(const wf_xrpc_server_auth_config *cfg,
                          const char *nsid) {
    if (!nsid || cfg->protected_nsid_count == 0) {
        return 0;
    }
    for (size_t i = 0; i < cfg->protected_nsid_count; i++) {
        const char *p = cfg->protected_nsids[i];
        size_t pl = p ? strlen(p) : 0;
        if (pl == 0) {
            continue;
        }
        if (strncmp(nsid, p, pl) == 0 &&
            (nsid[pl] == '\0' || nsid[pl] == '.')) {
            return 1;
        }
    }
    return 0;
}

/* Extract the token following a "Bearer " scheme (case-insensitive). */
static char *parse_bearer(const char *authz) {
    if (!authz) {
        return NULL;
    }
    if (strncasecmp(authz, "Bearer ", 7) == 0) {
        const char *t = authz + 7;
        while (*t == ' ') {
            t++;
        }
        return strdup(t);
    }
    return NULL;
}

/* Decode the (unverified) JWT payload and pull iss / aud / lxm strings. */
static wf_status decode_jwt_claims(const char *token, char **out_iss,
                                   char **out_aud, char **out_lxm) {
    const char *d1 = strchr(token, '.');
    const char *d2 = d1 ? strchr(d1 + 1, '.') : NULL;
    unsigned char *pbuf = NULL;
    size_t plen = 0;
    cJSON *root = NULL;
    cJSON *v;
    wf_status s;
    size_t payload_len;
    char *payload_seg = NULL;

    if (!d1 || !d2) {
        return WF_ERR_PARSE;
    }
    /* wf_crypto_base64url_decode expects a NUL-terminated string, so copy just
     * the payload segment (between the two dots) into a temporary buffer. */
    payload_len = (size_t)(d2 - (d1 + 1));
    payload_seg = malloc(payload_len + 1);
    if (!payload_seg) {
        return WF_ERR_ALLOC;
    }
    memcpy(payload_seg, d1 + 1, payload_len);
    payload_seg[payload_len] = '\0';
    s = wf_crypto_base64url_decode(payload_seg, &pbuf, &plen);
    free(payload_seg);
    if (s != WF_OK) {
        return s;
    }
    root = cJSON_ParseWithLength((const char *)pbuf, plen);
    free(pbuf);
    if (!root) {
        return WF_ERR_PARSE;
    }
    v = cJSON_GetObjectItemCaseSensitive(root, "iss");
    if (v && cJSON_IsString(v) && v->valuestring) {
        *out_iss = strdup(v->valuestring);
    }
    v = cJSON_GetObjectItemCaseSensitive(root, "aud");
    if (v && cJSON_IsString(v) && v->valuestring) {
        *out_aud = strdup(v->valuestring);
    } else if (v && cJSON_IsArray(v) && cJSON_GetArraySize(v) > 0) {
        cJSON *first = cJSON_GetArrayItem(v, 0);
        if (first && cJSON_IsString(first) && first->valuestring) {
            *out_aud = strdup(first->valuestring);
        }
    }
    v = cJSON_GetObjectItemCaseSensitive(root, "lxm");
    if (v && cJSON_IsString(v) && v->valuestring) {
        *out_lxm = strdup(v->valuestring);
    }
    cJSON_Delete(root);
    return WF_OK;
}

/* True when `aud` equals `server_did` or is `server_did#serviceId`. */
static int aud_matches(const char *aud, const char *server_did) {
    size_t n = strlen(server_did);
    if (strncmp(aud, server_did, n) == 0 &&
        (aud[n] == '\0' || aud[n] == '#')) {
        return 1;
    }
    return 0;
}

/* Built-in resolver: did:key is self-describing (no network); other methods
 * defer to an optional resolver_client (wf_did_resolve). */
static wf_status default_resolver(const char *did, char **out_didkey,
                                  void *ctx) {
    mw_ctx *m = (mw_ctx *)ctx;

    if (!did || !out_didkey) {
        return WF_ERR_INVALID_ARG;
    }
    if (strncmp(did, "did:key:", 8) == 0) {
        *out_didkey = strdup(did);
        return *out_didkey ? WF_OK : WF_ERR_ALLOC;
    }
    /* Non-did:key DIDs require an HTTP client to fetch the DID document. When
     * the caller supplied resolver_client we resolve and extract the
     * verification key; otherwise we decline (production should inject a
     * resolver or a client rather than force network I/O into this path). */
    if (m && m->cfg.resolver_client) {
        wf_did_document doc = {0};
        if (wf_did_resolve(m->cfg.resolver_client, did, &doc) == WF_OK &&
            doc.signing_key) {
            *out_didkey = strdup(doc.signing_key);
            wf_did_document_free(&doc);
            return *out_didkey ? WF_OK : WF_ERR_ALLOC;
        }
        wf_did_document_free(&doc);
    }
    return WF_ERR_NOT_FOUND;
}

/* ------------------------------------------------------------------ */
/* Middleware auth callback                                             */
/* ------------------------------------------------------------------ */

static wf_status mw_auth_cb(wf_xrpc_request *req, void *ctx) {
    mw_ctx *m = (mw_ctx *)ctx;
    const wf_xrpc_server_auth_config *cfg = &m->cfg;
    wf_xrpc_server_auth_resolve_fn resolve =
        cfg->resolver ? cfg->resolver : default_resolver;
    void *rctx = cfg->resolver ? cfg->resolver_ctx : m;

    char *token = NULL;
    char *iss = NULL, *aud = NULL, *lxm = NULL, *didkey = NULL;
    wf_service_auth_claims claims = {0};
    wf_status service_status;
    wf_status rc = WF_ERR_INVALID_ARG;

    /* 1. Unprotected route → allow. */
    if (!nsid_protected(cfg, req->nsid)) {
        return WF_OK;
    }

    /* 2. A token is required. */
    if (!req->auth_header) {
        goto done;
    }
    token = parse_bearer(req->auth_header);
    if (!token) {
        goto done;
    }

    /* 3. Learn the issuer (unverified) so we can resolve its key. */
    if (decode_jwt_claims(token, &iss, &aud, &lxm) != WF_OK) {
        goto done;
    }

    /* 4. Resolve the issuer's verification key. */
    if (resolve(iss, &didkey, rctx) != WF_OK) {
        goto done;
    }

    /* 5. Try app.bsky service JWT. On signature failure, resolve once more so
     * a recently rotated signing key can replace a stale cached value, matching
     * upstream verifyJwt's force-refresh retry. */
    service_status = wf_server_verify_service_auth(token, didkey, 0, &claims);
    if (service_status != WF_OK) {
        free(didkey);
        didkey = NULL;
        wf_service_auth_claims_free(&claims);
        if (resolve(iss, &didkey, rctx) == WF_OK) {
            service_status = wf_server_verify_service_auth(token, didkey, 0,
                                                           &claims);
        }
    }
    if (service_status == WF_OK) {
        if (cfg->require_aud && cfg->server_did && aud &&
            !aud_matches(aud, cfg->server_did)) {
            goto done;
        }
        /* Match verifyJwt: when an lxm is required, the claim must be present
         * and exactly equal to the XRPC method being authorized. */
        if (cfg->require_lxm &&
            (!claims.lxm || !req->nsid ||
             strcmp(claims.lxm, req->nsid) != 0)) {
            goto done;
        }
        req->authed_subject = strdup(claims.iss);
        req->authed_principal_kind = WF_XRPC_PRINCIPAL_SERVICE;
        rc = WF_OK;
        goto done;
    }

    /* 6. Fall back to OAuth access-token (user) verification when keys are
     *    configured. wf_oauth_verify_request honours a DPoP proof when one is
     *    supplied (the server currently surfaces only the Authorization header
     *    on the request, so DPoP is NULL here). */
    if (cfg->trusted_keys) {
        wf_oauth_verified_token *vt = NULL;
        if (wf_oauth_verify_request(req->auth_header, NULL, req->method,
                                    NULL, cfg->trusted_keys, NULL, &vt) ==
                WF_OK &&
            vt->sub) {
            if (cfg->require_aud && cfg->server_did && vt->aud &&
                !aud_matches(vt->aud, cfg->server_did)) {
                wf_oauth_verified_token_free(vt);
                goto done;
            }
            req->authed_subject = strdup(vt->sub);
            req->authed_principal_kind = WF_XRPC_PRINCIPAL_USER;
            wf_oauth_verified_token_free(vt);
            rc = WF_OK;
            goto done;
        }
        if (vt) {
            wf_oauth_verified_token_free(vt);
        }
    }

done:
    free(token);
    free(iss);
    free(aud);
    free(lxm);
    free(didkey);
    wf_service_auth_claims_free(&claims);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Context lifecycle + attachment                                       */
/* ------------------------------------------------------------------ */

static void mw_ctx_free(void *p) {
    mw_ctx *m = (mw_ctx *)p;
    if (!m) {
        return;
    }
    free(m->cfg.server_did);
    for (size_t i = 0; i < m->cfg.protected_nsid_count; i++) {
        free(m->cfg.protected_nsids[i]);
    }
    free(m->cfg.protected_nsids);
    /* resolver_client, resolver_ctx and trusted_keys are borrowed: not freed. */
    free(m);
}

/* Deep-copy `src` into a fresh mw_ctx (owned by the middleware). */
static wf_status mw_ctx_init(mw_ctx *m, const wf_xrpc_server_auth_config *src) {
    memset(m, 0, sizeof(*m));
    m->cfg.require_aud = 1;
    m->cfg.require_lxm = 1;

    if (src->server_did) {
        m->cfg.server_did = strdup(src->server_did);
        if (!m->cfg.server_did) {
            return WF_ERR_ALLOC;
        }
    }
    if (src->protected_nsid_count) {
        m->cfg.protected_nsids =
            (char **)calloc(src->protected_nsid_count, sizeof(char *));
        if (!m->cfg.protected_nsids) {
            return WF_ERR_ALLOC;
        }
        for (size_t i = 0; i < src->protected_nsid_count; i++) {
            m->cfg.protected_nsids[i] = strdup(src->protected_nsids[i]);
            if (!m->cfg.protected_nsids[i]) {
                return WF_ERR_ALLOC;
            }
            m->cfg.protected_nsid_count++;
        }
    }

    /* Copy scalar/borrowed fields (override the defaults above). */
    m->cfg.require_aud = src->require_aud;
    m->cfg.require_lxm = src->require_lxm;
    m->cfg.resolver_client = src->resolver_client;
    m->cfg.resolver = src->resolver;
    m->cfg.resolver_ctx = src->resolver_ctx;
    m->cfg.trusted_keys = src->trusted_keys;
    return WF_OK;
}

wf_status wf_xrpc_server_set_auth_middleware(
    wf_xrpc_server *server, const wf_xrpc_server_auth_config *cfg) {
    mw_ctx *m;

    if (!server || !cfg) {
        return WF_ERR_INVALID_ARG;
    }
    m = (mw_ctx *)calloc(1, sizeof(*m));
    if (!m) {
        return WF_ERR_ALLOC;
    }
    if (mw_ctx_init(m, cfg) != WF_OK) {
        mw_ctx_free(m);
        return WF_ERR_ALLOC;
    }

    /* Install via the server's owned-callback setter so the deep-copied config
     * is freed in wf_xrpc_server_free. */
    wf_xrpc_server_set_auth_callback_owned(server, mw_auth_cb, m, m,
                                           mw_ctx_free);
    return WF_OK;
}
