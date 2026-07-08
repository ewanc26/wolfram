#include "wolfram/validate.h"
#include "wolfram/syntax.h"

#include <cJSON.h>
#include <curl/curl.h>

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define WF_VALIDATE_MAX_DEPTH 128

typedef struct wf_lexicon_doc {
    char *id;
    cJSON *root;
    struct wf_lexicon_doc *next;
} wf_lexicon_doc;

struct wf_lexicon_registry {
    wf_lexicon_doc *docs;
};

typedef struct wf_validation_ctx {
    wf_validate_error *head;
    wf_validate_error *tail;
    int valid;
} wf_validation_ctx;

typedef struct wf_resolved_schema {
    const char *lexicon_id;
    const cJSON *schema;
} wf_resolved_schema;

static char *wf_strdup(const char *s) {
    size_t len;
    char *copy;
    if (!s) return NULL;
    len = strlen(s) + 1;
    copy = (char *)malloc(len);
    if (!copy) return NULL;
    memcpy(copy, s, len);
    return copy;
}

static char *wf_vformat(const char *fmt, va_list ap) {
    va_list ap2;
    int needed;
    char *buf;
    if (!fmt) return NULL;
    va_copy(ap2, ap);
    needed = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (needed < 0) return NULL;
    buf = (char *)malloc((size_t)needed + 1);
    if (!buf) return NULL;
    if (vsnprintf(buf, (size_t)needed + 1, fmt, ap) < 0) {
        free(buf);
        return NULL;
    }
    return buf;
}

static void wf_ctx_error(wf_validation_ctx *ctx, const char *path, const char *fmt, ...) {
    wf_validate_error *error;
    char *message;
    va_list ap;

    if (!ctx) return;
    ctx->valid = 0;

    va_start(ap, fmt);
    message = wf_vformat(fmt, ap);
    va_end(ap);
    if (!message) return;

    error = (wf_validate_error *)calloc(1, sizeof(*error));
    if (!error) {
        free(message);
        return;
    }
    error->path = wf_strdup(path ? path : "$");
    error->message = message;
    if (!error->path) {
        free(error->message);
        free(error);
        return;
    }

    if (!ctx->head) {
        ctx->head = ctx->tail = error;
    } else {
        ctx->tail->next = error;
        ctx->tail = error;
    }
}

static void wf_ctx_simple(wf_validation_ctx *ctx, const char *path, const char *message) {
    wf_ctx_error(ctx, path, "%s", message ? message : "validation failed");
}

static char *wf_path_join(const char *base, const char *child) {
    size_t base_len, child_len;
    char *path;

    if (!child || !*child) return wf_strdup(base ? base : "");
    if (!base || !*base) return wf_strdup(child);

    base_len = strlen(base);
    child_len = strlen(child);
    path = (char *)malloc(base_len + 1 + child_len + 1);
    if (!path) return NULL;
    memcpy(path, base, base_len);
    path[base_len] = '/';
    memcpy(path + base_len + 1, child, child_len);
    path[base_len + 1 + child_len] = '\0';
    return path;
}

static int wf_is_json_string(const cJSON *item) {
    return item && cJSON_IsString(item) && item->valuestring != NULL;
}

static int wf_json_value_is_null(const cJSON *item) {
    return item && cJSON_IsNull(item);
}

static int wf_is_json_integer(const cJSON *item, int64_t *out) {
    double value;
    if (!item || !cJSON_IsNumber(item) || !isfinite(item->valuedouble)) return 0;
    value = item->valuedouble;
    if (trunc(value) != value) return 0;
    if (value < (double)LLONG_MIN || value > (double)LLONG_MAX) return 0;
    if (out) *out = (int64_t)value;
    return 1;
}

static size_t wf_utf8_codepoint_count(const char *s) {
    size_t count = 0;
    const unsigned char *p;
    if (!s) return 0;
    for (p = (const unsigned char *)s; *p; p++) {
        if ((*p & 0xC0u) != 0x80u) count++;
    }
    return count;
}

static int wf_is_valid_base64(const char *s) {
    size_t len, i;
    int pad = 0;

    if (!s) return 0;
    len = strlen(s);
    if (len == 0) return 1;
    if ((len % 4) != 0) return 0;

    for (i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == '=') {
            pad++;
            if (i < len - 2) return 0;
            continue;
        }
        if (pad > 0) return 0;
        if (!(isalnum(ch) || ch == '+' || ch == '/')) return 0;
    }

    if (pad > 2) return 0;
    if (pad == 1 && s[len - 1] != '=') return 0;
    if (pad == 2 && (s[len - 1] != '=' || s[len - 2] != '=')) return 0;
    return 1;
}

static int wf_base32_value(unsigned char ch) {
    if (ch >= 'a' && ch <= 'z') return (int)(ch - 'a');
    if (ch >= '2' && ch <= '7') return 26 + (int)(ch - '2');
    return -1;
}

static int wf_cid_varint(const unsigned char *data, size_t len, uint64_t *value, size_t *used) {
    uint64_t result = 0;
    unsigned shift = 0;
    size_t i;

    if (!data || !value || !used) return 0;
    for (i = 0; i < len && i < 10; i++) {
        unsigned char byte = data[i];
        if (shift == 63 && (byte & 0x7eu) != 0) return 0;
        result |= (uint64_t)(byte & 0x7fu) << shift;
        if ((byte & 0x80u) == 0) {
            if (i > 0 && byte == 0) return 0;
            *value = result;
            *used = i + 1;
            return 1;
        }
        shift += 7;
    }
    return 0;
}

static int wf_cid_bytes_is_valid(const unsigned char *data, size_t len) {
    uint64_t value;
    size_t used;
    size_t pos = 0;

    if (!data || len == 0) return 0;
    if (len == 34 && data[0] == 0x12 && data[1] == 0x20) return 1;

    if (!wf_cid_varint(data + pos, len - pos, &value, &used) || value != 1) return 0;
    pos += used;
    if (!wf_cid_varint(data + pos, len - pos, &value, &used) || value == 0) return 0;
    pos += used;
    if (!wf_cid_varint(data + pos, len - pos, &value, &used) || value == 0) return 0;
    pos += used;
    if (!wf_cid_varint(data + pos, len - pos, &value, &used)) return 0;
    pos += used;
    return value == len - pos;
}

