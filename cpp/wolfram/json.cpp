#include "wolfram/json.h"
#include "wolfram/syntax.h"

#include <cstdarg>
#include <cmath>
#include <cJSON.h>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <regex.h>
#include <string>
#include <vector>

namespace {

struct cjson_deleter {
    void operator()(cJSON *p) const noexcept {
        cJSON_Delete(p);
    }
};

using cjson_ptr = std::unique_ptr<cJSON, cjson_deleter>;

static bool wf_json_type_matches(const char *type, const cJSON *node) {
    if (std::strcmp(type, "object") == 0) return cJSON_IsObject(node);
    if (std::strcmp(type, "array") == 0) return cJSON_IsArray(node);
    if (std::strcmp(type, "string") == 0) return cJSON_IsString(node);
    if (std::strcmp(type, "number") == 0) return cJSON_IsNumber(node);
    if (std::strcmp(type, "boolean") == 0) return cJSON_IsBool(node);
    if (std::strcmp(type, "null") == 0) return cJSON_IsNull(node);
    return true;
}

static void wf_json_fail(char **out_error, const std::string &path,
                         const char *fmt, ...) {
    if (!out_error) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::string msg = "at " + path + ": " + buf;
    *out_error = static_cast<char *>(std::malloc(msg.size() + 1));
    if (*out_error) std::memcpy(*out_error, msg.c_str(), msg.size() + 1);
}

static bool wf_json_deep_equal(const cJSON *a, const cJSON *b) {
    if (cJSON_IsNull(a) && cJSON_IsNull(b)) return true;
    if (cJSON_IsBool(a) && cJSON_IsBool(b))
        return (cJSON_IsTrue(a) != 0) == (cJSON_IsTrue(b) != 0);
    if (cJSON_IsNumber(a) && cJSON_IsNumber(b))
        return a->valuedouble == b->valuedouble;
    if (cJSON_IsString(a) && cJSON_IsString(b))
        return std::strcmp(a->valuestring, b->valuestring) == 0;
    if (cJSON_IsObject(a) && cJSON_IsObject(b)) {
        int na = cJSON_GetArraySize(a);
        int nb = cJSON_GetArraySize(b);
        if (na != nb) return false;
        cJSON *item;
        cJSON_ArrayForEach(item, a) {
            cJSON *ob = cJSON_GetObjectItem(b, item->string);
            if (!ob) return false;
            if (!wf_json_deep_equal(item, ob)) return false;
        }
        return true;
    }
    if (cJSON_IsArray(a) && cJSON_IsArray(b)) {
        int n = cJSON_GetArraySize(a);
        if (n != cJSON_GetArraySize(b)) return false;
        cJSON *ia, *ib;
        int i = 0;
        cJSON_ArrayForEach(ia, a) {
            ib = cJSON_GetArrayItem(b, i);
            if (!wf_json_deep_equal(ia, ib)) return false;
            i++;
        }
        return true;
    }
    return false;
}

static bool wf_json_format_email(const char *s) {
    const char *at = std::strchr(s, '@');
    if (!at || at == s) return false;
    if (std::strchr(at + 1, '@')) return false;
    if (std::strlen(at + 1) == 0) return false;
    return true;
}

static bool wf_json_format_hostname(const char *s) {
    size_t n = std::strlen(s);
    if (n == 0 || n > 253) return false;
    if (s[0] == '.' || s[n - 1] == '.') return false;
    size_t i = 0;
    while (i < n) {
        size_t j = i;
        while (j < n && s[j] != '.') j++;
        size_t lblen = j - i;
        if (lblen == 0) return false;
        if (s[i] == '-' || s[j - 1] == '-') return false;
        for (size_t k = i; k < j; k++) {
            char c = s[k];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-')) {
                return false;
            }
        }
        i = (j < n) ? j + 1 : n;
    }
    return true;
}

