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
 *   post_display.h: postDisplay(recordText?, embedText?) -- the display model
 *              of a post from its raw record/embed JSON; fetching (network)
 *              stays in the consumer
 *   chat_group.h: chatConvoIsGroup(convoText), chatJoinLinkUrl(code),
 *              chatJoinLinkIsEnabled(status) -- group-conversation helpers
 *              over a parsed getConvo body
 *   ageassurance_typed.h: ageassuranceParseBegin/Config/State(jsonText) --
 *              the app.bsky.ageassurance response parsers
 *   notification_v2_typed.h: notifV2PreferencesParse(jsonText),
 *              notifV2PreferencesBuild(prefs), notifV2ParsePutActivity-
 *              Subscription(jsonText), notifV2ParseListActivitySubscriptions
 *              (jsonText) -- the app.bsky.notification v2 parsers/builder
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

#include "wolfram/ageassurance_typed.h"
#include "wolfram/chat_group.h"
#include "wolfram/chat_typed.h"
#include "wolfram/notification_v2_typed.h"
#include "wolfram/oauth_pairing.h"
#include "wolfram/post_display.h"
#include "wolfram/update.h"

#include <cJSON.h>

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

/* ---- post_display -------------------------------------------------------- */

/* A nullable C string to JS: NULL becomes JS null, "absent" for a member the
 * consumer can distinguish from empty. */
static bool set_utf8_or_null(napi_env env, napi_value obj, const char *key,
                             const char *value) {
    napi_value s;

    if (value == NULL) {
        if (napi_get_null(env, &s) != napi_ok) return false;
        return napi_set_named_property(env, obj, key, s) == napi_ok;
    }
    if (napi_create_string_utf8(env, value, NAPI_AUTO_LENGTH, &s) != napi_ok)
        return false;
    return napi_set_named_property(env, obj, key, s) == napi_ok;
}

/* Wolfram's own post_display contract takes an already-parsed tree; the addon
 * takes JSON text, so a text that does not parse never reaches Wolfram's C and
 * is surfaced here as WF_ERR_PARSE. */
typedef struct wf_json_arg {
    cJSON *tree; /* NULL when the JS argument was absent */
} wf_json_arg;

/* Reads one JSON-text argument of postDisplay: arguments beyond the supplied
 * ones, and an explicit JS undefined or null, mean "absent". Wolfram's own
 * post_display contract takes an already-parsed tree; the addon takes JSON
 * text, so a text that does not parse never reaches Wolfram's C and is
 * surfaced here as WF_ERR_PARSE. */
static bool take_json(napi_env env, napi_value argv[], size_t i, size_t argc,
                      wf_json_arg *out, bool *threw) {
    wf_bytes b = {NULL, 0, false};
    napi_valuetype type;

    out->tree = NULL;
    if (argc <= i) return true; /* absent member */
    napi_typeof(env, argv[i], &type);
    if (type == napi_undefined || type == napi_null) return true;
    b = take_bytes(env, argv[i], threw);
    if (*threw) return false;
    out->tree = cJSON_ParseWithLength((const char *)b.data, b.len);
    release_bytes(b);
    if (out->tree == NULL) {
        throw_status(env, WF_ERR_PARSE, "postDisplay");
        *threw = true;
        return false;
    }
    return true;
}

static bool set_int(napi_env env, napi_value obj, const char *key, double v) {
    napi_value n;

    if (napi_create_double(env, v, &n) != napi_ok) return false;
    return napi_set_named_property(env, obj, key, n) == napi_ok;
}

static const char *facet_kind_name(wf_facet_kind kind) {
    switch (kind) {
        case WF_FACET_LINK:
            return "link";
        case WF_FACET_MENTION:
            return "mention";
        default:
            return "tag";
    }
}

static const char *embed_kind_name(wf_post_embed_kind kind) {
    switch (kind) {
        case WF_EMBED_NONE:
            return "none";
        case WF_EMBED_IMAGES:
            return "images";
        case WF_EMBED_VIDEO:
            return "video";
        case WF_EMBED_EXTERNAL:
            return "external";
        case WF_EMBED_RECORD:
            return "record";
        case WF_EMBED_RECORD_WITH_MEDIA:
            return "recordWithMedia";
        default:
            return "unknown";
    }
}

/* Fills one facet/images member object; private to this surface, no throw. */
static bool make_facet(napi_env env, const wf_display_facet *f,
                       napi_value *out) {
    napi_value o, kind;

    if (napi_create_object(env, &o) != napi_ok) return false;
    if (napi_create_string_utf8(env, facet_kind_name(f->kind), NAPI_AUTO_LENGTH,
                                &kind) != napi_ok ||
        napi_set_named_property(env, o, "kind", kind) != napi_ok)
        return false;
    if (set_int(env, o, "byteStart", (double)f->byte_start) == false ||
        set_int(env, o, "byteEnd", (double)f->byte_end) == false)
        return false;
    if (!set_utf8_or_null(env, o, "target", f->target)) return false;
    *out = o;
    return true;
}