static int wf_is_valid_cid_string(const char *cid) {
    size_t len, i, out_len = 0;
    unsigned char *bytes = NULL;
    uint64_t buffer = 0;
    int bits = 0;
    int ok = 0;

    if (!cid || cid[0] != 'b') return 0;
    len = strlen(cid);
    if (len < 2) return 0;

    bytes = (unsigned char *)malloc(((len - 1) * 5 / 8) + 2);
    if (!bytes) return 0;

    for (i = 1; i < len; i++) {
        int v = wf_base32_value((unsigned char)cid[i]);
        if (v < 0) goto done;
        buffer = (buffer << 5) | (uint64_t)v;
        bits += 5;
        while (bits >= 8) {
            bits -= 8;
            bytes[out_len++] = (unsigned char)((buffer >> bits) & 0xffu);
        }
    }
    if (bits > 0) {
        uint64_t mask = ((uint64_t)1 << bits) - 1u;
        if ((buffer & mask) != 0) goto done;
    }

    ok = wf_cid_bytes_is_valid(bytes, out_len);

done:
    free(bytes);
    return ok;
}

static int wf_is_valid_uri(const char *uri) {
    CURLU *parsed = NULL;
    char *scheme = NULL;
    int valid = 0;

    if (!uri || !*uri) return 0;
    parsed = curl_url();
    if (!parsed) return 0;
    if (curl_url_set(parsed, CURLUPART_URL, uri, CURLU_NON_SUPPORT_SCHEME) != CURLUE_OK) goto done;
    if (curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK || !scheme || !*scheme) goto done;
    valid = 1;

done:
    curl_free(scheme);
    curl_url_cleanup(parsed);
    return valid;
}

static const wf_lexicon_doc *wf_registry_find_doc(const wf_lexicon_registry *registry,
                                                 const char *lexicon_id) {
    const wf_lexicon_doc *doc;
    if (!registry || !lexicon_id) return NULL;
    for (doc = registry->docs; doc; doc = doc->next) {
        if (doc->id && strcmp(doc->id, lexicon_id) == 0) return doc;
    }
    return NULL;
}

static wf_lexicon_doc *wf_registry_find_doc_mut(wf_lexicon_registry *registry,
                                               const char *lexicon_id) {
    wf_lexicon_doc *doc;
    if (!registry || !lexicon_id) return NULL;
    for (doc = registry->docs; doc; doc = doc->next) {
        if (doc->id && strcmp(doc->id, lexicon_id) == 0) return doc;
    }
    return NULL;
}

static const cJSON *wf_doc_find_def(const wf_lexicon_doc *doc, const char *def_id) {
    const cJSON *defs;
    if (!doc || !def_id) return NULL;
    defs = cJSON_GetObjectItemCaseSensitive(doc->root, "defs");
    if (!cJSON_IsObject(defs)) return NULL;
    return cJSON_GetObjectItemCaseSensitive((cJSON *)defs, def_id);
}

static const char *wf_schema_type(const cJSON *schema) {
    const cJSON *type = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "type");
    return wf_is_json_string(type) ? type->valuestring : NULL;
}

static const cJSON *wf_def_body_for_validation(const cJSON *def) {
    const char *type = wf_schema_type(def);
    if (type && strcmp(type, "record") == 0) {
        const cJSON *record = cJSON_GetObjectItemCaseSensitive((cJSON *)def, "record");
        return cJSON_IsObject(record) ? record : NULL;
    }
    return def;
}

static char *wf_canonicalize_ref(const char *current_lexicon_id, const char *ref) {
    size_t current_len, ref_len;
    char *canon;

    if (!ref || !*ref) return NULL;
    if (ref[0] == '#') {
        if (!current_lexicon_id || !*current_lexicon_id) return NULL;
        current_len = strlen(current_lexicon_id);
        ref_len = strlen(ref);
        canon = (char *)malloc(current_len + ref_len + 1);
        if (!canon) return NULL;
        memcpy(canon, current_lexicon_id, current_len);
        memcpy(canon + current_len, ref, ref_len + 1);
        return canon;
    }
    if (strchr(ref, '#')) return wf_strdup(ref);
    ref_len = strlen(ref);
    canon = (char *)malloc(ref_len + 6);
    if (!canon) return NULL;
    memcpy(canon, ref, ref_len);
    memcpy(canon + ref_len, "#main", 6);
    return canon;
}

static const cJSON *wf_resolve_schema_ref(const wf_lexicon_registry *registry,
                                         const char *current_lexicon_id,
                                         const char *ref,
                                         const char **resolved_lexicon_id,
                                         char **canonical_ref_out) {
    char *canon;
    char *hash;
    wf_lexicon_doc *doc;
    const cJSON *schema;
    char *def_id;

    if (resolved_lexicon_id) *resolved_lexicon_id = NULL;
    if (canonical_ref_out) *canonical_ref_out = NULL;

    canon = wf_canonicalize_ref(current_lexicon_id, ref);
    if (!canon) return NULL;
    hash = strchr(canon, '#');
    if (!hash || hash == canon) {
        free(canon);
        return NULL;
    }
    *hash = '\0';
    def_id = hash + 1;
    doc = (wf_lexicon_doc *)wf_registry_find_doc(registry, canon);
    if (!doc) {
        free(canon);
        return NULL;
    }
    schema = wf_doc_find_def(doc, def_id);
    if (!schema) {
        free(canon);
        return NULL;
    }
    if (resolved_lexicon_id) *resolved_lexicon_id = doc->id;
    if (canonical_ref_out) *canonical_ref_out = canon;
    else free(canon);
    return schema;
}

static wf_status wf_validate_schema(wf_validation_ctx *ctx,
                                    const wf_lexicon_registry *registry,
                                    const char *current_lexicon_id,
                                    const cJSON *schema,
                                    const cJSON *value,
                                    const char *path,
                                    int depth,
                                    const char *expected_token_value);

