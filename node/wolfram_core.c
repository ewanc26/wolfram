/*
 * wolfram_core.c -- Wolfram's Node.js addon: libwolfram compiled as a Node
 * module, so a Node consumer executes Wolfram's C instead of carrying a port
 * of it (see docs/bindings-cpp-csharp.md: wrappers only, never re-implement).
 *
 * The surface is the smallest one a Node consumer needs to drop its ports of
 * Wolfram's contracts (Platinum's bridge is the first):
 *
 *   update.h:  compareVersions, versionValid, sha256Hex, parseManifest,
 *              verifySignature
 *   oauth_pairing.h: buildPairBegin, parsePairBegin, parsePairPoll -- the
 *              parsers only; the begin/poll loop stays in the consumer, whose
 *              transport is its own
 *
 * Errors are wolfram's statuses surfaced, not messages: every throw carries
 * an Error whose `code` is the wf_status name (WF_ERR_PARSE and so on), so
 * the consumer maps them into its own error kinds and wording. Nothing here
 * formats prose.
 *
 * Every exported function is synchronous and runs on the JS thread, and
 * nothing shares state across calls: the policy pointers, the manifest struct
 * and the pairing poll struct -- which holds the bearer token for one call and
 * is wiped before this file returns -- all live inside one call, so no
 * consumer-visible lifecycle exists. A worker-thread form would be a separate,
 * explicit API, not a silent flip of this one.
 *
 * Building: cmake -DWOLFRAM_BUILD_NODE=ON with WOLFRAM_NODE_HEADERS_DIR
 * pointing at node's include/node directory; with no directory set, the
 * pinned node headers FetchContent in CMakeLists.txt supplies them. The
 * configuration needs -DCMAKE_POSITION_INDEPENDENT_CODE=ON, because a Node
 * module is a loadable object: the static archive members it links in must
 * be PIC, and libwolfram's own link mode must not change for this target.
 */

#include "wolfram/oauth_pairing.h"
#include "wolfram/update.h"

#include <node_api.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NODE_GYP_MODULE_NAME
#define NODE_GYP_MODULE_NAME wolfram_core
#endif

/* The encode direction lives here: libwolfram exposes hex-decode
 * (wf_sha256_from_hex) but no hex-encode, and the digest and asset SHA-256
 * both leave this binding as lowercase hex. */
