#include "json-enhanced/json.h"
#include "wolfram/json.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include "wolfram/syntax.h"

#define WF_JSON_FORMAT_MAX_LENGTH 2048
#define WF_JSON_BUFFER_SIZE 4096

/*
 * ========================================================================
 * Forward Declarations
 * ========================================================================
 */

static bool wf_json_type_matches(const char *type, const cJSON *node);
static bool wf_json_validate_rec_enhanced(const cJSON *schema, const cJSON *doc,
                                        const char *path, wf_json_validation_options *options,
                                        char **out_error);
static bool wf_json_validate_formats_enhanced(const cJSON *schema, const cJSON *doc,
                                               const char *path, wf_json_validation_options *options,
                                               char **out_error);
static bool wf_json_validate_enums_enhanced(const cJSON *schema, const cJSON *doc,
                                            const char *path, wf_json_validation_options *options,
                                            char **out_error);
static bool wf_json_validate_number_ranges_enhanced(const cJSON *schema, const cJSON *doc,
                                                   const char *path, wf_json_validation_options *options,
                                                   char **out_error);
static bool wf_json_validate_patterns_enhanced(const cJSON *schema, const cJSON *doc,
                                               const char *path, wf_json_validation_options *options,
                                               char **out_error);
static bool wf_json_validate_additional_props_enhanced(const cJSON *schema, const cJSON *doc,
                                                       const char *path,
                                                       wf_json_validation_options *options,
                                                       char **out_error);
static bool wf_json_validate_unions_enhanced(const cJSON *schema, const cJSON *doc,
                                            const char *path, wf_json_validation_options *options,
                                            char **out_error);
static bool wf_json_validate_cid_links_enhanced(const cJSON *schema, const cJSON *doc,
                                               const char *path, wf_json_validation_options *options,
                                               char **out_error);
static bool wf_json_validate_bytes_enhanced(const cJSON *schema, const cJSON *doc,
                                            const char *path, wf_json_validation_options *options,
                                            char **out_error);
static int wf_json_matches_format(const char *value, wf_json_format format,
                                 wf_json_validation_options *options);
static int wf_json_matches_pattern(const char *value, const char *pattern);
static char *wf_path_join(const char *base, const char *child);
wf_status wf_json_canonicalize_sorted_implementation(const char *in, size_t len, char **out,
                                                   int sort_objects);
wf_status wf_json_to_lex_implementation(const char *json, size_t len, char **out_lex);
wf_status wf_lex_to_json_implementation(const char *lex, size_t len, char **out_json);

/* Helper function to convert error to string */
static char *wf_json_validation_error(const char *path, const char *message) {
    size_t len = (path ? strlen(path) : 0) + (message ? strlen(message) : 0) + 64;
    char *buf = (char *)malloc(len);
    if (buf) snprintf(buf, len, "%s%s%s", path ? (path[0] ? ("at " + std::string(path) + ": ") : "") : "",
                        message ? (std::string(message) + "") : "");
    return buf;
}

/* Initialize validation options with defaults */
void wf_json_validation_options_init(wf_json_validation_options *options) {
    if (!options) return;
    memset(options, 0, sizeof(*options));
    options->validate_formats = 1;
    options->validate_enums = 1;
    options->validate_numbers = 1;
    options->validate_patterns = 1;
    options->validate_additional_props = 1;
    options->validate_unions = 1;
    options->validate_cid_links = 1;
    options->validate_bytes = 1;
    options->strict_mode = 1;
    options->format_whitelist[0] = "rfc3339";
    options->format_whitelist[1] = "date-time";
    options->format_whitelist[2] = "email";
    options->format_whitelist[3] = "uri";
    options->format_whitelist[4] = "handle";
    options->format_whitelist[5] = "cid";
    options->default_formats[0] = "rfc3339";
    options->default_formats[1] = "date-time";
    options->default_formats[2] = "email";
}

