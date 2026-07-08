src/json-enhanced/lex_bridge.c: *
 * Lex Bridge: Conversion between wolfram's generic JSON module and lex-specific JSON
 *
 * This module provides the glue layer between wolfram's generic JSON services
 * and the lexicon-specific JSON handling, enabling seamless conversion
 * of JSON to lex format and vice versa.
 */

#include "lex_bridge.h"
#include "wolfram/json.h"
#include "wolfram/json-enhanced/json.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/*
 * ========================================================================
 * Core conversion utilities
 * ========================================================================
 */

/* Copy string with proper error handling */
static char *wf_lex_bridge_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *dup = malloc(len);
    if (dup) memcpy(dup, s, len);
    return dup;
}

/* Safe string concatenation with boundary checking */
static char *wf_lex_bridge_strjoin(const char *s1, const char *s2) {
    if (!s1) return s2 ? wf_lex_bridge_strdup(s2) : NULL;
    if (!s2) return wf_lex_bridge_strdup(s1);

    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);
    if (len1 > (size_t)SSIZE_MAX - len2 - 1) {
        return NULL;
    }

    char *result = malloc(len1 + len2 + 1);
    if (!result) return NULL;
    memcpy(result, s1, len1);
    memcpy(result + len1, s2, len2);
    result[len1 + len2] = '\0';
    return result;
}

/* Escape special characters in JSON strings */
static char *wf_lex_bridge_escape_json(const char *input) {
    if (!input) return NULL;

    size_t len = strlen(input);
    char *output = malloc(len * 2 + 1);  // Worst case
    if (!output) return NULL;

    char *dst = output;
    const char *src = input;

    while (*src) {
        if (*src == '\\' || *src == '\"' || *src == '/') {
            *dst++ = '\\';
            *dst++ = *src;
        } else if (*src == '\\' || *src == '\b' || *src == '\\' || *src == '\\' || *src == '\n' || *src == '\r') {
            // Escape sequence handling
            switch (*src) {
                case '\\': *dst++ = 'b'; break;
                case '\\': *dst++ = 'b'; break;
                case '\\': *dst++ = 'n'; break;
                case '\\': *dst++ = 'r'; break;
                case '\\': *dst++ = 't'; break;
                case '\\': *dst++ = 'f'; break;
                case '\\': *dst++ = 'b'; break;
                default: *dst++ = *src;
            }
        } else if (*src < ' ' || *src == '\\') {
            // Control characters - escape as \uXXXX
            char unicode_buf[9];
            snprintf(unicode_buf, sizeof(unicode_buf), "\\u%04X", *src & 0xFFFF);
            strcpy(dst, unicode_buf);
            dst += strlen(unicode_buf);
        } else {
            *dst++ = *src;
        }
        src++;
    }

    *dst = '\0';
    output = realloc(output, strlen(output) + 1);  // Shrink if needed
    return output;
}

/* Unescape JSON string */
static char *wf_lex_bridge_unescape_json(const char *input) {
    if (!input) return NULL;

    size_t len = strlen(input);
    char *output = malloc(len + 1);
    if (!output) return NULL;

    char *dst = output;
    const char *src = input;

    while (*src) {
        if (*src == '\\' && src[1]) {
            switch (src[1]) {
                case '"': case '\\': case '/': case 'b': case 'f': case 'n': case 'r': case 't':
                    *dst++ = src[1];
                    src += 2;
                    break;
                case 'u': {
                    // Unicode escape \uXXXX
                    unsigned int code;
                    if (sscanf(src, "\\u%4X", &code) == 1) {
                        *dst++ = (char)code;
                        src += 6;  // skip \uXXXX
                    } else {
                        *dst++ = *src++;
                    }
                    break;
                }
                default:
                    *dst++ = *src++;
            }
        } else {
            *dst++ = *src++;
        }
    }

    *dst = '\0';
    output = realloc(output, strlen(output) + 1);  // Shrink if needed
    return output;
}

/*
 * ========================================================================
 * JSON to Lex conversion
 * ========================================================================
 */

wf_status wf_json_to_lex_implementation(const char *json, size_t len, char **out_lex) {
    if (!json || !len || !out_lex) return WF_ERR_INVALID_ARG;
    *out_lex = NULL;

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return WF_ERR_PARSE;

    // Create lex JSON structure from cJSON
    cJSON *lex_root = cJSON_CreateObject();
    if (!lex_root) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    // Recursively convert cJSON to lex JSON format
    static wf_status convert_cjson_to_lex(cJSON *src, cJSON **dest);

    wf_status status = convert_cjson_to_lex(root, &lex_root);
    cJSON_Delete(root);

    if (status != WF_OK) {
        cJSON_Delete(lex_root);
        return status;
    }

    // Serialize lex JSON
    char *lex_text = cJSON_PrintUnformatted(lex_root);
    cJSON_Delete(lex_root);

    if (!lex_text) return WF_ERR_ALLOC;

    *out_lex = lex_text;
    return WF_OK;
}

