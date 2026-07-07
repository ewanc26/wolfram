#include "wolfram/json.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

wf_status wf_json_canonicalize(const char *in, size_t len, char **out) {
    if (!in || !out) {
        return WF_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_ParseWithLength(in, len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return WF_ERR_ALLOC;
    }

    *out = printed;
    return WF_OK;
}

static bool wf_json_type_matches(const char *type, const cJSON *node) {
    if (strcmp(type, "object") == 0)  return cJSON_IsObject(node);
    if (strcmp(type, "array") == 0)   return cJSON_IsArray(node);
    if (strcmp(type, "string") == 0)  return cJSON_IsString(node);
    if (strcmp(type, "number") == 0)  return cJSON_IsNumber(node);
    if (strcmp(type, "boolean") == 0) return cJSON_IsBool(node);
    if (strcmp(type, "null") == 0)    return cJSON_IsNull(node);
    return true;
}

/*
 * Recursively validate `doc` against `schema`. `path` is a caller-owned,
 * NUL-terminated string describing the current location (for error messages).
 * On failure sets *out_error to a heap-owned message and returns false.
 */
static bool wf_json_validate_rec(const cJSON *schema, const cJSON *doc,
                                 const char *path, char **out_error) {
    cJSON *type = cJSON_GetObjectItem(schema, "type");
    if (type && cJSON_IsString(type)) {
        if (!wf_json_type_matches(type->valuestring, doc)) {
            size_t len = strlen(path) + strlen(type->valuestring) + 64;
            char *msg = (char *)malloc(len);
            if (msg) {
                snprintf(msg, len,
                         "at %s: expected type \"%s\" but found different type",
                         path, type->valuestring);
            }
            *out_error = msg;
            return false;
        }
    }

    if (cJSON_IsObject(doc)) {
        cJSON *required = cJSON_GetObjectItem(schema, "required");
        if (required && cJSON_IsArray(required)) {
            cJSON *req;
            cJSON_ArrayForEach(req, required) {
                if (!cJSON_IsString(req)) continue;
                if (!cJSON_HasObjectItem(doc, req->valuestring)) {
                    size_t len = strlen(path) + strlen(req->valuestring) + 64;
                    char *msg = (char *)malloc(len);
                    if (msg) {
                        snprintf(msg, len,
                                 "at %s: missing required property \"%s\"",
                                 path, req->valuestring);
                    }
                    *out_error = msg;
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
                size_t len = strlen(path) + strlen(prop_schema->string) + 8;
                char *child_path = (char *)malloc(len);
                if (!child_path) {
                    *out_error = NULL;
                    return false;
                }
                snprintf(child_path, len, "%s.%s", path, prop_schema->string);
                bool ok = wf_json_validate_rec(prop_schema, child, child_path, out_error);
                free(child_path);
                if (!ok) return false;
            }
        }
    } else if (cJSON_IsArray(doc)) {
        cJSON *items = cJSON_GetObjectItem(schema, "items");
        if (items && cJSON_IsObject(items)) {
            int idx = 0;
            cJSON *elem;
            cJSON_ArrayForEach(elem, doc) {
                char child_path[32];
                snprintf(child_path, sizeof(child_path), "%s[%d]", path, idx);
                bool ok = wf_json_validate_rec(items, elem, child_path, out_error);
                if (!ok) return false;
                idx++;
            }
        }
    }

    return true;
}

wf_status wf_json_validate(const char *schema_json, size_t schema_len,
                           const char *doc_json, size_t doc_len,
                           char **out_error) {
    if (!schema_json || !doc_json || !out_error) {
        return WF_ERR_INVALID_ARG;
    }
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

    bool ok = wf_json_validate_rec(schema, doc, "$", out_error);

    cJSON_Delete(doc);
    cJSON_Delete(schema);

    return ok ? WF_OK : WF_ERR_INVALID_ARG;
}