/* Enhanced validation with custom options */
wf_status wf_json_validate_with_options(const char *schema_json, size_t schema_len,
                                         const char *doc_json, size_t doc_len,
                                         const wf_json_validation_options *options,
                                         char **out_error) {
    if (!schema_json || !doc_json || !out_error) return WF_ERR_INVALID_ARG;
    *out_error = NULL;

    cJSON *schema = cJSON_ParseWithLength(schema_json, schema_len);
    if (!schema) {
        char *msg = (char *)malloc(64);
        if (msg) strcpy(msg, "schema is not valid JSON");
        *out_error = msg;
        return WF_ERR_INVALID_ARG;
    }

    cJSON *doc = cJSON_ParseWithLength(doc_json, doc_len);
    if (!doc) {
        char *msg = (char *)malloc(64);
        if (msg) strcpy(msg, "document is not valid JSON");
        cJSON_Delete(schema);
        *out_error = msg;
        return WF_ERR_INVALID_ARG;
    }

    bool ok = wf_json_validate_rec_enhanced(schema, doc, "$", (wf_json_validation_options *)options, out_error);

    cJSON_Delete(doc);
    cJSON_Delete(schema);

    return ok ? WF_OK : WF_ERR_INVALID_ARG;
}

/* Extended validation that includes all enhanced features */
wf_status wf_json_validate_enhanced(const char *schema_json, size_t schema_len,
                                     const char *doc_json, size_t doc_len,
                                     char **out_error) {
    wf_json_validation_options options;
    wf_json_validation_options_init(&options);
    return wf_json_validate_with_options(schema_json, schema_len, doc_json, doc_len, &options, out_error);
}

/* Format validation implementation */
static bool wf_json_validate_formats_enhanced(const cJSON *schema, const cJSON *doc,
                                               const char *path, wf_json_validation_options *options,
                                               char **out_error) {
    if (!schema || !doc || !options || !out_error) return true;

    cJSON *format = cJSON_GetObjectItem(schema, "format");
    if (format && cJSON_IsString(format)) {
        const char *format_str = format->valuestring;
        wf_json_format fmt;

        if (strcmp(format_str, "rfc3339") == 0 || strcmp(format_str, "date-time") == 0) {
            fmt = WF_FORMAT_DATE_TIME_RFC3339;
        } else if (strcmp(format_str, "email") == 0) {
            fmt = WF_FORMAT_EMAIL;
        } else if (strcmp(format_str, "uri") == 0) {
            fmt = WF_FORMAT_URI;
        } else if (strcmp(format_str, "handle") == 0) {
            fmt = WF_FORMAT_HANDLE;
        } else if (strcmp(format_str, "cid") == 0) {
            fmt = WF_FORMAT_CID;
        } else if (strcmp(format_str, "schema") == 0) {
            fmt = WF_FORMAT_SCHEMA;
        } else {
            return true;  // Unknown format, skip validation
        }

        if (cJSON_IsString(doc)) {
            int matches = wf_json_matches_format(doc->valuestring, fmt, options);
            if (!matches && options->strict_mode) {
                char *err = wf_json_validation_error(path, "format does not match \"YYYY-MM-DDTHH:MM:SSZ\"");
                if (out_error) *out_error = err;
                return false;
            }
        }
    }

    return true;
}

/* Enum validation implementation */
static bool wf_json_validate_enums_enhanced(const cJSON *schema, const cJSON *doc,
                                            const char *path, wf_json_validation_options *options,
                                            char **out_error) {
    if (!schema || !doc || !options || !out_error) return true;

    cJSON *enum_arr = cJSON_GetObjectItem(schema, "enum");
    if (enum_arr && cJSON_IsArray(enum_arr)) {
        int enum_size = cJSON_GetArraySize(enum_arr);
        int matches = 0;
        cJSON *enum_value;

        cJSON_ArrayForEach(enum_value, enum_arr) {
            if (cJSON_IsNumber(enum_value) && cJSON_IsNumber(doc)) {
                if (enum_value->valuedouble == doc->valuedouble) {
                    matches++;
                    break;
                }
            } else if (cJSON_IsString(enum_value) && cJSON_IsString(doc)) {
                if (strcmp(enum_value->valuestring, doc->valuestring) == 0) {
                    matches++;
                    break;
                }
            } else if (cJSON_IsBool(enum_value) && cJSON_IsBool(doc)) {
                if (enum_value->type == cJSON_True && doc->type == cJSON_True) {
                    matches++;
                    break;
                }
                if (enum_value->type == cJSON_False && doc->type == cJSON_False) {
                    matches++;
                    break;
                }
            }
        }

        if (matches == 0 && enum_size > 0) {
            char *err = wf_json_validation_error(path, "value is not in enum list");
            if (out_error) *out_error = err;
            return false;
        }
    }

    return true;
}