static bool make_image(napi_env env, const wf_display_image *im,
                       napi_value *out) {
    napi_value o;

    if (napi_create_object(env, &o) != napi_ok) return false;
    if (!set_utf8_or_null(env, o, "thumb", im->thumb) ||
        !set_utf8_or_null(env, o, "alt", im->alt))
        return false;
    if (set_int(env, o, "width", im->width) == false ||
        set_int(env, o, "height", im->height) == false)
        return false;
    *out = o;
    return true;
}

/* Assembles the whole display object. Returns false only on a pure napi
 * failure (the exception is already pending); shapes that Wolfram's C leaves
 * empty are empty here, never errors. */
static bool make_display(napi_env env, const wf_post_display *d,
                         napi_value *out) {
    napi_value o, arr, item, sub;
    size_t i;

    if (napi_create_object(env, &o) != napi_ok) return false;
    if (!set_utf8_or_null(env, o, "text", d->text)) return false;
    if (!set_utf8_or_null(env, o, "createdAt", d->created_at)) return false;

    if (napi_create_array_with_length(env, d->facet_count, &arr) != napi_ok)
        return false;
    for (i = 0; i < d->facet_count; ++i) {
        if (!make_facet(env, &d->facets[i], &item) ||
            napi_set_element(env, arr, (uint32_t)i, item) != napi_ok)
            return false;
    }
    if (napi_set_named_property(env, o, "facets", arr) != napi_ok) return false;

    if (napi_create_array_with_length(env, d->image_count, &arr) != napi_ok)
        return false;
    for (i = 0; i < d->image_count; ++i) {
        if (!make_image(env, &d->images[i], &item) ||
            napi_set_element(env, arr, (uint32_t)i, item) != napi_ok)
            return false;
    }
    if (napi_set_named_property(env, o, "images", arr) != napi_ok) return false;

    if (napi_create_string_utf8(env, embed_kind_name(d->embed_kind),
                                NAPI_AUTO_LENGTH, &sub) != napi_ok ||
        napi_set_named_property(env, o, "embedKind", sub) != napi_ok)
        return false;
    if (!set_bool(env, o, "isReply", d->is_reply != 0)) return false;

    if (napi_create_object(env, &sub) != napi_ok) return false;
    if (!set_utf8_or_null(env, sub, "title", d->external_title) ||
        !set_utf8_or_null(env, sub, "uri", d->external_uri) ||
        !set_utf8_or_null(env, sub, "description", d->external_description) ||
        !set_utf8_or_null(env, sub, "thumb", d->external_thumb))
        return false;
    if (napi_set_named_property(env, o, "external", sub) != napi_ok)
        return false;

    if (napi_create_object(env, &sub) != napi_ok) return false;
    if (!set_utf8_or_null(env, sub, "thumb", d->video_thumb) ||
        !set_utf8_or_null(env, sub, "alt", d->video_alt))
        return false;
    if (set_int(env, sub, "width", d->video_width) == false ||
        set_int(env, sub, "height", d->video_height) == false)
        return false;
    if (napi_set_named_property(env, o, "video", sub) != napi_ok) return false;

    if (napi_create_object(env, &sub) != napi_ok) return false;
    if (!set_utf8_or_null(env, sub, "uri", d->quote_uri) ||
        !set_utf8_or_null(env, sub, "authorHandle", d->quote_author_handle) ||
        !set_utf8_or_null(env, sub, "text", d->quote_text))
        return false;
    if (napi_set_named_property(env, o, "quote", sub) != napi_ok) return false;

    *out = o;
    return true;
}

/* postDisplay(recordText?, embedText?) fills the display model of
 * post_display.h from raw record/embed JSON text. Both arguments are absent
 * or a Buffer/string of JSON; the embed argument may be omitted. Returns the
 * display object (text, createdAt, validated facets, images, embedKind,
 * isReply, external, video, quote). Hostile data yields empty members per the
 * C contract, never a throw; only unparseable intake text throws
 * WF_ERR_PARSE, and only INT_ARG/ALLOC failures surface as statuses. */
static napi_value post_display(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value argv[2], result = NULL;
    wf_json_arg rec = {NULL}, emb = {NULL};
    bool threw = false;
    wf_agent_post_view post;
    wf_post_display d;
    wf_status st;
    napi_handle_scope scope;

    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok ||
        argc > 2) {
        napi_throw_type_error(env, NULL,
                              "postDisplay(record, embed?) takes JSON text");
        return NULL;
    }
    if (napi_open_handle_scope(env, &scope) != napi_ok) return NULL;
    if (take_json(env, argv, 0, argc, &rec, &threw) &&
        take_json(env, argv, 1, argc, &emb, &threw)) {
        memset(&post, 0, sizeof(post));
        post.record = rec.tree;
        post.embed = emb.tree;
        memset(&d, 0, sizeof(d));
        st = wf_agent_post_view_display(&post, &d);
        if (st != WF_OK) {
            throw_status(env, st, "wf_agent_post_view_display");
        } else if (!make_display(env, &d, &result)) {
            /* A pure napi failure; the exception is already pending. */
        }
        if (st == WF_OK) wf_post_display_free(&d);
    }
    cJSON_Delete(rec.tree);
    cJSON_Delete(emb.tree);
    napi_close_handle_scope(env, scope);
    return result;
}

