#include "wolfram/spaces/httpsig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include "sf.h"
#include "wolfram/spaces/uri.h"
#include "wolfram/syntax.h"

#define CREDENTIAL_TAG "atproto-space-credential"
#define CREDENTIAL_COMPONENTS "(\"authorization\" \"atproto-space-audience\")"
#define CREDENTIAL_ALG "ecdsa-p256-sha256"
#define MAC_ALG "hmac-sha256"

static const char *mac_tag(wf_space_mac_profile profile) {
    return profile == WF_SPACE_MAC_COMMIT ? "atproto-space-commit"
                                          : "atproto-space-record";
}

static const char *mac_components(wf_space_mac_profile profile) {
    return profile == WF_SPACE_MAC_COMMIT ? "(\"atproto-space-commit\";sf)"
                                          : "(\"atproto-space-record\";sf)";
}

static int profile_ok(wf_space_mac_profile profile) {
    return profile == WF_SPACE_MAC_COMMIT || profile == WF_SPACE_MAC_RECORD;
}

/* A peer's bad input is a validation failure; running out of memory is not. */
static wf_status peer_status(wf_status status) {
    return status == WF_ERR_PARSE ? WF_ERR_VALIDATION : status;
}

static char *copy_string(const char *text) {
    size_t len = strlen(text) + 1;
    char *copy = malloc(len);
    if (copy) {
        memcpy(copy, text, len);
    }
    return copy;
}

/* ---- finding a signature by tag ---- */

static int has_tag(const wf_sf_member *member, const char *tag) {
    const wf_sf_bare *value;

    if (member->item.bare.type != WF_SF_INNER_LIST) {
        return 0;
    }
    value = wf_sf_param_get(&member->item, "tag");
    return value && value->type == WF_SF_STRING &&
           strcmp(value->data, tag) == 0;
}

/* The single member tagged `tag`. WF_ERR_VALIDATION when there is not exactly
 * one. */
static wf_status only_tagged(const wf_sf_dict *dict, const char *tag,
                             const wf_sf_member **out) {
    size_t i;

    *out = NULL;
    for (i = 0; i < dict->count; i++) {
        if (has_tag(&dict->members[i], tag)) {
            if (*out) {
                return WF_ERR_VALIDATION;
            }
            *out = &dict->members[i];
        }
    }
    return *out ? WF_OK : WF_ERR_VALIDATION;
}

/* The serialised component list, without the list's own parameters. */
static char *components_of(const wf_sf_item *list) {
    wf_sf_item bare_list = *list;
    wf_sf_out out = {0};

    bare_list.params = NULL;
    bare_list.param_count = 0;
    wf_sf_write_item(&out, &bare_list);
    return wf_sf_take(&out);
}

static int param_is_string(const wf_sf_item *item, const char *name,
                           const char *expected) {
    const wf_sf_bare *value = wf_sf_param_get(item, name);
    return !value ||
           (value->type == WF_SF_STRING && strcmp(value->data, expected) == 0);
}

/*
 * Find the signature tagged `tag`, require it to cover exactly `components` and
 * to name no other algorithm or key, and return its serialised parameters and
 * raw bytes (exactly `want` of them).
 */