static void hexify(const unsigned char *in, size_t len, char *out) {
    static const char digits[] = "0123456789abcdef";
    size_t i;

    for (i = 0; i < len; i++) {
        out[i * 2] = digits[in[i] >> 4];
        out[i * 2 + 1] = digits[in[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

/* Copies `v` as UTF-8 into a fresh buffer and reports its byte length
 * (len_out may be NULL). Throws on a non-string argument or an allocation
 * failure, in which case *threw is set and NULL returns. The caller owns the
 * buffer. */
static char *take_utf8(napi_env env, napi_value v, size_t *len_out,
                       bool *threw) {
    napi_valuetype type;
    size_t len = 0;
    char *out;

    napi_typeof(env, v, &type);
    if (type != napi_string) {
        napi_throw_type_error(env, NULL, "expected a string");
        *threw = true;
        return NULL;
    }
    if (napi_get_value_string_utf8(env, v, NULL, 0, &len) != napi_ok) {
        *threw = true;
        return NULL;
    }
    out = malloc(len + 1);
    if (out == NULL) {
        napi_throw_error(env, "WF_ERR_ALLOC", "out of memory");
        *threw = true;
        return NULL;
    }
    if (napi_get_value_string_utf8(env, v, out, len + 1, &len) != napi_ok) {
        free(out);
        *threw = true;
        return NULL;
    }
    if (len_out != NULL) *len_out = len;
    return out;
}

/* An argument accepted as bytes: a Buffer aliases memory Node owns (`owned`
 * false), a string is a private copy the caller must free. */
typedef struct wf_bytes {
    unsigned char *data;
    size_t len;
    bool owned;
} wf_bytes;

/* A stand-in for the address of a zero-length Buffer: Node may give no
 * backing pointer for one, but libwolfram distinguishes "no argument"
 * (WF_ERR_INVALID_ARG) from an empty byte string, which is a legitimate
 * input to a hash or a signature check. */
static unsigned char wf_zero_byte;

/* Reads `v` as raw bytes when it is a Buffer, or its UTF-8 bytes when it is
 * a string. Consumers hand update.json over as bytes for the signature
 * check and as text for the manifest parse; both forms arrive here. */
static wf_bytes take_bytes(napi_env env, napi_value v, bool *threw) {
    napi_valuetype type;
    void *data;
    wf_bytes out = {NULL, 0, false};

    napi_typeof(env, v, &type);
    if (type == napi_object &&
        napi_get_buffer_info(env, v, &data, &out.len) == napi_ok) {
        out.data = data;
        if (out.data == NULL) out.data = &wf_zero_byte;
        return out;
    }
    if (type == napi_string) {
        out.data = (unsigned char *)take_utf8(env, v, &out.len, threw);
        if (out.data != NULL) out.owned = true;
        return out;
    }
    napi_throw_type_error(env, NULL, "expected a Buffer or a string");
    *threw = true;
    return out;
}

static void release_bytes(wf_bytes b) {
    if (b.owned) free(b.data);
}

static bool set_utf8(napi_env env, napi_value obj, const char *key,
                     const char *value) {
    napi_value s;

    if (napi_create_string_utf8(env, value, NAPI_AUTO_LENGTH, &s) != napi_ok) {
        return false;
    }
    return napi_set_named_property(env, obj, key, s) == napi_ok;
}

static bool set_bool(napi_env env, napi_value obj, const char *key,
                     bool value) {
    napi_value b;

    if (napi_get_boolean(env, value, &b) != napi_ok) return false;
    return napi_set_named_property(env, obj, key, b) == napi_ok;
}

static napi_value make_number(napi_env env, double value) {
    napi_value n;

    if (napi_create_double(env, value, &n) != napi_ok) return NULL;
    return n;
}

/* Throws an Error whose `code` is the status name and whose message is
 * diagnostic only. Consumers map codes; no prose is part of the contract. */
static void throw_status(napi_env env, wf_status status, const char *context) {
    const char *code;
    char text[128];
    napi_value err, message, code_value;

    switch (status) {
        case WF_ERR_INVALID_ARG:
            code = "WF_ERR_INVALID_ARG";
            break;
        case WF_ERR_PARSE:
            code = "WF_ERR_PARSE";
            break;
        case WF_ERR_VALIDATION:
            code = "WF_ERR_VALIDATION";
            break;
        default:
            code = "WF_ERR_UNKNOWN";
            break;
    }
    snprintf(text, sizeof(text), "%s failed (%s)", context, code);
    if (napi_create_string_utf8(env, text, NAPI_AUTO_LENGTH, &message) ==
            napi_ok &&
        napi_create_error(env, NULL, message, &err) == napi_ok &&
        napi_create_string_utf8(env, code, NAPI_AUTO_LENGTH, &code_value) ==
            napi_ok &&
        napi_set_named_property(env, err, "code", code_value) == napi_ok) {
        napi_throw(env, err);
        return;
    }
    napi_throw_error(env, code, context);
}

/* ---- compareVersions, versionValid ------------------------------------- */

static napi_value compare_versions(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value argv[2], result = NULL;
    char *a = NULL, *b = NULL;
    bool threw = false;
    int err = 0;
    napi_handle_scope scope;

    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok ||
        argc != 2) {
        napi_throw_type_error(env, NULL,
                              "compareVersions(a, b) takes two strings");
        return NULL;
    }
    if (napi_open_handle_scope(env, &scope) != napi_ok) return NULL;
    a = take_utf8(env, argv[0], NULL, &threw);
    if (!threw) b = take_utf8(env, argv[1], NULL, &threw);
    if (!threw) {
        int cmp = wf_update_compare_versions(a, b, &err);
        if (err != 0) {
            napi_throw_type_error(env, "WF_ERR_PARSE", "not a version");
        } else {
            result = make_number(env, (double)cmp);
        }
    }
    free(a);
    free(b);
    napi_close_handle_scope(env, scope);
    return result;
}

static napi_value version_valid(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1], result = NULL, boolean;
    char *v = NULL;
    bool threw = false;
    napi_handle_scope scope;

    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok ||
        argc != 1) {
        napi_throw_type_error(env, NULL, "versionValid(v) takes one string");
        return NULL;
    }
    if (napi_open_handle_scope(env, &scope) != napi_ok) return NULL;
    v = take_utf8(env, argv[0], NULL, &threw);
    if (!threw && napi_get_boolean(env, wf_update_version_valid(v) != 0,
                                   &boolean) == napi_ok) {
        result = boolean;
    }
    free(v);
    napi_close_handle_scope(env, scope);
    return result;
}

/* ---- sha256Hex --------------------------------------------------------- */

static napi_value sha256_hex(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1], result = NULL, s;
    bool threw = false;
    napi_handle_scope scope;
    unsigned char digest[WF_SHA256_DIGEST_LEN];
    char hex[WF_SHA256_DIGEST_LEN * 2 + 1];

    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok ||
        argc != 1) {
        napi_throw_type_error(env, NULL, "sha256Hex(data) takes one Buffer");
        return NULL;
    }
    if (napi_open_handle_scope(env, &scope) != napi_ok) return NULL;
    wf_bytes data = take_bytes(env, argv[0], &threw);
    if (!threw) {
        wf_sha256_buffer(data.data, data.len, digest);
        hexify(digest, sizeof(digest), hex);
        if (napi_create_string_utf8(env, hex, NAPI_AUTO_LENGTH, &s) ==
            napi_ok) {
            result = s;
        }
    }
    release_bytes(data);
    napi_close_handle_scope(env, scope);
    return result;
}

