// wf_lexgen — C++ port of tools/wf_lexgen.py.
//
// Generate C declarations and optional implementations from Lexicon JSON.
// Generated endpoint wrappers delegate all transport to wolfram/xrpc.h.
//
// The C++ tool is the successor to the Python generator. It links against the
// vendored cJSON (target `cjson`) and mirrors the Python generator's output
// byte for byte, so regenerating the checked-in atproto_lex.h/atproto_lex.c
// with either tool yields the same files.
//
// Usage:
//   wf_lexgen <lexicons...> [-o output.h]
//              [--source-output output.c] [--guard GUARD] [--header-rel H]

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <cJSON.h>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers mirroring the Python generator.
// ---------------------------------------------------------------------------

static const std::set<std::string> C_KEYWORDS = {
    "auto",       "break",     "case",           "char",
    "const",      "continue",  "default",        "do",
    "double",     "else",      "enum",           "extern",
    "float",      "for",       "goto",           "if",
    "inline",     "int",       "long",           "register",
    "restrict",   "return",    "short",          "signed",
    "sizeof",     "static",    "struct",         "switch",
    "typedef",    "union",     "unsigned",       "void",
    "volatile",   "while",     "_Alignas",       "_Alignof",
    "_Atomic",    "_Bool",     "_Complex",       "_Generic",
    "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local",
};

static const std::set<std::string> CPP_KEYWORDS = {
    "alignas",      "alignof",
    "and",          "and_eq",
    "asm",          "bitand",
    "bitor",        "bool",
    "catch",        "char8_t",
    "char16_t",     "char32_t",
    "class",        "compl",
    "concept",      "consteval",
    "constexpr",    "constinit",
    "const_cast",   "co_await",
    "co_return",    "co_yield",
    "decltype",     "delete",
    "dynamic_cast", "explicit",
    "export",       "false",
    "friend",       "mutable",
    "namespace",    "new",
    "noexcept",     "not",
    "not_eq",       "nullptr",
    "operator",     "or",
    "or_eq",        "private",
    "protected",    "public",
    "reflexpr",     "reinterpret_cast",
    "requires",     "static_assert",
    "static_cast",  "synchronized",
    "template",     "this",
    "thread_local", "throw",
    "true",         "try",
    "typeid",       "typename",
    "using",        "virtual",
    "wchar_t",      "xor",
    "xor_eq",
};

static const std::set<std::string> PY_KEYWORDS = {
    "False",  "None",   "True",    "and",      "as",       "assert", "async",
    "await",  "break",  "class",   "continue", "def",      "del",    "elif",
    "else",   "except", "finally", "for",      "from",     "global", "if",
    "import", "in",     "is",      "lambda",   "nonlocal", "not",    "or",
    "pass",   "raise",  "return",  "try",      "while",    "with",   "yield",
};

static bool is_lower_or_digit(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return std::islower(uc) || std::isdigit(uc);
}

static std::string snake(const std::string &value) {
    // re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", value)
    std::string camel;
    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (i > 0 && is_lower_or_digit(value[i - 1]) &&
            std::isupper(static_cast<unsigned char>(c)))
            camel += '_';
        camel += c;
    }
    // re.sub(r"[^A-Za-z0-9]+", "_", value).strip("_").lower()
    std::string out;
    bool pending = false;
    for (char c : camel) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            if (pending) {
                out += '_';
                pending = false;
            }
            out +=
                static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            pending = true;
        }
    }
    if (out.empty()) out = "value";
    if (std::isdigit(static_cast<unsigned char>(out[0])) ||
        C_KEYWORDS.count(out) || PY_KEYWORDS.count(out))
        out += '_';
    return out;
}

static std::string member_name(const std::string &value) {
    std::string s = snake(value);
    if (CPP_KEYWORDS.count(s)) s += '_';
    return s;
}

static std::string type_name(const std::string &nsid,
                             const std::string &suffix) {
    return "wf_lex_" + snake(nsid) +
           (suffix.empty() ? "" : "_" + snake(suffix));
}

static std::string ref_type(const std::string &nsid, const std::string &ref) {
    if (!ref.empty() && ref[0] == '#') return type_name(nsid, ref.substr(1));
    size_t hash = ref.find('#');
    if (hash != std::string::npos)
        return type_name(ref.substr(0, hash), ref.substr(hash + 1));
    return type_name(ref, "main");
}

static std::vector<std::string> comment(const std::string &text,
                                        const std::string &indent = "") {
    std::string clean;
    {
        std::string with_slash = text;
        // replace "*/" with "* /"
        size_t pos = 0;
        while ((pos = with_slash.find("*/", pos)) != std::string::npos) {
            with_slash.replace(pos, 2, "* /");
            pos += 3;
        }
        std::istringstream stream(with_slash);
        std::string word;
        std::vector<std::string> words;
        while (stream >> word) words.push_back(word);
        for (size_t i = 0; i < words.size(); ++i) {
            if (i) clean += ' ';
            clean += words[i];
        }
    }
    if (clean.empty()) return {};
    return {indent + "/** " + clean + " */"};
}

// ---------------------------------------------------------------------------
// cJSON helpers.
// ---------------------------------------------------------------------------

static std::string schema_type(cJSON *schema) {
    if (!schema) return "";
    cJSON *t = cJSON_GetObjectItemCaseSensitive(schema, "type");
    if (t && cJSON_IsString(t) && t->valuestring) return t->valuestring;
    return "";
}

static bool is_required(cJSON *schema, const std::string &wire) {
    cJSON *req = cJSON_GetObjectItemCaseSensitive(schema, "required");
    if (!req || !cJSON_IsArray(req)) return false;
    int n = cJSON_GetArraySize(req);
    for (int i = 0; i < n; ++i) {
        cJSON *item = cJSON_GetArrayItem(req, i);
        if (cJSON_IsString(item) && item->valuestring &&
            wire == item->valuestring)
            return true;
    }
    return false;
}

static std::set<std::string> required_set(cJSON *schema) {
    std::set<std::string> out;
    cJSON *req = cJSON_GetObjectItemCaseSensitive(schema, "required");
    if (req && cJSON_IsArray(req)) {
        int n = cJSON_GetArraySize(req);
        for (int i = 0; i < n; ++i) {
            cJSON *item = cJSON_GetArrayItem(req, i);
            if (cJSON_IsString(item) && item->valuestring)
                out.insert(item->valuestring);
        }
    }
    return out;
}

// A document is a single parsed Lexicon file. `raw` is owned by the Doc and
// freed in the destructor; `defs` borrows from `raw`.
struct Doc {
    std::string id;
    cJSON *raw;
    cJSON *defs;
    Doc() : raw(nullptr), defs(nullptr) {}
    ~Doc() {
        if (raw) cJSON_Delete(raw);
    }
    Doc(const Doc &) = delete;
    Doc &operator=(const Doc &) = delete;
    Doc(Doc &&other) noexcept
        : id(std::move(other.id)), raw(other.raw), defs(other.defs) {
        other.raw = nullptr;
        other.defs = nullptr;
    }
    Doc &operator=(Doc &&other) noexcept {
        if (this != &other) {
            if (raw) cJSON_Delete(raw);
            id = std::move(other.id);
            raw = other.raw;
            defs = other.defs;
            other.raw = nullptr;
            other.defs = nullptr;
        }
        return *this;
    }
};

class Generator {
  public:
    std::vector<Doc> docs; // sorted by id
    std::string guard;

    Generator(std::vector<Doc> parsed, std::string g)
        : docs(std::move(parsed)), guard(std::move(g)) {
        std::sort(docs.begin(), docs.end(),
                  [](const Doc &a, const Doc &b) { return a.id < b.id; });
    }

    // -----------------------------------------------------------------------
    // Schema resolution.
    // -----------------------------------------------------------------------