/* Number range validation implementation */
static bool wf_json_validate_number_ranges_enhanced(const cJSON *schema, const cJSON *doc,
                                                   const char *path, wf_json_validation_options *options,
                                                   char **out_error) {
    if (!schema || !doc || !cJSON_IsNumber(doc) || !options || !out_error) return true;

    cJSON *minimum = cJSON_GetObjectItem(schema, "minimum");
    if (minimum && cJSON_IsNumber(minimum)) {
        if (doc->valuedouble < minimum->valuedouble) {
            char *err = wf_json_validation_error(path, "value less than minimum");
            if (out_error) *out_error = err;
            return false;
        }
    }

    cJSON *maximum = cJSON_GetObjectItem(schema, "maximum");
    if (maximum && cJSON_IsNumber(maximum)) {
        if (doc->valuedouble > maximum->valuedouble) {
            char *err = wf_json_validation_error(path, "value greater than maximum");
            if (out_error) *out_error = err;
            return false;
        }
    }

    return true;
}

/* Pattern validation implementation */
static bool wf_json_validate_patterns_enhanced(const cJSON *schema, const cJSON *doc,
                                               const char *path, wf_json_validation_options *options,
                                               char **out_error) {
    if (!schema || !doc || !cJSON_IsString(doc) || !options || !out_error) return true;

    cJSON *pattern = cJSON_GetObjectItem(schema, "pattern");
    if (pattern && cJSON_IsString(pattern)) {
        int matches = wf_json_matches_pattern(doc->valuestring, pattern->valuestring);
        if (!matches) {
            char *err = wf_json_validation_error(path, "value does not match pattern");
            if (out_error) *out_error = err;
            return false;
        }
    }

    return true;
}

/* Additional properties validation */
static bool wf_json_validate_additional_props_enhanced(const cJSON *schema, const cJSON *doc,
                                                       const char *path,
                                                       wf_json_validation_options *options,
                                                       char **out_error) {
    if (!schema || !doc || !cJSON_IsObject(doc) || !options || !out_error) return true;

    cJSON *additional = cJSON_GetObjectItem(schema, "additionalProperties");
    if (additional) {
        int allow_additional = 1;  // Default is true

        if (cJSON_IsBool(additional)) {
            allow_additional = additional->type == cJSON_True;
        } else if (cJSON_IsObject(additional)) {
            // Schema for additional properties, validate them
            cJSON *child = NULL;
            cJSON *child_path;
            const char *base_path = wf_path_join(path, "");

            cJSON_ArrayForEach(child, cJSON_GetObjectItem(doc, "$")) {
                child_path = wf_path_join(base_path, child->string);
                bool ok = wf_json_validate_rec_enhanced(additional, child, child_path,
                                                      options, out_error);
                free((void *)base_path);
                free((void *)child_path);
                if (!ok) return false;
            }
            free((void *)base_path);
            return true;
        }

        if (!allow_additional) {
            // All properties are already validated via properties keyword
            // No additional validation needed
        }
    }

    return true;
}