/* ---- cJSON -> JS value --------------------------------------------------- */

/* Converts a cJSON subtree (an owned detached `extra`/`regions` member) into
 * the matching JS value, so consumers never handle Wolfram's JSON tree. */
static bool cjson_to_js(napi_env env, const cJSON *node, napi_value *out) {
    if (node == NULL || cJSON_IsNull(node)) {
        return napi_get_null(env, out) == napi_ok;
    }
    if (cJSON_IsTrue(node)) return napi_get_boolean(env, true, out) == napi_ok;
    if (cJSON_IsFalse(node))
        return napi_get_boolean(env, false, out) == napi_ok;
    if (cJSON_IsNumber(node)) {
        return napi_create_double(env, node->valuedouble, out) == napi_ok;
    }
    if (cJSON_IsString(node)) {
        return napi_create_string_utf8(
                   env, node->valuestring != NULL ? node->valuestring : "",
                   NAPI_AUTO_LENGTH, out) == napi_ok;
    }
    if (cJSON_IsArray(node)) {
        napi_value arr;
        cJSON *item;
        uint32_t i = 0;
        if (napi_create_array_with_length(env, (size_t)cJSON_GetArraySize(node),
                                          &arr) != napi_ok)
            return false;
        cJSON_ArrayForEach(item, node) {
            napi_value v;
            if (!cjson_to_js(env, item, &v) ||
                napi_set_element(env, arr, i++, v) != napi_ok)
                return false;
        }
        *out = arr;
        return true;
    }
    if (cJSON_IsObject(node)) {
        napi_value obj;
        cJSON *item;
        if (napi_create_object(env, &obj) != napi_ok) return false;
        cJSON_ArrayForEach(item, node) {
            napi_value v;
            if (!cjson_to_js(env, item, &v) ||
                napi_set_named_property(
                    env, obj, item->string != NULL ? item->string : "", v) !=
                    napi_ok)
                return false;
        }
        *out = obj;
        return true;
    }
    return napi_get_undefined(env, out) == napi_ok;
}

/* ---- chat_group ---------------------------------------------------------- */

/* chatConvoIsGroup(convoText) parses a getConvo body with
 * wf_agent_parse_convo and answers wf_chat_convo_is_group. Unparseable or
 * non-convo text throws the parser's status. */
static napi_value chat_convo_is_group(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1], result = NULL;
    wf_bytes body = {NULL, 0, false};
    bool threw = false;
    wf_chat_convo convo;
    wf_status st;
    napi_handle_scope scope;

    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok ||
        argc != 1) {
        napi_throw_type_error(env, NULL,
                              "chatConvoIsGroup(convoText) takes JSON text");
        return NULL;
    }
    if (napi_open_handle_scope(env, &scope) != napi_ok) return NULL;
    body = take_bytes(env, argv[0], &threw);
    if (!threw) {
        memset(&convo, 0, sizeof(convo));
        st = wf_agent_parse_convo((const char *)body.data, body.len, &convo);
        if (st != WF_OK) {
            throw_status(env, st, "wf_agent_parse_convo");
        } else {
            napi_value b;
            if (napi_get_boolean(env, wf_chat_convo_is_group(&convo) != 0,
                                 &b) == napi_ok)
                result = b;
        }
        wf_chat_convo_free(&convo);
    }
    release_bytes(body);
    napi_close_handle_scope(env, scope);
    return result;
}

/* chatJoinLinkUrl(code) is wf_chat_join_link_url with a fixed buffer: the
 * https://bsky.app/chat/<code> address, or "" when the C rejects the code
 * (empty, or too long to fit). */
static napi_value chat_join_link_url(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1], result = NULL;
    char *code = NULL;
    size_t code_len = 0;
    bool threw = false;
    char url[512];
    napi_handle_scope scope;

    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok ||
        argc != 1) {
        napi_throw_type_error(env, NULL,
                              "chatJoinLinkUrl(code) takes a string");
        return NULL;
    }
    if (napi_open_handle_scope(env, &scope) != napi_ok) return NULL;
    code = take_utf8(env, argv[0], &code_len, &threw);
    if (!threw) {
        if (wf_chat_join_link_url(code, url, sizeof(url)) &&
            napi_create_string_utf8(env, url, NAPI_AUTO_LENGTH, &result) !=
                napi_ok) {
            result = NULL; /* pure napi failure, exception pending */
        } else if (result == NULL) {
            if (napi_create_string_utf8(env, "", 0, &result) != napi_ok)
                result = NULL;
        }
    }
    free(code);
    napi_close_handle_scope(env, scope);
    return result;
}

/* chatJoinLinkIsEnabled(status) is wf_chat_join_link_is_enabled; a JS null
 * argument maps to the C NULL. */