    // Returns (document_id, definition) for a ref, or nullopt.
    std::optional<std::pair<std::string, cJSON *>>
    resolve_ref(const std::string &nsid, const std::string &ref) const {
        std::string document_id, fragment;
        if (!ref.empty() && ref[0] == '#') {
            document_id = nsid;
            fragment = ref.substr(1);
        } else {
            size_t hash = ref.find('#');
            if (hash != std::string::npos) {
                document_id = ref.substr(0, hash);
                fragment = ref.substr(hash + 1);
            } else {
                document_id = ref;
                fragment = "main";
            }
        }
        for (const auto &doc : docs) {
            if (doc.id != document_id) continue;
            cJSON *definition =
                cJSON_GetObjectItemCaseSensitive(doc.defs, fragment.c_str());
            if (!definition) return std::nullopt;
            if (schema_type(definition) == "record") {
                cJSON *record =
                    cJSON_GetObjectItemCaseSensitive(definition, "record");
                if (record) definition = record;
            }
            return std::make_pair(document_id, definition);
        }
        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Naming.
    // -----------------------------------------------------------------------

    static std::string field_name(cJSON *schema, const std::string &wire) {
        std::string base = member_name(wire);
        std::set<std::string> presence;
        cJSON *props = cJSON_GetObjectItemCaseSensitive(schema, "properties");
        if (props) {
            for (cJSON *p = props->child; p; p = p->next) {
                if (!p->string) continue;
                std::string name = p->string;
                if (!is_required(schema, name))
                    presence.insert("has_" + member_name(name));
            }
        }
        return presence.count(base) ? base + "_value" : base;
    }

    std::pair<std::string, bool> c_type(const std::string &nsid,
                                        const std::string &owner,
                                        const std::string &field,
                                        cJSON *schema) const {
        std::string kind = schema_type(schema);
        if (kind == "string") return {"const char *", false};
        if (kind == "integer") return {"int64_t", true};
        if (kind == "boolean") return {"bool", true};
        if (kind == "bytes") return {"wf_lex_bytes", false};
        if (kind == "cid-link") return {"wf_lex_cid_link", false};
        if (kind == "blob") return {"wf_lex_blob", false};
        if (kind == "ref") {
            cJSON *ref = cJSON_GetObjectItemCaseSensitive(schema, "ref");
            if (ref && cJSON_IsString(ref) && ref->valuestring) {
                auto resolved = resolve_ref(nsid, ref->valuestring);
                if (resolved && schema_type(resolved->second) != "object") {
                    if (schema_type(resolved->second) == "token")
                        return {"const char *", false};
                    return c_type(resolved->first, owner, field,
                                  resolved->second);
                }
                // Borrowed pointers support recursive and cross-document refs
                // without imposing declaration order on generated headers.
                return {"const " + ref_type(nsid, ref->valuestring) + " *",
                        false};
            }
        }
        if (kind == "object") return {owner + "_" + snake(field), false};
        if (kind == "union") return {union_name(owner, field), false};
        if (kind == "unknown") return {"wf_lex_json", false};
        if (kind == "array") {
            cJSON *items = cJSON_GetObjectItemCaseSensitive(schema, "items");
            auto item =
                c_type(nsid, owner, field + "_item",
                       items ? items : cJSON_CreateObject()); // temp fallback
            return {"WF_LEX_ARRAY(" + item.first + ")", false};
        }
        return {"wf_lex_json", false};
    }

    std::string union_name(const std::string &owner,
                           const std::string &field) const {
        return owner + "_" + snake(field) + "_union";
    }

    // -----------------------------------------------------------------------
    // Object / union catalogs.
    // -----------------------------------------------------------------------

    // schemas(doc): named and inline schema roots for a document, in def order.
    std::vector<std::pair<std::string, cJSON *>> schemas(const Doc &doc) const {
        std::vector<std::pair<std::string, cJSON *>> found;
        for (cJSON *def = doc.defs->child; def; def = def->next) {
            if (!def->string) continue;
            std::string def_name = def->string;
            std::string kind = schema_type(def);
            std::string base = type_name(doc.id, def_name);
            if (kind == "object") {
                found.emplace_back(base, def);
            } else if (kind == "record") {
                cJSON *record = cJSON_GetObjectItemCaseSensitive(def, "record");
                if (record && schema_type(record) == "object")
                    found.emplace_back(base, record);
            } else if (kind == "query" || kind == "procedure") {
                cJSON *params =
                    cJSON_GetObjectItemCaseSensitive(def, "parameters");
                if (params && schema_type(params) == "params")
                    found.emplace_back(base + "_params", params);
                for (const char *label : {"input", "output"}) {
                    cJSON *value = cJSON_GetObjectItemCaseSensitive(def, label);
                    cJSON *schema =
                        value
                            ? cJSON_GetObjectItemCaseSensitive(value, "schema")
                            : nullptr;
                    if (schema && schema_type(schema) == "object")
                        found.emplace_back(base + "_" + label, schema);
                }
            }
        }
        return found;
    }

    // Ordered catalog of named and inline objects. `order` preserves the
    // dependency-safe insertion order the Python generator produced.
    std::map<std::string, std::pair<std::string, cJSON *>> &
    object_catalog() const {
        if (!_objects_ready) {
            build_object_catalog();
            _objects_ready = true;
        }
        return _objects;
    }

    std::vector<std::string> &object_order() const {
        if (!_objects_ready) {
            build_object_catalog();
            _objects_ready = true;
        }
        return _object_order;
    }

    std::map<std::string, std::pair<std::string, cJSON *>> &
    collect_unions() const {
        if (_unions_ready) return _unions;
        _unions.clear();
        object_catalog();

        // Local recursive walk, mirroring the Python closure.
        std::function<void(const std::string &, const std::string &, cJSON *,
                           const std::string &)>
            walk;
        walk = [&](const std::string &nsid, const std::string &owner,
                   cJSON *schema, const std::string &path) {
            std::string kind = schema_type(schema);
            if (kind == "union") {
                _unions[union_name(owner, path)] = std::make_pair(nsid, schema);
            } else if (kind == "array") {
                cJSON *items =
                    cJSON_GetObjectItemCaseSensitive(schema, "items");
                walk(nsid, owner, items ? items : cJSON_CreateObject(),
                     path + "_item");
            } else if (kind == "object") {
                cJSON *props =
                    cJSON_GetObjectItemCaseSensitive(schema, "properties");
                if (props) {
                    for (cJSON *p = props->child; p; p = p->next) {
                        if (!p->string) continue;
                        walk(nsid, owner, p, field_name(schema, p->string));
                    }
                }
            } else if (kind == "ref") {
                cJSON *ref = cJSON_GetObjectItemCaseSensitive(schema, "ref");
                if (ref && cJSON_IsString(ref) && ref->valuestring) {
                    auto resolved = resolve_ref(nsid, ref->valuestring);
                    if (!resolved) return;
                    cJSON *target = resolved->second;
                    if (schema_type(target) == "object")
                        return; // Borrowed pointer; walked via catalog.
                    cJSON walked_schema;
                    memset(&walked_schema, 0, sizeof(walked_schema));
                    if (schema_type(target) == "token") {
                        cJSON_DeleteItemFromObject(&walked_schema, "type");
                    }
                    // Mirror `walked = {"type": "string"} if token else
                    // target`.
                    walk(resolved->first, owner, target, path);
                }
            }
        };

        for (const auto &name : _object_order) {
            auto it = _objects.find(name);
            if (it != _objects.end())
                walk(it->second.first, name, it->second.second, "");
        }
        _unions_ready = true;
        return _unions;
    }

    // union_members: (index, full_$type, c_type, member_name) for resolvable
    // object members.
    std::vector<std::tuple<int, std::string, std::string, std::string>>
    union_members(const std::string &nsid, cJSON *schema) const {
        std::vector<std::tuple<int, std::string, std::string, std::string>>
            members;
        std::set<std::string> seen;
        cJSON *refs = cJSON_GetObjectItemCaseSensitive(schema, "refs");
        if (!refs || !cJSON_IsArray(refs)) return members;
        int n = cJSON_GetArraySize(refs);
        for (int i = 0; i < n; ++i) {
            cJSON *ref_item = cJSON_GetArrayItem(refs, i);
            if (!cJSON_IsString(ref_item) || !ref_item->valuestring) continue;
            std::string ref = ref_item->valuestring;
            auto resolved = resolve_ref(nsid, ref);
            if (!resolved || schema_type(resolved->second) != "object")
                continue;
            std::string full = ref[0] == '#' ? nsid + ref : ref;
            std::string frag;
            size_t hash = ref.find('#');
            if (hash != std::string::npos)
                frag = ref.substr(hash + 1);
            else
                frag = ref.substr(ref.rfind('.') + 1);
            std::string mname = member_name(frag);
            if (seen.count(mname)) mname += "_" + std::to_string(i);
            seen.insert(mname);
            members.emplace_back(i, full, ref_type(nsid, ref), mname);
        }
        return members;
    }

    std::set<std::string> referenced_types() const {
        std::set<std::string> found;
        std::function<void(const std::string &, cJSON *)> visit;
        visit = [&](const std::string &nsid, cJSON *value) {
            if (!value) return;
            if (value->type == cJSON_Object) {
                if (value->type == cJSON_Object) {
                    cJSON *type_item =
                        cJSON_GetObjectItemCaseSensitive(value, "type");
                    cJSON *ref_item =
                        cJSON_GetObjectItemCaseSensitive(value, "ref");
                    if (type_item && cJSON_IsString(type_item) &&
                        strcmp(type_item->valuestring, "ref") == 0 &&
                        ref_item && cJSON_IsString(ref_item) &&
                        ref_item->valuestring) {
                        std::string ref = ref_item->valuestring;
                        auto resolved = resolve_ref(nsid, ref);
                        if (!resolved ||
                            schema_type(resolved->second) == "object")
                            found.insert(ref_type(nsid, ref));
                    }
                    for (cJSON *child = value->child; child;
                         child = child->next)
                        visit(nsid, child);
                }
            } else if (value->type == cJSON_Array) {
                for (cJSON *child = value->child; child; child = child->next)
                    visit(nsid, child);
            }
        };
        for (const auto &doc : docs) visit(doc.id, doc.defs);
        return found;
    }

    // -----------------------------------------------------------------------
    // Input type helpers.
    // -----------------------------------------------------------------------

    static std::optional<std::string> json_input_type(const std::string &nsid,
                                                      const std::string &base,
                                                      cJSON *schema) {
        (void)nsid;
        std::string kind = schema_type(schema);
        static const std::set<std::string> kinds = {
            "array",  "blob", "boolean", "bytes", "cid-link", "integer",
            "object", "ref",  "string",  "union", "unknown"};
        if (kinds.count(kind)) return base + "_input";
        return std::nullopt;
    }

    std::string json_input_alias_type(const std::string &nsid,
                                      const std::string &base,
                                      cJSON *schema) const {
        if (schema_type(schema) == "ref") {
            cJSON *ref = cJSON_GetObjectItemCaseSensitive(schema, "ref");
            if (ref && cJSON_IsString(ref) && ref->valuestring) {
                auto resolved = resolve_ref(nsid, ref->valuestring);
                if (resolved && schema_type(resolved->second) == "object")
                    return ref_type(nsid, ref->valuestring);
            }
        }
        return c_type(nsid, base, "input", schema).first;
    }

    // -----------------------------------------------------------------------
    // Output type helpers.
    // -----------------------------------------------------------------------

    // If a query/procedure's output schema is either an inline object or a
    // `ref` resolving to one, returns the C type name to use for
    // `<base>_output` — `base + "_output"` itself for an inline object (already
    // emitted via the object catalog), or the referenced object's own type for
    // a ref (aliased rather than duplicated, mirroring
    // `json_input_alias_type`). Otherwise `nullopt`: no decoder is generated
    // (no output body, a non-JSON encoding such as a CAR/blob stream, or a ref
    // to something other than an object).
    std::optional<std::string> output_object_type(const std::string &nsid,
                                                  const std::string &base,
                                                  cJSON *output_schema) const {
        if (!output_schema) return std::nullopt;
        if (schema_type(output_schema) == "object") return base + "_output";
        if (schema_type(output_schema) == "ref") {
            cJSON *ref = cJSON_GetObjectItemCaseSensitive(output_schema, "ref");
            if (ref && cJSON_IsString(ref) && ref->valuestring) {
                auto resolved = resolve_ref(nsid, ref->valuestring);
                if (resolved && schema_type(resolved->second) == "object")
                    return ref_type(nsid, ref->valuestring);
            }
        }
        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Header emission.
    // -----------------------------------------------------------------------

    std::vector<std::string> emit_union_struct(const std::string &name,
                                               const std::string &nsid,
                                               cJSON *schema) const {
        std::vector<std::string> lines = {
            "typedef struct " + name + " {",
            "    int kind;",
            "    /* Retained raw JSON (mirrors wf_lex_json) for re-encoding",
            "     * and open-union/unknown $type fallback. */",
            "    const char *data;",
            "    size_t length;",
            "    union {"};
        auto members = union_members(nsid, schema);
        if (!members.empty()) {
            for (const auto &m : members)
                lines.push_back("        const " + std::get<2>(m) + " *" +
                                std::get<3>(m) + ";");
        } else {
            lines.push_back("        unsigned char _unused;");
        }
        lines.push_back("    } value;");
        lines.push_back("} " + name + ";");
        lines.push_back("");
        return lines;
    }

    std::vector<std::string> emit_union_decoder(const std::string &name,
                                                const std::string &nsid,
                                                cJSON *schema) const {
        std::vector<std::string> lines = {
            "static wf_status wf_lex_decode_" + name + "(cJSON *node, " + name +
                " *value) {",
            "    if (!node || !value) return WF_ERR_INVALID_ARG;",
            "    memset(value, 0, sizeof(*value));",
            "    value->kind = -1;",
            "    char *raw = cJSON_PrintUnformatted(node);",
            "    if (!raw) return WF_ERR_ALLOC;",
            "    value->data = raw; value->length = strlen(raw);",
            "    cJSON *type = cJSON_GetObjectItemCaseSensitive(node, "
            "\"$type\");",
            "    const char *t = (type && cJSON_IsString(type)) ? "
            "type->valuestring : NULL;",
            "    if (t) {"};
        for (const auto &m : union_members(nsid, schema)) {
            int idx = std::get<0>(m);
            const std::string &full = std::get<1>(m);
            const std::string &ctype = std::get<2>(m);
            const std::string &mname = std::get<3>(m);
            lines.push_back("        if (strcmp(t, \"" + full + "\") == 0) {");
            lines.push_back("            value->kind = " + std::to_string(idx) +
                            ";");
            lines.push_back("            " + ctype +
                            " *m = calloc(1, sizeof(*m));");
            lines.push_back("            if (!m) { wf_lex_clear_" + name +
                            "(value); return WF_ERR_ALLOC; }");
            lines.push_back("            wf_status status = wf_lex_decode_" +
                            ctype + "(node, m);");
            lines.push_back("            if (status != WF_OK) {");
            lines.push_back("                free(m); value->kind = -1;");
            lines.push_back("            } else {");
            lines.push_back("                value->value." + mname + " = m;");
            lines.push_back("            }");
            lines.push_back("        }");
        }
        lines.push_back("    }");
        lines.push_back("    return WF_OK;");
        lines.push_back("}");
        lines.push_back("");
        return lines;
    }

    std::vector<std::string> emit_union_clear(const std::string &name,
                                              const std::string &nsid,
                                              cJSON *schema) const {
        std::vector<std::string> lines = {
            "static void wf_lex_clear_" + name + "(" + name + " *value) {",
            "    if (!value) return;", "    free((void *)value->data);",
            "    switch (value->kind) {"};
        for (const auto &m : union_members(nsid, schema)) {
            int idx = std::get<0>(m);
            const std::string &ctype = std::get<2>(m);
            const std::string &mname = std::get<3>(m);
            lines.push_back("    case " + std::to_string(idx) + ":");
            lines.push_back("        if (value->value." + mname + ") {");
            lines.push_back("            wf_lex_clear_" + ctype + "((" + ctype +
                            " *)value->value." + mname + ");");
            lines.push_back("            free((void *)value->value." + mname +
                            ");");
            lines.push_back("        }");
            lines.push_back("        break;");
        }
        lines.push_back("    default: break;");
        lines.push_back("    }");
        lines.push_back("    memset(value, 0, sizeof(*value));");
        lines.push_back("}");
        lines.push_back("");
        return lines;
    }

    std::vector<std::string> emit_object(const std::string &nsid,
                                         const std::string &name,
                                         cJSON *schema) const {
        std::vector<std::string> lines;
        cJSON *desc = cJSON_GetObjectItemCaseSensitive(schema, "description");
        if (desc && cJSON_IsString(desc) && desc->valuestring) {
            auto c = comment(desc->valuestring);
            lines.insert(lines.end(), c.begin(), c.end());
        }
        lines.push_back("typedef struct " + name + " {");
        cJSON *props = cJSON_GetObjectItemCaseSensitive(schema, "properties");
        bool any = props && props->child;
        if (!any) lines.push_back("    unsigned char _unused;");
        if (props) {
            for (cJSON *p = props->child; p; p = p->next) {
                if (!p->string) continue;
                std::string wire = p->string;
                std::string field = field_name(schema, wire);
                auto type = c_type(nsid, name, field, p);
                cJSON *pdesc =
                    cJSON_GetObjectItemCaseSensitive(p, "description");
                if (pdesc && cJSON_IsString(pdesc) && pdesc->valuestring) {
                    auto c = comment(pdesc->valuestring, "    ");
                    lines.insert(lines.end(), c.begin(), c.end());
                }
                if (!is_required(schema, wire))
                    lines.push_back("    bool has_" + field + ";");
                lines.push_back("    " + type.first + " " + field + ";");
            }
        }
        lines.push_back("} " + name + ";");
        return lines;
    }

    // -----------------------------------------------------------------------
    // Source emission: encoders.
    // -----------------------------------------------------------------------

    void add_encoded_value(std::vector<std::string> &lines,
                           const std::string &nsid, const std::string &owner,
                           cJSON *prop, const std::string &target,
                           const std::string &result, const std::string &path,
                           const std::string &indent) const {
        std::string kind = schema_type(prop);
        std::optional<std::string> expr;
        if (kind == "string") {
            lines.push_back(indent + "if (!" + target + ") goto invalid;");
            expr = "cJSON_CreateString(" + target + ")";
        } else if (kind == "integer") {
            expr = "cJSON_CreateNumber((double)" + target + ")";
        } else if (kind == "boolean") {
            expr = "cJSON_CreateBool(" + target + ")";
        } else if (kind == "union" || kind == "unknown") {
            lines.push_back(indent + "if (!" + target + ".data) goto invalid;");
            lines.push_back(indent + result + " = cJSON_ParseWithLength(" +
                            target + ".data, " + target + ".length);");
            lines.push_back(indent + "if (!" + result + ") goto invalid;");
        } else if (kind == "bytes") {
            lines.push_back(indent + "status = wf_lex_bytes_encode(&" + target +
                            ", &" + result + ");");
            lines.push_back(indent + "if (status != WF_OK) goto status_fail;");
        } else if (kind == "cid-link") {
            lines.push_back(indent + "status = wf_lex_cid_encode(&" + target +
                            ", &" + result + ");");
            lines.push_back(indent + "if (status != WF_OK) goto status_fail;");
        } else if (kind == "blob") {
            lines.push_back(indent + "status = wf_lex_blob_encode(&" + target +
                            ", &" + result + ");");
            lines.push_back(indent + "if (status != WF_OK) goto status_fail;");
        } else if (kind == "object") {
            std::string inline_name = owner + "_" + snake(path);
            lines.push_back(indent + "status = wf_lex_encode_" + inline_name +
                            "(&" + target + ", &" + result + ");");
            lines.push_back(indent + "if (status != WF_OK) goto status_fail;");
        } else if (kind == "ref") {
            cJSON *ref = cJSON_GetObjectItemCaseSensitive(prop, "ref");
            if (!ref || !cJSON_IsString(ref) || !ref->valuestring)
                throw std::runtime_error("cannot encode unresolved ref in " +
                                         owner);
            std::string ref_s = ref->valuestring;
            auto resolved = resolve_ref(nsid, ref_s);
            if (!resolved)
                throw std::runtime_error("cannot encode unresolved ref " +
                                         ref_s + " in " + owner);
            cJSON *target_schema = resolved->second;
            if (schema_type(target_schema) == "object") {
                std::string referenced = ref_type(nsid, ref_s);
                lines.push_back(indent + "if (!" + target + ") goto invalid;");
                lines.push_back(indent + "status = wf_lex_encode_" +
                                referenced + "(" + target + ", &" + result +
                                ");");
                lines.push_back(indent +
                                "if (status != WF_OK) goto status_fail;");
            } else {
                if (schema_type(target_schema) == "token") {
                    cJSON temp;
                    memset(&temp, 0, sizeof(temp));
                    temp.type = cJSON_String;
                    cJSON_AddItemToObject(&temp, "type",
                                          cJSON_CreateString("string"));
                    add_encoded_value(lines, resolved->first, owner, &temp,
                                      target, result, path, indent);
                    cJSON_Delete(temp.child);
                } else {
                    add_encoded_value(lines, resolved->first, owner,
                                      target_schema, target, result, path,
                                      indent);
                }
            }
        } else if (kind == "array") {
            cJSON *items = cJSON_GetObjectItemCaseSensitive(prop, "items");
            cJSON *item_schema = items ? items : cJSON_CreateObject();
            lines.push_back(indent + "if (" + target + ".count && !" + target +
                            ".items) goto invalid;");
            lines.push_back(indent + result + " = cJSON_CreateArray();");
            lines.push_back(indent + "if (!" + result + ") goto fail;");
            std::string suffix = snake(path);
            std::string index = "i_" + suffix;
            std::string element = "element_" + suffix;
            lines.push_back(indent + "for (size_t " + index + " = 0; " + index +
                            " < " + target + ".count; ++" + index + ") {");
            lines.push_back(indent + "    cJSON *" + element + " = NULL;");
            add_encoded_value(lines, nsid, owner, item_schema,
                              target + ".items[" + index + "]", element,
                              path + "_item", indent + "    ");
            lines.push_back(indent + "    if (!cJSON_AddItemToArray(" + result +
                            ", " + element + ")) {");
            lines.push_back(indent + "        cJSON_Delete(" + element +
                            "); goto fail;");
            lines.push_back(indent + "    }");
            lines.push_back(indent + "}");
        } else {
            throw std::runtime_error("JSON encoding is not supported for " +
                                     path + " (" + kind + ")");
        }
        if (expr) {
            lines.push_back(indent + result + " = " + *expr + ";");
            lines.push_back(indent + "if (!" + result + ") goto fail;");
        }
    }

    void add_value(std::vector<std::string> &lines, const std::string &nsid,
                   const std::string &owner, cJSON *prop,
                   const std::string &field, const std::string &wire_name,
                   const std::string &indent = "    ") const {
        lines.push_back(indent + "item = NULL;");
        add_encoded_value(lines, nsid, owner, prop, "value->" + field, "item",
                          field, indent);
        lines.push_back(
            indent + "if (!item || !cJSON_AddItemToObject(root, \"" +
            wire_name +
            "\", item)) { cJSON_Delete(item); item = NULL; goto fail; }");
        lines.push_back(indent + "item = NULL;");
    }

    std::vector<std::string> emit_object_encoder(const std::string &nsid,
                                                 const std::string &name,
                                                 cJSON *schema) const {
        std::vector<std::string> lines = {
            "static wf_status wf_lex_encode_" + name + "(const " + name +
                " *value, cJSON **out) {",
            "    if (!value || !out) return WF_ERR_INVALID_ARG;",
            "    *out = NULL;",
            "    wf_status status = WF_OK;",
            "    (void)status;",
            "    cJSON *root = cJSON_CreateObject();",
            "    cJSON *item = NULL;",
            "    (void)item;",
            "    if (!root) return WF_ERR_ALLOC;"};
        cJSON *props = cJSON_GetObjectItemCaseSensitive(schema, "properties");
        if (props) {
            for (cJSON *p = props->child; p; p = p->next) {
                if (!p->string) continue;
                std::string wire = p->string;
                std::string field = field_name(schema, wire);
                if (!is_required(schema, wire)) {
                    lines.push_back("    if (value->has_" + field + ") {");
                    add_value(lines, nsid, name, p, field, wire, "        ");
                    lines.push_back("    }");
                } else {
                    add_value(lines, nsid, name, p, field, wire);
                }
            }
        }
        lines.push_back("    *out = root; return WF_OK;");
        bool has_invalid = false, has_fail = false, has_status = false;
        for (const auto &l : lines) {
            if (l.find("goto invalid;") != std::string::npos)
                has_invalid = true;
            if (l.find("goto fail;") != std::string::npos) has_fail = true;
            if (l.find("goto status_fail;") != std::string::npos)
                has_status = true;
        }
        if (has_invalid)
            lines.insert(lines.end(),
                         {"invalid:",
                          "    cJSON_Delete(item); cJSON_Delete(root); "
                          "return WF_ERR_INVALID_ARG;"});
        if (has_fail)
            lines.insert(lines.end(),
                         {"fail:", "    cJSON_Delete(item); "
                                   "cJSON_Delete(root); return WF_ERR_ALLOC;"});
        if (has_status)
            lines.insert(
                lines.end(),
                {"status_fail:",
                 "    cJSON_Delete(item); cJSON_Delete(root); return status;"});
        lines.push_back("}");
        lines.push_back("");
        return lines;
    }

    // -----------------------------------------------------------------------
    // Source emission: decoders.
    // -----------------------------------------------------------------------

    void emit_decode_value(std::vector<std::string> &lines,
                           const std::string &nsid, const std::string &owner,
                           cJSON *schema, const std::string &target,
                           const std::string &source, const std::string &indent,
                           const std::string &field = "") const {
        std::string kind = schema_type(schema);
        if (kind == "string") {
            lines.push_back(
                indent + "if (!cJSON_IsString(" + source +
                ")) { status = WF_ERR_INVALID_ARG; goto cleanup; }");
            lines.push_back(indent + target + " = wf_lex_strdup(" + source +
                            "->valuestring);");
            lines.push_back(indent + "if (!" + target +
                            ") { status = WF_ERR_ALLOC; goto cleanup; }");
        } else if (kind == "integer") {
            lines.push_back(
                indent + "if (!wf_lex_json_integer(" + source + ", &" + target +
                ")) { status = WF_ERR_INVALID_ARG; goto cleanup; }");
        } else if (kind == "boolean") {
            lines.push_back(
                indent + "if (!cJSON_IsBool(" + source +
                ")) { status = WF_ERR_INVALID_ARG; goto cleanup; }");
            lines.push_back(indent + target + " = cJSON_IsTrue(" + source +
                            ");");
        } else if (kind == "union") {
            std::string name = union_name(owner, field);
            lines.push_back(indent + "status = wf_lex_decode_" + name + "(" +
                            source + ", &(" + target + "));");
            lines.push_back(indent + "if (status != WF_OK) goto cleanup;");
        } else if (kind == "unknown") {
            lines.push_back(indent + "status = wf_lex_json_copy(" + source +
                            ", &" + target + ");");
            lines.push_back(indent + "if (status != WF_OK) goto cleanup;");
        } else if (kind == "bytes") {
            lines.push_back(indent + "status = wf_lex_bytes_decode(" + source +
                            ", &" + target + ");");
            lines.push_back(indent + "if (status != WF_OK) goto cleanup;");
        } else if (kind == "cid-link") {
            lines.push_back(indent + "status = wf_lex_cid_decode(" + source +
                            ", &" + target + ");");
            lines.push_back(indent + "if (status != WF_OK) goto cleanup;");
        } else if (kind == "blob") {
            lines.push_back(indent + "status = wf_lex_blob_decode(" + source +
                            ", &" + target + ");");
            lines.push_back(indent + "if (status != WF_OK) goto cleanup;");
        } else if (kind == "object") {
            std::string inline_name =
                owner + "_" + snake(target_suffix_name(target));
            if (target_ends_with(target, "items[i]"))
                inline_name = owner + "_array_item";
            lines.push_back(indent + "status = wf_lex_decode_" + inline_name +
                            "(" + source + ", &" + target + ");");
            lines.push_back(indent + "if (status != WF_OK) goto cleanup;");
        } else if (kind == "ref") {
            cJSON *ref = cJSON_GetObjectItemCaseSensitive(schema, "ref");
            if (!ref || !cJSON_IsString(ref) || !ref->valuestring)
                throw std::runtime_error("cannot decode unresolved ref in " +
                                         owner);
            std::string ref_s = ref->valuestring;
            std::string refn = ref_type(nsid, ref_s);
            auto resolved = resolve_ref(nsid, ref_s);
            if (resolved && schema_type(resolved->second) != "object") {
                cJSON *target_schema = resolved->second;
                if (schema_type(target_schema) == "token") {
                    cJSON temp;
                    memset(&temp, 0, sizeof(temp));
                    cJSON_AddItemToObject(&temp, "type",
                                          cJSON_CreateString("string"));
                    emit_decode_value(lines, resolved->first, owner, &temp,
                                      target, source, indent, field);
                    cJSON_Delete(temp.child);
                } else {
                    emit_decode_value(lines, resolved->first, owner,
                                      target_schema, target, source, indent,
                                      field);
                }
            } else {
                if (!object_catalog().count(refn))
                    throw std::runtime_error("cannot decode unresolved ref " +
                                             ref_s + " in " + owner);
                lines.push_back(indent + target + " = calloc(1, sizeof(*" +
                                target + "));");
                lines.push_back(indent + "if (!" + target +
                                ") { status = WF_ERR_ALLOC; goto cleanup; }");
                lines.push_back(indent + "status = wf_lex_decode_" + refn +
                                "(" + source + ", (" + refn + " *)" + target +
                                ");");
                lines.push_back(indent + "if (status != WF_OK) goto cleanup;");
            }
        } else if (kind == "array") {
            cJSON *items = cJSON_GetObjectItemCaseSensitive(schema, "items");
            cJSON *item_schema = items ? items : cJSON_CreateObject();
            std::string array_field = target_field(target);
            std::string item_field = array_field + "_item";
            auto item = c_type(nsid, owner, item_field, item_schema);
            std::string item_type = item.first;
            lines.push_back(
                indent + "if (!cJSON_IsArray(" + source +
                ")) { status = WF_ERR_INVALID_ARG; goto cleanup; }");
            lines.push_back(indent + target +
                            ".count = (size_t)cJSON_GetArraySize(" + source +
                            ");");
            lines.push_back(indent + "if (" + target + ".count) {");
            lines.push_back(indent + "    " + item_type + " *items = calloc(" +
                            target + ".count, sizeof(*items));");
            lines.push_back(
                indent +
                "    if (!items) { status = WF_ERR_ALLOC; goto cleanup; }");
            lines.push_back(indent + "    " + target + ".items = items;");
            lines.push_back(indent + "    for (size_t i = 0; i < " + target +
                            ".count; ++i) {");
            lines.push_back(indent +
                            "        cJSON *element = cJSON_GetArrayItem(" +
                            source + ", (int)i);");
            if (schema_type(item_schema) == "object") {
                lines.push_back(indent + "        status = wf_lex_decode_" +
                                item_type + "(element, &items[i]);");
                lines.push_back(indent +
                                "        if (status != WF_OK) goto cleanup;");
            } else {
                emit_decode_value(lines, nsid, owner, item_schema, "items[i]",
                                  "element", indent + "        ", item_field);
            }
            lines.push_back(indent + "    }");
            lines.push_back(indent + "}");
        } else {
            throw std::runtime_error("JSON decoding is not supported for " +
                                     owner + " (" + kind + ")");
        }
    }

    void emit_clear_value(std::vector<std::string> &lines,
                          const std::string &nsid, const std::string &owner,
                          cJSON *schema, const std::string &target,
                          const std::string &indent,
                          const std::string &field = "") const {
        std::string kind = schema_type(schema);
        if (kind == "string") {
            lines.push_back(indent + "free((void *)" + target + ");");
        } else if (kind == "union") {
            std::string union_type = union_name(owner, field);
            if (target_ends_with(target, "items[i]"))
                lines.push_back(indent + "wf_lex_clear_" + union_type + "((" +
                                union_type + " *)&(" + target + "));");
            else
                lines.push_back(indent + "wf_lex_clear_" + union_type + "(&(" +
                                target + "));");
        } else if (kind == "unknown" || kind == "bytes") {
            lines.push_back(indent + "free((void *)" + target + ".data);");
        } else if (kind == "cid-link") {
            lines.push_back(indent + "free((void *)" + target + ".cid);");
        } else if (kind == "blob") {
            lines.push_back(indent + "free((void *)" + target + ".cid);");
            lines.push_back(indent + "free((void *)" + target + ".mime_type);");
        } else if (kind == "object") {
            std::string field_name2 = target_field(target);
            std::string inline_name = owner + "_" + snake(field_name2);
            if (target_ends_with(target, "items[i]"))
                inline_name = owner + "_array_item";
            lines.push_back(indent + "wf_lex_clear_" + inline_name + "(&" +
                            target + ");");
        } else if (kind == "ref") {
            cJSON *ref = cJSON_GetObjectItemCaseSensitive(schema, "ref");
            if (!ref || !cJSON_IsString(ref) || !ref->valuestring)
                throw std::runtime_error("cannot clear unresolved ref in " +
                                         owner);
            std::string ref_s = ref->valuestring;
            std::string refn = ref_type(nsid, ref_s);
            auto resolved = resolve_ref(nsid, ref_s);
            if (resolved && schema_type(resolved->second) != "object") {
                cJSON *target_schema = resolved->second;
                if (schema_type(target_schema) == "token") {
                    cJSON temp;
                    memset(&temp, 0, sizeof(temp));
                    cJSON_AddItemToObject(&temp, "type",
                                          cJSON_CreateString("string"));
                    emit_clear_value(lines, resolved->first, owner, &temp,
                                     target, indent, field);
                    cJSON_Delete(temp.child);
                } else {
                    emit_clear_value(lines, resolved->first, owner,
                                     target_schema, target, indent, field);
                }
            } else {
                lines.push_back(indent + "if (" + target + ") { wf_lex_clear_" +
                                refn + "((" + refn + " *)" + target +
                                "); free((void *)" + target + "); }");
            }
        } else if (kind == "array") {
            cJSON *items = cJSON_GetObjectItemCaseSensitive(schema, "items");
            cJSON *item = items ? items : cJSON_CreateObject();
            std::string array_field = target_field(target);
            std::string item_field = array_field + "_item";
            auto item_type = c_type(nsid, owner, item_field, item).first;
            lines.push_back(indent + "for (size_t i = 0; i < " + target +
                            ".count; ++i) {");
            if (schema_type(item) == "object") {
                lines.push_back(indent + "    wf_lex_clear_" + item_type +
                                "((" + item_type + " *)&" + target +
                                ".items[i]);");
            } else {
                emit_clear_value(lines, nsid, owner, item, target + ".items[i]",
                                 indent + "    ", item_field);
            }
            lines.push_back(indent + "}");
            lines.push_back(indent + "free((void *)" + target + ".items);");
        }
    }

    std::vector<std::string> emit_object_decoder(const std::string &nsid,
                                                 const std::string &name,
                                                 cJSON *schema) const {
        std::vector<std::string> lines = {"static void wf_lex_clear_" + name +
                                              "(" + name + " *value) {",
                                          "    if (!value) return;"};
        cJSON *props = cJSON_GetObjectItemCaseSensitive(schema, "properties");
        if (props) {
            for (cJSON *p = props->child; p; p = p->next) {
                if (!p->string) continue;
                std::string wire = p->string;
                std::string field = field_name(schema, wire);
                emit_clear_value(lines, nsid, name, p, "value->" + field,
                                 "    ", field);
            }
        }
        lines.push_back("    memset(value, 0, sizeof(*value));");
        lines.push_back("}");
        lines.push_back("");
        lines.push_back("static wf_status wf_lex_decode_" + name +
                        "(cJSON *node, " + name + " *value) {");
        lines.push_back("    wf_status status = WF_OK;");
        lines.push_back("    (void)status;");
        lines.push_back("    if (!cJSON_IsObject(node) || !value) return "
                        "WF_ERR_INVALID_ARG;");
        bool has_cleanup = false;
        if (props) {
            for (cJSON *p = props->child; p; p = p->next) {
                if (!p->string) continue;
                std::string wire = p->string;
                std::string field = field_name(schema, wire);
                lines.push_back("    {");
                lines.push_back("        cJSON *member = "
                                "cJSON_GetObjectItemCaseSensitive(node, \"" +
                                wire + "\");");
                std::string indent;
                if (is_required(schema, wire)) {
                    lines.push_back("        if (!member) { status = "
                                    "WF_ERR_INVALID_ARG; goto cleanup; }");
                    has_cleanup = true;
                    indent = "        ";
                } else {
                    lines.push_back("        if (member) {");
                    lines.push_back("            value->has_" + field +
                                    " = true;");
                    indent = "            ";
                }
                emit_decode_value(lines, nsid, name, p, "value->" + field,
                                  "member", indent, field);
                if (!is_required(schema, wire)) lines.push_back("        }");
                lines.push_back("    }");
            }
        }
        lines.push_back("    return WF_OK;");
        for (const auto &l : lines)
            if (l.find("goto cleanup;") != std::string::npos)
                has_cleanup = true;
        if (has_cleanup) {
            lines.push_back("cleanup:");
            lines.push_back("    wf_lex_clear_" + name + "(value);");
            lines.push_back("    return status;");
        }
        lines.push_back("}");
        lines.push_back("");
        return lines;
    }

    // -----------------------------------------------------------------------
    // Encoder catalog for source generation.
    // -----------------------------------------------------------------------

    // encoder_objects: subset of the object catalog reachable from endpoint
    // inputs, in catalog insertion order.
    std::vector<std::pair<std::string, std::pair<std::string, cJSON *>>>
    encoder_objects() {
        auto &catalog = object_catalog();
        auto &order = object_order();
        std::set<std::string> wanted;

        std::function<void(const std::string &, const std::string &, cJSON *)>
            visit_object;
        std::function<void(const std::string &, const std::string &, cJSON *,
                           const std::string &)>
            visit_schema;

        visit_object = [&](const std::string &nsid, const std::string &name,
                           cJSON *schema) {
            if (wanted.count(name)) return;
            wanted.insert(name);
            cJSON *props =
                cJSON_GetObjectItemCaseSensitive(schema, "properties");
            if (props) {
                for (cJSON *p = props->child; p; p = p->next) {
                    if (!p->string) continue;
                    visit_schema(nsid, name, p, field_name(schema, p->string));
                }
            }
        };

        visit_schema = [&](const std::string &nsid, const std::string &owner,
                           cJSON *schema, const std::string &path) {
            std::string kind = schema_type(schema);
            if (kind == "array") {
                cJSON *items =
                    cJSON_GetObjectItemCaseSensitive(schema, "items");
                visit_schema(nsid, owner, items ? items : cJSON_CreateObject(),
                             path + "_item");
            } else if (kind == "object") {
                std::string child = owner + "_" + snake(path);
                visit_object(nsid, child, catalog.at(child).second);
            } else if (kind == "ref") {
                cJSON *ref = cJSON_GetObjectItemCaseSensitive(schema, "ref");
                if (!ref || !cJSON_IsString(ref) || !ref->valuestring)
                    throw std::runtime_error(
                        "cannot encode unresolved ref in " + owner);
                std::string ref_s = ref->valuestring;
                auto resolved = resolve_ref(nsid, ref_s);
                if (!resolved)
                    throw std::runtime_error("cannot encode unresolved ref " +
                                             ref_s + " in " + owner);
                cJSON *target = resolved->second;
                if (schema_type(target) == "object") {
                    std::string target_name = ref_type(nsid, ref_s);
                    if (!catalog.count(target_name))
                        throw std::runtime_error("cannot encode ref " + ref_s +
                                                 " in " + owner);
                    visit_object(resolved->first, target_name,
                                 catalog.at(target_name).second);
                } else if (schema_type(target) != "token") {
                    visit_schema(resolved->first, owner, target, path);
                }
            }
        };

        for (const auto &doc : docs) {
            for (cJSON *def = doc.defs->child; def; def = def->next) {
                if (!def->string) continue;
                std::string kind = schema_type(def);
                if (kind != "query" && kind != "procedure") continue;
                cJSON *input = cJSON_GetObjectItemCaseSensitive(def, "input");
                cJSON *schema =
                    input ? cJSON_GetObjectItemCaseSensitive(input, "schema")
                          : nullptr;
                if (!schema) schema = cJSON_CreateObject();
                if (schema_type(schema) == "object") {
                    std::string root =
                        type_name(doc.id, def->string) + "_input";
                    visit_object(doc.id, root, schema);
                } else if (schema->type == cJSON_Object &&
                           schema_type(schema) != "") {
                    visit_schema(doc.id, type_name(doc.id, def->string), schema,
                                 "input");
                }
            }
        }

        std::vector<std::pair<std::string, std::pair<std::string, cJSON *>>>
            result;
        for (const auto &name : order)
            if (wanted.count(name)) result.emplace_back(name, catalog.at(name));
        return result;
    }

    // -----------------------------------------------------------------------
    // Top-level generation.
    // -----------------------------------------------------------------------

    std::string generate() {
        std::vector<std::string> out = {
            "/* Generated by tools/wf_lexgen.cpp; do not edit. */",
            "#ifndef " + guard,
            "#define " + guard,
            "",
            "#include <stdbool.h>",
            "#include <stddef.h>",
            "#include <stdint.h>",
            "#include <wolfram/xrpc.h>",
            "#include <wolfram/auth_client.h>",
            "",
            "#ifdef __cplusplus",
            "extern \"C\" {",
            "#endif",
            "",
            "/** Encoded JSON view. Decoded outputs own data; input values "
            "borrow it. */",
            "typedef struct wf_lex_json { const char *data; size_t length; } "
            "wf_lex_json;",
            "/** Byte sequence view. Decoded outputs own data; input values "
            "borrow it. */",
            "typedef struct wf_lex_bytes { const uint8_t *data; size_t length; "
            "} wf_lex_bytes;",
            "/** CID link string view. */",
            "typedef struct wf_lex_cid_link { const char *cid; } "
            "wf_lex_cid_link;",
            "/** Typed AT Protocol blob reference. */",
            "typedef struct wf_lex_blob { const char *cid; const char "
            "*mime_type; int64_t size; } wf_lex_blob;",
            "#define WF_LEX_ARRAY(type_) struct { type_ const *items; size_t "
            "count; }",
            "",
        };

        for (const auto &doc : docs) {
            const std::string &nsid = doc.id;
            cJSON *main = cJSON_GetObjectItemCaseSensitive(doc.defs, "main");
            std::string kind = main ? schema_type(main) : "definition";
            if (kind.empty()) kind = "definition";
            std::string symbol = snake(nsid);
            std::string upper;
            for (char c : symbol)
                upper += static_cast<char>(
                    std::toupper(static_cast<unsigned char>(c)));
            if (main) {
                cJSON *desc =
                    cJSON_GetObjectItemCaseSensitive(main, "description");
                if (desc && cJSON_IsString(desc) && desc->valuestring) {
                    auto c = comment(desc->valuestring);
                    out.insert(out.end(), c.begin(), c.end());
                }
            }
            out.push_back("#define WF_LEX_" + upper + "_NSID \"" + nsid + "\"");
            out.push_back("#define WF_LEX_" + upper + "_KIND \"" + kind + "\"");
            out.push_back("");
        }

        auto &objects = object_catalog();
        auto &unions = collect_unions();
        std::set<std::string> declarations;
        for (const auto &kv : objects) declarations.insert(kv.first);
        auto refs = referenced_types();
        declarations.insert(refs.begin(), refs.end());
        for (const auto &kv : unions) declarations.insert(kv.first);
        for (const auto &name : declarations) {
            out.push_back("typedef struct " + name + " " + name + ";");
        }
        if (!declarations.empty()) out.push_back("");

        for (const auto &kv : unions) {
            auto u =
                emit_union_struct(kv.first, kv.second.first, kv.second.second);
            out.insert(out.end(), u.begin(), u.end());
        }
        for (const auto &name : object_order()) {
            auto it = objects.find(name);
            if (it == objects.end()) continue;
            auto o = emit_object(it->second.first, name, it->second.second);
            out.insert(out.end(), o.begin(), o.end());
            out.push_back("");
        }

        for (const auto &doc : docs) {
            const std::string &nsid = doc.id;
            for (cJSON *def = doc.defs->child; def; def = def->next) {
                if (!def->string) continue;
                std::string kind = schema_type(def);
                if (kind != "query" && kind != "procedure") continue;
                cJSON *input = cJSON_GetObjectItemCaseSensitive(def, "input");
                cJSON *schema =
                    input ? cJSON_GetObjectItemCaseSensitive(input, "schema")
                          : nullptr;
                if (!schema || schema_type(schema) == "object") continue;
                std::string base = type_name(nsid, def->string);
                auto it = json_input_type(nsid, base, schema);
                if (it) {
                    std::string alias =
                        json_input_alias_type(nsid, base, schema);
                    out.push_back("typedef " + alias + " " + base + "_input;");
                    out.push_back("");
                }
            }
        }

        // `<base>_output` aliases for endpoints whose output schema is a `ref`
        // to an object def rather than an inline object: the referenced type
        // already has a full definition (emitted above via the object catalog),
        // so `<base>_output` is just another name for it rather than a
        // duplicate struct.
        for (const auto &doc : docs) {
            const std::string &nsid = doc.id;
            for (cJSON *def = doc.defs->child; def; def = def->next) {
                if (!def->string) continue;
                std::string kind = schema_type(def);
                if (kind != "query" && kind != "procedure") continue;
                cJSON *output = cJSON_GetObjectItemCaseSensitive(def, "output");
                cJSON *output_schema =
                    output ? cJSON_GetObjectItemCaseSensitive(output, "schema")
                           : nullptr;
                if (!output_schema || schema_type(output_schema) != "ref")
                    continue;
                std::string base = type_name(nsid, def->string);
                auto alias = output_object_type(nsid, base, output_schema);
                if (alias && *alias != base + "_output") {
                    out.push_back("typedef " + *alias + " " + base +
                                  "_output;");
                    out.push_back("");
                }
            }
        }

        for (const auto &doc : docs) {
            const std::string &nsid = doc.id;
            for (cJSON *def = doc.defs->child; def; def = def->next) {
                if (!def->string) continue;
                std::string kind = schema_type(def);
                if (kind != "query" && kind != "procedure") continue;
                std::string base = type_name(nsid, def->string);
                cJSON *input = cJSON_GetObjectItemCaseSensitive(def, "input");
                cJSON *input_schema =
                    input ? cJSON_GetObjectItemCaseSensitive(input, "schema")
                          : nullptr;
                auto input_type =
                    input_schema ? json_input_type(nsid, base, input_schema)
                                 : std::optional<std::string>();
                if (input_type) {
                    out.push_back("wf_status " + base + "_input_encode_json(");
                    out.push_back("    const " + *input_type +
                                  " *value, char **out_json);");
                    out.push_back(
                        "/** Free JSON returned by the matching encoder. */");
                    out.push_back("void " + base + "_json_free(char *json);");
                }
                cJSON *output = cJSON_GetObjectItemCaseSensitive(def, "output");
                cJSON *output_schema =
                    output ? cJSON_GetObjectItemCaseSensitive(output, "schema")
                           : nullptr;
                if (output_object_type(nsid, base, output_schema)) {
                    out.push_back("/** Decode an owning output value; free it "
                                  "with the matching function. */");
                    out.push_back("wf_status " + base + "_output_decode_json(");
                    out.push_back("    const char *json, size_t length, " +
                                  base + "_output **out_value);");
                    out.push_back("void " + base + "_output_free(" + base +
                                  "_output *value);");
                }
                if (kind == "procedure") {
                    if (input_type) {
                        out.push_back("wf_status " + base +
                                      "_call(wf_xrpc_client *client,");
                        out.push_back("    const " + *input_type +
                                      " *input, wf_response *out);");
                        out.push_back("wf_status " + base +
                                      "_call_auth(wf_auth_client *client,");
                        out.push_back("    const " + *input_type +
                                      " *input, wf_response *out);");
                    } else {
                        out.push_back(
                            "wf_status " + base +
                            "_call(wf_xrpc_client *client, wf_response *out);");
                        out.push_back("wf_status " + base +
                                      "_call_auth(wf_auth_client *client, "
                                      "wf_response *out);");
                    }
                } else if (kind == "query") {
                    cJSON *params =
                        cJSON_GetObjectItemCaseSensitive(def, "parameters");
                    bool has_params = params && schema_type(params) == "params";
                    std::string arg =
                        has_params ? "const " + base + "_params *params, " : "";
                    out.push_back("wf_status " + base +
                                  "_call(wf_xrpc_client *client,");
                    out.push_back("    " + arg + "wf_response *out);");
                    out.push_back("wf_status " + base +
                                  "_call_auth(wf_auth_client *client,");
                    out.push_back("    " + arg + "wf_response *out);");
                }
                out.push_back("");
            }
        }

        out.push_back("#undef WF_LEX_ARRAY");
        out.push_back("");
        out.push_back("#ifdef __cplusplus");
        out.push_back("}");
        out.push_back("#endif");
        out.push_back("");
        out.push_back("#endif /* " + guard + " */");
        out.push_back("");
        return join_lines(out);
    }

    std::string generate_source(const std::string &header_name) {
        std::vector<std::string> out = {
            "/* Generated by tools/wf_lexgen.cpp; do not edit. */",
            "#include \"" + header_name + "\"",
            "#include <cJSON.h>",
            "#include <openssl/evp.h>",
            "#include <inttypes.h>",
            "#include <limits.h>",
            "#include <math.h>",
            "#include <stdio.h>",
            "#include <stdlib.h>",
            "#include <string.h>",
            "",
            "#if defined(__GNUC__) || defined(__clang__)",
            "#define WF_LEX_UNUSED __attribute__((unused))",
            "#else",
            "#define WF_LEX_UNUSED",
            "#endif",
            "",
            "static WF_LEX_UNUSED char *wf_lex_strdup(const char *source) {",
            "    size_t length = strlen(source) + 1; char *copy = "
            "malloc(length);",
            "    if (copy) memcpy(copy, source, length);",
            "    return copy;",
            "}",
            "",
            "static WF_LEX_UNUSED bool wf_lex_json_integer(cJSON *item, "
            "int64_t *out) {",
            "    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||",
            "        item->valuedouble < -9007199254740991.0 || "
            "item->valuedouble > 9007199254740991.0 ||",
            "        (double)(int64_t)item->valuedouble != item->valuedouble) "
            "return false;",
            "    *out = (int64_t)item->valuedouble; return true;",
            "}",
            "",
            "static WF_LEX_UNUSED wf_status wf_lex_json_copy(cJSON *item, "
            "wf_lex_json *out) {",
            "    char *json = cJSON_PrintUnformatted(item); if (!json) return "
            "WF_ERR_ALLOC;",
            "    out->data = json; out->length = strlen(json); return WF_OK;",
            "}",
            "",
            "static WF_LEX_UNUSED wf_status wf_lex_bytes_encode(const "
            "wf_lex_bytes *value, cJSON **out) {",
            "    if (!value || !out || (value->length && !value->data) || "
            "value->length > INT_MAX)",
            "        return WF_ERR_INVALID_ARG;",
            "    *out = NULL;",
            "    if (value->length > (SIZE_MAX / 4) * 3 - 2) return "
            "WF_ERR_INVALID_ARG;",
            "    size_t length = 4 * ((value->length + 2) / 3);",
            "    char *encoded = malloc(length + 1); if (!encoded) return "
            "WF_ERR_ALLOC;",
            "    const uint8_t *data = value->length ? value->data : (const "
            "uint8_t *)\"\";",
            "    int written = EVP_EncodeBlock((unsigned char *)encoded, data, "
            "(int)value->length);",
            "    if (written < 0 || (size_t)written != length) { "
            "free(encoded); return WF_ERR_INVALID_ARG; }",
            "    encoded[length] = '\\0'; cJSON *root = cJSON_CreateObject();",
            "    cJSON *tag = cJSON_CreateString(encoded); free(encoded);",
            "    if (!root || !tag || !cJSON_AddItemToObject(root, \"$bytes\", "
            "tag)) {",
            "        cJSON_Delete(tag); cJSON_Delete(root); return "
            "WF_ERR_ALLOC;",
            "    }",
            "    *out = root; return WF_OK;",
            "}",
            "",
            "static WF_LEX_UNUSED wf_status wf_lex_cid_encode(const "
            "wf_lex_cid_link *value, cJSON **out) {",
            "    if (!value || !out || !value->cid || !value->cid[0]) return "
            "WF_ERR_INVALID_ARG;",
            "    *out = NULL;",
            "    cJSON *root = cJSON_CreateObject(); cJSON *link = "
            "cJSON_CreateString(value->cid);",
            "    if (!root || !link || !cJSON_AddItemToObject(root, \"$link\", "
            "link)) {",
            "        cJSON_Delete(link); cJSON_Delete(root); return "
            "WF_ERR_ALLOC;",
            "    }",
            "    *out = root; return WF_OK;",
            "}",
            "",
            "static WF_LEX_UNUSED wf_status wf_lex_blob_encode(const "
            "wf_lex_blob *value, cJSON **out) {",
            "    if (!value || !out || !value->cid || !value->cid[0] || "
            "!value->mime_type || value->size < 0)",
            "        return WF_ERR_INVALID_ARG;",
            "    *out = NULL;",
            "    cJSON *root = cJSON_CreateObject(); cJSON *ref = NULL;",
            "    wf_lex_cid_link link = {value->cid};",
            "    if (!root) return WF_ERR_ALLOC;",
            "    wf_status status = wf_lex_cid_encode(&link, &ref);",
            "    if (status != WF_OK) { cJSON_Delete(root); return status; }",
            "    cJSON *type = cJSON_CreateString(\"blob\");",
            "    cJSON *mime = cJSON_CreateString(value->mime_type);",
            "    cJSON *size = cJSON_CreateNumber((double)value->size);",
            "    if (!type || !mime || !size) {",
            "        cJSON_Delete(type); cJSON_Delete(ref); "
            "cJSON_Delete(mime); cJSON_Delete(size);",
            "        cJSON_Delete(root); return WF_ERR_ALLOC;",
            "    }",
            "    if (!cJSON_AddItemToObject(root, \"$type\", type)) goto "
            "blob_fail;",
            "    type = NULL;",
            "    if (!cJSON_AddItemToObject(root, \"ref\", ref)) goto "
            "blob_fail;",
            "    ref = NULL;",
            "    if (!cJSON_AddItemToObject(root, \"mimeType\", mime)) goto "
            "blob_fail;",
            "    mime = NULL;",
            "    if (!cJSON_AddItemToObject(root, \"size\", size)) goto "
            "blob_fail;",
            "    size = NULL;",
            "    *out = root; return WF_OK;",
            "blob_fail:",
            "    cJSON_Delete(type); cJSON_Delete(ref); cJSON_Delete(mime); "
            "cJSON_Delete(size);",
            "    cJSON_Delete(root); return WF_ERR_ALLOC;",
            "}",
            "",
            "static WF_LEX_UNUSED wf_status wf_lex_cid_decode(cJSON *item, "
            "wf_lex_cid_link *out) {",
            "    cJSON *link = cJSON_IsObject(item) ? "
            "cJSON_GetObjectItemCaseSensitive(item, \"$link\") : NULL;",
            "    if (!cJSON_IsString(link) || !link->valuestring[0]) return "
            "WF_ERR_INVALID_ARG;",
            "    out->cid = wf_lex_strdup(link->valuestring); return out->cid "
            "? WF_OK : WF_ERR_ALLOC;",
            "}",
            "",
            "static WF_LEX_UNUSED wf_status wf_lex_bytes_decode(cJSON *item, "
            "wf_lex_bytes *out) {",
            "    cJSON *tag = cJSON_IsObject(item) ? "
            "cJSON_GetObjectItemCaseSensitive(item, \"$bytes\") : NULL;",
            "    if (!cJSON_IsString(tag)) return WF_ERR_INVALID_ARG;",
            "    size_t encoded = strlen(tag->valuestring);",
            "    if (encoded % 4 != 0 || encoded > (size_t)INT_MAX) return "
            "WF_ERR_INVALID_ARG;",
            "    size_t capacity = encoded / 4 * 3; uint8_t *data = capacity ? "
            "malloc(capacity) : NULL;",
            "    if (capacity && !data) return WF_ERR_ALLOC;",
            "    int decoded = encoded ? EVP_DecodeBlock(data, (const unsigned "
            "char *)tag->valuestring, (int)encoded) : 0;",
            "    if (decoded < 0) { free(data); return WF_ERR_INVALID_ARG; }",
            "    size_t padding = encoded && tag->valuestring[encoded - 1] == "
            "'=';",
            "    padding += encoded > 1 && tag->valuestring[encoded - 2] == "
            "'=';",
            "    out->data = data; out->length = (size_t)decoded - padding; "
            "return WF_OK;",
            "}",
            "",
            "static WF_LEX_UNUSED wf_status wf_lex_blob_decode(cJSON *item, "
            "wf_lex_blob *out) {",
            "    if (!cJSON_IsObject(item)) return WF_ERR_INVALID_ARG;",
            "    cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "
            "\"$type\");",
            "    cJSON *ref = cJSON_GetObjectItemCaseSensitive(item, \"ref\");",
            "    cJSON *mime = cJSON_GetObjectItemCaseSensitive(item, "
            "\"mimeType\");",
            "    cJSON *size = cJSON_GetObjectItemCaseSensitive(item, "
            "\"size\"); wf_lex_cid_link link = {0};",
            "    if (!cJSON_IsString(type) || strcmp(type->valuestring, "
            "\"blob\") != 0 ||",
            "        !cJSON_IsString(mime) || !wf_lex_json_integer(size, "
            "&out->size)) return WF_ERR_INVALID_ARG;",
            "    wf_status status = wf_lex_cid_decode(ref, &link); if (status "
            "!= WF_OK) return status;",
            "    out->mime_type = wf_lex_strdup(mime->valuestring);",
            "    if (!out->mime_type) { free((void *)link.cid); return "
            "WF_ERR_ALLOC; }",
            "    out->cid = link.cid; return WF_OK;",
            "}",
            "",
        };

        auto &catalog = object_catalog();
        auto encoders = encoder_objects();
        auto &unions = collect_unions();

        for (const auto &enc : encoders)
            out.push_back("static WF_LEX_UNUSED wf_status wf_lex_encode_" +
                          enc.first + "(const " + enc.first +
                          " *value, cJSON **out);");
        for (const auto &kv : catalog) {
            out.push_back("static WF_LEX_UNUSED void wf_lex_clear_" + kv.first +
                          "(" + kv.first + " *value);");
            out.push_back("static WF_LEX_UNUSED wf_status wf_lex_decode_" +
                          kv.first + "(cJSON *node, " + kv.first + " *value);");
        }
        for (const auto &kv : unions) {
            out.push_back("static WF_LEX_UNUSED void wf_lex_clear_" + kv.first +
                          "(" + kv.first + " *value);");
            out.push_back("static WF_LEX_UNUSED wf_status wf_lex_decode_" +
                          kv.first + "(cJSON *node, " + kv.first + " *value);");
        }
        if (!catalog.empty() || !unions.empty()) out.push_back("");

        for (const auto &enc : encoders) {
            auto e = emit_object_encoder(enc.second.first, enc.first,
                                         enc.second.second);
            out.insert(out.end(), e.begin(), e.end());
        }
        for (const auto &kv : catalog) {
            auto d = emit_object_decoder(kv.second.first, kv.first,
                                         kv.second.second);
            out.insert(out.end(), d.begin(), d.end());
        }
        for (const auto &kv : unions) {
            auto u =
                emit_union_decoder(kv.first, kv.second.first, kv.second.second);
            out.insert(out.end(), u.begin(), u.end());
            auto uc =
                emit_union_clear(kv.first, kv.second.first, kv.second.second);
            out.insert(out.end(), uc.begin(), uc.end());
        }

        for (const auto &doc : docs) {
            const std::string &nsid = doc.id;
            for (cJSON *def = doc.defs->child; def; def = def->next) {
                if (!def->string) continue;
                std::string kind = schema_type(def);
                if (kind != "query" && kind != "procedure") continue;
                std::string base = type_name(nsid, def->string);
                cJSON *output = cJSON_GetObjectItemCaseSensitive(def, "output");
                cJSON *output_schema =
                    output ? cJSON_GetObjectItemCaseSensitive(output, "schema")
                           : nullptr;
                auto output_type =
                    output_object_type(nsid, base, output_schema);
                if (output_type) {
                    out.push_back("wf_status " + base + "_output_decode_json(");
                    out.push_back("    const char *json, size_t length, " +
                                  base + "_output **out_value) {");
                    out.push_back("    if (!json || !out_value) return "
                                  "WF_ERR_INVALID_ARG;");
                    out.push_back("    *out_value = NULL; cJSON *root = "
                                  "cJSON_ParseWithLength(json, length);");
                    out.push_back("    if (!root) return WF_ERR_INVALID_ARG;");
                    out.push_back(
                        "    " + base +
                        "_output *value = calloc(1, sizeof(*value));");
                    out.push_back("    if (!value) { cJSON_Delete(root); "
                                  "return WF_ERR_ALLOC; }");
                    out.push_back("    wf_status status = wf_lex_decode_" +
                                  *output_type + "(root, value);");
                    out.push_back("    cJSON_Delete(root);");
                    out.push_back("    if (status != WF_OK) { free(value); "
                                  "return status; }");
                    out.push_back("    *out_value = value; return WF_OK;");
                    out.push_back("}");
                    out.push_back("");
                    out.push_back("void " + base + "_output_free(" + base +
                                  "_output *value) {");
                    out.push_back("    wf_lex_clear_" + *output_type +
                                  "(value); free(value);");
                    out.push_back("}");
                    out.push_back("");
                }
                cJSON *input = cJSON_GetObjectItemCaseSensitive(def, "input");
                cJSON *schema =
                    input ? cJSON_GetObjectItemCaseSensitive(input, "schema")
                          : nullptr;
                auto input_type = schema ? json_input_type(nsid, base, schema)
                                         : std::optional<std::string>();
                if (input_type) {
                    std::string encoder;
                    if (schema_type(schema) == "object") {
                        encoder = base + "_input";
                    } else {
                        cJSON *ref =
                            cJSON_GetObjectItemCaseSensitive(schema, "ref");
                        if (ref && cJSON_IsString(ref) && ref->valuestring) {
                            auto resolved = resolve_ref(nsid, ref->valuestring);
                            if (resolved &&
                                schema_type(resolved->second) == "object")
                                encoder = ref_type(nsid, ref->valuestring);
                        }
                    }
                    std::vector<std::string> function = {
                        "wf_status " + base + "_input_encode_json(",
                        "    const " + *input_type +
                            " *value, char **out_json) {",
                        "    if (!value || !out_json) return "
                        "WF_ERR_INVALID_ARG;",
                        "    *out_json = NULL; cJSON *root = NULL;",
                        "    wf_status status = WF_OK;",
                        "    (void)status;"};
                    if (!encoder.empty()) {
                        function.push_back("    status = wf_lex_encode_" +
                                           encoder + "(value, &root);");
                        function.push_back(
                            "    if (status != WF_OK) return status;");
                    } else {
                        add_encoded_value(function, nsid, base, schema,
                                          "*value", "root", "input", "    ");
                    }
                    function.push_back(
                        "    *out_json = cJSON_PrintUnformatted(root);");
                    function.push_back("    cJSON_Delete(root);");
                    function.push_back(
                        "    return *out_json ? WF_OK : WF_ERR_ALLOC;");
                    bool has_invalid = false, has_fail = false,
                         has_status = false;
                    for (const auto &l : function) {
                        if (l.find("goto invalid;") != std::string::npos)
                            has_invalid = true;
                        if (l.find("goto fail;") != std::string::npos)
                            has_fail = true;
                        if (l.find("goto status_fail;") != std::string::npos)
                            has_status = true;
                    }
                    if (has_invalid) {
                        function.push_back("invalid:");
                        function.push_back("    cJSON_Delete(root); return "
                                           "WF_ERR_INVALID_ARG;");
                    }
                    if (has_fail) {
                        function.push_back("fail:");
                        function.push_back(
                            "    cJSON_Delete(root); return WF_ERR_ALLOC;");
                    }
                    if (has_status) {
                        function.push_back("status_fail:");
                        function.push_back(
                            "    cJSON_Delete(root); return status;");
                    }
                    out.insert(out.end(), function.begin(), function.end());
                    out.push_back("}");
                    out.push_back("");
                    out.push_back(
                        "void " + base +
                        "_json_free(char *json) { cJSON_free(json); }");
                    out.push_back("");
                }
                if (kind == "procedure" && !def_has_params(def)) {
                    if (input_type) {
                        out.push_back("wf_status " + base +
                                      "_call(wf_xrpc_client *client,");
                        out.push_back("    const " + *input_type +
                                      " *input, wf_response *response) {");
                        out.push_back("    if (!client || !input || !response) "
                                      "return WF_ERR_INVALID_ARG;");
                        out.push_back("    char *json = NULL;");
                        out.push_back("    wf_status status = " + base +
                                      "_input_encode_json(input, &json);");
                        out.push_back(
                            "    if (status != WF_OK) return status;");
                        out.push_back(
                            "    status = wf_xrpc_procedure(client, \"" + nsid +
                            "\", json, response);");
                        out.push_back("    cJSON_free(json);");
                        out.push_back("    return status;");
                        out.push_back("}");
                        out.push_back("");
                        out.push_back("wf_status " + base +
                                      "_call_auth(wf_auth_client *client,");
                        out.push_back("    const " + *input_type +
                                      " *input, wf_response *response) {");
                        out.push_back("    if (!client || !input || !response) "
                                      "return WF_ERR_INVALID_ARG;");
                        out.push_back("    char *json = NULL;");
                        out.push_back("    wf_status status = " + base +
                                      "_input_encode_json(input, &json);");
                        out.push_back(
                            "    if (status != WF_OK) return status;");
                        out.push_back(
                            "    status = wf_auth_client_procedure(client, \"" +
                            nsid + "\", json, response);");
                        out.push_back("    cJSON_free(json);");
                        out.push_back("    return status;");
                        out.push_back("}");
                        out.push_back("");
                    } else {
                        out.push_back("wf_status " + base +
                                      "_call(wf_xrpc_client *client, "
                                      "wf_response *response) {");
                        out.push_back("    if (!client || !response) return "
                                      "WF_ERR_INVALID_ARG;");
                        out.push_back(
                            "    return wf_xrpc_procedure(client, \"" + nsid +
                            "\", NULL, response);");
                        out.push_back("}");
                        out.push_back("");
                        out.push_back("wf_status " + base +
                                      "_call_auth(wf_auth_client *client, "
                                      "wf_response *response) {");
                        out.push_back("    if (!client || !response) return "
                                      "WF_ERR_INVALID_ARG;");
                        out.push_back(
                            "    return wf_auth_client_procedure(client, \"" +
                            nsid + "\", NULL, response);");
                        out.push_back("}");
                        out.push_back("");
                    }
                }
            }

            cJSON *main = cJSON_GetObjectItemCaseSensitive(doc.defs, "main");
            if (main && schema_type(main) == "query") {
                std::string base = type_name(nsid, "main");
                cJSON *params =
                    cJSON_GetObjectItemCaseSensitive(main, "parameters");
                if (!params) {
                    out.push_back("wf_status " + base +
                                  "_call(wf_xrpc_client *client, wf_response "
                                  "*response) {");
                    out.push_back("    if (!client || !response) return "
                                  "WF_ERR_INVALID_ARG;");
                    out.push_back("    return wf_xrpc_query(client, \"" + nsid +
                                  "\", NULL, response);");
                    out.push_back("}");
                    out.push_back("");
                    out.push_back("wf_status " + base +
                                  "_call_auth(wf_auth_client *client, "
                                  "wf_response *response) {");
                    out.push_back("    if (!client || !response) return "
                                  "WF_ERR_INVALID_ARG;");
                    out.push_back("    return wf_auth_client_query(client, \"" +
                                  nsid + "\", NULL, response);");
                    out.push_back("}");
                    out.push_back("");
                } else if (schema_type(params) == "params") {
                    cJSON *props =
                        cJSON_GetObjectItemCaseSensitive(params, "properties");
                    // Validate parameter kinds.
                    std::set<std::string> required = required_set(params);
                    if (props) {
                        for (cJSON *p = props->child; p; p = p->next) {
                            if (!p->string) continue;
                            std::string wire = p->string;
                            std::string kind = schema_type(p);
                            if (kind == "array") {
                                cJSON *items = cJSON_GetObjectItemCaseSensitive(
                                    p, "items");
                                std::string item_kind = schema_type(items);
                                if (item_kind != "string" &&
                                    item_kind != "integer" &&
                                    item_kind != "boolean")
                                    throw std::runtime_error(
                                        "query parameter " + wire +
                                        " has unsupported array item type " +
                                        item_kind);
                            } else if (kind != "string" && kind != "integer" &&
                                       kind != "boolean") {
                                throw std::runtime_error(
                                    "query parameter " + wire +
                                    " has unsupported type " + kind);
                            }
                        }
                    }
                    std::vector<std::string> call_body = {
                        "    if (!params || !response) return "
                        "WF_ERR_INVALID_ARG;",
                        "    size_t encoded_capacity = 0, number_capacity = "
                        "0;"};
                    if (props) {
                        for (cJSON *p = props->child; p; p = p->next) {
                            if (!p->string) continue;
                            std::string wire = p->string;
                            std::string field = field_name(params, wire);
                            std::optional<std::string> condition;
                            if (!required.count(wire))
                                condition = "params->has_" + field;
                            std::string indent2 = "    ";
                            if (condition) {
                                call_body.push_back("    if (" + *condition +
                                                    ") {");
                                indent2 = "        ";
                            }
                            std::string kind = schema_type(p);
                            if (kind == "array") {
                                cJSON *items = cJSON_GetObjectItemCaseSensitive(
                                    p, "items");
                                std::string item_kind = schema_type(items);
                                call_body.push_back(
                                    indent2 + "if (params->" + field +
                                    ".count && !params->" + field +
                                    ".items) return WF_ERR_INVALID_ARG;");
                                if (item_kind == "string") {
                                    call_body.push_back(
                                        indent2 +
                                        "for (size_t i = 0; i < params->" +
                                        field + ".count; ++i)");
                                    call_body.push_back(
                                        indent2 + "    if (!params->" + field +
                                        ".items[i]) return "
                                        "WF_ERR_INVALID_ARG;");
                                }
                                call_body.push_back(
                                    indent2 + "if (params->" + field +
                                    ".count > SIZE_MAX - encoded_capacity) "
                                    "return WF_ERR_INVALID_ARG;");
                                call_body.push_back(
                                    indent2 + "encoded_capacity += params->" +
                                    field + ".count;");
                                if (item_kind == "integer") {
                                    call_body.push_back(
                                        indent2 + "if (params->" + field +
                                        ".count > SIZE_MAX - number_capacity) "
                                        "return WF_ERR_INVALID_ARG;");
                                    call_body.push_back(
                                        indent2 +
                                        "number_capacity += params->" + field +
                                        ".count;");
                                }
                            } else {
                                if (kind == "string")
                                    call_body.push_back(
                                        indent2 + "if (!params->" + field +
                                        ") return WF_ERR_INVALID_ARG;");
                                call_body.push_back(
                                    indent2 +
                                    "if (encoded_capacity == SIZE_MAX) return "
                                    "WF_ERR_INVALID_ARG;");
                                call_body.push_back(indent2 +
                                                    "++encoded_capacity;");
                                if (kind == "integer") {
                                    call_body.push_back(
                                        indent2 +
                                        "if (number_capacity == SIZE_MAX) "
                                        "return WF_ERR_INVALID_ARG;");
                                    call_body.push_back(indent2 +
                                                        "++number_capacity;");
                                }
                            }
                            if (condition) call_body.push_back("    }");
                        }
                    }
                    call_body.push_back("    if (encoded_capacity > SIZE_MAX / "
                                        "sizeof(wf_xrpc_param) ||");
                    call_body.push_back(
                        "        number_capacity > SIZE_MAX / "
                        "sizeof(char[32])) return WF_ERR_INVALID_ARG;");
                    call_body.push_back(
                        "    wf_xrpc_param *encoded = encoded_capacity ? "
                        "calloc(encoded_capacity, sizeof(*encoded)) : NULL;");
                    call_body.push_back(
                        "    char (*number_values)[32] = number_capacity ? "
                        "malloc(number_capacity * sizeof(*number_values)) : "
                        "NULL;");
                    call_body.push_back(
                        "    if ((encoded_capacity && !encoded) || "
                        "(number_capacity && !number_values)) {");
                    call_body.push_back(
                        "        free(encoded); free(number_values); return "
                        "WF_ERR_ALLOC;");
                    call_body.push_back("    }");
                    call_body.push_back(
                        "    size_t count = 0, number_count = 0;");
                    call_body.push_back("    (void)number_count;");
                    if (props) {
                        for (cJSON *p = props->child; p; p = p->next) {
                            if (!p->string) continue;
                            std::string wire = p->string;
                            std::string field = field_name(params, wire);
                            std::optional<std::string> condition;
                            if (!required.count(wire))
                                condition = "params->has_" + field;
                            std::string indent2 = "    ";
                            if (condition) {
                                call_body.push_back("    if (" + *condition +
                                                    ") {");
                                indent2 = "        ";
                            }
                            std::string kind = schema_type(p);
                            std::string value;
                            if (kind == "string") {
                                value = "params->" + field;
                            } else if (kind == "boolean") {
                                value = "(params->" + field +
                                        " ? \"true\" : \"false\")";
                            } else if (kind == "integer") {
                                call_body.push_back(
                                    indent2 +
                                    "snprintf(number_values[number_count], "
                                    "sizeof(number_values[number_count]), "
                                    "\"%\" PRId64, params->" +
                                    field + ");");
                                value = "number_values[number_count++]";
                            } else {
                                cJSON *items = cJSON_GetObjectItemCaseSensitive(
                                    p, "items");
                                std::string item_kind = schema_type(items);
                                call_body.push_back(
                                    indent2 +
                                    "for (size_t i = 0; i < params->" + field +
                                    ".count; ++i) {");
                                if (item_kind == "string") {
                                    value = "params->" + field + ".items[i]";
                                } else if (item_kind == "boolean") {
                                    value = "(params->" + field +
                                            ".items[i] ? \"true\" : \"false\")";
                                } else {
                                    call_body.push_back(
                                        indent2 +
                                        "    "
                                        "snprintf(number_values[number_count], "
                                        "sizeof(number_values[number_count]), "
                                        "\"%\" PRId64, params->" +
                                        field + ".items[i]);");
                                    value = "number_values[number_count++]";
                                }
                                call_body.push_back(indent2 +
                                                    "    encoded[count++] = "
                                                    "(wf_xrpc_param){\"" +
                                                    wire + "\", " + value +
                                                    "};");
                                call_body.push_back(indent2 + "}");
                                if (condition) call_body.push_back("    }");
                                continue;
                            }
                            call_body.push_back(
                                indent2 +
                                "encoded[count++] = (wf_xrpc_param){\"" + wire +
                                "\", " + value + "};");
                            if (condition) call_body.push_back("    }");
                        }
                    }
                    // _call variant
                    out.push_back("wf_status " + base +
                                  "_call(wf_xrpc_client *client,");
                    out.push_back("    const " + base +
                                  "_params *params, wf_response *response) {");
                    out.push_back(
                        "    if (!client) return WF_ERR_INVALID_ARG;");
                    out.insert(out.end(), call_body.begin(), call_body.end());
                    out.push_back("    wf_status status = "
                                  "wf_xrpc_query_params(client, \"" +
                                  nsid + "\", encoded, count, response);");
                    out.push_back("    free(encoded); free(number_values); "
                                  "return status;");
                    out.push_back("}");
                    out.push_back("");
                    // _call_auth variant
                    out.push_back("wf_status " + base +
                                  "_call_auth(wf_auth_client *client,");
                    out.push_back("    const " + base +
                                  "_params *params, wf_response *response) {");
                    out.push_back(
                        "    if (!client) return WF_ERR_INVALID_ARG;");
                    out.insert(out.end(), call_body.begin(), call_body.end());
                    out.push_back("    wf_status status = "
                                  "wf_auth_client_query_params(client, \"" +
                                  nsid + "\", encoded, count, response);");
                    out.push_back("    free(encoded); free(number_values); "
                                  "return status;");
                    out.push_back("}");
                    out.push_back("");
                }
            }
        }

        return join_lines(out);
    }

  private:
    mutable std::map<std::string, std::pair<std::string, cJSON *>> _objects;
    mutable bool _objects_ready = false;
    mutable std::vector<std::string> _object_order;
    mutable std::map<std::string, std::pair<std::string, cJSON *>> _unions;
    mutable bool _unions_ready = false;

    void build_object_catalog() const {
        _objects.clear();
        _object_order.clear();
        std::set<std::string> visiting;

        std::function<void(const std::string &, const std::string &, cJSON *)>
            collect;
        collect = [&](const std::string &nsid, const std::string &name,
                      cJSON *schema) {
            auto it = _objects.find(name);
            if (it != _objects.end()) {
                if (it->second.first != nsid || it->second.second != schema)
                    throw std::runtime_error(
                        "inline object name collision for " + name);
                return;
            }
            if (visiting.count(name))
                throw std::runtime_error("recursive inline object in " + name);
            visiting.insert(name);

            std::function<void(cJSON *, const std::string &)> visit;
            visit = [&](cJSON *value, const std::string &path) {
                std::string kind = schema_type(value);
                if (kind == "array") {
                    cJSON *items =
                        cJSON_GetObjectItemCaseSensitive(value, "items");
                    visit(items ? items : cJSON_CreateObject(), path + "_item");
                } else if (kind == "object") {
                    std::string child = name + "_" + snake(path);
                    collect(nsid, child, value);
                }
            };
            cJSON *props =
                cJSON_GetObjectItemCaseSensitive(schema, "properties");
            if (props) {
                for (cJSON *p = props->child; p; p = p->next) {
                    if (!p->string) continue;
                    visit(p, field_name(schema, p->string));
                }
            }
            visiting.erase(name);
            // Children are inserted first, so direct embedded fields are
            // complete.
            _objects[name] = std::make_pair(nsid, schema);
            _object_order.push_back(name);
        };

        for (const auto &doc : docs) {
            for (auto &pair : schemas(doc))
                collect(doc.id, pair.first, pair.second);
        }
    }

    static std::string join_lines(const std::vector<std::string> &lines) {
        std::string result;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i) result += '\n';
            result += lines[i];
        }
        return result;
    }