/* ---- parseManifest ----------------------------------------------------- */

/* Fills wf_update_policy from a JS object { maxSize?, app?, urlPrefix? }.
 * Zeroed fields mean "caller default", which wf_update_parse_manifest reads
 * as its documented policies: 64 MiB, no app or URL constraint. The strings
 * must outlive the parse call, so they are returned through app_out and
 * url_out and freed by the caller after C has finished with them. */
static bool read_policy(napi_env env, napi_value obj, wf_update_policy *policy,
                        char **app_out, char **url_out, bool *threw) {
    napi_value key;
    napi_valuetype type;
    char *app = NULL;
    char *url = NULL;
    double max_size = 0;

    *app_out = NULL;
    *url_out = NULL;
    napi_typeof(env, obj, &type);
    if (type == napi_undefined || type == napi_null) return true;
    if (type != napi_object) {
        napi_throw_type_error(env, NULL, "policy must be an object");
        *threw = true;
        return false;
    }
    if (napi_get_named_property(env, obj, "maxSize", &key) == napi_ok) {
        napi_typeof(env, key, &type);
        if (type == napi_number &&
            napi_get_value_double(env, key, &max_size) == napi_ok) {
            if (max_size < 0 || max_size > 4294967295.0) {
                napi_throw_type_error(env, NULL, "maxSize out of range");
                *threw = true;
                return false;
            }
            policy->max_size = (unsigned long)max_size;
        }
    }
    if (napi_get_named_property(env, obj, "app", &key) == napi_ok) {
        napi_typeof(env, key, &type);
        if (type == napi_string) {
            app = take_utf8(env, key, NULL, threw);
            if (*threw) return false;
            policy->app = app;
        }
    }
    if (napi_get_named_property(env, obj, "urlPrefix", &key) == napi_ok) {
        napi_typeof(env, key, &type);
        if (type == napi_string) {
            url = take_utf8(env, key, NULL, threw);
            if (*threw) {
                free(app);
                return false;
            }
            policy->url_prefix = url;
        }
    }
    *app_out = app;
    *url_out = url;
    return true;
}