static napi_value chat_join_link_is_enabled(napi_env env,
                                            napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1], result = NULL;
    napi_valuetype type;
    char *status = NULL;
    bool threw = false;
    napi_handle_scope scope;

    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok ||
        argc != 1) {
        napi_throw_type_error(env, NULL,
                              "chatJoinLinkIsEnabled(status) takes a string "
                              "or null");
        return NULL;
    }
    if (napi_open_handle_scope(env, &scope) != napi_ok) return NULL;
    napi_typeof(env, argv[0], &type);
    if (type == napi_string) {
        status = take_utf8(env, argv[0], NULL, &threw);
    } else if (type != napi_null && type != napi_undefined) {
        napi_throw_type_error(env, NULL, "expected a string or null");
        threw = true;
    }
    if (!threw) {
        napi_value b;
        if (napi_get_boolean(env, wf_chat_join_link_is_enabled(status) != 0,
                             &b) == napi_ok)
            result = b;
    }
    free(status);
    napi_close_handle_scope(env, scope);
    return result;
}

/* ---- ageassurance_typed ---------------------------------------------------
 */

/* Shared intake: exactly one JSON-text argument, returned as bytes. */
static bool take_one_json(napi_env env, napi_callback_info info,
                          const char *signature, wf_bytes *body, bool *threw,
                          napi_handle_scope *scope) {
    size_t argc = 1;
    napi_value argv[1];

    *threw = false;
    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok ||
        argc != 1) {
        napi_throw_type_error(env, NULL, signature);
        return false;
    }
    if (napi_open_handle_scope(env, scope) != napi_ok) return false;
    *body = take_bytes(env, argv[0], threw);
    return !*threw;
}

static bool set_ageassurance_state(napi_env env, napi_value obj,
                                   const wf_ageassurance_begin *s) {
    return set_utf8_or_null(env, obj, "lastInitiatedAt",
                            s->last_initiated_at) &&
           set_utf8_or_null(env, obj, "status", s->status) &&
           set_utf8_or_null(env, obj, "access", s->access);
}

/* ageassuranceParseBegin(jsonText) parses an app.bsky.ageassurance.begin body
 * into { lastInitiatedAt, status, access }. */
static napi_value ageassurance_parse_begin(napi_env env,
                                           napi_callback_info info) {
    wf_bytes body = {NULL, 0, false};
    bool threw;
    napi_handle_scope scope;
    napi_value result = NULL, obj;
    wf_ageassurance_begin out;
    wf_status st;

    if (!take_one_json(env, info,
                       "ageassuranceParseBegin(jsonText) takes "
                       "JSON text",
                       &body, &threw, &scope))
        return NULL;
    memset(&out, 0, sizeof(out));
    st = wf_ageassurance_parse_begin((const char *)body.data, body.len, &out);
    if (st != WF_OK) {
        throw_status(env, st, "wf_ageassurance_parse_begin");
    } else if (napi_create_object(env, &obj) == napi_ok &&
               set_ageassurance_state(env, obj, &out)) {
        result = obj;
    }
    wf_ageassurance_begin_free(&out);
    release_bytes(body);
    napi_close_handle_scope(env, scope);
    return result;
}

/* ageassuranceParseConfig(jsonText) parses a getConfig body into
 * { regions }, where regions is the owned array as a plain JS value. */
static napi_value ageassurance_parse_config(napi_env env,
                                            napi_callback_info info) {
    wf_bytes body = {NULL, 0, false};
    bool threw;
    napi_handle_scope scope;
    napi_value result = NULL, obj, regions;
    wf_ageassurance_config out;
    wf_status st;

    if (!take_one_json(env, info,
                       "ageassuranceParseConfig(jsonText) takes "
                       "JSON text",
                       &body, &threw, &scope))
        return NULL;
    memset(&out, 0, sizeof(out));
    st = wf_ageassurance_parse_config((const char *)body.data, body.len, &out);
    if (st != WF_OK) {
        throw_status(env, st, "wf_ageassurance_parse_config");
    } else if (napi_create_object(env, &obj) == napi_ok &&
               cjson_to_js(env, out.regions, &regions) &&
               napi_set_named_property(env, obj, "regions", regions) ==
                   napi_ok) {
        result = obj;
    }
    wf_ageassurance_config_free(&out);
    release_bytes(body);
    napi_close_handle_scope(env, scope);
    return result;
}

/* ageassuranceParseState(jsonText) parses a getState body into
 * { state: {...}, metadata: { accountCreatedAt } }. */