static wf_status find_signature(const char *input_field,
                                const char *signature_field, const char *tag,
                                const char *components, const char *alg,
                                const char *key_id, char **out_params,
                                unsigned char *out_bytes, size_t want) {
    wf_sf_dict inputs = {0};
    wf_sf_dict signatures = {0};
    const wf_sf_member *tagged = NULL;
    const wf_sf_member *signature = NULL;
    char *covered = NULL;
    wf_sf_out params = {0};
    wf_status status;

    *out_params = NULL;
    if (!input_field || !signature_field) {
        return WF_ERR_VALIDATION;
    }
    status = wf_sf_parse_dict(input_field, &inputs);
    if (status == WF_OK) {
        status = wf_sf_parse_dict(signature_field, &signatures);
    }
    if (status == WF_OK) {
        status = only_tagged(&inputs, tag, &tagged);
    }
    if (status == WF_OK) {
        signature = wf_sf_dict_get(&signatures, tagged->key);
        if (!signature || signature->item.bare.type != WF_SF_BYTES ||
            signature->item.param_count != 0 ||
            signature->item.bare.len != want ||
            !param_is_string(&tagged->item, "alg", alg) ||
            !param_is_string(&tagged->item, "keyid", key_id)) {
            status = WF_ERR_VALIDATION;
        }
    }
    if (status == WF_OK) {
        covered = components_of(&tagged->item);
        if (!covered) {
            status = WF_ERR_ALLOC;
        } else if (strcmp(covered, components) != 0) {
            status = WF_ERR_VALIDATION;
        }
    }
    if (status == WF_OK) {
        wf_sf_write_item(&params, &tagged->item);
        *out_params = wf_sf_take(&params);
        if (!*out_params) {
            status = WF_ERR_ALLOC;
        } else {
            memcpy(out_bytes, signature->item.bare.data, want);
        }
    }
    free(covered);
    wf_sf_dict_free(&inputs);
    wf_sf_dict_free(&signatures);
    return peer_status(status);
}

/* "label=:base64:" and "label=params". */
static wf_status format_fields(const char *label, const char *params,
                               const unsigned char *bytes, size_t len,
                               char **out_input, char **out_signature) {
    wf_sf_out input = {0};
    wf_sf_out signature = {0};

    wf_sf_puts(&input, label);
    wf_sf_puts(&input, "=");
    wf_sf_puts(&input, params);
    wf_sf_puts(&signature, label);
    wf_sf_puts(&signature, "=");
    wf_sf_write_bytes(&signature, bytes, len);
    *out_input = wf_sf_take(&input);
    *out_signature = wf_sf_take(&signature);
    if (!*out_input || !*out_signature) {
        free(*out_input);
        free(*out_signature);
        *out_input = NULL;
        *out_signature = NULL;
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}

/* ---- credential binding ---- */

void wf_space_signature_free(wf_space_signature *signature) {
    if (signature) {
        free(signature->signature_input);
        free(signature->signature);
        memset(signature, 0, sizeof(*signature));
    }
}

/* Append a covered value, trimmed as RFC 9421 requires. A line break would let
 * a value forge further lines of the base, so it is refused. */
static int put_value(wf_sf_out *base, const char *value) {
    size_t len = strlen(value);

    while (len && (*value == ' ' || *value == '\t')) {
        value++;
        len--;
    }
    while (len && (value[len - 1] == ' ' || value[len - 1] == '\t')) {
        len--;
    }
    if (len == 0 || memchr(value, '\n', len) || memchr(value, '\r', len)) {
        return 0;
    }
    wf_sf_put(base, value, len);
    return 1;
}

static wf_status credential_base(const char *authorization,
                                 const char *audience, const char *params,
                                 char **out) {
    wf_sf_out base = {0};
    int ok;

    wf_sf_puts(&base, "\"authorization\": ");
    ok = put_value(&base, authorization);
    wf_sf_puts(&base, "\n\"atproto-space-audience\": ");
    ok = put_value(&base, audience) && ok;
    wf_sf_puts(&base, "\n\"@signature-params\": ");
    wf_sf_puts(&base, params);
    *out = wf_sf_take(&base);
    if (!ok) {
        free(*out);
        *out = NULL;
        return WF_ERR_VALIDATION;
    }
    return *out ? WF_OK : WF_ERR_ALLOC;
}

wf_status wf_space_credential_sign(const wf_signing_key *key,
                                   const char *authorization,
                                   const char *audience,
                                   wf_space_signature *out) {
    static const char params[] =
        CREDENTIAL_COMPONENTS ";tag=\"" CREDENTIAL_TAG "\"";
    unsigned char raw[64];
    char *base = NULL;
    wf_status status;

    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!key || key->type != WF_KEY_TYPE_P256 || !authorization || !audience) {
        return WF_ERR_INVALID_ARG;
    }
    status = credential_base(authorization, audience, params, &base);
    if (status == WF_ERR_VALIDATION) {
        status = WF_ERR_INVALID_ARG;
    }
    if (status == WF_OK) {
        status = wf_sign(key, (const unsigned char *)base, strlen(base), raw,
                         sizeof(raw));
    }
    if (status == WF_OK) {
        status = format_fields("sig", params, raw, sizeof(raw),
                               &out->signature_input, &out->signature);
    }
    free(base);
    return status;
}