static wf_status wf_validate_object_schema(wf_validation_ctx *ctx,
                                         const wf_lexicon_registry *registry,
                                         const char *current_lexicon_id,
                                         const cJSON *schema,
                                         const cJSON *value,
                                         const char *path,
                                         int depth) {
    const cJSON *properties;
    const cJSON *required;
    const cJSON *nullable;
    const cJSON *prop_item;
    int valid = 1;

    if (!cJSON_IsObject(value)) {
        wf_ctx_simple(ctx, path, "must be an object");
        return WF_ERR_INVALID_ARG;
    }

    properties = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "properties");
    nullable = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "nullable");
    if (cJSON_IsObject(properties)) {
        cJSON_ArrayForEach(prop_item, properties) {
            const char *prop_name = prop_item->string;
            const cJSON *prop_value;
            char *prop_path;
            int is_nullable = 0;

            if (!prop_name) continue;
            prop_value = cJSON_GetObjectItemCaseSensitive((cJSON *)value, prop_name);
            prop_path = wf_path_join(path, prop_name);
            if (!prop_path) {
                valid = 0;
                continue;
            }

            if (wf_json_value_is_null(prop_value)) {
                const cJSON *nullable_name;
                if (cJSON_IsArray(nullable)) {
                    cJSON_ArrayForEach(nullable_name, nullable) {
                        if (wf_is_json_string(nullable_name) && strcmp(nullable_name->valuestring, prop_name) == 0) {
                            is_nullable = 1;
                            break;
                        }
                    }
                }
                if (is_nullable) {
                    free(prop_path);
                    continue;
                }
            }

            if (prop_value != NULL) {
                if (wf_validate_schema(ctx, registry, current_lexicon_id,
                                       prop_item, prop_value, prop_path, depth + 1, NULL) != WF_OK) {
                    valid = 0;
                }
            }
            free(prop_path);
        }
    }

    required = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "required");
    if (cJSON_IsArray(required)) {
        const cJSON *required_item;
        cJSON_ArrayForEach(required_item, required) {
            char *missing_path;
            if (!wf_is_json_string(required_item)) continue;
            if (cJSON_GetObjectItemCaseSensitive((cJSON *)value, required_item->valuestring) != NULL) continue;
            missing_path = wf_path_join(path, required_item->valuestring);
            wf_ctx_simple(ctx, missing_path ? missing_path : path, "is required");
            free(missing_path);
            valid = 0;
        }
    }

    return valid ? WF_OK : WF_ERR_INVALID_ARG;
}

static wf_status wf_validate_array_schema(wf_validation_ctx *ctx,
                                        const wf_lexicon_registry *registry,
                                        const char *current_lexicon_id,
                                        const cJSON *schema,
                                        const cJSON *value,
                                        const char *path,
                                        int depth) {
    const cJSON *items;
    const cJSON *entry;
    int index = 0;
    int valid = 1;
    int64_t min_len = 0, max_len = 0;

    if (!cJSON_IsArray(value)) {
        wf_ctx_simple(ctx, path, "must be an array");
        return WF_ERR_INVALID_ARG;
    }

    items = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "items");
    if (!items) {
        wf_ctx_simple(ctx, path, "array schema is missing an items definition");
        return WF_ERR_INVALID_ARG;
    }

    cJSON_ArrayForEach(entry, value) {
        char index_buf[32];
        char *item_path;
        snprintf(index_buf, sizeof(index_buf), "%d", index);
        item_path = wf_path_join(path, index_buf);
        if (!item_path) {
            valid = 0;
            index++;
            continue;
        }
        if (wf_validate_schema(ctx, registry, current_lexicon_id,
                               items, entry, item_path, depth + 1, NULL) != WF_OK) {
            valid = 0;
        }
        free(item_path);
        index++;
    }

    if (wf_is_json_integer(cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "minLength"), &min_len) &&
        cJSON_GetArraySize((cJSON *)value) < (int)min_len) {
        wf_ctx_error(ctx, path, "must not have fewer than %lld elements", (long long)min_len);
        valid = 0;
    }
    if (wf_is_json_integer(cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "maxLength"), &max_len) &&
        cJSON_GetArraySize((cJSON *)value) > (int)max_len) {
        wf_ctx_error(ctx, path, "must not have more than %lld elements", (long long)max_len);
        valid = 0;
    }

    return valid ? WF_OK : WF_ERR_INVALID_ARG;
}