    static bool target_ends_with(const std::string &target,
                                 const char *suffix) {
        size_t n = strlen(suffix);
        return target.size() >= n &&
               target.compare(target.size() - n, n, suffix) == 0;
    }

    static std::string target_field(const std::string &target) {
        // rsplit("->", 1)[-1].split(".")[-1]
        size_t arrow = target.rfind("->");
        std::string rest =
            arrow == std::string::npos ? target : target.substr(arrow + 2);
        size_t dot = rest.rfind('.');
        return dot == std::string::npos ? rest : rest.substr(dot + 1);
    }

    static std::string target_suffix_name(const std::string &target) {
        return target_field(target);
    }

    static bool def_has_params(cJSON *def) {
        cJSON *params = cJSON_GetObjectItemCaseSensitive(def, "parameters");
        return params != nullptr;
    }
};

// ---------------------------------------------------------------------------
// CLI.
// ---------------------------------------------------------------------------

static Doc load(const fs::path &path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error(path.string() + ": cannot open");
    std::string text((std::istreambuf_iterator<char>(stream)),
                     std::istreambuf_iterator<char>());
    cJSON *raw = cJSON_Parse(text.c_str());
    if (!raw) throw std::runtime_error(path.string() + ": invalid JSON");
    Doc doc;
    doc.raw = raw;
    cJSON *lexicon = cJSON_GetObjectItemCaseSensitive(raw, "lexicon");
    cJSON *id = cJSON_GetObjectItemCaseSensitive(raw, "id");
    cJSON *defs = cJSON_GetObjectItemCaseSensitive(raw, "defs");
    if (!lexicon || !cJSON_IsNumber(lexicon) ||
        (int)lexicon->valuedouble != 1 || !id || !cJSON_IsString(id) ||
        !id->valuestring)
        throw std::runtime_error(path.string() +
                                 ": expected a Lexicon 1 document with an id");
    if (!defs || !cJSON_IsObject(defs))
        throw std::runtime_error(path.string() + ": expected a defs object");
    doc.id = id->valuestring;
    doc.defs = defs;
    return doc;
}