wf_status wf_space_credential_verify(const char *authorization,
                                     const char *audience,
                                     const char *signature_input,
                                     const char *signature,
                                     const char *cnf_kid) {
    unsigned char raw[64];
    unsigned char *point = NULL;
    size_t point_len = 0;
    wf_key_type type;
    char *params = NULL;
    char *base = NULL;
    wf_status status;

    if (!cnf_kid) {
        return WF_ERR_INVALID_ARG;
    }
    if (!authorization || !audience || strncmp(cnf_kid, "did:key:", 8) != 0) {
        return WF_ERR_VALIDATION;
    }
    status = wf_didkey_decode(cnf_kid, &type, &point, &point_len);
    free(point);
    if (status != WF_OK) {
        return status == WF_ERR_ALLOC ? status : WF_ERR_VALIDATION;
    }
    if (type != WF_KEY_TYPE_P256) {
        return WF_ERR_VALIDATION;
    }
    status = find_signature(signature_input, signature, CREDENTIAL_TAG,
                            CREDENTIAL_COMPONENTS, CREDENTIAL_ALG, cnf_kid,
                            &params, raw, sizeof(raw));
    if (status == WF_OK) {
        status = credential_base(authorization, audience, params, &base);
    }
    if (status == WF_OK &&
        wf_verify_allow_malleable(cnf_kid, (const unsigned char *)base,
                                  strlen(base), raw, sizeof(raw)) != WF_OK) {
        status = WF_ERR_VALIDATION;
    }
    free(params);
    free(base);
    return status;
}

/* ---- commits ---- */

void wf_space_commit_free(wf_space_commit *commit) {
    if (commit) {
        free(commit->owned);
        memset(commit, 0, sizeof(*commit));
    }
}

/* Decode the `$bytes` of a lexicon bytes value into exactly 32 bytes. */
static int digest_from_base64(const char *text, unsigned char out[32]) {
    wf_sf_dict dict = {0};
    wf_sf_out wrapped = {0};
    char *item;
    int ok = 0;

    wf_sf_puts(&wrapped, "d=:");
    wf_sf_puts(&wrapped, text);
    wf_sf_puts(&wrapped, ":");
    item = wf_sf_take(&wrapped);
    if (item && wf_sf_parse_dict(item, &dict) == WF_OK && dict.count == 1 &&
        dict.members[0].item.bare.type == WF_SF_BYTES &&
        dict.members[0].item.param_count == 0 &&
        dict.members[0].item.bare.len == 32) {
        memcpy(out, dict.members[0].item.bare.data, 32);
        ok = 1;
    }
    wf_sf_dict_free(&dict);
    free(item);
    return ok;
}

wf_status wf_space_commit_parse_json(const char *json, size_t len,
                                     wf_space_commit *out) {
    cJSON *root;
    const cJSON *ver;
    const cJSON *repo;
    const cJSON *space;
    const cJSON *rev;
    const cJSON *bytes;
    wf_space_uri uri = {0};
    wf_status status = WF_ERR_PARSE;

    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!json) {
        return WF_ERR_INVALID_ARG;
    }
    root = cJSON_ParseWithLength(json, len);
    ver = cJSON_GetObjectItemCaseSensitive(root, "ver");
    repo = cJSON_GetObjectItemCaseSensitive(root, "repo");
    space = cJSON_GetObjectItemCaseSensitive(root, "space");
    rev = cJSON_GetObjectItemCaseSensitive(root, "rev");
    bytes = cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(root, "data"), "$bytes");

    if (cJSON_IsNumber(ver) && ver->valuedouble == WF_SPACE_COMMIT_VERSION &&
        cJSON_IsString(repo) && cJSON_IsString(space) && cJSON_IsString(rev) &&
        cJSON_IsString(bytes) && wf_syntax_did_is_valid(repo->valuestring) &&
        wf_syntax_tid_is_valid(rev->valuestring) &&
        wf_space_uri_parse(space->valuestring, &uri) == WF_OK && !uri.author &&
        digest_from_base64(bytes->valuestring, out->data)) {
        size_t a = strlen(repo->valuestring) + 1;
        size_t b = strlen(space->valuestring) + 1;
        size_t c = strlen(rev->valuestring) + 1;
        char *owned = malloc(a + b + c);

        if (!owned) {
            status = WF_ERR_ALLOC;
        } else {
            memcpy(owned, repo->valuestring, a);
            memcpy(owned + a, space->valuestring, b);
            memcpy(owned + a + b, rev->valuestring, c);
            out->ver = WF_SPACE_COMMIT_VERSION;
            out->repo = owned;
            out->space = owned + a;
            out->rev = owned + a + b;
            out->owned = owned;
            status = WF_OK;
        }
    }
    if (status != WF_OK) {
        memset(out, 0, sizeof(*out));
    }
    wf_space_uri_free(&uri);
    cJSON_Delete(root);
    return status;
}