static wf_status wf_validate_string_schema(wf_validation_ctx *ctx,
                                          const cJSON *schema,
                                          const cJSON *value,
                                          const char *path) {
    const cJSON *item;
    size_t len;
    size_t graphemes;

    if (!wf_is_json_string(value)) {
        wf_ctx_simple(ctx, path, "must be a string");
        return WF_ERR_INVALID_ARG;
    }

    item = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "const");
    if (wf_is_json_string(item) && strcmp(item->valuestring, value->valuestring) != 0) {
        wf_ctx_error(ctx, path, "must be %s", item->valuestring);
        return WF_ERR_INVALID_ARG;
    }

    item = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "knownValues");
    if (cJSON_IsArray(item)) {
        const cJSON *entry;
        int found = 0;
        cJSON_ArrayForEach(entry, item) {
            if (wf_is_json_string(entry) && strcmp(entry->valuestring, value->valuestring) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            wf_ctx_simple(ctx, path, "must be one of the known values");
            return WF_ERR_INVALID_ARG;
        }
    }

    item = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "enum");
    if (cJSON_IsArray(item)) {
        const cJSON *entry;
        int found = 0;
        cJSON_ArrayForEach(entry, item) {
            if (wf_is_json_string(entry) && strcmp(entry->valuestring, value->valuestring) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            wf_ctx_simple(ctx, path, "must match one of the enum values");
            return WF_ERR_INVALID_ARG;
        }
    }

    len = strlen(value->valuestring);
    if (wf_is_json_integer(cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "minLength"), NULL) &&
        len < (size_t)cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "minLength")->valuedouble) {
        wf_ctx_error(ctx, path, "must not be shorter than %lld bytes",
                     (long long)cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "minLength")->valuedouble);
        return WF_ERR_INVALID_ARG;
    }
    if (wf_is_json_integer(cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "maxLength"), NULL) &&
        len > (size_t)cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "maxLength")->valuedouble) {
        wf_ctx_error(ctx, path, "must not be longer than %lld bytes",
                     (long long)cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "maxLength")->valuedouble);
        return WF_ERR_INVALID_ARG;
    }

    graphemes = wf_utf8_codepoint_count(value->valuestring);
    if (wf_is_json_integer(cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "minGraphemes"), NULL) &&
        graphemes < (size_t)cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "minGraphemes")->valuedouble) {
        wf_ctx_error(ctx, path, "must not be shorter than %lld graphemes",
                     (long long)cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "minGraphemes")->valuedouble);
        return WF_ERR_INVALID_ARG;
    }
    if (wf_is_json_integer(cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "maxGraphemes"), NULL) &&
        graphemes > (size_t)cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "maxGraphemes")->valuedouble) {
        wf_ctx_error(ctx, path, "must not be longer than %lld graphemes",
                     (long long)cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "maxGraphemes")->valuedouble);
        return WF_ERR_INVALID_ARG;
    }

    item = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "format");
    if (wf_is_json_string(item)) {
        const char *fmt = item->valuestring;
        int valid = 0;
        if (strcmp(fmt, "datetime") == 0) {
            valid = wf_syntax_datetime_is_valid(value->valuestring);
        } else if (strcmp(fmt, "uri") == 0) {
            valid = wf_is_valid_uri(value->valuestring);
        } else if (strcmp(fmt, "at-uri") == 0) {
            wf_syntax_aturi parsed;
            valid = wf_syntax_aturi_parse(value->valuestring, &parsed);
            if (valid) wf_syntax_aturi_free(&parsed);
        } else if (strcmp(fmt, "did") == 0) {
            valid = wf_syntax_did_is_valid(value->valuestring);
        } else if (strcmp(fmt, "handle") == 0) {
            valid = wf_syntax_handle_is_valid(value->valuestring);
        } else if (strcmp(fmt, "at-identifier") == 0) {
            valid = wf_syntax_at_identifier_is_valid(value->valuestring);
        } else if (strcmp(fmt, "nsid") == 0) {
            valid = wf_syntax_nsid_is_valid(value->valuestring);
        } else if (strcmp(fmt, "cid") == 0) {
            valid = wf_is_valid_cid_string(value->valuestring);
        } else if (strcmp(fmt, "language") == 0) {
            valid = wf_syntax_language_is_valid(value->valuestring);
        } else if (strcmp(fmt, "tid") == 0) {
            valid = wf_syntax_tid_is_valid(value->valuestring);
        } else if (strcmp(fmt, "record-key") == 0) {
            valid = wf_syntax_record_key_is_valid(value->valuestring);
        }
        if (!valid) {
            wf_ctx_error(ctx, path, "must be a valid %s", fmt);
            return WF_ERR_INVALID_ARG;
        }
    }

    return WF_OK;
}

static wf_status wf_validate_integer_schema(wf_validation_ctx *ctx,
                                           const cJSON *schema,
                                           const cJSON *value,
                                           const char *path) {
    const cJSON *item;
    int64_t integer;
    int64_t limit;

    if (!wf_is_json_integer(value, &integer)) {
        wf_ctx_simple(ctx, path, "must be an integer");
        return WF_ERR_INVALID_ARG;
    }

    item = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "const");
    if (wf_is_json_integer(item, &limit) && integer != limit) {
        wf_ctx_error(ctx, path, "must be %lld", (long long)limit);
        return WF_ERR_INVALID_ARG;
    }

    item = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "enum");
    if (cJSON_IsArray(item)) {
        const cJSON *entry;
        int found = 0;
        cJSON_ArrayForEach(entry, item) {
            if (wf_is_json_integer(entry, &limit) && integer == limit) {
                found = 1;
                break;
            }
        }
        if (!found) {
            wf_ctx_simple(ctx, path, "must match one of the enum values");
            return WF_ERR_INVALID_ARG;
        }
    }

    if (wf_is_json_integer(cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "minimum"), &limit) && integer < limit) {
        wf_ctx_error(ctx, path, "must not be less than %lld", (long long)limit);
        return WF_ERR_INVALID_ARG;
    }
    if (wf_is_json_integer(cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "maximum"), &limit) && integer > limit) {
        wf_ctx_error(ctx, path, "must not be greater than %lld", (long long)limit);
        return WF_ERR_INVALID_ARG;
    }

    return WF_OK;
}

static wf_status wf_validate_boolean_schema(wf_validation_ctx *ctx,
                                           const cJSON *schema,
                                           const cJSON *value,
                                           const char *path) {
    const cJSON *item;

    if (!cJSON_IsBool(value)) {
        wf_ctx_simple(ctx, path, "must be a boolean");
        return WF_ERR_INVALID_ARG;
    }

    item = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "const");
    if (cJSON_IsBool(item) && cJSON_IsTrue(item) != cJSON_IsTrue(value)) {
        wf_ctx_error(ctx, path, "must be %s", cJSON_IsTrue(item) ? "true" : "false");
        return WF_ERR_INVALID_ARG;
    }

    return WF_OK;
}

static wf_status wf_validate_unknown_schema(wf_validation_ctx *ctx,
                                           const cJSON *schema,
                                           const cJSON *value,
                                           const char *path) {
    (void)ctx;
    (void)schema;
    (void)value;
    (void)path;
    return WF_OK;
}