/* Union validation (anyOf, oneOf, not) */
static bool wf_json_validate_unions_enhanced(const cJSON *schema, const cJSON *doc,
                                            const char *path, wf_json_validation_options *options,
                                            char **out_error) {
    if (!schema || !doc || !options || !out_error) return true;

    cJSON *anyOf = cJSON_GetObjectItem(schema, "anyOf");
    if (anyOf && cJSON_IsArray(anyOf)) {
        int any_satisfied = 0;
        cJSON *sub_schema;
        cJSON *error_buf = NULL;

        cJSON_ArrayForEach(sub_schema, anyOf) {
            if (wf_json_validate_rec_enhanced(sub_schema, doc, path, options, &error_buf)) {
                any_satisfied++;
                break;  // anyOf satisfied
            }
            if (error_buf) {
                // Store error for potential reporting
                free(error_buf);
                error_buf = NULL;
            }
        }

        if (any_satisfied == 0) {
            if (error_buf) {
                if (out_error) *out_error = error_buf;
            } else {
                char *err = wf_json_validation_error(path, "value does not match any schema in anyOf");
                if (out_error) *out_error = err;
            }
            return false;
        }
        return true;
    }

    cJSON *oneOf = cJSON_GetObjectItem(schema, "oneOf");
    if (oneOf && cJSON_IsArray(oneOf)) {
        int satisfied_count = 0;
        cJSON *sub_schema;
        cJSON *error_buf = NULL;

        cJSON_ArrayForEach(sub_schema, oneOf) {
            if (wf_json_validate_rec_enhanced(sub_schema, doc, path, options, &error_buf)) {
                satisfied_count++;
            }
            if (error_buf) {
                free(error_buf);
                error_buf = NULL;
            }
        }

        if (satisfied_count != 1) {
            if (error_buf) {
                if (out_error) *out_error = error_buf;
            } else {
                char *err = wf_json_validation_error(path, "value does not match exactly one schema in oneOf");
                if (out_error) *out_error = err;
            }
            return false;
        }
        return true;
    }

    cJSON *not_schema = cJSON_GetObjectItem(schema, "not");
    if (not_schema && cJSON_IsObject(not_schema)) {
        if (wf_json_validate_rec_enhanced(not_schema, doc, path, options, out_error)) {
            // Not schema matched, meaning validation should fail
            char *err = wf_json_validation_error(path, "value matches forbidden schema");
            if (out_error) *out_error = err;
            return false;
        }
        return true;  // Not validation succeeded
    }

    return true;
}

/* CID link validation */
static bool wf_json_validate_cid_links_enhanced(const cJSON *schema, const cJSON *doc,
                                               const char *path, wf_json_validation_options *options,
                                               char **out_error) {
    if (!schema || !doc || !options || !out_error) return true;

    // Check if doc is an object with $link property (CID reference)
    if (cJSON_IsObject(doc)) {
        cJSON *link = cJSON_GetObjectItem(doc, "$link");
        if (link && cJSON_IsString(link)) {
            // Validate CID format
            if (!wf_syntax_cid_is_valid(link->valuestring)) {
                char *err = wf_json_validation_error(path, "CID format is invalid");
                if (out_error) *out_error = err;
                return false;
            }
        }
    }

    return true;
}

/* Bytes validation */
static bool wf_json_validate_bytes_enhanced(const cJSON *schema, const cJSON *doc,
                                            const char *path, wf_json_validation_options *options,
                                            char **out_error) {
    if (!schema || !doc || !options || !out_error) return true;

    // Check if doc is an object with $bytes property (base64 encoded data)
    if (cJSON_IsObject(doc)) {
        cJSON *bytes = cJSON_GetObjectItem(doc, "$bytes");
        if (bytes && cJSON_IsString(bytes)) {
            if (!wf_is_valid_base64(bytes->valuestring)) {
                char *err = wf_json_validation_error(path, "bytes field is not valid base64");
                if (out_error) *out_error = err;
                return false;
            }
        }
    }

    return true;
}