static napi_value ageassurance_parse_state(napi_env env,
                                           napi_callback_info info) {
    wf_bytes body = {NULL, 0, false};
    bool threw;
    napi_handle_scope scope;
    napi_value result = NULL, obj, state_obj, metadata_obj;
    wf_ageassurance_state out;
    wf_status st;

    if (!take_one_json(env, info,
                       "ageassuranceParseState(jsonText) takes "
                       "JSON text",
                       &body, &threw, &scope))
        return NULL;
    memset(&out, 0, sizeof(out));
    st = wf_ageassurance_parse_state((const char *)body.data, body.len, &out);
    if (st != WF_OK) {
        throw_status(env, st, "wf_ageassurance_parse_state");
    } else if (napi_create_object(env, &obj) == napi_ok &&
               napi_create_object(env, &state_obj) == napi_ok &&
               set_ageassurance_state(env, state_obj, &out.state) &&
               napi_set_named_property(env, obj, "state", state_obj) ==
                   napi_ok &&
               napi_create_object(env, &metadata_obj) == napi_ok &&
               set_utf8_or_null(env, metadata_obj, "accountCreatedAt",
                                out.metadata.account_created_at) &&
               napi_set_named_property(env, obj, "metadata", metadata_obj) ==
                   napi_ok) {
        result = obj;
    }
    wf_ageassurance_state_free(&out);
    release_bytes(body);
    napi_close_handle_scope(env, scope);
    return result;
}

/* ---- notification_v2_typed ------------------------------------------------
 */

static const char *include_name(wf_notif_v2_include include) {
    switch (include) {
        case WF_NOTIF_V2_INCLUDE_ALL:
            return "all";
        case WF_NOTIF_V2_INCLUDE_FOLLOWS:
            return "follows";
        case WF_NOTIF_V2_INCLUDE_ACCEPTED:
            return "accepted";
        default:
            return NULL;
    }
}

/* Emits one preference slot; absent slots (has == 0) are omitted entirely and
 * unset scalars (has_* == 0) are omitted within the slot. */
static bool set_filterable_pref(napi_env env, napi_value obj, const char *key,
                                int has, const wf_notif_v2_filterable_pref *p) {
    napi_value sub;
    if (!has) return true;
    if (napi_create_object(env, &sub) != napi_ok) return false;
    if (p->has_include &&
        !set_utf8(env, sub, "include", include_name(p->include)))
        return false;
    if (p->has_list && !set_bool(env, sub, "list", p->list != 0)) return false;
    if (p->has_push && !set_bool(env, sub, "push", p->push != 0)) return false;
    return napi_set_named_property(env, obj, key, sub) == napi_ok;
}

static bool set_plain_pref(napi_env env, napi_value obj, const char *key,
                           int has, const wf_notif_v2_pref *p) {
    napi_value sub;
    if (!has) return true;
    if (napi_create_object(env, &sub) != napi_ok) return false;
    if (p->has_list && !set_bool(env, sub, "list", p->list != 0)) return false;
    if (p->has_push && !set_bool(env, sub, "push", p->push != 0)) return false;
    return napi_set_named_property(env, obj, key, sub) == napi_ok;
}

static bool set_chat_pref(napi_env env, napi_value obj, const char *key,
                          int has, const wf_notif_v2_chat_pref *p) {
    napi_value sub;
    if (!has) return true;
    if (napi_create_object(env, &sub) != napi_ok) return false;
    if (p->has_include &&
        !set_utf8(env, sub, "include", include_name(p->include)))
        return false;
    if (p->has_push && !set_bool(env, sub, "push", p->push != 0)) return false;
    return napi_set_named_property(env, obj, key, sub) == napi_ok;
}

/* Serializes a parsed wf_notification_v2_preferences into the camelCase JS
 * object shape shared with notifV2PreferencesBuild. */
static bool make_prefs(napi_env env, const wf_notification_v2_preferences *p,
                       napi_value *out) {
    napi_value o;
    if (napi_create_object(env, &o) != napi_ok) return false;
    if (!set_chat_pref(env, o, "chat", p->has_chat, &p->chat) ||
        !set_filterable_pref(env, o, "follow", p->has_follow, &p->follow) ||
        !set_filterable_pref(env, o, "like", p->has_like, &p->like) ||
        !set_filterable_pref(env, o, "likeViaRepost", p->has_like_via_repost,
                             &p->like_via_repost) ||
        !set_filterable_pref(env, o, "mention", p->has_mention, &p->mention) ||
        !set_filterable_pref(env, o, "quote", p->has_quote, &p->quote) ||
        !set_filterable_pref(env, o, "reply", p->has_reply, &p->reply) ||
        !set_filterable_pref(env, o, "repost", p->has_repost, &p->repost) ||
        !set_filterable_pref(env, o, "repostViaRepost",
                             p->has_repost_via_repost, &p->repost_via_repost) ||
        !set_plain_pref(env, o, "starterpackJoined", p->has_starterpack_joined,
                        &p->starterpack_joined) ||
        !set_plain_pref(env, o, "subscribedPost", p->has_subscribed_post,
                        &p->subscribed_post) ||
        !set_plain_pref(env, o, "unverified", p->has_unverified,
                        &p->unverified) ||
        !set_plain_pref(env, o, "verified", p->has_verified, &p->verified))
        return false;
    *out = o;
    return true;
}

/* notifV2PreferencesParse(jsonText) parses a #preferences object or a
 * putPreferencesV2 output body via wf_notification_v2_preferences_parse. */