/* Builds the manifest object the TS consumers know: { app, version, notes,
 * asset: { name, url, size, sha256 }, hasSignature }, sha256 lowercase hex.
 * Buffer sizes in this struct are bounded by WF_UPDATE_*_MAX, and every put
 * in the caller will hold. */
static napi_value parse_manifest(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value argv[2], obj, asset, s;
    wf_bytes body = {NULL, 0, false};
    bool threw = false;
    wf_update_policy policy;
    wf_update_manifest manifest;
    char *policy_app = NULL, *policy_url = NULL;
    wf_status status;
    napi_handle_scope scope;
    char hex[WF_SHA256_DIGEST_LEN * 2 + 1];
    napi_value result = NULL;

    memset(&policy, 0, sizeof(policy));
    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok ||
        argc < 1) {
        napi_throw_type_error(env, NULL, "parseManifest(body, policy?)");
        return NULL;
    }
    if (napi_open_handle_scope(env, &scope) != napi_ok) return NULL;
    body = take_bytes(env, argv[0], &threw);
    if (threw) goto out;
    if (!read_policy(env, argv[1], &policy, &policy_app, &policy_url, &threw) ||
        threw) {
        goto out;
    }
    status = wf_update_parse_manifest((const char *)body.data, body.len,
                                      &policy, &manifest);
    if (status != WF_OK) {
        throw_status(env, status, "wf_update_parse_manifest");
        goto out;
    }
    if (napi_create_object(env, &obj) != napi_ok) goto out;
    if (!set_utf8(env, obj, "app", manifest.app) ||
        !set_utf8(env, obj, "version", manifest.version) ||
        !set_utf8(env, obj, "notes", manifest.notes)) {
        goto out;
    }
    if (napi_create_object(env, &asset) != napi_ok) goto out;
    if (!set_utf8(env, asset, "name", manifest.asset.name) ||
        !set_utf8(env, asset, "url", manifest.asset.url) ||
        napi_create_double(env, (double)manifest.asset.size, &s) != napi_ok ||
        napi_set_named_property(env, asset, "size", s) != napi_ok) {
        goto out;
    }
    hexify(manifest.asset.sha256, WF_SHA256_DIGEST_LEN, hex);
    if (!set_utf8(env, asset, "sha256", hex) ||
        napi_set_named_property(env, obj, "asset", asset) != napi_ok) {
        goto out;
    }
    if (!set_bool(env, obj, "hasSignature", manifest.has_signature)) goto out;
    result = obj;

out:
    free(policy_app);
    free(policy_url);
    release_bytes(body);
    napi_close_handle_scope(env, scope);
    return result;
}

/* ---- verifySignature --------------------------------------------------- */

/* The public key arrives as 64 hex characters, which is what the TS
 * consumers store (bridge/src/update/update_key.ts), so no second
 * representation is carried around. It is decoded with the module's own
 * generic decoder, wf_sha256_from_hex: the same length-and-hex rules, not a
 * digest-specific re-implementation. A bad key is WF_ERR_PARSE, so the
 * consumer maps it with the rest. */