/* Main recursive validation function */
static bool wf_json_validate_rec_enhanced(const cJSON *schema, const cJSON *doc,
                                        const char *path, wf_json_validation_options *options,
                                        char **out_error) {
    if (!schema || !doc || !options) return true;

    // First, do original validation for basic types
    cJSON *type = cJSON_GetObjectItem(schema, "type");
    if (type && cJSON_IsString(type)) {
        if (!wf_json_type_matches(type->valuestring, doc)) {
            char *err = wf_json_validation_error(path, "type mismatch");
            if (out_error) *out_error = err;
            return false;
        }
    }

    // Apply enhanced validation rules
    if (options && options->validate_formats) {
        if (!wf_json_validate_formats_enhanced(schema, doc, path, options, out_error)) {
            return false;
        }
    }

    if (options && options->validate_enums) {
        if (!wf_json_validate_enums_enhanced(schema, doc, path, options, out_error)) {
            return false;
        }
    }

    if (options && options->validate_numbers) {
        if (!wf_json_validate_number_ranges_enhanced(schema, doc, path, options, out_error)) {
            return false;
        }
    }

    if (options && options->validate_patterns) {
        if (!wf_json_validate_patterns_enhanced(schema, doc, path, options, out_error)) {
            return false;
        }
    }

    if (options && options->validate_additional_props) {
        if (!wf_json_validate_additional_props_enhanced(schema, doc, path, options, out_error)) {
            return false;
        }
    }

    if (options && options->validate_unions) {
        if (!wf_json_validate_unions_enhanced(schema, doc, path, options, out_error)) {
            return false;
        }
    }

    if (options && options->validate_cid_links) {
        if (!wf_json_validate_cid_links_enhanced(schema, doc, path, options, out_error)) {
            return false;
        }
    }

    if (options && options->validate_bytes) {
        if (!wf_json_validate_bytes_enhanced(schema, doc, path, options, out_error)) {
            return false;
        }
    }

    // Original recursive validation for objects and arrays
    if (cJSON_IsObject(doc)) {
        cJSON *required = cJSON_GetObjectItem(schema, "required");
        if (required && cJSON_IsArray(required)) {
            cJSON *req;
            cJSON_ArrayForEach(req, required) {
                if (!cJSON_IsString(req)) continue;
                if (!cJSON_HasObjectItem(doc, req->valuestring)) {
                    char *err = wf_json_validation_error(path, "missing required property");
                    if (out_error) *out_error = err;
                    return false;
                }
            }
        }

        cJSON *properties = cJSON_GetObjectItem(schema, "properties");
        if (properties && cJSON_IsObject(properties)) {
            cJSON *prop_schema;
            cJSON_ArrayForEach(prop_schema, properties) {
                cJSON *child = cJSON_GetObjectItem(doc, prop_schema->string);
                if (!child) continue;
                char *child_path = wf_path_join(path, prop_schema->string);
                bool ok = wf_json_validate_rec_enhanced(prop_schema, child, child_path, options, out_error);
                free((void *)child_path);
                if (!ok) return false;
            }
        }
    } else if (cJSON_IsArray(doc)) {
        cJSON *items = cJSON_GetObjectItem(schema, "items");
        if (items && cJSON_IsObject(items)) {
            int idx = 0;
            cJSON *elem;
            cJSON_ArrayForEach(elem, doc) {
                char *child_path = (char *)malloc(64);
                snprintf(child_path, 64, "%s[%d]", path, idx);
                bool ok = wf_json_validate_rec_enhanced(items, elem, child_path, options, out_error);
                free(child_path);
                if (!ok) return false;
                idx++;
            }
        }
    }

    return true;
}

/* Format matching implementation */
static int wf_json_matches_format(const char *value, wf_json_format format,
                                 wf_json_validation_options *options) {
    if (!value || !options) return 0;

    switch (format) {
        case WF_FORMAT_DATE_TIME_RFC3339:
            // Simple RFC 3339-like validation
            return (strlen(value) == 20 || strlen(value) == 24) &&
                   value[10] == 'T' && value[13] == ':' && value[16] == ':' &&
                   (value[19] == 'Z' || value[19] == '+' || value[19] == '-') &&
                   (value[4] == '-' && value[7] == '-') &&
                   (value[13] >= '0' && value[13] <= '2') &&
                   (value[16] >= '0' && value[16] <= '5') &&
                   (value[16] >= '0' && value[16] <= '5');

        case WF_FORMAT_EMAIL:
            // Simple email validation
            return (strlen(value) > 0 && strchr(value, '@') != NULL &&
                    strchr(value, '.') != NULL && value[strlen(value) - 1] != '@');

        case WF_FORMAT_URI:
            // URI validation
            return (strlen(value) > 0 && (strstr(value, "://") != NULL ||
                                          strstr(value, "http://") != NULL ||
                                          strstr(value, "https://") != NULL ||
                                          strstr(value, "file://") != NULL));

        case WF_FORMAT_HANDLE:
            // Handle validation (alphanumeric + hyphens, no special chars)
            for (const char *p = value; *p; p++) {
                if (!isalnum(*p) && *p != '-' && *p != '_' && *p != '.') {
                    return 0;
                }
            }
            return strlen(value) > 0;

        case WF_FORMAT_CID:
            return wf_syntax_cid_is_valid(value);

        case WF_FORMAT_SCHEMA:
            return strncmp(value, "app.", 4) == 0 || strncmp(value, "com.", 4) == 0 ||
                   strncmp(value, "internal.", 9) == 0;

        default:
            return 0;
    }
}