/* ---- MACs ---- */

wf_status wf_space_mac_request_format(wf_space_mac_profile profile,
                                      const char *key_share, char **out) {
    wf_sf_out field = {0};
    int ok;

    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!profile_ok(profile) || !key_share ||
        !wf_syntax_did_is_valid(key_share)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_sf_puts(&field, "mac=");
    wf_sf_puts(&field, mac_components(profile));
    wf_sf_puts(&field, ";keyid=");
    ok = wf_sf_write_string(&field, key_share);
    wf_sf_puts(&field, ";tag=\"");
    wf_sf_puts(&field, mac_tag(profile));
    wf_sf_puts(&field, "\"");
    *out = wf_sf_take(&field);
    if (!ok) {
        free(*out);
        *out = NULL;
        return WF_ERR_INVALID_ARG;
    }
    return *out ? WF_OK : WF_ERR_ALLOC;
}

void wf_space_mac_request_free(wf_space_mac_request *request) {
    if (request) {
        free(request->label);
        free(request->key_share);
        free(request->params);
        memset(request, 0, sizeof(*request));
    }
}

wf_status wf_space_mac_request_parse(const char *accept_signature,
                                     wf_space_mac_profile profile,
                                     wf_space_mac_request *out) {
    wf_sf_dict dict = {0};
    const wf_sf_member *member = NULL;
    const wf_sf_bare *key_id = NULL;
    char *covered = NULL;
    wf_status status;
    size_t i;

    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!profile_ok(profile)) {
        return WF_ERR_INVALID_ARG;
    }
    if (!accept_signature) {
        return WF_ERR_VALIDATION;
    }
    status = wf_sf_parse_dict(accept_signature, &dict);
    if (status == WF_OK) {
        status = only_tagged(&dict, mac_tag(profile), &member);
    }
    if (status == WF_OK) {
        for (i = 0; i < member->item.param_count; i++) {
            const char *name = member->item.params[i].key;
            if (strcmp(name, "keyid") != 0 && strcmp(name, "tag") != 0 &&
                strcmp(name, "alg") != 0) {
                status = WF_ERR_VALIDATION;
            }
        }
        key_id = wf_sf_param_get(&member->item, "keyid");
        if (!key_id || key_id->type != WF_SF_STRING ||
            !wf_syntax_did_is_valid(key_id->data) ||
            !param_is_string(&member->item, "alg", MAC_ALG)) {
            status = WF_ERR_VALIDATION;
        }
    }
    if (status == WF_OK) {
        covered = components_of(&member->item);
        if (!covered) {
            status = WF_ERR_ALLOC;
        } else if (strcmp(covered, mac_components(profile)) != 0) {
            status = WF_ERR_VALIDATION;
        }
    }
    if (status == WF_OK) {
        wf_sf_out params = {0};

        wf_sf_write_item(&params, &member->item);
        out->profile = profile;
        out->params = wf_sf_take(&params);
        out->label = copy_string(member->key);
        out->key_share = copy_string(key_id->data);
        if (!out->params || !out->label || !out->key_share) {
            wf_space_mac_request_free(out);
            status = WF_ERR_ALLOC;
        }
    }
    free(covered);
    wf_sf_dict_free(&dict);
    return peer_status(status);
}