static bool wf_json_check_format(const char *format, const char *value) {
    if (std::strcmp(format, "date-time") == 0)
        return wf_syntax_datetime_is_valid(value) != 0;
    if (std::strcmp(format, "email") == 0) return wf_json_format_email(value);
    if (std::strcmp(format, "uri") == 0)
        return std::strstr(value, "://") != nullptr;
    if (std::strcmp(format, "hostname") == 0)
        return wf_json_format_hostname(value);
    return true;
}

static bool wf_json_validate_rec(const cJSON *schema, const cJSON *doc,
                                 const std::string &path, char **out_error) {
    cJSON *type = cJSON_GetObjectItem(schema, "type");
    if (type && cJSON_IsString(type)) {
        if (!wf_json_type_matches(type->valuestring, doc)) {
            wf_json_fail(out_error, path,
                         "expected type \"%s\" but found different type",
                         type->valuestring);
            return false;
        }
    }

    cJSON *en = cJSON_GetObjectItem(schema, "enum");
    if (en && cJSON_IsArray(en)) {
        bool found = false;
        cJSON *cand;
        cJSON_ArrayForEach(cand, en) {
            if (wf_json_deep_equal(cand, doc)) {
                found = true;
                break;
            }
        }
        if (!found) {
            wf_json_fail(out_error, path,
                         "value does not match any element of enum");
            return false;
        }
    }

    cJSON *con = cJSON_GetObjectItem(schema, "const");
    if (con) {
        if (!wf_json_deep_equal(con, doc)) {
            wf_json_fail(out_error, path, "value does not equal const");
            return false;
        }
    }

    cJSON *fmt = cJSON_GetObjectItem(schema, "format");
    if (fmt && cJSON_IsString(fmt) && cJSON_IsString(doc)) {
        if (!wf_json_check_format(fmt->valuestring, doc->valuestring)) {
            wf_json_fail(out_error, path, "value does not match format \"%s\"",
                         fmt->valuestring);
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
                    wf_json_fail(out_error, path,
                                 "missing required property \"%s\"",
                                 req->valuestring);
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
                std::string child_path = path + "." + prop_schema->string;
                if (!wf_json_validate_rec(prop_schema, child, child_path,
                                          out_error))
                    return false;
            }
        }

        cJSON *ap = cJSON_GetObjectItem(schema, "additionalProperties");
        if (ap && cJSON_IsObject(ap)) {
            cJSON *child;
            cJSON_ArrayForEach(child, doc) {
                if (properties &&
                    cJSON_HasObjectItem(properties, child->string))
                    continue;
                std::string child_path = path + "." + child->string;
                if (!wf_json_validate_rec(ap, child, child_path, out_error))
                    return false;
            }
        } else if (ap && cJSON_IsFalse(ap)) {
            cJSON *child;
            cJSON_ArrayForEach(child, doc) {
                if (properties &&
                    cJSON_HasObjectItem(properties, child->string))
                    continue;
                wf_json_fail(out_error, path,
                             "additional property \"%s\" is not allowed",
                             child->string);
                return false;
            }
        }
    } else if (cJSON_IsArray(doc)) {
        cJSON *items = cJSON_GetObjectItem(schema, "items");
        if (items && cJSON_IsObject(items)) {
            int idx = 0;
            cJSON *elem;
            cJSON_ArrayForEach(elem, doc) {
                char child_path[32];
                std::snprintf(child_path, sizeof(child_path), "%s[%d]",
                              path.c_str(), idx);
                if (!wf_json_validate_rec(items, elem, child_path, out_error))
                    return false;
                idx++;
            }
        }

        int n = cJSON_GetArraySize(doc);
        cJSON *min_items = cJSON_GetObjectItem(schema, "minItems");
        if (min_items && cJSON_IsNumber(min_items) &&
            n < static_cast<int>(min_items->valuedouble)) {
            wf_json_fail(out_error, path, "array has fewer than %d items",
                         static_cast<int>(min_items->valuedouble));
            return false;
        }
        cJSON *max_items = cJSON_GetObjectItem(schema, "maxItems");
        if (max_items && cJSON_IsNumber(max_items) &&
            n > static_cast<int>(max_items->valuedouble)) {
            wf_json_fail(out_error, path, "array has more than %d items",
                         static_cast<int>(max_items->valuedouble));
            return false;
        }
        cJSON *unique = cJSON_GetObjectItem(schema, "uniqueItems");
        if (unique && cJSON_IsTrue(unique)) {
            for (int i = 0; i < n; i++) {
                cJSON *a = cJSON_GetArrayItem(doc, i);
                for (int j = i + 1; j < n; j++) {
                    cJSON *b = cJSON_GetArrayItem(doc, j);
                    if (wf_json_deep_equal(a, b)) {
                        wf_json_fail(out_error, path,
                                     "array contains duplicate items");
                        return false;
                    }
                }
            }
        }
    }

    if (cJSON_IsNumber(doc)) {
        cJSON *minimum = cJSON_GetObjectItem(schema, "minimum");
        if (minimum && cJSON_IsNumber(minimum) &&
            doc->valuedouble < minimum->valuedouble) {
            wf_json_fail(out_error, path, "value is less than minimum %g",
                         minimum->valuedouble);
            return false;
        }
        cJSON *maximum = cJSON_GetObjectItem(schema, "maximum");
        if (maximum && cJSON_IsNumber(maximum) &&
            doc->valuedouble > maximum->valuedouble) {
            wf_json_fail(out_error, path, "value is greater than maximum %g",
                         maximum->valuedouble);
            return false;
        }
        cJSON *exmin = cJSON_GetObjectItem(schema, "exclusiveMinimum");
        if (exmin && cJSON_IsNumber(exmin) &&
            doc->valuedouble <= exmin->valuedouble) {
            wf_json_fail(out_error, path,
                         "value is not greater than exclusiveMinimum %g",
                         exmin->valuedouble);
            return false;
        }
        cJSON *exmax = cJSON_GetObjectItem(schema, "exclusiveMaximum");
        if (exmax && cJSON_IsNumber(exmax) &&
            doc->valuedouble >= exmax->valuedouble) {
            wf_json_fail(out_error, path,
                         "value is not less than exclusiveMaximum %g",
                         exmax->valuedouble);
            return false;
        }
        cJSON *mult = cJSON_GetObjectItem(schema, "multipleOf");
        if (mult && cJSON_IsNumber(mult) && mult->valuedouble != 0) {
            double r = std::fmod(doc->valuedouble, mult->valuedouble);
            if (r < 0) r += mult->valuedouble;
            if (r > 1e-9 && (mult->valuedouble - r) > 1e-9) {
                wf_json_fail(out_error, path, "value is not a multiple of %g",
                             mult->valuedouble);
                return false;
            }
        }
    }

    if (cJSON_IsString(doc)) {
        size_t blen = std::strlen(doc->valuestring);
        cJSON *minlen = cJSON_GetObjectItem(schema, "minLength");
        if (minlen && cJSON_IsNumber(minlen) &&
            blen < static_cast<size_t>(minlen->valuedouble)) {
            wf_json_fail(out_error, path, "string is shorter than minLength %d",
                         static_cast<int>(minlen->valuedouble));
            return false;
        }
        cJSON *maxlen = cJSON_GetObjectItem(schema, "maxLength");
        if (maxlen && cJSON_IsNumber(maxlen) &&
            blen > static_cast<size_t>(maxlen->valuedouble)) {
            wf_json_fail(out_error, path, "string is longer than maxLength %d",
                         static_cast<int>(maxlen->valuedouble));
            return false;
        }
        cJSON *pat = cJSON_GetObjectItem(schema, "pattern");
        if (pat && cJSON_IsString(pat)) {
            regex_t re;
            if (regcomp(&re, pat->valuestring, REG_EXTENDED | REG_NOSUB) != 0) {
                regfree(&re);
                wf_json_fail(out_error, path, "invalid pattern in schema");
                return false;
            }
            int m = regexec(&re, doc->valuestring, 0, nullptr, 0);
            regfree(&re);
            if (m != 0) {
                wf_json_fail(out_error, path, "string does not match pattern");
                return false;
            }
        }
    }

    cJSON *any = cJSON_GetObjectItem(schema, "anyOf");
    if (any && cJSON_IsArray(any)) {
        int passes = 0;
        cJSON *sub;
        cJSON_ArrayForEach(sub, any) {
            char *se = nullptr;
            if (wf_json_validate_rec(sub, doc, path, &se))
                passes++;
            else
                std::free(se);
        }
        if (passes == 0) {
            wf_json_fail(out_error, path, "no subschema in anyOf matched");
            return false;
        }
    }

    cJSON *one = cJSON_GetObjectItem(schema, "oneOf");
    if (one && cJSON_IsArray(one)) {
        int passes = 0;
        cJSON *sub;
        cJSON_ArrayForEach(sub, one) {
            char *se = nullptr;
            if (wf_json_validate_rec(sub, doc, path, &se))
                passes++;
            else
                std::free(se);
        }
        if (passes != 1) {
            wf_json_fail(
                out_error, path,
                "expected exactly one subschema in oneOf to match, got %d",
                passes);
            return false;
        }
    }

    cJSON *no = cJSON_GetObjectItem(schema, "not");
    if (no && cJSON_IsObject(no)) {
        char *se = nullptr;
        if (wf_json_validate_rec(no, doc, path, &se)) {
            std::free(se);
            wf_json_fail(out_error, path, "subschema in not matched");
            return false;
        }
        std::free(se);
    }

    return true;
}

} // namespace