/* Pattern matching implementation */
static int wf_json_matches_pattern(const char *value, const char *pattern) {
    if (!value || !pattern) return 0;

    // Implement simple regex-like pattern matching
    // For production, consider using a proper regex library
    int wildcard_count = 0;
    const char *p = pattern;

    while (*p) {
        if (*p == '*') {
            wildcard_count++;
            p++;
            continue;
        }

        if (*p == '?' || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || strchr("!@#$%^&()_+-=[]{}|;:'\\", *p) != NULL) {
            // Simple exact matching for now
            if (strchr(value, *p) == NULL) return 0;
            p++;
            const char *v = value;
            while (*v && *v != *p) v++;
            if (!*v) return 0;
            value = v + 1;
            continue;
        }

        p++;
    }

    return (*value == '\0');
}

/* Validation options helpers */
void wf_json_validation_options_init(wf_json_validation_options *options) {
    if (!options) return;
    memset(options, 0, sizeof(*options));
    options->validate_formats = 1;
    options->validate_enums = 1;
    options->validate_numbers = 1;
    options->validate_patterns = 1;
    options->validate_additional_props = 1;
    options->validate_unions = 1;
    options->validate_cid_links = 1;
    options->validate_bytes = 1;
    options->strict_mode = 1;
    options->format_whitelist[0] = "rfc3339";
    options->format_whitelist[1] = "date-time";
    options->format_whitelist[2] = "email";
    options->format_whitelist[3] = "uri";
    options->format_whitelist[4] = "handle";
    options->format_whitelist[5] = "cid";
    options->format_whitelist[6] = "schema";
    options->default_formats[0] = "rfc3339";
    options->default_formats[1] = "date-time";
    options->default_formats[2] = "email";
    options->default_formats[3] = "uri";
    options->default_formats[4] = "handle";
    options->default_formats[5] = "cid";
    options->default_formats[6] = "schema";
}

/* Sorted canonical JSON implementation */
wf_status wf_json_canonicalize_sorted_implementation(const char *in, size_t len, char **out,
                                                   int sort_objects) {
    if (!in || !out) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_ParseWithLength(in, len);
    if (!root) return WF_ERR_PARSE;

    // Recursive function to sort object keys
    static void wf_sort_object_keys(cJSON *obj) {
        if (!obj || !cJSON_IsObject(obj)) return;

        cJSON *child;
        cJSON_ArrayForEach(child, obj) {
            if (cJSON_IsObject(child)) {
                // Sort this object's keys
                wf_sort_object_keys(child);
                // If child has associated for loop or similar, handle it
            }
        }
    }

    if (sort_objects) {
        wf_sort_object_keys(root);
    }

    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) return WF_ERR_ALLOC;

    *out = printed;
    return WF_OK;
}

/* Lex JSON conversion implementation */
f wf_status wf_json_to_lex_implementation(const char *json, size_t len, char **out_lex) {
    // Placeholder for future implementation
    // This would convert generic JSON to AT Protocol lex format
    // with proper CID, bytes, blob handling
    (void)json;
    (void)len;
    (void)out_lex;
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_lex_to_json_implementation(const char *lex, size_t len, char **out_json) {
    // Placeholder for future implementation
    // This would convert AT Protocol lex format to generic JSON
    (void)lex;
    (void)len;
    (void)out_json;
    return WF_ERR_NOT_IMPLEMENTED;
}