static void write_file(const fs::path &path, const std::string &content) {
    if (path.has_parent_path()) fs::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out) throw std::runtime_error(path.string() + ": cannot write");
    out << content;
}

int main(int argc, char *argv[]) {
    std::vector<std::string> lexicons;
    std::string output;
    std::string source_output;
    std::string guard = "WOLFRAM_GENERATED_LEXICONS_H";
    std::string header_rel;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) output = argv[++i];
        } else if (arg == "--source-output") {
            if (i + 1 < argc) source_output = argv[++i];
        } else if (arg == "--guard") {
            if (i + 1 < argc) guard = argv[++i];
        } else if (arg == "--header-rel") {
            if (i + 1 < argc) header_rel = argv[++i];
        } else {
            lexicons.push_back(arg);
        }
    }

    if (lexicons.empty()) {
        std::cerr << "wf_lexgen: no lexicon files given\n";
        return 1;
    }

    try {
        std::vector<Doc> parsed;
        for (const auto &path : lexicons) parsed.push_back(load(path));
        Generator generator(std::move(parsed), guard);

        std::string result = generator.generate();
        if (!output.empty()) {
            write_file(output, result);
        } else {
            std::cout << result;
        }

        if (!source_output.empty()) {
            if (output.empty())
                throw std::runtime_error("--source-output requires --output");
            std::string header = header_rel.empty()
                                     ? fs::path(output).filename().string()
                                     : header_rel;
            write_file(source_output, generator.generate_source(header));
        }
    } catch (const std::exception &error) {
        std::cerr << "wf_lexgen: error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