static wf_status wf_validate_bytes_schema(wf_validation_ctx *ctx,
                                         const cJSON *schema,
                                         const cJSON *value,
                                         const char *path) {
    const cJSON *bytes;
    char *subpath;

    (void)schema;
    if (!cJSON_IsObject(value)) {
        wf_ctx_simple(ctx, path, "must be an object with a $bytes property");
        return WF_ERR_INVALID_ARG;
    }

    bytes = cJSON_GetObjectItemCaseSensitive((cJSON *)value, "$bytes");
    subpath = wf_path_join(path, "$bytes");
    if (!wf_is_json_string(bytes) || !wf_is_valid_base64(bytes->valuestring)) {
        wf_ctx_simple(ctx, subpath ? subpath : path, "must be valid base64");
        free(subpath);
        return WF_ERR_INVALID_ARG;
    }
    free(subpath);
    return WF_OK;
}

static wf_status wf_validate_cid_link_schema(wf_validation_ctx *ctx,
                                            const cJSON *schema,
                                            const cJSON *value,
                                            const char *path) {
    const cJSON *link;
    char *subpath;

    (void)schema;
    if (!cJSON_IsObject(value)) {
        wf_ctx_simple(ctx, path, "must be an object with a $link property");
        return WF_ERR_INVALID_ARG;
    }

    link = cJSON_GetObjectItemCaseSensitive((cJSON *)value, "$link");
    subpath = wf_path_join(path, "$link");
    if (!wf_is_json_string(link) || !wf_is_valid_cid_string(link->valuestring)) {
        wf_ctx_simple(ctx, subpath ? subpath : path, "must be a valid CID");
        free(subpath);
        return WF_ERR_INVALID_ARG;
    }
    free(subpath);
    return WF_OK;
}

static wf_status wf_validate_token_schema_with_expected(wf_validation_ctx *ctx,
                                                        const cJSON *schema,
                                                        const cJSON *value,
                                                        const char *path,
                                                        const char *expected_value) {
    const cJSON *const_item;
    const char *expected;

    if (!wf_is_json_string(value)) {
        wf_ctx_simple(ctx, path, "must be a string");
        return WF_ERR_INVALID_ARG;
    }

    const_item = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "const");
    if (wf_is_json_string(const_item)) {
        expected = const_item->valuestring;
    } else {
        expected = expected_value;
    }
    if (!expected) {
        wf_ctx_simple(ctx, path, "token schema is missing a const value");
        return WF_ERR_INVALID_ARG;
    }
    if (strcmp(value->valuestring, expected) != 0) {
        wf_ctx_error(ctx, path, "must be %s", expected);
        return WF_ERR_INVALID_ARG;
    }
    return WF_OK;
}