static napi_value verify_signature(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value argv[3], result = NULL, boolean;
    wf_bytes body = {NULL, 0, false};
    wf_bytes sig = {NULL, 0, false};
    char *pk = NULL;
    bool threw = false;
    unsigned char pk_bytes[32];
    wf_status status;
    napi_handle_scope scope;

    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok ||
        argc != 3) {
        napi_throw_type_error(env, NULL,
                              "verifySignature(manifest, sig, pkHex)");
        return NULL;
    }
    if (napi_open_handle_scope(env, &scope) != napi_ok) return NULL;
    body = take_bytes(env, argv[0], &threw);
    if (!threw) sig = take_bytes(env, argv[1], &threw);
    if (!threw) pk = take_utf8(env, argv[2], NULL, &threw);
    if (!threw) {
        if (wf_sha256_from_hex(pk, strlen(pk), pk_bytes) != WF_OK) {
            napi_throw_type_error(env, "WF_ERR_PARSE",
                                  "public key is not 64 hex characters");
        } else {
            status = wf_update_verify_signature(
                body.data, body.len, (const char *)sig.data, sig.len, pk_bytes);
            if (status != WF_OK) {
                throw_status(env, status, "wf_update_verify_signature");
            } else if (napi_get_boolean(env, true, &boolean) == napi_ok) {
                result = boolean;
            }
        }
    }
    free(pk);
    release_bytes(body);
    release_bytes(sig);
    napi_close_handle_scope(env, scope);
    return result;
}

/* ---- oauth pairing ------------------------------------------------------ */

/* Builds the begin body. The output buffer is sized here, from the input:
 * cJSON's own printing doubles `"` and `\` and passes every other byte of a
 * control-character-free handle through untouched, so twice the UTF-8 length
 * plus slack cannot be exceeded. The consumer keeps no length limit beyond
 * allocation, as its port had. */
static napi_value build_pair_begin(napi_env env, napi_callback_info info) {
    size_t argc = 1, handle_len = 0;
    napi_value argv[1], result = NULL;
    char *handle = NULL, *body = NULL;
    bool threw = false;
    size_t cap;
    wf_status st;
    napi_handle_scope scope;

    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok ||
        argc != 1) {
        napi_throw_type_error(env, NULL,
                              "buildPairBegin(handle) takes one string");
        return NULL;
    }
    if (napi_open_handle_scope(env, &scope) != napi_ok) return NULL;
    handle = take_utf8(env, argv[0], &handle_len, &threw);
    if (!threw) {
        /* A JS string may carry NUL that strlen would swallow; the C contract
         * rejects a control character in the handle, and NUL is one, so it is
         * surfaced here rather than silently truncated. */
        if (memchr(handle, '\0', handle_len) != NULL) {
            napi_throw_type_error(env, "WF_ERR_INVALID_ARG",
                                  "handle contains a NUL");
        } else {
            cap = handle_len * 2 + 24;
            body = malloc(cap);
            if (body == NULL) {
                napi_throw_error(env, "WF_ERR_ALLOC", "out of memory");
            } else {
                st = wf_oauth_pair_build_begin(handle, body, cap);
                if (st != WF_OK) {
                    throw_status(env, st, "wf_oauth_pair_build_begin");
                } else {
                    napi_value s;
                    if (napi_create_string_utf8(env, body, NAPI_AUTO_LENGTH,
                                                &s) == napi_ok) {
                        result = s;
                    }
                }
            }
        }
    }
    free(handle);
    free(body);
    napi_close_handle_scope(env, scope);
    return result;
}

static napi_value parse_pair_begin(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1], obj = NULL, result = NULL, num;
    wf_bytes body = {NULL, 0, false};
    bool threw = false;
    wf_status st;
    wf_oauth_pair_begin begin;
    napi_handle_scope scope;

    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok ||
        argc != 1) {
        napi_throw_type_error(
            env, NULL, "parsePairBegin(body) takes a Buffer or a string");
        return NULL;
    }
    if (napi_open_handle_scope(env, &scope) != napi_ok) return NULL;
    body = take_bytes(env, argv[0], &threw);
    if (!threw) {
        st = wf_oauth_pair_parse_begin((const char *)body.data, body.len,
                                       &begin);
        if (st != WF_OK) {
            throw_status(env, st, "wf_oauth_pair_parse_begin");
        } else if (napi_create_object(env, &obj) == napi_ok &&
                   set_utf8(env, obj, "pairCode", begin.pair_code) &&
                   set_utf8(env, obj, "pairUrl", begin.pair_url) &&
                   napi_create_int64(env, begin.expires_at, &num) == napi_ok &&
                   napi_set_named_property(env, obj, "expiresAt", num) ==
                       napi_ok) {
            result = obj;
        }
        memset(&begin, 0, sizeof(begin));
    }
    release_bytes(body);
    napi_close_handle_scope(env, scope);
    return result;
}