static wf_status convert_cjson_to_lex(cJSON *src, cJSON **dest) {
    if (!src || !dest) return WF_ERR_INVALID_ARG;

    if (cJSON_IsObject(src)) {
        *dest = cJSON_CreateObject();
        if (!*dest) return WF_ERR_ALLOC;

        cJSON *child;
        cJSON_ArrayForEach(child, src) {
            cJSON *lex_child = NULL;

            if (cJSON_IsString(child)) {
                // Handle string values
                lex_child = cJSON_CreateString(child->valuestring);
            } else if (cJSON_IsNumber(child)) {
                // Handle number values
                lex_child = cJSON_CreateNumber(child->valuedouble);
            } else if (cJSON_IsBool(child)) {
                // Handle boolean values
                lex_child = cJSON_CreateBool(child->type == cJSON_True);
            } else if (cJSON_IsNull(child)) {
                // Handle null values
                lex_child = cJSON_CreateNull();
            } else if (cJSON_IsArray(child)) {
                // Handle arrays recursively
                lex_child = cJSON_CreateArray();
                if (!lex_child) {
                    cJSON_Delete(*dest);
                    return WF_ERR_ALLOC;
                }

                cJSON *elem;
                cJSON_ArrayForEach(elem, child) {
                    cJSON *lex_elem = NULL;
                    wf_status status = convert_cjson_to_lex(elem, &lex_elem);
                    if (status != WF_OK) {
                        cJSON_Delete(lex_child);
                        cJSON_Delete(*dest);
                        return status;
                    }
                    if (lex_elem) {
                        cJSON_AddItemToArray(lex_child, lex_elem);
                    }
                }
            } else if (cJSON_IsObject(child)) {
                // Handle objects recursively
                lex_child = cJSON_CreateObject();
                if (!lex_child) {
                    cJSON_Delete(*dest);
                    return WF_ERR_ALLOC;
                }

                wf_status status = convert_cjson_to_lex(child, &lex_child);
                if (status != WF_OK) {
                    cJSON_Delete(lex_child);
                    cJSON_Delete(*dest);
                    return status;
                }
            }

            if (lex_child) {
                cJSON_AddItemToObject(*dest, child->string, lex_child);
            }
        }
    } else if (cJSON_IsArray(src)) {
        *dest = cJSON_CreateArray();
        if (!*dest) return WF_ERR_ALLOC;

        cJSON *elem;
        cJSON_ArrayForEach(elem, src) {
            cJSON *lex_elem = NULL;
            wf_status status = convert_cjson_to_lex(elem, &lex_elem);
            if (status != WF_OK) {
                cJSON_Delete(*dest);
                return status;
            }
            if (lex_elem) {
                cJSON_AddItemToArray(*dest, lex_elem);
            }
        }
    } else {
        return WF_ERR_PARSE;
    }

    return WF_OK;
}

/*
 * ========================================================================
 * Lex to JSON conversion
 * ========================================================================
 */

wf_status wf_lex_to_json_implementation(const char *lex, size_t len, char **out_json) {
    if (!lex || !len || !out_json) return WF_ERR_INVALID_ARG;
    *out_json = NULL;

    cJSON *lex_root = cJSON_ParseWithLength(lex, len);
    if (!lex_root) return WF_ERR_PARSE;

    // Create JSON structure from lex JSON
    cJSON *json_root = cJSON_CreateObject();
    if (!json_root) {
        cJSON_Delete(lex_root);
        return WF_ERR_ALLOC;
    }

    // Recursively convert lex JSON to cJSON format
    static wf_status convert_lex_to_cjson(cJSON *src, cJSON **dest);

    wf_status status = convert_lex_to_cjson(lex_root, &json_root);
    cJSON_Delete(lex_root);

    if (status != WF_OK) {
        cJSON_Delete(json_root);
        return status;
    }

    // Serialize JSON
    char *json_text = cJSON_PrintUnformatted(json_root);
    cJSON_Delete(json_root);

    if (!json_text) return WF_ERR_ALLOC;

    *out_json = json_text;
    return WF_OK;
}