void wf_space_mac_headers_free(wf_space_mac_headers *headers) {
    if (headers) {
        free(headers->covered);
        free(headers->signature_input);
        free(headers->signature);
        memset(headers, 0, sizeof(*headers));
    }
}

/* HMAC over the two-line signature base of a covered header. */
static wf_status mac_of(wf_space_mac_profile profile, const char *covered,
                        const char *params, const unsigned char secret[32],
                        unsigned char out[32]) {
    wf_sf_out base = {0};
    unsigned int len = 32;
    char *text;
    wf_status status = WF_OK;

    wf_sf_puts(&base, "\"");
    wf_sf_puts(&base, mac_tag(profile));
    wf_sf_puts(&base, "\";sf: ");
    wf_sf_puts(&base, covered);
    wf_sf_puts(&base, "\n\"@signature-params\": ");
    wf_sf_puts(&base, params);
    text = wf_sf_take(&base);
    if (!text) {
        return WF_ERR_ALLOC;
    }
    if (!HMAC(EVP_sha256(), secret, 32, (const unsigned char *)text,
              strlen(text), out, &len) ||
        len != 32) {
        status = WF_ERR_CRYPTO;
    }
    free(text);
    return status;
}

/* Takes ownership of `covered`. */
static wf_status make_headers(char *covered,
                              const wf_space_mac_request *request,
                              const unsigned char secret[32],
                              wf_space_mac_headers *out) {
    unsigned char mac[32];
    wf_status status =
        mac_of(request->profile, covered, request->params, secret, mac);

    if (status == WF_OK) {
        status =
            format_fields(request->label, request->params, mac, sizeof(mac),
                          &out->signature_input, &out->signature);
    }
    if (status == WF_OK) {
        out->covered = covered;
    } else {
        free(covered);
    }
    return status;
}

static int request_ok(const wf_space_mac_request *request,
                      wf_space_mac_profile profile) {
    return request && request->profile == profile && request->label &&
           request->params;
}

wf_status wf_space_commit_mac(const wf_space_commit *commit,
                              const wf_space_mac_request *request,
                              const unsigned char secret[32],
                              wf_space_mac_headers *out) {
    wf_sf_out header = {0};
    char *covered;
    char number[32];
    int ok;

    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!commit || !commit->repo || !commit->space || !commit->rev ||
        commit->ver != WF_SPACE_COMMIT_VERSION || !secret ||
        !request_ok(request, WF_SPACE_MAC_COMMIT)) {
        return WF_ERR_INVALID_ARG;
    }
    snprintf(number, sizeof(number), "ver=%lld, repo=", commit->ver);
    wf_sf_puts(&header, number);
    ok = wf_sf_write_string(&header, commit->repo);
    wf_sf_puts(&header, ", space=");
    ok = wf_sf_write_string(&header, commit->space) && ok;
    wf_sf_puts(&header, ", rev=");
    ok = wf_sf_write_string(&header, commit->rev) && ok;
    wf_sf_puts(&header, ", data=");
    wf_sf_write_bytes(&header, commit->data, sizeof(commit->data));
    covered = wf_sf_take(&header);
    if (!ok) {
        free(covered);
        return WF_ERR_INVALID_ARG;
    }
    if (!covered) {
        return WF_ERR_ALLOC;
    }
    return make_headers(covered, request, secret, out);
}