/* The poll struct holds the bearer token; it is wiped on every path out of
 * parse_pair_poll, after these strings have been copied into JS. */
static bool set_poll(napi_env env, napi_value obj,
                     const wf_oauth_pair_poll *poll) {
    const char *state;
    napi_value s;

    switch (poll->state) {
        case WF_OAUTH_POLL_PENDING:
            state = "pending";
            break;
        case WF_OAUTH_POLL_COMPLETE:
            state = "complete";
            break;
        default:
            state = "error";
            break;
    }
    if (napi_create_string_utf8(env, state, NAPI_AUTO_LENGTH, &s) != napi_ok ||
        napi_set_named_property(env, obj, "state", s) != napi_ok) {
        return false;
    }
    if (poll->state == WF_OAUTH_POLL_COMPLETE)
        return set_utf8(env, obj, "token", poll->token) &&
               set_utf8(env, obj, "handle", poll->handle) &&
               set_utf8(env, obj, "did", poll->did) &&
               set_utf8(env, obj, "service", poll->service);
    if (poll->state == WF_OAUTH_POLL_ERROR)
        return set_utf8(env, obj, "message", poll->message);
    return true;
}

static napi_value parse_pair_poll(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1], obj = NULL, result = NULL;
    wf_bytes body = {NULL, 0, false};
    bool threw = false;
    wf_status st;
    wf_oauth_pair_poll poll;
    napi_handle_scope scope;

    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok ||
        argc != 1) {
        napi_throw_type_error(env, NULL,
                              "parsePairPoll(body) takes a Buffer or a string");
        return NULL;
    }
    if (napi_open_handle_scope(env, &scope) != napi_ok) return NULL;
    memset(&poll, 0, sizeof(poll));
    body = take_bytes(env, argv[0], &threw);
    if (!threw) {
        st = wf_oauth_pair_parse_poll((const char *)body.data, body.len, &poll);
        if (st != WF_OK) {
            throw_status(env, st, "wf_oauth_pair_parse_poll");
        } else if (napi_create_object(env, &obj) == napi_ok &&
                   set_poll(env, obj, &poll)) {
            result = obj;
        }
        wf_oauth_pair_poll_wipe(&poll);
    }
    release_bytes(body);
    napi_close_handle_scope(env, scope);
    return result;
}

/* ---- module ------------------------------------------------------------ */

static napi_value init(napi_env env, napi_value exports) {
    napi_property_descriptor props[] = {
        {"compareVersions", NULL, compare_versions, NULL, NULL, NULL,
         napi_default, NULL},
        {"versionValid", NULL, version_valid, NULL, NULL, NULL, napi_default,
         NULL},
        {"sha256Hex", NULL, sha256_hex, NULL, NULL, NULL, napi_default, NULL},
        {"parseManifest", NULL, parse_manifest, NULL, NULL, NULL, napi_default,
         NULL},
        {"verifySignature", NULL, verify_signature, NULL, NULL, NULL,
         napi_default, NULL},
        {"buildPairBegin", NULL, build_pair_begin, NULL, NULL, NULL,
         napi_default, NULL},
        {"parsePairBegin", NULL, parse_pair_begin, NULL, NULL, NULL,
         napi_default, NULL},
        {"parsePairPoll", NULL, parse_pair_poll, NULL, NULL, NULL, napi_default,
         NULL},
    };
    size_t count = sizeof(props) / sizeof(props[0]);

    if (napi_define_properties(env, exports, count, props) != napi_ok) {
        return NULL;
    }
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, init)