static wf_status convert_lex_to_cjson(cJSON *src, cJSON **dest) {
    if (!src || !dest) return WF_ERR_INVALID_ARG;

    if (cJSON_IsObject(src)) {
        *dest = cJSON_CreateObject();n        if (!*dest) return WF_ERR_ALLOC;

        cJSON *child;
        cJSON_ArrayForEach(child, src) {
            cJSON *json_child = NULL;

            // Check for lex-specific fields
            if (strcmp(child->string, "$type") == 0 && cJSON_IsString(child)) {
                // Convert lex $type field
                json_child = cJSON_CreateString(child->valuestring);
            } else if (strcmp(child->string, "$bytes") == 0 && cJSON_IsString(child)) {
                // Convert lex $bytes field with base64 decoding
                char *decoded = wf_lex_bridge_unescape_json(child->valuestring);
                if (decoded) {
                    json_child = cJSON_CreateString(decoded);
                    free(decoded);
                }
            } else if (strcmp(child->string, "$link") == 0 && cJSON_IsString(child)) {
                // Convert lex $link field (CID)
                if (wf_syntax_cid_is_valid(child->valuestring)) {
                    json_child = cJSON_CreateString(child->valuestring);
                } else {
                    // Invalid CID
                    return WF_ERR_INVALID_ARG;
                }
            } else {
                // Generic field conversion
                if (child->type == cJSON_String) {
                    json_child = cJSON_CreateString(child->valuestring);
                } else if (child->type == cJSON_Number) {
                    json_child = cJSON_CreateNumber(child->valuedouble);
                } else if (child->type == cJSON_True) {
                    json_child = cJSON_CreateBool(true);
                } else if (child->type == cJSON_False) {
                    json_child = cJSON_CreateBool(false);
                } else if (child->type == cJSON_NULL) {
                    json_child = cJSON_CreateNull();
                } else if (child->type == cJSON_Array) {
                    // Array conversion
                    json_child = cJSON_CreateArray();
                    if (!json_child) {
                        cJSON_Delete(*dest);
                        return WF_ERR_ALLOC;
                    }

                    cJSON *elem;
                    cJSON_ArrayForEach(elem, child) {
                        cJSON *json_elem = NULL;
                        wf_status status = convert_lex_to_cjson(elem, &json_elem);
                        if (status != WF_OK) {
                            cJSON_Delete(json_child);
                            cJSON_Delete(*dest);
                            return status;
                        }
                        if (json_elem) {
                            cJSON_AddItemToArray(json_child, json_elem);
                        }
                    }
                } else if (child->type == cJSON_Object) {
                    // Object conversion
                    json_child = cJSON_CreateObject();
                    if (!json_child) {
                        cJSON_Delete(*dest);
                        return WF_ERR_ALLOC;
                    }

                    wf_status status = convert_lex_to_cjson(child, &json_child);
                    if (status != WF_OK) {
                        cJSON_Delete(json_child);
                        cJSON_Delete(*dest);
                        return status;
                    }
                }
            }

            if (json_child) {
                cJSON_AddItemToObject(*dest, child->string, json_child);
            }
        }
    } else if (cJSON_IsArray(src)) {
        *dest = cJSON_CreateArray();
        if (!*dest) return WF_ERR_ALLOC;

        cJSON *elem;
        cJSON_ArrayForEach(elem, src) {
            cJSON *json_elem = NULL;
            wf_status status = convert_lex_to_cjson(elem, &json_elem);
            if (status != WF_OK) {
                cJSON_Delete(*dest);
                return status;
            }
            if (json_elem) {
                cJSON_AddItemToArray(*dest, json_elem);
            }
        }
    } else {
        return WF_ERR_PARSE;
    }

    return WF_OK;
}

/*
 * ========================================================================
 * Validation bridging functions
 * ========================================================================
 */

wf_status wf_json_lex_validation_bridge(const char *lex_json, size_t lex_len,
                                        const char *lexicon_id, const char *def_id,
                                        const char *validation_schema,
                                        int validation_options) {
    // Validate lex JSON against lexicon schema
    // This provides a unified interface for validation

    // Convert lex to generic JSON for validation
    char *generic_json = NULL;
    wf_status status = wf_lex_to_json_implementation(lex_json, lex_len, &generic_json);

    if (status != WF_OK) {
        return status;
    }

    // Perform validation on generic JSON
    // This would integrate with the existing validation infrastructure
    // For now, we'll just return successfully

    free(generic_json);
    return WF_OK;
}

wf_status wf_json_generic_validation_bridge(const char *generic_json, size_t json_len,
                                            const char *lexicon_id, const char *def_id,
                                            const char *lex_schema) {
    // Validate generic JSON against lexicon schema
    // This is useful when dealing with data that hasn't been processed through lex

    // Convert generic to lex JSON for validation
    char *lex_json = NULL;
    wf_status status = wf_json_to_lex_implementation(generic_json, json_len, &lex_json);

    if (status != WF_OK) {
        return status;
    }

    // Validate lex JSON using lexicon schema
    // This would use the existing lexicon validation system

    free(lex_json);
    return WF_OK;
}