static napi_value notif_v2_preferences_parse(napi_env env,
                                             napi_callback_info info) {
    wf_bytes body = {NULL, 0, false};
    bool threw;
    napi_handle_scope scope;
    napi_value result = NULL;
    wf_notification_v2_preferences prefs;
    wf_status st;

    if (!take_one_json(env, info,
                       "notifV2PreferencesParse(jsonText) takes "
                       "JSON text",
                       &body, &threw, &scope))
        return NULL;
    memset(&prefs, 0, sizeof(prefs));
    st = wf_notification_v2_preferences_parse((const char *)body.data, body.len,
                                              &prefs);
    if (st != WF_OK) {
        throw_status(env, st, "wf_notification_v2_preferences_parse");
    } else if (!make_prefs(env, &prefs, &result)) {
        /* pure napi failure, exception pending */
    }
    wf_notification_v2_preferences_free(&prefs);
    release_bytes(body);
    napi_close_handle_scope(env, scope);
    return result;
}

/* Reads one optional boolean member of a JS slot object into the has/value
 * pair. */
static bool read_bool_member(napi_env env, napi_value slot, const char *key,
                             int *has, int *value, bool *threw) {
    napi_value v;
    napi_valuetype type;
    if (napi_get_named_property(env, slot, key, &v) != napi_ok) return false;
    napi_typeof(env, v, &type);
    if (type == napi_undefined || type == napi_null) return true;
    if (type != napi_boolean) {
        napi_throw_type_error(env, NULL, "preference members must be booleans");
        *threw = true;
        return false;
    }
    *has = 1;
    {
        bool b;
        if (napi_get_value_bool(env, v, &b) != napi_ok) return false;
        *value = b ? 1 : 0;
    }
    return true;
}

/* Reads one optional include member; an out-of-range string is the same
 * WF_ERR_INVALID_ARG the C builder raises. */
static bool read_include_member(napi_env env, napi_value slot, int *has,
                                wf_notif_v2_include *include, bool *threw) {
    napi_value v;
    napi_valuetype type;
    size_t len = 0;
    char buf[16];
    if (napi_get_named_property(env, slot, "include", &v) != napi_ok)
        return false;
    napi_typeof(env, v, &type);
    if (type == napi_undefined || type == napi_null) return true;
    if (type != napi_string) {
        napi_throw_type_error(env, NULL, "include must be a string");
        *threw = true;
        return false;
    }
    if (napi_get_value_string_utf8(env, v, buf, sizeof(buf), &len) != napi_ok)
        return false;
    *has = 1;
    if (strcmp(buf, "all") == 0)
        *include = WF_NOTIF_V2_INCLUDE_ALL;
    else if (strcmp(buf, "follows") == 0)
        *include = WF_NOTIF_V2_INCLUDE_FOLLOWS;
    else if (strcmp(buf, "accepted") == 0)
        *include = WF_NOTIF_V2_INCLUDE_ACCEPTED;
    else {
        throw_status(env, WF_ERR_INVALID_ARG, "include");
        *threw = true;
        return false;
    }
    return true;
}

/* Reads one slot object; absent/null means has == 0. */
static bool read_slot(napi_env env, napi_value obj, const char *key, int *has,
                      napi_value *slot_out, bool *threw) {
    napi_valuetype type;
    *has = 0;
    if (napi_get_named_property(env, obj, key, slot_out) != napi_ok)
        return false;
    napi_typeof(env, *slot_out, &type);
    if (type == napi_undefined || type == napi_null) return true;
    if (type != napi_object) {
        napi_throw_type_error(env, NULL, "preference slots must be objects");
        *threw = true;
        return false;
    }
    *has = 1;
    return true;
}

static bool read_filterable_slot(napi_env env, napi_value obj, const char *key,
                                 int *has, wf_notif_v2_filterable_pref *p,
                                 bool *threw) {
    napi_value slot;
    memset(p, 0, sizeof(*p));
    if (!read_slot(env, obj, key, has, &slot, threw) || !*has) return !*threw;
    if (!read_include_member(env, slot, &p->has_include, &p->include, threw) ||
        !read_bool_member(env, slot, "list", &p->has_list, &p->list, threw) ||
        !read_bool_member(env, slot, "push", &p->has_push, &p->push, threw))
        return false;
    return true;
}

static bool read_plain_slot(napi_env env, napi_value obj, const char *key,
                            int *has, wf_notif_v2_pref *p, bool *threw) {
    napi_value slot;
    memset(p, 0, sizeof(*p));
    if (!read_slot(env, obj, key, has, &slot, threw) || !*has) return !*threw;
    if (!read_bool_member(env, slot, "list", &p->has_list, &p->list, threw) ||
        !read_bool_member(env, slot, "push", &p->has_push, &p->push, threw))
        return false;
    return true;
}

static bool read_chat_slot(napi_env env, napi_value obj, int *has,
                           wf_notif_v2_chat_pref *p, bool *threw) {
    napi_value slot;
    memset(p, 0, sizeof(*p));
    if (!read_slot(env, obj, "chat", has, &slot, threw) || !*has)
        return !*threw;
    if (!read_include_member(env, slot, &p->has_include, &p->include, threw) ||
        !read_bool_member(env, slot, "push", &p->has_push, &p->push, threw))
        return false;
    return true;
}