wf_status wf_space_record_mac(const char *uri, const char *cid,
                              const wf_space_mac_request *request,
                              const unsigned char secret[32],
                              wf_space_mac_headers *out) {
    wf_sf_out header = {0};
    char *covered;
    int ok;

    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (!uri || !cid || !secret || !request_ok(request, WF_SPACE_MAC_RECORD)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_sf_puts(&header, "uri=");
    ok = wf_sf_write_string(&header, uri);
    wf_sf_puts(&header, ", cid=");
    ok = wf_sf_write_string(&header, cid) && ok;
    covered = wf_sf_take(&header);
    if (!ok) {
        free(covered);
        return WF_ERR_INVALID_ARG;
    }
    if (!covered) {
        return WF_ERR_ALLOC;
    }
    return make_headers(covered, request, secret, out);
}

/* Check the MAC of a response and hand back the covered header's fields. */
static wf_status verify_mac(wf_space_mac_profile profile, const char *key_share,
                            const unsigned char secret[32], const char *header,
                            const char *signature_input, const char *signature,
                            wf_sf_dict *fields) {
    unsigned char received[32];
    unsigned char expected[32];
    char *params = NULL;
    char *covered = NULL;
    wf_sf_out canonical = {0};
    wf_status status;

    status = find_signature(signature_input, signature, mac_tag(profile),
                            mac_components(profile), MAC_ALG, key_share,
                            &params, received, sizeof(received));
    if (status == WF_OK) {
        status = header ? peer_status(wf_sf_parse_dict(header, fields))
                        : WF_ERR_VALIDATION;
    }
    if (status == WF_OK) {
        /* The `sf` flag covers the header as re-serialised from its parse. */
        wf_sf_write_dict(&canonical, fields);
        covered = wf_sf_take(&canonical);
        status = covered ? mac_of(profile, covered, params, secret, expected)
                         : WF_ERR_VALIDATION;
    }
    if (status == WF_OK &&
        CRYPTO_memcmp(received, expected, sizeof(expected)) != 0) {
        status = WF_ERR_VALIDATION;
    }
    OPENSSL_cleanse(expected, sizeof(expected));
    free(params);
    free(covered);
    return status;
}

static int field_is_string(const wf_sf_dict *fields, const char *key,
                           const char *expected) {
    const wf_sf_member *member = wf_sf_dict_get(fields, key);
    return member && member->item.bare.type == WF_SF_STRING &&
           strcmp(member->item.bare.data, expected) == 0;
}

wf_status wf_space_commit_verify(const wf_space_commit *commit,
                                 const char *repo, const char *space,
                                 const char *key_share,
                                 const unsigned char secret[32],
                                 const char *commit_header,
                                 const char *signature_input,
                                 const char *signature) {
    wf_sf_dict fields = {0};
    wf_status status;

    if (!commit || !commit->repo || !commit->space || !commit->rev || !repo ||
        !space || !key_share || !secret) {
        return WF_ERR_INVALID_ARG;
    }
    if (commit->ver != WF_SPACE_COMMIT_VERSION ||
        strcmp(commit->repo, repo) != 0 || strcmp(commit->space, space) != 0) {
        return WF_ERR_VALIDATION;
    }
    status = verify_mac(WF_SPACE_MAC_COMMIT, key_share, secret, commit_header,
                        signature_input, signature, &fields);
    if (status == WF_OK) {
        const wf_sf_member *ver = wf_sf_dict_get(&fields, "ver");
        const wf_sf_member *data = wf_sf_dict_get(&fields, "data");

        if (!ver || ver->item.bare.type != WF_SF_INTEGER ||
            ver->item.bare.integer != commit->ver ||
            !field_is_string(&fields, "repo", commit->repo) ||
            !field_is_string(&fields, "space", commit->space) ||
            !field_is_string(&fields, "rev", commit->rev) || !data ||
            data->item.bare.type != WF_SF_BYTES ||
            data->item.bare.len != sizeof(commit->data) ||
            memcmp(data->item.bare.data, commit->data, sizeof(commit->data)) !=
                0) {
            status = WF_ERR_VALIDATION;
        }
    }
    wf_sf_dict_free(&fields);
    return status;
}

wf_status wf_space_record_verify(const char *uri, const char *cid,
                                 const char *key_share,
                                 const unsigned char secret[32],
                                 const char *record_header,
                                 const char *signature_input,
                                 const char *signature) {
    wf_sf_dict fields = {0};
    wf_status status;

    if (!uri || !cid || !key_share || !secret) {
        return WF_ERR_INVALID_ARG;
    }
    status = verify_mac(WF_SPACE_MAC_RECORD, key_share, secret, record_header,
                        signature_input, signature, &fields);
    if (status == WF_OK && (!field_is_string(&fields, "uri", uri) ||
                            !field_is_string(&fields, "cid", cid))) {
        status = WF_ERR_VALIDATION;
    }
    wf_sf_dict_free(&fields);
    return status;
}