static wf_status wf_validate_schema(wf_validation_ctx *ctx,
                                    const wf_lexicon_registry *registry,
                                    const char *current_lexicon_id,
                                    const cJSON *schema,
                                    const cJSON *value,
                                    const char *path,
                                    int depth,
                                    const char *expected_token_value) {
    const char *type;

    if (!ctx || !schema) {
        wf_ctx_simple(ctx, path, "invalid schema");
        return WF_ERR_INVALID_ARG;
    }
    if (depth > WF_VALIDATE_MAX_DEPTH) {
        wf_ctx_simple(ctx, path, "validation depth limit exceeded");
        return WF_ERR_INVALID_ARG;
    }

    type = wf_schema_type(schema);
    if (!type) {
        wf_ctx_simple(ctx, path, "schema is missing a type");
        return WF_ERR_INVALID_ARG;
    }

    if (strcmp(type, "record") == 0) {
        const cJSON *body = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "record");
        if (!cJSON_IsObject(body)) {
            wf_ctx_simple(ctx, path, "record schema is missing a record body");
            return WF_ERR_INVALID_ARG;
        }
        return wf_validate_schema(ctx, registry, current_lexicon_id, body, value, path, depth + 1, NULL);
    }
    if (strcmp(type, "object") == 0) {
        return wf_validate_object_schema(ctx, registry, current_lexicon_id, schema, value, path, depth);
    }
    if (strcmp(type, "array") == 0) {
        return wf_validate_array_schema(ctx, registry, current_lexicon_id, schema, value, path, depth);
    }
    if (strcmp(type, "string") == 0) {
        return wf_validate_string_schema(ctx, schema, value, path);
    }
    if (strcmp(type, "integer") == 0) {
        return wf_validate_integer_schema(ctx, schema, value, path);
    }
    if (strcmp(type, "boolean") == 0) {
        return wf_validate_boolean_schema(ctx, schema, value, path);
    }
    if (strcmp(type, "bytes") == 0) {
        return wf_validate_bytes_schema(ctx, schema, value, path);
    }
    if (strcmp(type, "cid-link") == 0) {
        return wf_validate_cid_link_schema(ctx, schema, value, path);
    }
    if (strcmp(type, "unknown") == 0) {
        return wf_validate_unknown_schema(ctx, schema, value, path);
    }
    if (strcmp(type, "query") == 0) {
        const cJSON *params = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "parameters");
        if (!cJSON_IsObject(params)) {
            wf_ctx_simple(ctx, path, "query schema is missing parameters");
            return WF_ERR_INVALID_ARG;
        }
        return wf_validate_schema(ctx, registry, current_lexicon_id, params, value, path, depth + 1, NULL);
    }
    if (strcmp(type, "procedure") == 0) {
        const cJSON *input = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "input");
        const cJSON *inner;
        if (!cJSON_IsObject(input)) {
            wf_ctx_simple(ctx, path, "procedure schema is missing input");
            return WF_ERR_INVALID_ARG;
        }
        inner = cJSON_GetObjectItemCaseSensitive((cJSON *)input, "schema");
        if (!cJSON_IsObject(inner)) {
            wf_ctx_simple(ctx, path, "procedure input schema is missing schema body");
            return WF_ERR_INVALID_ARG;
        }
        return wf_validate_schema(ctx, registry, current_lexicon_id, inner, value, path, depth + 1, NULL);
    }
    if (strcmp(type, "params") == 0) {
        return wf_validate_object_schema(ctx, registry, current_lexicon_id, schema, value, path, depth);
    }
    if (strcmp(type, "subscription") == 0) {
        wf_ctx_simple(ctx, path, "subscription schemas have no request-body validation");
        return WF_OK;
    }
    if (strcmp(type, "blob") == 0) {
        const cJSON *type_item;
        const cJSON *ref;
        const cJSON *mime_type;
        const cJSON *size;
        const cJSON *accept;
        int has_constraints;
        char *subpath;

        if (!cJSON_IsObject(value)) {
            wf_ctx_simple(ctx, path, "must be an object");
            return WF_ERR_INVALID_ARG;
        }

        type_item = cJSON_GetObjectItemCaseSensitive((cJSON *)value, "$type");
        accept = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "accept");
        has_constraints = cJSON_IsArray(accept) || cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "maxSize"));

        if (wf_is_json_string(type_item) && strcmp(type_item->valuestring, "blob") == 0) {
            ref = cJSON_GetObjectItemCaseSensitive((cJSON *)value, "ref");
            mime_type = cJSON_GetObjectItemCaseSensitive((cJSON *)value, "mimeType");
            size = cJSON_GetObjectItemCaseSensitive((cJSON *)value, "size");

            subpath = wf_path_join(path, "ref");
            if (!cJSON_IsObject(ref) || wf_validate_cid_link_schema(ctx, schema, ref, subpath ? subpath : path) != WF_OK) {
                free(subpath);
                return WF_ERR_INVALID_ARG;
            }
            free(subpath);

            subpath = wf_path_join(path, "mimeType");
            if (!wf_is_json_string(mime_type)) {
                wf_ctx_simple(ctx, subpath ? subpath : path, "must be a string");
                free(subpath);
                return WF_ERR_INVALID_ARG;
            }
            if (cJSON_IsArray(accept)) {
                const cJSON *entry;
                int found = 0;
                cJSON_ArrayForEach(entry, accept) {
                    if (wf_is_json_string(entry) && strcmp(entry->valuestring, mime_type->valuestring) == 0) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    wf_ctx_simple(ctx, subpath ? subpath : path, "must match one of the accepted MIME types");
                    free(subpath);
                    return WF_ERR_INVALID_ARG;
                }
            }
            free(subpath);

            subpath = wf_path_join(path, "size");
            if (!wf_is_json_integer(size, NULL)) {
                wf_ctx_simple(ctx, subpath ? subpath : path, "must be an integer");
                free(subpath);
                return WF_ERR_INVALID_ARG;
            }
            if (wf_is_json_integer(cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "maxSize"), NULL) &&
                (int64_t)size->valuedouble > (int64_t)cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "maxSize")->valuedouble) {
                wf_ctx_error(ctx, subpath ? subpath : path, "must not be greater than %lld",
                             (long long)cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "maxSize")->valuedouble);
                free(subpath);
                return WF_ERR_INVALID_ARG;
            }
            free(subpath);
            return WF_OK;
        }

        /* Allow a simple CID reference only when no blob constraints need checking. */
        if (!has_constraints && wf_validate_cid_link_schema(ctx, schema, value, path) == WF_OK) {
            return WF_OK;
        }
        if (has_constraints) {
            wf_ctx_simple(ctx, path, "must be a full blob object to validate size or MIME type constraints");
        } else {
            wf_ctx_simple(ctx, path, "must be a blob object");
        }
        return WF_ERR_INVALID_ARG;
    }
    if (strcmp(type, "union") == 0) {
        const cJSON *refs;
        const cJSON *ref_item;
        const cJSON *type_item;
        const cJSON *closed_item;
        const cJSON *resolved_schema;
        const char *resolved_lexicon_id = NULL;
        char *canonical_type = NULL;
        char *canonical_ref = NULL;
        char *subpath = NULL;
        int matched = 0;
        wf_status status;

        if (!cJSON_IsObject(value)) {
            wf_ctx_simple(ctx, path, "must be an object which includes the \"$type\" property");
            return WF_ERR_INVALID_ARG;
        }
        type_item = cJSON_GetObjectItemCaseSensitive((cJSON *)value, "$type");
        if (!wf_is_json_string(type_item)) {
            subpath = wf_path_join(path, "$type");
            wf_ctx_simple(ctx, subpath ? subpath : path, "must be a string");
            free(subpath);
            return WF_ERR_INVALID_ARG;
        }

        canonical_type = wf_canonicalize_ref(NULL, type_item->valuestring);
        if (!canonical_type) {
            subpath = wf_path_join(path, "$type");
            wf_ctx_simple(ctx, subpath ? subpath : path, "must be a valid type reference");
            free(subpath);
            return WF_ERR_INVALID_ARG;
        }
        refs = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "refs");
        if (canonical_type && cJSON_IsArray(refs)) {
            cJSON_ArrayForEach(ref_item, refs) {
                canonical_ref = wf_canonicalize_ref(current_lexicon_id, wf_is_json_string(ref_item) ? ref_item->valuestring : NULL);
                if (canonical_ref && strcmp(canonical_ref, canonical_type) == 0) {
                    matched = 1;
                    free(canonical_ref);
                    canonical_ref = NULL;
                    break;
                }
                free(canonical_ref);
                canonical_ref = NULL;
            }
        }

        if (!matched) {
            closed_item = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "closed");
            if (cJSON_IsBool(closed_item) && cJSON_IsTrue(closed_item)) {
                subpath = wf_path_join(path, "$type");
                wf_ctx_simple(ctx, subpath ? subpath : path, "must be one of the union refs");
                free(subpath);
                free(canonical_type);
                return WF_ERR_INVALID_ARG;
            }
            free(canonical_type);
            return WF_OK;
        }

        resolved_schema = wf_resolve_schema_ref(registry, current_lexicon_id,
                                                type_item->valuestring,
                                                &resolved_lexicon_id,
                                                &canonical_ref);
        if (!resolved_schema) {
            subpath = wf_path_join(path, "$type");
            wf_ctx_error(ctx, subpath ? subpath : path, "references an unknown definition");
            free(subpath);
            free(canonical_type);
            free(canonical_ref);
            return WF_ERR_INVALID_ARG;
        }

        {
            const char *resolved_type = wf_schema_type(resolved_schema);
            if (resolved_type && strcmp(resolved_type, "token") == 0) {
                status = wf_validate_token_schema_with_expected(ctx, resolved_schema, value, path, canonical_type);
                free(canonical_type);
                free(canonical_ref);
                return status;
            }
        }

        status = wf_validate_schema(ctx, registry, resolved_lexicon_id,
                                    resolved_schema, value, path, depth + 1, NULL);
        free(canonical_type);
        free(canonical_ref);
        return status;
    }
    if (strcmp(type, "ref") == 0) {
        const cJSON *ref_item;
        const cJSON *resolved_schema;
        const char *resolved_lexicon_id = NULL;
        char *canonical_ref = NULL;
        wf_status status;

        ref_item = cJSON_GetObjectItemCaseSensitive((cJSON *)schema, "ref");
        if (!wf_is_json_string(ref_item)) {
            wf_ctx_simple(ctx, path, "reference schema is missing a ref");
            return WF_ERR_INVALID_ARG;
        }
        resolved_schema = wf_resolve_schema_ref(registry, current_lexicon_id,
                                                ref_item->valuestring,
                                                &resolved_lexicon_id,
                                                &canonical_ref);
        if (!resolved_schema) {
            wf_ctx_error(ctx, path, "references an unknown definition: %s", ref_item->valuestring);
            free(canonical_ref);
            return WF_ERR_INVALID_ARG;
        }
        {
            const char *resolved_type = wf_schema_type(resolved_schema);
            if (resolved_type && strcmp(resolved_type, "token") == 0) {
                status = wf_validate_token_schema_with_expected(ctx, resolved_schema, value, path, canonical_ref);
                free(canonical_ref);
                return status;
            }
        }
        status = wf_validate_schema(ctx, registry, resolved_lexicon_id,
                                    resolved_schema, value, path, depth + 1, NULL);
        free(canonical_ref);
        return status;
    }
    if (strcmp(type, "token") == 0) {
        return wf_validate_token_schema_with_expected(ctx, schema, value, path, expected_token_value);
    }

    wf_ctx_error(ctx, path, "unsupported lexicon type: %s", type);
    return WF_ERR_INVALID_ARG;
}

