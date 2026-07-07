/**
 * test_crypto_interop.c — crypto interop conformance tests using
 * atproto reference fixtures.
 */

#include "wolfram/crypto.h"
#include "test.h"

#include <cJSON.h>
#include <openssl/evp.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WF_TEST_FIXTURE_DIR
#define WF_TEST_FIXTURE_DIR "test/fixtures/crypto"
#endif

static char *read_entire_file(const char *path, size_t *len_out) {
    FILE *fp = fopen(path, "rb");
    char *buf = NULL;
    long size;
    size_t len;

    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    len = (size_t)size;
    buf = malloc(len + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (len > 0 && fread(buf, 1, len, fp) != len) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    buf[len] = '\0';
    fclose(fp);
    if (len_out) *len_out = len;
    return buf;
}

static cJSON *load_fixture_json(const char *filename) {
    char path[1024];
    char *json = NULL;
    size_t json_len = 0;
    cJSON *root = NULL;

    snprintf(path, sizeof(path), "%s/%s", WF_TEST_FIXTURE_DIR, filename);
    json = read_entire_file(path, &json_len);
    if (!json) return NULL;

    root = cJSON_ParseWithLength(json, json_len);
    free(json);
    return root;
}

static unsigned char *base64_decode_any(const char *encoded, size_t len,
                                        size_t *decoded_len) {
    size_t padded_len = ((len + 3) / 4) * 4;
    char *padded = malloc(padded_len + 1);
    unsigned char *decoded = malloc(padded_len + 1);
    size_t i, padding;
    int result;

    if (!padded || !decoded) {
        free(padded);
        free(decoded);
        return NULL;
    }

    for (i = 0; i < len; i++) {
        padded[i] = encoded[i] == '-' ? '+' : encoded[i] == '_' ? '/' : encoded[i];
    }
    for (; i < padded_len; i++) padded[i] = '=';
    padded[padded_len] = '\0';

    result = EVP_DecodeBlock(decoded, (const unsigned char *)padded,
                             (int)padded_len);
    padding = padded_len - len;
    free(padded);
    if (result < 0 || (size_t)result < padding) {
        free(decoded);
        return NULL;
    }

    *decoded_len = (size_t)result - padding;
    return decoded;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static wf_status hex_decode_32(const char *hex, unsigned char out[32]) {
    size_t i;

    if (!hex || strlen(hex) != 64) return WF_ERR_INVALID_ARG;
    for (i = 0; i < 32; i++) {
        int hi = hex_value(hex[i * 2]);
        int lo = hex_value(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return WF_ERR_PARSE;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return WF_OK;
}

static wf_status base58btc_decode(const char *encoded,
                                  unsigned char *out,
                                  size_t out_cap,
                                  size_t *out_len) {
    static const char alphabet[] =
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    unsigned char tmp[128] = {0};
    size_t tmp_len = 0;
    size_t zero_count = 0;

    if (!encoded || !out || !out_len) return WF_ERR_INVALID_ARG;

    while (encoded[zero_count] == '1') zero_count++;

    for (const char *p = encoded + zero_count; *p; p++) {
        const char *digit = strchr(alphabet, *p);
        unsigned carry;
        size_t i;

        if (!digit) return WF_ERR_PARSE;
        carry = (unsigned)(digit - alphabet);
        for (i = 0; i < tmp_len; i++) {
            unsigned value = (unsigned)tmp[i] * 58u + carry;
            tmp[i] = (unsigned char)(value & 0xffu);
            carry = value >> 8;
        }
        while (carry) {
            if (tmp_len == sizeof(tmp)) return WF_ERR_ALLOC;
            tmp[tmp_len++] = (unsigned char)(carry & 0xffu);
            carry >>= 8;
        }
    }

    if (zero_count + tmp_len > out_cap) return WF_ERR_INVALID_ARG;
    memset(out, 0, zero_count);
    for (size_t i = 0; i < tmp_len; i++) {
        out[zero_count + i] = tmp[tmp_len - 1 - i];
    }
    *out_len = zero_count + tmp_len;
    return WF_OK;
}

static wf_status parse_multikey(const char *value,
                                unsigned char *raw,
                                size_t raw_cap,
                                size_t *raw_len,
                                wf_key_type *type) {
    const char *encoded = value;
    wf_status status;

    if (!value || !raw || !raw_len || !type) return WF_ERR_INVALID_ARG;
    if (strncmp(encoded, "did:key:", 8) == 0) encoded += 8;
    if (*encoded != 'z') return WF_ERR_INVALID_ARG;

    status = base58btc_decode(encoded + 1, raw, raw_cap, raw_len);
    if (status != WF_OK) return status;

    if (*raw_len == 35 && raw[0] == 0xe7 && raw[1] == 0x01) {
        *type = WF_KEY_TYPE_SECP256K1;
    } else if (*raw_len == 35 && raw[0] == 0x80 && raw[1] == 0x24) {
        *type = WF_KEY_TYPE_P256;
    } else {
        return WF_ERR_PARSE;
    }

    return WF_OK;
}

static wf_status load_signing_key_from_hex(wf_key_type type,
                                           const char *hex,
                                           wf_signing_key *out) {
    wf_status status;

    if (!out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->type = type;

    status = hex_decode_32(hex, out->bytes);
    if (status != WF_OK) {
        memset(out, 0, sizeof(*out));
        return status;
    }

    return WF_OK;
}

static wf_status load_signing_key_from_base58(wf_key_type type,
                                              const char *b58,
                                              wf_signing_key *out) {
    wf_status status;
    size_t decoded_len = 0;

    if (!out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    out->type = type;

    status = base58btc_decode(b58, out->bytes, sizeof(out->bytes), &decoded_len);
    if (status != WF_OK || decoded_len != sizeof(out->bytes)) {
        memset(out, 0, sizeof(*out));
        return status != WF_OK ? status : WF_ERR_PARSE;
    }

    return WF_OK;
}

static wf_status decode_bare_multibase(const char *value,
                                       unsigned char *raw,
                                       size_t raw_cap,
                                       size_t *raw_len) {
    const char *encoded = value;

    if (!value || !raw || !raw_len) return WF_ERR_INVALID_ARG;
    if (*encoded == 'z') encoded++;
    return base58btc_decode(encoded, raw, raw_cap, raw_len);
}

static void test_w3c_didkey_fixture(const char *filename, wf_key_type type) {
    cJSON *root = load_fixture_json(filename);
    cJSON *item;
    const unsigned char msg[] = "atproto interop";
    size_t msg_len = sizeof(msg) - 1;

    WF_CHECK(root != NULL);
    WF_CHECK(cJSON_IsArray(root));
    if (!root || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return;
    }

    cJSON_ArrayForEach(item, root) {
        const cJSON *hex_item = cJSON_GetObjectItemCaseSensitive(item, "privateKeyBytesHex");
        const cJSON *base58_item = cJSON_GetObjectItemCaseSensitive(item, "privateKeyBytesBase58");
        const cJSON *did_item = cJSON_GetObjectItemCaseSensitive(item, "publicDidKey");
        wf_signing_key key;
        unsigned char raw_key[64];
        size_t raw_key_len = 0;
        wf_key_type parsed_type = WF_KEY_TYPE_UNKNOWN;
        unsigned char sig[64];
        wf_status status;

        WF_CHECK(cJSON_IsString(did_item));
        WF_CHECK(cJSON_IsString(hex_item) || cJSON_IsString(base58_item));
        if (!cJSON_IsString(did_item) || (!cJSON_IsString(hex_item) && !cJSON_IsString(base58_item))) continue;

        if (cJSON_IsString(hex_item)) {
            status = load_signing_key_from_hex(type, hex_item->valuestring, &key);
        } else {
            status = load_signing_key_from_base58(type, base58_item->valuestring, &key);
        }
        WF_CHECK(status == WF_OK);
        WF_CHECK(key.type == type);

        status = parse_multikey(did_item->valuestring, raw_key, sizeof(raw_key),
                                &raw_key_len, &parsed_type);
        WF_CHECK(status == WF_OK);
        WF_CHECK(parsed_type == type);
        WF_CHECK(raw_key_len == 35);
        WF_CHECK(raw_key[0] == (type == WF_KEY_TYPE_SECP256K1 ? 0xe7 : 0x80));
        WF_CHECK(raw_key[1] == (type == WF_KEY_TYPE_SECP256K1 ? 0x01 : 0x24));

        status = wf_sign(&key, msg, msg_len, sig, sizeof(sig));
#ifdef HAVE_LIBSECP256K1
        WF_CHECK(status == WF_OK);
        WF_CHECK(wf_verify(did_item->valuestring, msg, msg_len, sig, sizeof(sig)) == WF_OK);
#else
        if (type == WF_KEY_TYPE_P256) {
            WF_CHECK(status == WF_OK);
            WF_CHECK(wf_verify(did_item->valuestring, msg, msg_len, sig, sizeof(sig)) == WF_OK);
        } else {
            WF_CHECK(status == WF_ERR_INVALID_ARG);
        }
#endif
    }

    cJSON_Delete(root);
}

static void test_signature_fixtures(void) {
    cJSON *root = load_fixture_json("signature-fixtures.json");
    cJSON *item;

    WF_CHECK(root != NULL);
    WF_CHECK(cJSON_IsArray(root));
    if (!root || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return;
    }

    cJSON_ArrayForEach(item, root) {
        const cJSON *algorithm = cJSON_GetObjectItemCaseSensitive(item, "algorithm");
        const cJSON *did_item = cJSON_GetObjectItemCaseSensitive(item, "publicKeyDid");
        const cJSON *multibase_item = cJSON_GetObjectItemCaseSensitive(item, "publicKeyMultibase");
        const cJSON *message_item = cJSON_GetObjectItemCaseSensitive(item, "messageBase64");
        const cJSON *signature_item = cJSON_GetObjectItemCaseSensitive(item, "signatureBase64");
        const cJSON *valid_item = cJSON_GetObjectItemCaseSensitive(item, "validSignature");
        unsigned char *message = NULL;
        unsigned char *signature = NULL;
        size_t message_len = 0;
        size_t signature_len = 0;
        unsigned char did_raw[64];
        unsigned char multibase_raw[64];
        size_t did_raw_len = 0;
        size_t multibase_raw_len = 0;
        wf_key_type did_type = WF_KEY_TYPE_UNKNOWN;
        wf_status did_status;
        wf_status multibase_status;
        wf_status verify_did;
        int valid_signature = 0;

        WF_CHECK(cJSON_IsString(algorithm));
        WF_CHECK(cJSON_IsString(did_item));
        WF_CHECK(cJSON_IsString(multibase_item));
        WF_CHECK(cJSON_IsString(message_item));
        WF_CHECK(cJSON_IsString(signature_item));
        WF_CHECK(cJSON_IsBool(valid_item));
        if (!cJSON_IsString(algorithm) || !cJSON_IsString(did_item) ||
            !cJSON_IsString(multibase_item) || !cJSON_IsString(message_item) ||
            !cJSON_IsString(signature_item) || !cJSON_IsBool(valid_item)) {
            continue;
        }

        valid_signature = cJSON_IsTrue(valid_item) ? 1 : 0;
        message = base64_decode_any(message_item->valuestring,
                                   strlen(message_item->valuestring),
                                   &message_len);
        signature = base64_decode_any(signature_item->valuestring,
                                     strlen(signature_item->valuestring),
                                     &signature_len);
        WF_CHECK(message != NULL);
        WF_CHECK(signature != NULL);
        if (!message || !signature) {
            free(message);
            free(signature);
            continue;
        }

        did_status = parse_multikey(did_item->valuestring, did_raw, sizeof(did_raw),
                                    &did_raw_len, &did_type);
        multibase_status = decode_bare_multibase(multibase_item->valuestring,
                                                multibase_raw, sizeof(multibase_raw),
                                                &multibase_raw_len);
        WF_CHECK(did_status == WF_OK);
        WF_CHECK(multibase_status == WF_OK);
        WF_CHECK(did_raw_len == multibase_raw_len + 2);
        WF_CHECK(memcmp(did_raw + 2, multibase_raw, multibase_raw_len) == 0);

        verify_did = wf_verify(did_item->valuestring, message, message_len,
                               signature, signature_len);

#ifdef HAVE_LIBSECP256K1
        if (valid_signature) {
            WF_CHECK(verify_did == WF_OK);
        } else {
            WF_CHECK(verify_did != WF_OK);
        }
#else
        if (strcmp(algorithm->valuestring, "ES256K") == 0) {
            WF_CHECK(verify_did == WF_ERR_INVALID_ARG);
        } else if (valid_signature) {
            WF_CHECK(verify_did == WF_OK);
        } else {
            WF_CHECK(verify_did != WF_OK);
        }
#endif

        free(message);
        free(signature);
    }

    cJSON_Delete(root);
}

int main(void) {
    test_w3c_didkey_fixture("w3c_didkey_K256.json", WF_KEY_TYPE_SECP256K1);
    test_w3c_didkey_fixture("w3c_didkey_P256.json", WF_KEY_TYPE_P256);
    test_signature_fixtures();
    WF_TEST_SUMMARY();
}