/* notifV2PreferencesBuild(prefs) maps the camelCase JS object (the same shape
 * notifV2PreferencesParse returns) onto the typed struct and serializes it
 * with wf_notification_v2_preferences_build, returning the input JSON text. */
static napi_value notif_v2_preferences_build(napi_env env,
                                             napi_callback_info info) {
    size_t argc = 1;
    napi_value argv[1], result = NULL;
    bool threw = false;
    wf_notification_v2_preferences prefs;
    napi_handle_scope scope;
    char *json = NULL;
    wf_status st;

    if (napi_get_cb_info(env, info, &argc, argv, NULL, NULL) != napi_ok ||
        argc != 1) {
        napi_throw_type_error(env, NULL,
                              "notifV2PreferencesBuild(prefs) takes an object");
        return NULL;
    }
    if (napi_open_handle_scope(env, &scope) != napi_ok) return NULL;
    memset(&prefs, 0, sizeof(prefs));
    {
        napi_valuetype type;
        napi_typeof(env, argv[0], &type);
        if (type != napi_object) {
            napi_throw_type_error(env, NULL, "prefs must be an object");
            threw = true;
        }
    }
    if (!threw &&
        (!read_chat_slot(env, argv[0], &prefs.has_chat, &prefs.chat, &threw) ||
         !read_filterable_slot(env, argv[0], "follow", &prefs.has_follow,
                               &prefs.follow, &threw) ||
         !read_filterable_slot(env, argv[0], "like", &prefs.has_like,
                               &prefs.like, &threw) ||
         !read_filterable_slot(env, argv[0], "likeViaRepost",
                               &prefs.has_like_via_repost,
                               &prefs.like_via_repost, &threw) ||
         !read_filterable_slot(env, argv[0], "mention", &prefs.has_mention,
                               &prefs.mention, &threw) ||
         !read_filterable_slot(env, argv[0], "quote", &prefs.has_quote,
                               &prefs.quote, &threw) ||
         !read_filterable_slot(env, argv[0], "reply", &prefs.has_reply,
                               &prefs.reply, &threw) ||
         !read_filterable_slot(env, argv[0], "repost", &prefs.has_repost,
                               &prefs.repost, &threw) ||
         !read_filterable_slot(env, argv[0], "repostViaRepost",
                               &prefs.has_repost_via_repost,
                               &prefs.repost_via_repost, &threw) ||
         !read_plain_slot(env, argv[0], "starterpackJoined",
                          &prefs.has_starterpack_joined,
                          &prefs.starterpack_joined, &threw) ||
         !read_plain_slot(env, argv[0], "subscribedPost",
                          &prefs.has_subscribed_post, &prefs.subscribed_post,
                          &threw) ||
         !read_plain_slot(env, argv[0], "unverified", &prefs.has_unverified,
                          &prefs.unverified, &threw) ||
         !read_plain_slot(env, argv[0], "verified", &prefs.has_verified,
                          &prefs.verified, &threw)))
        threw = true;
    if (!threw) {
        st = wf_notification_v2_preferences_build(&prefs, &json);
        if (st != WF_OK) {
            throw_status(env, st, "wf_notification_v2_preferences_build");
        } else if (napi_create_string_utf8(env, json, NAPI_AUTO_LENGTH,
                                           &result) != napi_ok) {
            result = NULL;
        }
        free(json);
    }
    wf_notification_v2_preferences_free(&prefs);
    napi_close_handle_scope(env, scope);
    return result;
}

/* notifV2ParsePutActivitySubscription(jsonText) parses a
 * putActivitySubscription body into { subject, activitySubscription?, extra? }.
 */
static napi_value
notif_v2_parse_put_activity_subscription(napi_env env,
                                         napi_callback_info info) {
    wf_bytes body = {NULL, 0, false};
    bool threw;
    napi_handle_scope scope;
    napi_value result = NULL, obj, sub, extra;
    wf_notif_v2_activity_subscription_result out;
    wf_status st;

    if (!take_one_json(env, info,
                       "notifV2ParsePutActivitySubscription(jsonText) "
                       "takes JSON text",
                       &body, &threw, &scope))
        return NULL;
    memset(&out, 0, sizeof(out));
    st = wf_notif_v2_parse_put_activity_subscription((const char *)body.data,
                                                     body.len, &out);
    if (st != WF_OK) {
        throw_status(env, st, "wf_notif_v2_parse_put_activity_subscription");
    } else if (napi_create_object(env, &obj) == napi_ok &&
               set_utf8_or_null(env, obj, "subject", out.subject)) {
        result = obj;
        if (out.has_activity_subscription) {
            if (napi_create_object(env, &sub) == napi_ok) {
                bool ok = true;
                if (out.activity_subscription.has_post)
                    ok = set_bool(env, sub, "post",
                                  out.activity_subscription.post != 0);
                if (ok && out.activity_subscription.has_reply)
                    ok = set_bool(env, sub, "reply",
                                  out.activity_subscription.reply != 0);
                if (ok && out.activity_subscription.extra != NULL)
                    ok = cjson_to_js(env, out.activity_subscription.extra,
                                     &extra) &&
                         napi_set_named_property(env, sub, "extra", extra) ==
                             napi_ok;
                if (ok && napi_set_named_property(
                              env, obj, "activitySubscription", sub) != napi_ok)
                    ok = false;
                if (!ok) result = NULL;
            } else {
                result = NULL;
            }
        }
        if (result != NULL && out.extra != NULL) {
            if (!(cjson_to_js(env, out.extra, &extra) &&
                  napi_set_named_property(env, obj, "extra", extra) == napi_ok))
                result = NULL;
        }
    }
    wf_notif_v2_activity_subscription_result_free(&out);
    release_bytes(body);
    napi_close_handle_scope(env, scope);
    return result;
}