static wf_validate_result wf_validate_with_def(const wf_lexicon_registry *registry,
                                               const char *lexicon_id,
                                               const cJSON *def,
                                               const char *expected_token_value,
                                               const char *root_path,
                                               const char *json,
                                               size_t json_len) {
    wf_validation_ctx ctx = {0};
    cJSON *value;
    const cJSON *schema;

    ctx.valid = 1;
    value = cJSON_ParseWithLength(json, json_len);
    if (!value) {
        wf_ctx_simple(&ctx, root_path, "invalid JSON");
        return (wf_validate_result){0, ctx.head};
    }

    schema = wf_def_body_for_validation(def);
    if (!schema) {
        wf_ctx_simple(&ctx, root_path, "invalid or unsupported lexicon definition");
        cJSON_Delete(value);
        return (wf_validate_result){0, ctx.head};
    }

    {
        const char *schema_type = wf_schema_type(schema);
        if (schema_type && strcmp(schema_type, "token") == 0) {
            (void)wf_validate_token_schema_with_expected(&ctx, schema, value, root_path, expected_token_value);
        } else {
            (void)wf_validate_schema(&ctx, registry, lexicon_id, schema, value, root_path, 0, NULL);
        }
    }

    cJSON_Delete(value);
    return (wf_validate_result){ctx.valid && ctx.head == NULL, ctx.head};
}

void wf_validate_result_free(wf_validate_result *result) {
    wf_validate_error *error;
    wf_validate_error *next;

    if (!result) return;
    error = result->errors;
    while (error) {
        next = error->next;
        free(error->path);
        free(error->message);
        free(error);
        error = next;
    }
    result->errors = NULL;
    result->success = 0;
}

wf_lexicon_registry *wf_lexicon_registry_new(void) {
    return (wf_lexicon_registry *)calloc(1, sizeof(wf_lexicon_registry));
}

void wf_lexicon_registry_free(wf_lexicon_registry *registry) {
    wf_lexicon_doc *doc;
    wf_lexicon_doc *next;

    if (!registry) return;
    doc = registry->docs;
    while (doc) {
        next = doc->next;
        free(doc->id);
        cJSON_Delete(doc->root);
        free(doc);
        doc = next;
    }
    free(registry);
}