wf_status wf_json_canonicalize(const char *in, size_t len, char **out) {
    if (!in || !out) {
        return WF_ERR_INVALID_ARG;
    }

    const char *parse_end = nullptr;
    cjson_ptr root(cJSON_ParseWithLengthOpts(in, len, &parse_end, 0));
    if (!root) {
        return WF_ERR_PARSE;
    }
    if (parse_end) {
        const char *end = in + len;
        while (parse_end < end && (*parse_end == ' ' || *parse_end == '\t' ||
                                   *parse_end == '\n' || *parse_end == '\r')) {
            parse_end++;
        }
        if (parse_end < end) {
            return WF_ERR_PARSE;
        }
    }

    char *printed = cJSON_PrintUnformatted(root.get());
    if (!printed) {
        return WF_ERR_ALLOC;
    }

    *out = printed;
    return WF_OK;
}

wf_status wf_json_validate(const char *schema_json, size_t schema_len,
                           const char *doc_json, size_t doc_len,
                           char **out_error) {
    if (!schema_json || !doc_json || !out_error) {
        return WF_ERR_INVALID_ARG;
    }
    *out_error = nullptr;

    cjson_ptr schema(cJSON_ParseWithLength(schema_json, schema_len));
    if (!schema) {
        char *msg = static_cast<char *>(std::malloc(64));
        if (msg) std::strcpy(msg, "schema is not valid JSON");
        *out_error = msg;
        return WF_ERR_INVALID_ARG;
    }

    cjson_ptr doc(cJSON_ParseWithLength(doc_json, doc_len));
    if (!doc) {
        char *msg = static_cast<char *>(std::malloc(64));
        if (msg) std::strcpy(msg, "document is not valid JSON");
        *out_error = msg;
        return WF_ERR_INVALID_ARG;
    }

    bool ok = wf_json_validate_rec(schema.get(), doc.get(), "$", out_error);

    return ok ? WF_OK : WF_ERR_INVALID_ARG;
}