/* notifV2ParseListActivitySubscriptions(jsonText) parses a
 * listActivitySubscriptions body into { cursor, subscriptions: [...] }. */
static napi_value
notif_v2_parse_list_activity_subscriptions(napi_env env,
                                           napi_callback_info info) {
    wf_bytes body = {NULL, 0, false};
    bool threw;
    napi_handle_scope scope;
    napi_value result = NULL, obj, arr, item;
    wf_notif_v2_subscription_view_list out;
    wf_status st;
    size_t i;

    if (!take_one_json(env, info,
                       "notifV2ParseListActivitySubscriptions("
                       "jsonText) takes JSON text",
                       &body, &threw, &scope))
        return NULL;
    memset(&out, 0, sizeof(out));
    st = wf_notif_v2_parse_list_activity_subscriptions((const char *)body.data,
                                                       body.len, &out);
    if (st != WF_OK) {
        throw_status(env, st, "wf_notif_v2_parse_list_activity_subscriptions");
    } else if (napi_create_object(env, &obj) == napi_ok &&
               set_utf8_or_null(env, obj, "cursor", out.cursor) &&
               napi_create_array_with_length(env, out.count, &arr) == napi_ok) {
        bool ok =
            napi_set_named_property(env, obj, "subscriptions", arr) == napi_ok;
        for (i = 0; ok && i < out.count; ++i) {
            const wf_notif_v2_subscription_view *v = &out.items[i];
            if (napi_create_object(env, &item) != napi_ok ||
                !set_utf8_or_null(env, item, "did", v->did) ||
                !set_utf8_or_null(env, item, "handle", v->handle) ||
                !set_utf8_or_null(env, item, "displayName", v->display_name) ||
                !set_utf8_or_null(env, item, "avatar", v->avatar)) {
                ok = false;
                break;
            }
            if (v->extra != NULL) {
                napi_value extra;
                if (!cjson_to_js(env, v->extra, &extra) ||
                    napi_set_named_property(env, item, "extra", extra) !=
                        napi_ok) {
                    ok = false;
                    break;
                }
            }
            if (napi_set_element(env, arr, (uint32_t)i, item) != napi_ok) {
                ok = false;
                break;
            }
        }
        if (ok) result = obj;
    }
    wf_notif_v2_subscription_view_list_free(&out);
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
        {"postDisplay", NULL, post_display, NULL, NULL, NULL, napi_default,
         NULL},
        {"chatConvoIsGroup", NULL, chat_convo_is_group, NULL, NULL, NULL,
         napi_default, NULL},
        {"chatJoinLinkUrl", NULL, chat_join_link_url, NULL, NULL, NULL,
         napi_default, NULL},
        {"chatJoinLinkIsEnabled", NULL, chat_join_link_is_enabled, NULL, NULL,
         NULL, napi_default, NULL},
        {"ageassuranceParseBegin", NULL, ageassurance_parse_begin, NULL, NULL,
         NULL, napi_default, NULL},
        {"ageassuranceParseConfig", NULL, ageassurance_parse_config, NULL, NULL,
         NULL, napi_default, NULL},
        {"ageassuranceParseState", NULL, ageassurance_parse_state, NULL, NULL,
         NULL, napi_default, NULL},
        {"notifV2PreferencesParse", NULL, notif_v2_preferences_parse, NULL,
         NULL, NULL, napi_default, NULL},
        {"notifV2PreferencesBuild", NULL, notif_v2_preferences_build, NULL,
         NULL, NULL, napi_default, NULL},
        {"notifV2ParsePutActivitySubscription", NULL,
         notif_v2_parse_put_activity_subscription, NULL, NULL, NULL,
         napi_default, NULL},
        {"notifV2ParseListActivitySubscriptions", NULL,
         notif_v2_parse_list_activity_subscriptions, NULL, NULL, NULL,
         napi_default, NULL},
    };
    size_t count = sizeof(props) / sizeof(props[0]);

    if (napi_define_properties(env, exports, count, props) != napi_ok) {
        return NULL;
    }
    return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, init)