wf_status wf_lexicon_registry_load(wf_lexicon_registry *registry,
                                   const char *json, size_t json_len) {
    cJSON *root;
    const cJSON *lexicon_item;
    const cJSON *id_item;
    const cJSON *defs_item;
    wf_lexicon_doc *existing;
    wf_lexicon_doc *doc;

    if (!registry || !json || json_len == 0) return WF_ERR_INVALID_ARG;
    root = cJSON_ParseWithLength(json, json_len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    lexicon_item = cJSON_GetObjectItemCaseSensitive(root, "lexicon");
    id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
    defs_item = cJSON_GetObjectItemCaseSensitive(root, "defs");
    if (!wf_is_json_integer(lexicon_item, NULL) || (int)lexicon_item->valuedouble != 1 ||
        !wf_is_json_string(id_item) || !cJSON_IsObject(defs_item) ||
        !wf_syntax_nsid_is_valid(id_item->valuestring)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    existing = wf_registry_find_doc_mut(registry, id_item->valuestring);
    if (existing) {
        cJSON *old_root = existing->root;
        char *old_id = existing->id;
        existing->root = root;
        existing->id = wf_strdup(id_item->valuestring);
        if (!existing->id) {
            existing->root = old_root;
            existing->id = old_id;
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
        free(old_id);
        cJSON_Delete(old_root);
        return WF_OK;
    }

    doc = (wf_lexicon_doc *)calloc(1, sizeof(*doc));
    if (!doc) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }
    doc->id = wf_strdup(id_item->valuestring);
    doc->root = root;
    if (!doc->id) {
        cJSON_Delete(root);
        free(doc);
        return WF_ERR_ALLOC;
    }
    doc->next = registry->docs;
    registry->docs = doc;
    return WF_OK;
}

wf_validate_result wf_validate_record(const wf_lexicon_registry *registry,
                                      const char *lexicon_id,
                                      const char *record_json, size_t json_len) {
    const wf_lexicon_doc *doc;
    const cJSON *def;
    const char *expected = NULL;
    char *expected_owned = NULL;

    if (!registry || !lexicon_id || !record_json || json_len == 0) {
        wf_validation_ctx ctx = {0};
        wf_ctx_simple(&ctx, "record", "invalid arguments");
        return (wf_validate_result){0, ctx.head};
    }

    doc = wf_registry_find_doc(registry, lexicon_id);
    if (!doc) {
        wf_validation_ctx ctx = {0};
        wf_ctx_error(&ctx, "record", "unknown lexicon: %s", lexicon_id);
        return (wf_validate_result){0, ctx.head};
    }

    def = wf_doc_find_def(doc, "main");
    if (!def) {
        wf_validation_ctx ctx = {0};
        wf_ctx_simple(&ctx, "record", "missing main definition");
        return (wf_validate_result){0, ctx.head};
    }

    {
        const char *def_type = wf_schema_type(def);
        if (def_type && strcmp(def_type, "token") == 0) {
            expected_owned = wf_canonicalize_ref(doc->id, "main");
            expected = expected_owned;
        }
    }

    {
        wf_validate_result result = wf_validate_with_def(registry, doc->id, def, expected,
                                                          "record", record_json, json_len);
        free(expected_owned);
        return result;
    }
}

wf_validate_result wf_validate_value(const wf_lexicon_registry *registry,
                                     const char *lexicon_id, const char *def_id,
                                     const char *json, size_t json_len) {
    const wf_lexicon_doc *doc;
    const cJSON *def;
    char *lookup_lexicon_id = NULL;
    const char *target_lexicon_id;
    const char *target_def_id;
    char *expected_owned = NULL;
    const char *expected = NULL;
    wf_validate_result result;

    if (!registry || !lexicon_id || !def_id || !json || json_len == 0) {
        wf_validation_ctx ctx = {0};
        wf_ctx_simple(&ctx, "value", "invalid arguments");
        return (wf_validate_result){0, ctx.head};
    }

    target_lexicon_id = lexicon_id;
    target_def_id = def_id;
    if (strchr(def_id, '#')) {
        const char *hash = strchr(def_id, '#');
        size_t lex_len = (size_t)(hash - def_id);
        lookup_lexicon_id = (char *)malloc(lex_len + 1);
        if (lookup_lexicon_id) {
            memcpy(lookup_lexicon_id, def_id, lex_len);
            lookup_lexicon_id[lex_len] = '\0';
            target_lexicon_id = lookup_lexicon_id;
            target_def_id = hash + 1;
        }
    }

    doc = wf_registry_find_doc(registry, target_lexicon_id);
    if (!doc) {
        wf_validation_ctx ctx = {0};
        wf_ctx_error(&ctx, "value", "unknown lexicon: %s", target_lexicon_id);
        free(lookup_lexicon_id);
        return (wf_validate_result){0, ctx.head};
    }

    def = wf_doc_find_def(doc, target_def_id);
    if (!def) {
        wf_validation_ctx ctx = {0};
        wf_ctx_error(&ctx, "value", "unknown definition: %s", def_id);
        free(lookup_lexicon_id);
        return (wf_validate_result){0, ctx.head};
    }

    {
        const char *def_type = wf_schema_type(def);
        if (def_type && strcmp(def_type, "token") == 0) {
            expected_owned = wf_canonicalize_ref(target_lexicon_id, target_def_id);
            expected = expected_owned;
        }
    }

    result = wf_validate_with_def(registry, doc->id, def, expected, "value", json, json_len);
    free(expected_owned);
    free(lookup_lexicon_id);
    return result;
}

/* Recursively read every lexicon document under a directory and load each into
 * the registry. Returns WF_OK if at least one lexicon loaded; malformed or
 * non-lexicon JSON files are skipped and counted rather than aborting the
 * whole load. The registry owns every successfully loaded document; free it
 * all with wf_lexicon_registry_free. */
static char *wf_read_file_contents(const char *path, size_t *out_len) {
    FILE *f;
    long size;
    char *buf;
    size_t got;

    f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) {
        free(buf);
        return NULL;
    }
    buf[size] = '\0';
    *out_len = (size_t)size;
    return buf;
}

static int wf_path_is_json(const char *name) {
    size_t len = strlen(name);
    return len >= 5 && strcmp(name + len - 5, ".json") == 0;
}

static void wf_lexicon_registry_load_dir_recursive(wf_lexicon_registry *registry,
                                                   const char *dir,
                                                   size_t *loaded_out,
                                                   size_t *skipped_out) {
    DIR *d = opendir(dir);
    struct dirent *e;

    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        const char *name = e->d_name;
        char *path;
        struct stat st;

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        path = wf_path_join(dir, name);
        if (!path) continue;

        if (stat(path, &st) != 0) {
            free(path);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            wf_lexicon_registry_load_dir_recursive(registry, path, loaded_out, skipped_out);
        } else if (S_ISREG(st.st_mode) && wf_path_is_json(name)) {
            char *contents = NULL;
            size_t len = 0;
            contents = wf_read_file_contents(path, &len);
            if (!contents) {
                (*skipped_out)++;
            } else {
                wf_status status = wf_lexicon_registry_load(registry, contents, len);
                free(contents);
                if (status == WF_OK) (*loaded_out)++;
                else (*skipped_out)++;
            }
        }
        free(path);
    }
    closedir(d);
}

wf_status wf_lexicon_registry_load_dir(wf_lexicon_registry *registry, const char *dir) {
    size_t loaded = 0;
    size_t skipped = 0;
    struct stat st;

    if (!registry || !dir) return WF_ERR_INVALID_ARG;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) return WF_ERR_NOT_FOUND;

    wf_lexicon_registry_load_dir_recursive(registry, dir, &loaded, &skipped);
    return loaded > 0 ? WF_OK : WF_ERR_NOT_FOUND;
}
