#!/usr/bin/env python3
"""Generate C declarations and optional implementations from Lexicon JSON.

Generated endpoint wrappers delegate all transport to wolfram/xrpc.h.
"""

from __future__ import annotations

import argparse
import json
import keyword
import re
import sys
from pathlib import Path
from typing import Any


C_KEYWORDS = {
    "auto", "break", "case", "char", "const", "continue", "default",
    "do", "double", "else", "enum", "extern", "float", "for", "goto",
    "if", "inline", "int", "long", "register", "restrict", "return",
    "short", "signed", "sizeof", "static", "struct", "switch", "typedef",
    "union", "unsigned", "void", "volatile", "while", "_Alignas",
    "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary",
    "_Noreturn", "_Static_assert", "_Thread_local",
}

CPP_KEYWORDS = {
    "alignas", "alignof", "and", "and_eq", "asm", "bitand", "bitor",
    "bool", "catch", "char8_t", "char16_t", "char32_t", "class",
    "compl", "concept", "consteval", "constexpr", "constinit", "const_cast",
    "co_await", "co_return", "co_yield", "decltype", "delete", "dynamic_cast",
    "explicit", "export", "false", "friend", "mutable", "namespace", "new",
    "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq",
    "private", "protected", "public", "reflexpr", "reinterpret_cast",
    "requires", "static_assert", "static_cast", "synchronized", "template",
    "this", "thread_local", "throw", "true", "try", "typeid", "typename",
    "using", "virtual", "wchar_t", "xor", "xor_eq",
}


def snake(value: str) -> str:
    value = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", value)
    value = re.sub(r"[^A-Za-z0-9]+", "_", value).strip("_").lower()
    if not value:
        value = "value"
    if value[0].isdigit() or value in C_KEYWORDS or keyword.iskeyword(value):
        value += "_"
    return value


def member_name(value: str) -> str:
    """Return an identifier safe as a struct/union member in C and C++."""
    value = snake(value)
    return value + "_" if value in CPP_KEYWORDS else value


def type_name(nsid: str, suffix: str) -> str:
    return "wf_lex_" + snake(nsid) + ("_" + snake(suffix) if suffix else "")


def ref_type(nsid: str, ref: str) -> str:
    if ref.startswith("#"):
        return type_name(nsid, ref[1:])
    if "#" in ref:
        doc, fragment = ref.split("#", 1)
        return type_name(doc, fragment)
    return type_name(ref, "main")


def comment(text: str, indent: str = "") -> list[str]:
    clean = " ".join(text.replace("*/", "* /").split())
    return [f"{indent}/** {clean} */"] if clean else []


class Generator:
    def __init__(self, docs: list[dict[str, Any]], guard: str):
        self.docs = sorted(docs, key=lambda doc: doc["id"])
        self.guard = guard
        self._objects: dict[str, tuple[str, dict[str, Any]]] | None = None
        self._unions: dict[str, tuple[str, dict[str, Any]]] | None = None

    def resolve_ref(self, nsid: str, ref: str) -> tuple[str, dict[str, Any]] | None:
        document_id, fragment = (nsid, ref[1:]) if ref.startswith("#") else (
            ref.split("#", 1) if "#" in ref else (ref, "main"))
        for doc in self.docs:
            if doc["id"] == document_id:
                definition = doc.get("defs", {}).get(fragment)
                if not isinstance(definition, dict):
                    return None
                if definition.get("type") == "record":
                    definition = definition.get("record", definition)
                return document_id, definition
        return None

    @staticmethod
    def field_name(schema: dict[str, Any], wire_name: str) -> str:
        base = member_name(wire_name)
        required = set(schema.get("required", []))
        presence = {"has_" + member_name(name) for name in schema.get("properties", {})
                    if name not in required}
        return base + "_value" if base in presence else base

    def c_type(self, nsid: str, owner: str, field: str,
               schema: dict[str, Any]) -> tuple[str, bool]:
        kind = schema.get("type")
        if kind == "string":
            return "const char *", False
        if kind == "integer":
            return "int64_t", True
        if kind == "boolean":
            return "bool", True
        if kind == "bytes":
            return "wf_lex_bytes", False
        if kind == "cid-link":
            return "wf_lex_cid_link", False
        if kind == "blob":
            return "wf_lex_blob", False
        if kind == "ref":
            resolved = self.resolve_ref(nsid, schema["ref"])
            if resolved and resolved[1].get("type") not in ("object",):
                target_nsid, target = resolved
                if target.get("type") == "token":
                    return "const char *", False
                return self.c_type(target_nsid, owner, field, target)
            # Borrowed pointers support recursive and cross-document refs
            # without imposing declaration order on generated headers.
            return "const " + ref_type(nsid, schema["ref"]) + " *", False
        if kind == "object":
            name = f"{owner}_{snake(field)}"
            return name, False
        # Closed/open unions decode into a generated typed struct keyed by the
        # owner+field name (see collect_unions / emit_union_struct). The struct
        # retains the raw JSON for re-encoding and open-union fallback.
        if kind == "union":
            return self.union_name(owner, field), False
        # Unknown (free-form) values remain opaque JSON.
        if kind == "unknown":
            return "wf_lex_json", False
        if kind == "array":
            item_type, _ = self.c_type(nsid, owner, field + "_item",
                                       schema.get("items", {"type": "unknown"}))
            return f"WF_LEX_ARRAY({item_type})", False
        return "wf_lex_json", False

    def union_name(self, owner: str, field: str) -> str:
        return f"{owner}_{snake(field)}_union"

    def collect_unions(self) -> dict[str, tuple[str, dict[str, Any]]]:
        """Register every union field (direct, array element, or reached
        through a ref to an array/union) as a typed struct, keyed by the same
        owner+field name the field's C type uses."""
        if self._unions is not None:
            return self._unions
        self._unions = {}
        catalog = self.object_catalog()

        def walk(nsid: str, owner: str, schema: dict[str, Any], path: str) -> None:
            kind = schema.get("type")
            if kind == "union":
                self._unions[self.union_name(owner, path)] = (nsid, schema)
            elif kind == "array":
                walk(nsid, owner, schema.get("items", {"type": "unknown"}),
                     path + "_item")
            elif kind == "object":
                for wire_name, prop in schema.get("properties", {}).items():
                    walk(nsid, owner, prop, self.field_name(schema, wire_name))
            elif kind == "ref":
                resolved = self.resolve_ref(nsid, schema["ref"])
                if not resolved:
                    return
                target_nsid, target = resolved
                if target.get("type") == "object":
                    # Borrowed pointer; its nested unions are walked via catalog.
                    return
                walked = {"type": "string"} if target.get("type") == "token" else target
                walk(target_nsid, owner, walked, path)

        for name, (nsid, schema) in catalog.items():
            walk(nsid, name, schema, "")
        return self._unions

    def union_members(self, nsid: str,
                      schema: dict[str, Any]) -> list[tuple[int, str, str, str]]:
        """Return (index, full_$type, c_type, member_name) for each resolvable
        object member of the union. Non-object refs (external/knownValues) are
        skipped; the raw JSON fallback covers them."""
        members: list[tuple[int, str, str, str]] = []
        seen: set[str] = set()
        for i, ref in enumerate(schema.get("refs", [])):
            resolved = self.resolve_ref(nsid, ref)
            if not resolved or resolved[1].get("type") != "object":
                continue
            full = ref if not ref.startswith("#") else nsid + ref
            frag = ref.split("#", 1)[1] if "#" in ref else ref.rsplit(".", 1)[-1]
            mname = member_name(frag)
            if mname in seen:
                mname = f"{mname}_{i}"
            seen.add(mname)
            members.append((i, full, ref_type(nsid, ref), mname))
        return members

    def emit_union_struct(self, name: str, nsid: str,
                          schema: dict[str, Any]) -> list[str]:
        lines = [f"typedef struct {name} {{",
                 "    int kind;",
                 "    /* Retained raw JSON (mirrors wf_lex_json) for re-encoding",
                 "     * and open-union/unknown $type fallback. */",
                 "    const char *data;",
                 "    size_t length;",
                 "    union {"]
        members = self.union_members(nsid, schema)
        if members:
            for _idx, _full, ctype, mname in members:
                lines.append(f"        const {ctype} *{mname};")
        else:
            lines.append("        unsigned char _unused;")
        lines += ["    } value;", f"}} {name};", ""]
        return lines

    def emit_union_decoder(self, name: str, nsid: str,
                           schema: dict[str, Any]) -> list[str]:
        lines = [
            f"static wf_status wf_lex_decode_{name}(cJSON *node, {name} *value) {{",
            "    if (!node || !value) return WF_ERR_INVALID_ARG;",
            "    memset(value, 0, sizeof(*value));",
            "    value->kind = -1;",
            "    char *raw = cJSON_PrintUnformatted(node);",
            "    if (!raw) return WF_ERR_ALLOC;",
            "    value->data = raw; value->length = strlen(raw);",
            '    cJSON *type = cJSON_GetObjectItemCaseSensitive(node, "$type");',
            "    const char *t = (type && cJSON_IsString(type)) ? type->valuestring : NULL;",
            "    if (t) {",
        ]
        for idx, full, ctype, mname in self.union_members(nsid, schema):
            lines.append(f'        if (strcmp(t, "{full}") == 0) {{')
            lines.append(f"            value->kind = {idx};")
            lines.append(f"            {ctype} *m = calloc(1, sizeof(*m));")
            lines.append(f"            if (!m) {{ wf_lex_clear_{name}(value); return WF_ERR_ALLOC; }}")
            lines.append(f"            wf_status status = wf_lex_decode_{ctype}(node, m);")
            lines.append(f"            if (status != WF_OK) {{")
            lines.append(f"                free(m); value->kind = -1;")
            lines.append(f"            }} else {{")
            lines.append(f"                value->value.{mname} = m;")
            lines.append(f"            }}")
            lines.append("        }")
        lines += ["    }", "    return WF_OK;", "}", ""]
        return lines

    def emit_union_clear(self, name: str, nsid: str,
                         schema: dict[str, Any]) -> list[str]:
        lines = [f"static void wf_lex_clear_{name}({name} *value) {{",
                 "    if (!value) return;",
                 "    free((void *)value->data);",
                 "    switch (value->kind) {"]
        for idx, _full, ctype, mname in self.union_members(nsid, schema):
            lines.append(f"    case {idx}:")
            lines.append(f"        if (value->value.{mname}) {{")
            lines.append(f"            wf_lex_clear_{ctype}(({ctype} *)value->value.{mname});")
            lines.append(f"            free((void *)value->value.{mname});")
            lines.append("        }")
            lines.append("        break;")
        lines += ["    default: break;", "    }",
                   "    memset(value, 0, sizeof(*value));", "}", ""]
        return lines

    def json_input_type(self, nsid: str, base: str,
                        schema: dict[str, Any]) -> str | None:
        """Return the named type accepted by a JSON endpoint input."""
        if schema.get("type") in {
                "array", "blob", "boolean", "bytes", "cid-link", "integer",
                "object", "ref", "string", "union", "unknown"}:
            return base + "_input"
        return None

    def json_input_alias_type(self, nsid: str, base: str,
                              schema: dict[str, Any]) -> str:
        """Return the C type aliased by a non-object endpoint input."""
        if schema.get("type") == "ref":
            resolved = self.resolve_ref(nsid, schema["ref"])
            if resolved and resolved[1].get("type") == "object":
                return ref_type(nsid, schema["ref"])
        return self.c_type(nsid, base, "input", schema)[0]

    def emit_object(self, nsid: str, name: str,
                    schema: dict[str, Any]) -> list[str]:
        lines = comment(schema.get("description", ""))
        lines.append(f"typedef struct {name} {{")
        properties = schema.get("properties", {})
        required = set(schema.get("required", []))
        if not properties:
            lines.append("    unsigned char _unused;")
        for wire_name, prop in properties.items():
            field = self.field_name(schema, wire_name)
            ctype, scalar = self.c_type(nsid, name, field, prop)
            lines.extend(comment(prop.get("description", ""), "    "))
            if wire_name not in required:
                lines.append(f"    bool has_{field};")
            lines.append(f"    {ctype} {field};")
        lines.append(f"}} {name};")
        return lines

    def schemas(self, doc: dict[str, Any]) -> list[tuple[str, dict[str, Any]]]:
        nsid = doc["id"]
        found: list[tuple[str, dict[str, Any]]] = []
        for def_name, definition in doc.get("defs", {}).items():
            kind = definition.get("type")
            base = type_name(nsid, def_name)
            if kind == "object":
                found.append((base, definition))
            elif kind == "record" and definition.get("record", {}).get("type") == "object":
                found.append((base, definition["record"]))
            elif kind in ("query", "procedure"):
                params = definition.get("parameters")
                if params and params.get("type") == "params":
                    found.append((base + "_params", params))
                for label in ("input", "output"):
                    value = definition.get(label, {})
                    schema = value.get("schema")
                    if schema and schema.get("type") == "object":
                        found.append((base + "_" + label, schema))
        return found

    def object_catalog(self) -> dict[str, tuple[str, dict[str, Any]]]:
        """Return named and inline objects in dependency-safe deterministic order."""
        if self._objects is not None:
            return self._objects
        catalog: dict[str, tuple[str, dict[str, Any]]] = {}
        visiting: set[str] = set()

        def collect(nsid: str, name: str, schema: dict[str, Any]) -> None:
            previous = catalog.get(name)
            if previous is not None:
                if previous != (nsid, schema):
                    raise ValueError(f"inline object name collision for {name}")
                return
            if name in visiting:
                raise ValueError(f"recursive inline object in {name}")
            visiting.add(name)

            def visit(value: dict[str, Any], path: str) -> None:
                kind = value.get("type")
                if kind == "array":
                    visit(value.get("items", {"type": "unknown"}), path + "_item")
                elif kind == "object":
                    child = f"{name}_{snake(path)}"
                    collect(nsid, child, value)

            for wire_name, prop in schema.get("properties", {}).items():
                visit(prop, self.field_name(schema, wire_name))
            visiting.remove(name)
            # Children are inserted first, so direct embedded fields are complete.
            catalog[name] = (nsid, schema)

        for doc in self.docs:
            for name, schema in self.schemas(doc):
                collect(doc["id"], name, schema)
        self._objects = catalog
        return catalog

    def encoder_objects(self) -> dict[str, tuple[str, dict[str, Any]]]:
        catalog = self.object_catalog()
        wanted: set[str] = set()

        def visit_object(nsid: str, name: str, schema: dict[str, Any]) -> None:
            if name in wanted:
                return
            wanted.add(name)
            for wire_name, prop in schema.get("properties", {}).items():
                field = self.field_name(schema, wire_name)
                visit_schema(nsid, name, prop, field)

        def visit_schema(nsid: str, owner: str, schema: dict[str, Any],
                         path: str) -> None:
            kind = schema.get("type")
            if kind == "array":
                visit_schema(nsid, owner,
                             schema.get("items", {"type": "unknown"}),
                             path + "_item")
            elif kind == "object":
                child = f"{owner}_{snake(path)}"
                visit_object(nsid, child, catalog[child][1])
            elif kind == "ref":
                resolved = self.resolve_ref(nsid, schema["ref"])
                if not resolved:
                    raise ValueError(
                        f"cannot encode unresolved ref {schema['ref']} in {owner}"
                    )
                target_nsid, target = resolved
                if target.get("type") == "object":
                    target_name = ref_type(nsid, schema["ref"])
                    if target_name not in catalog:
                        raise ValueError(
                            f"cannot encode ref {schema['ref']} in {owner}"
                        )
                    visit_object(target_nsid, target_name, catalog[target_name][1])
                elif target.get("type") != "token":
                    visit_schema(target_nsid, owner, target, path)

        for doc in self.docs:
            for def_name, definition in doc.get("defs", {}).items():
                schema = definition.get("input", {}).get("schema", {})
                if definition.get("type") not in ("query", "procedure"):
                    continue
                if schema.get("type") == "object":
                    root = type_name(doc["id"], def_name) + "_input"
                    visit_object(doc["id"], root, schema)
                elif schema:
                    visit_schema(doc["id"], type_name(doc["id"], def_name),
                                 schema, "input")
        return {name: value for name, value in catalog.items() if name in wanted}

    def referenced_types(self) -> set[str]:
        found: set[str] = set()

        def visit(nsid: str, value: Any) -> None:
            if isinstance(value, dict):
                if value.get("type") == "ref" and isinstance(value.get("ref"), str):
                    resolved = self.resolve_ref(nsid, value["ref"])
                    if not resolved or resolved[1].get("type") == "object":
                        found.add(ref_type(nsid, value["ref"]))
                for child in value.values():
                    visit(nsid, child)
            elif isinstance(value, list):
                for child in value:
                    visit(nsid, child)

        for doc in self.docs:
            visit(doc["id"], doc.get("defs", {}))
        return found

    def generate(self) -> str:
        out = [
            "/* Generated by tools/wf_lexgen.py; do not edit. */",
            f"#ifndef {self.guard}", f"#define {self.guard}", "",
            "#include <stdbool.h>", "#include <stddef.h>", "#include <stdint.h>",
            "#include <wolfram/xrpc.h>", "#include <wolfram/auth_client.h>", "",
            "#ifdef __cplusplus", 'extern "C" {', "#endif", "",
            "/** Encoded JSON view. Decoded outputs own data; input values borrow it. */",
            "typedef struct wf_lex_json { const char *data; size_t length; } wf_lex_json;",
            "/** Byte sequence view. Decoded outputs own data; input values borrow it. */",
            "typedef struct wf_lex_bytes { const uint8_t *data; size_t length; } wf_lex_bytes;",
            "/** CID link string view. */",
            "typedef struct wf_lex_cid_link { const char *cid; } wf_lex_cid_link;",
            "/** Typed AT Protocol blob reference. */",
            "typedef struct wf_lex_blob { const char *cid; const char *mime_type; int64_t size; } wf_lex_blob;",
            "#define WF_LEX_ARRAY(type_) struct { type_ const *items; size_t count; }", "",
        ]
        for doc in self.docs:
            nsid = doc["id"]
            main = doc.get("defs", {}).get("main", {})
            kind = main.get("type", "definition")
            symbol = snake(nsid).upper()
            out.extend(comment(main.get("description", "")))
            out.append(f'#define WF_LEX_{symbol}_NSID "{nsid}"')
            out.append(f'#define WF_LEX_{symbol}_KIND "{kind}"')
            out.append("")

        # Forward declarations allow refs to definitions emitted later.
        objects = self.object_catalog()
        unions = self.collect_unions()
        declarations = set(objects) | self.referenced_types() | set(unions)
        for name in sorted(declarations):
            out.append(f"typedef struct {name} {name};")
        if declarations:
            out.append("")
        # Union structs only contain pointers to object members plus a raw JSON
        # view, so they never need a complete object definition; emit them
        # first so object structs that embed a union value see a complete type.
        for name in sorted(unions):
            nsid, schema = unions[name]
            out.extend(self.emit_union_struct(name, nsid, schema))
        for name, (nsid, schema) in objects.items():
            out.extend(self.emit_object(nsid, name, schema))
            out.append("")
        for doc in self.docs:
            nsid = doc["id"]
            for def_name, definition in doc.get("defs", {}).items():
                if definition.get("type") not in ("query", "procedure"):
                    continue
                schema = definition.get("input", {}).get("schema")
                if not schema or schema.get("type") == "object":
                    continue
                base = type_name(nsid, def_name)
                if self.json_input_type(nsid, base, schema):
                    alias = self.json_input_alias_type(nsid, base, schema)
                    out.append(f"typedef {alias} {base}_input;")
                    out.append("")
        for doc in self.docs:
            nsid = doc["id"]
            for def_name, definition in doc.get("defs", {}).items():
                if definition.get("type") not in ("query", "procedure"):
                    continue
                base = type_name(nsid, def_name)
                input_schema = definition.get("input", {}).get("schema")
                input_type = (self.json_input_type(nsid, base, input_schema)
                              if input_schema else None)
                if input_type:
                    out.append(f"wf_status {base}_input_encode_json(")
                    out.append(f"    const {input_type} *value, char **out_json);")
                    out.append("/** Free JSON returned by the matching encoder. */")
                    out.append(f"void {base}_json_free(char *json);")
                output_schema = definition.get("output", {}).get("schema")
                if output_schema and output_schema.get("type") == "object":
                    out.append("/** Decode an owning output value; free it with the matching function. */")
                    out.append(f"wf_status {base}_output_decode_json(")
                    out.append(f"    const char *json, size_t length, {base}_output **out_value);")
                    out.append(f"void {base}_output_free({base}_output *value);")
                if definition.get("type") == "procedure":
                    if input_type:
                        out.append(f"wf_status {base}_call(wf_xrpc_client *client,")
                        out.append(f"    const {input_type} *input, wf_response *out);")
                        out.append(f"wf_status {base}_call_auth(wf_auth_client *client,")
                        out.append(f"    const {input_type} *input, wf_response *out);")
                    else:
                        out.append(f"wf_status {base}_call(wf_xrpc_client *client, wf_response *out);")
                        out.append(f"wf_status {base}_call_auth(wf_auth_client *client, wf_response *out);")
                elif definition.get("type") == "query":
                    params = definition.get("parameters")
                    has_params = params and params.get("type") == "params"
                    arg = f"const {base}_params *params, " if has_params else ""
                    out.append(f"wf_status {base}_call(wf_xrpc_client *client,")
                    out.append(f"    {arg}wf_response *out);")
                    out.append(f"wf_status {base}_call_auth(wf_auth_client *client,")
                    out.append(f"    {arg}wf_response *out);")
                out.append("")
        out.extend(["#undef WF_LEX_ARRAY", "", "#ifdef __cplusplus", "}",
                    "#endif", "", f"#endif /* {self.guard} */", ""])
        return "\n".join(out)

    def add_encoded_value(self, lines: list[str], nsid: str, owner: str,
                          prop: dict[str, Any], target: str, result: str,
                          path: str, indent: str) -> None:
        kind = prop.get("type")
        if kind == "string":
            lines.append(f"{indent}if (!{target}) goto invalid;")
            expr = f"cJSON_CreateString({target})"
        elif kind == "integer":
            expr = f"cJSON_CreateNumber((double){target})"
        elif kind == "boolean":
            expr = f"cJSON_CreateBool({target})"
        elif kind == "union":
            # Re-emit the retained raw JSON (see emit_union_struct).
            lines.append(f"{indent}if (!{target}.data) goto invalid;")
            lines.append(f'{indent}{result} = cJSON_ParseWithLength({target}.data, {target}.length);')
            lines.append(f"{indent}if (!{result}) goto invalid;")
            expr = None
        elif kind == "unknown":
            lines.append(f"{indent}if (!{target}.data) goto invalid;")
            lines.append(f'{indent}{result} = cJSON_ParseWithLength({target}.data, {target}.length);')
            lines.append(f"{indent}if (!{result}) goto invalid;")
            expr = None
        elif kind == "bytes":
            lines.append(f"{indent}status = wf_lex_bytes_encode(&{target}, &{result});")
            lines.append(f"{indent}if (status != WF_OK) goto status_fail;")
            expr = None
        elif kind == "cid-link":
            lines.append(f"{indent}status = wf_lex_cid_encode(&{target}, &{result});")
            lines.append(f"{indent}if (status != WF_OK) goto status_fail;")
            expr = None
        elif kind == "blob":
            lines.append(f"{indent}status = wf_lex_blob_encode(&{target}, &{result});")
            lines.append(f"{indent}if (status != WF_OK) goto status_fail;")
            expr = None
        elif kind == "object":
            inline = f"{owner}_{snake(path)}"
            lines.append(f"{indent}status = wf_lex_encode_{inline}(&{target}, &{result});")
            lines.append(f"{indent}if (status != WF_OK) goto status_fail;")
            expr = None
        elif kind == "ref":
            resolved = self.resolve_ref(nsid, prop["ref"])
            if not resolved:
                raise ValueError(f"cannot encode unresolved ref {prop['ref']} in {owner}")
            target_nsid, target_schema = resolved
            if target_schema.get("type") == "object":
                referenced = ref_type(nsid, prop["ref"])
                lines.append(f"{indent}if (!{target}) goto invalid;")
                lines.append(f"{indent}status = wf_lex_encode_{referenced}({target}, &{result});")
                lines.append(f"{indent}if (status != WF_OK) goto status_fail;")
            else:
                if target_schema.get("type") == "token":
                    target_schema = {"type": "string"}
                self.add_encoded_value(lines, target_nsid, owner, target_schema,
                                       target, result, path, indent)
            expr = None
        elif kind == "array":
            item_schema = prop.get("items", {})
            lines.append(f"{indent}if ({target}.count && !{target}.items) goto invalid;")
            lines.append(f"{indent}{result} = cJSON_CreateArray();")
            lines.append(f"{indent}if (!{result}) goto fail;")
            suffix = snake(path)
            index = f"i_{suffix}"
            element = f"element_{suffix}"
            lines.append(f"{indent}for (size_t {index} = 0; {index} < {target}.count; ++{index}) {{")
            lines.append(f"{indent}    cJSON *{element} = NULL;")
            self.add_encoded_value(lines, nsid, owner, item_schema,
                                   f"{target}.items[{index}]", element,
                                   path + "_item", indent + "    ")
            lines.append(f"{indent}    if (!cJSON_AddItemToArray({result}, {element})) {{")
            lines.append(f"{indent}        cJSON_Delete({element}); goto fail;")
            lines.append(f"{indent}    }}")
            lines.append(f"{indent}}}")
            expr = None
        else:
            raise ValueError(f"JSON encoding is not supported for {path} ({kind})")
        if expr:
            lines.append(f"{indent}{result} = {expr};")
            lines.append(f"{indent}if (!{result}) goto fail;")

    def add_value(self, lines: list[str], nsid: str, owner: str,
                  prop: dict[str, Any], field: str, wire_name: str,
                  indent: str = "    ") -> None:
        lines.append(f"{indent}item = NULL;")
        self.add_encoded_value(lines, nsid, owner, prop, f"value->{field}",
                               "item", field, indent)
        lines.append(f'{indent}if (!item || !cJSON_AddItemToObject(root, "{wire_name}", item)) {{ cJSON_Delete(item); item = NULL; goto fail; }}')
        lines.append(f"{indent}item = NULL;")

    def emit_object_encoder(self, nsid: str, name: str,
                            schema: dict[str, Any]) -> list[str]:
        lines = [f"static wf_status wf_lex_encode_{name}(const {name} *value, cJSON **out) {{",
                 "    if (!value || !out) return WF_ERR_INVALID_ARG;", "    *out = NULL;",
                 "    wf_status status = WF_OK;", "    (void)status;",
                 "    cJSON *root = cJSON_CreateObject();", "    cJSON *item = NULL;",
                 "    (void)item;", "    if (!root) return WF_ERR_ALLOC;"]
        required = set(schema.get("required", []))
        for wire_name, prop in schema.get("properties", {}).items():
            field = self.field_name(schema, wire_name)
            if wire_name not in required:
                lines.append(f"    if (value->has_{field}) {{")
                self.add_value(lines, nsid, name, prop, field, wire_name, "        ")
                lines.append("    }")
            else:
                self.add_value(lines, nsid, name, prop, field, wire_name)
        lines.append("    *out = root; return WF_OK;")
        if any("goto invalid;" in line for line in lines):
            lines += ["invalid:", "    cJSON_Delete(item); cJSON_Delete(root); return WF_ERR_INVALID_ARG;"]
        if any("goto fail;" in line for line in lines):
            lines += ["fail:", "    cJSON_Delete(item); cJSON_Delete(root); return WF_ERR_ALLOC;"]
        if any("goto status_fail;" in line for line in lines):
            lines += ["status_fail:", "    cJSON_Delete(item); cJSON_Delete(root); return status;"]
        lines += ["}", ""]
        return lines

    def emit_decode_value(self, lines: list[str], nsid: str, owner: str,
                           schema: dict[str, Any], target: str,
                           source: str, indent: str, field: str = "") -> None:
        kind = schema.get("type")
        if kind == "string":
            lines.append(f"{indent}if (!cJSON_IsString({source})) {{ status = WF_ERR_INVALID_ARG; goto cleanup; }}")
            lines.append(f"{indent}{target} = wf_lex_strdup({source}->valuestring);")
            lines.append(f"{indent}if (!{target}) {{ status = WF_ERR_ALLOC; goto cleanup; }}")
        elif kind == "integer":
            lines.append(f"{indent}if (!wf_lex_json_integer({source}, &{target})) {{ status = WF_ERR_INVALID_ARG; goto cleanup; }}")
        elif kind == "boolean":
            lines.append(f"{indent}if (!cJSON_IsBool({source})) {{ status = WF_ERR_INVALID_ARG; goto cleanup; }}")
            lines.append(f"{indent}{target} = cJSON_IsTrue({source});")
        elif kind == "union":
            name = self.union_name(owner, field)
            lines.append(f"{indent}status = wf_lex_decode_{name}({source}, &({target}));")
            lines.append(f"{indent}if (status != WF_OK) goto cleanup;")
        elif kind == "unknown":
            lines.append(f"{indent}status = wf_lex_json_copy({source}, &{target});")
            lines.append(f"{indent}if (status != WF_OK) goto cleanup;")
        elif kind == "bytes":
            lines.append(f"{indent}status = wf_lex_bytes_decode({source}, &{target});")
            lines.append(f"{indent}if (status != WF_OK) goto cleanup;")
        elif kind == "cid-link":
            lines.append(f"{indent}status = wf_lex_cid_decode({source}, &{target});")
            lines.append(f"{indent}if (status != WF_OK) goto cleanup;")
        elif kind == "blob":
            lines.append(f"{indent}status = wf_lex_blob_decode({source}, &{target});")
            lines.append(f"{indent}if (status != WF_OK) goto cleanup;")
        elif kind == "object":
            inline = f"{owner}_{snake(target.rsplit('->', 1)[-1].split('.')[-1])}"
            # Array elements pass an owner-specific item type through c_type.
            if target.endswith("items[i]"):
                inline = f"{owner}_array_item"
            lines.append(f"{indent}status = wf_lex_decode_{inline}({source}, &{target});")
            lines.append(f"{indent}if (status != WF_OK) goto cleanup;")
        elif kind == "ref":
            ref = ref_type(nsid, schema["ref"])
            resolved = self.resolve_ref(nsid, schema["ref"])
            if resolved and resolved[1].get("type") != "object":
                target_nsid, target_schema = resolved
                if target_schema.get("type") == "token":
                    target_schema = {"type": "string"}
                self.emit_decode_value(lines, target_nsid, owner, target_schema,
                                       target, source, indent, field)
            elif ref not in self.object_catalog():
                raise ValueError(f"cannot decode unresolved ref {schema['ref']} in {owner}")
            else:
                lines.append(f"{indent}{target} = calloc(1, sizeof(*{target}));")
                lines.append(f"{indent}if (!{target}) {{ status = WF_ERR_ALLOC; goto cleanup; }}")
                lines.append(f"{indent}status = wf_lex_decode_{ref}({source}, ({ref} *){target});")
                lines.append(f"{indent}if (status != WF_OK) goto cleanup;")
        elif kind == "array":
            item_schema = schema.get("items", {"type": "unknown"})
            array_field = target.rsplit("->", 1)[-1].split(".")[-1]
            item_field = array_field + "_item"
            item_type, _ = self.c_type(nsid, owner, item_field, item_schema)
            lines.append(f"{indent}if (!cJSON_IsArray({source})) {{ status = WF_ERR_INVALID_ARG; goto cleanup; }}")
            lines.append(f"{indent}{target}.count = (size_t)cJSON_GetArraySize({source});")
            lines.append(f"{indent}if ({target}.count) {{")
            lines.append(f"{indent}    {item_type} *items = calloc({target}.count, sizeof(*items));")
            lines.append(f"{indent}    if (!items) {{ status = WF_ERR_ALLOC; goto cleanup; }}")
            lines.append(f"{indent}    {target}.items = items;")
            lines.append(f"{indent}    for (size_t i = 0; i < {target}.count; ++i) {{")
            lines.append(f"{indent}        cJSON *element = cJSON_GetArrayItem({source}, (int)i);")
            if item_schema.get("type") == "object":
                lines.append(f"{indent}        status = wf_lex_decode_{item_type}(element, &items[i]);")
                lines.append(f"{indent}        if (status != WF_OK) goto cleanup;")
            else:
                self.emit_decode_value(lines, nsid, owner, item_schema, "items[i]",
                                       "element", indent + "        ", item_field)
            lines.append(f"{indent}    }}")
            lines.append(f"{indent}}}")
        else:
            raise ValueError(f"JSON decoding is not supported for {owner} ({kind})")

    def emit_clear_value(self, lines: list[str], nsid: str, owner: str,
                          schema: dict[str, Any], target: str,
                          indent: str, field: str = "") -> None:
        kind = schema.get("type")
        if kind == "string":
            lines.append(f"{indent}free((void *){target});")
        elif kind == "union":
            union_type = self.union_name(owner, field)
            if target.endswith("items[i]"):
                lines.append(
                    f"{indent}wf_lex_clear_{union_type}(({union_type} *)&({target}));"
                )
            else:
                lines.append(f"{indent}wf_lex_clear_{union_type}(&({target}));")
        elif kind == "unknown":
            lines.append(f"{indent}free((void *){target}.data);")
        elif kind == "bytes":
            lines.append(f"{indent}free((void *){target}.data);")
        elif kind == "cid-link":
            lines.append(f"{indent}free((void *){target}.cid);")
        elif kind == "blob":
            lines.append(f"{indent}free((void *){target}.cid);")
            lines.append(f"{indent}free((void *){target}.mime_type);")
        elif kind == "object":
            field = target.rsplit("->", 1)[-1].split(".")[-1]
            inline = f"{owner}_{snake(field)}"
            if target.endswith("items[i]"):
                inline = f"{owner}_array_item"
            lines.append(f"{indent}wf_lex_clear_{inline}(&{target});")
        elif kind == "ref":
            ref = ref_type(nsid, schema["ref"])
            resolved = self.resolve_ref(nsid, schema["ref"])
            if resolved and resolved[1].get("type") != "object":
                target_nsid, target_schema = resolved
                if target_schema.get("type") == "token":
                    target_schema = {"type": "string"}
                self.emit_clear_value(lines, target_nsid, owner, target_schema,
                                       target, indent, field)
            else:
                lines.append(f"{indent}if ({target}) {{ wf_lex_clear_{ref}(({ref} *){target}); free((void *){target}); }}")
        elif kind == "array":
            item = schema.get("items", {"type": "unknown"})
            array_field = target.rsplit("->", 1)[-1].split(".")[-1]
            item_field = array_field + "_item"
            item_type, _ = self.c_type(nsid, owner, item_field, item)
            lines.append(f"{indent}for (size_t i = 0; i < {target}.count; ++i) {{")
            if item.get("type") == "object":
                lines.append(f"{indent}    wf_lex_clear_{item_type}(({item_type} *)&{target}.items[i]);")
            else:
                self.emit_clear_value(lines, nsid, owner, item, f"{target}.items[i]",
                                      indent + "    ", item_field)
            lines.append(f"{indent}}}")
            lines.append(f"{indent}free((void *){target}.items);")

    def emit_object_decoder(self, nsid: str, name: str,
                            schema: dict[str, Any]) -> list[str]:
        lines = [f"static void wf_lex_clear_{name}({name} *value) {{",
                 "    if (!value) return;"]
        for wire_name, prop in schema.get("properties", {}).items():
            self.emit_clear_value(lines, nsid, name, prop,
                                  f"value->{self.field_name(schema, wire_name)}", "    ",
                                  self.field_name(schema, wire_name))
        lines += ["    memset(value, 0, sizeof(*value));", "}", "",
                  f"static wf_status wf_lex_decode_{name}(cJSON *node, {name} *value) {{",
                  "    wf_status status = WF_OK;",
                  "    (void)status;",
                  "    if (!cJSON_IsObject(node) || !value) return WF_ERR_INVALID_ARG;"]
        required = set(schema.get("required", []))
        for wire_name, prop in schema.get("properties", {}).items():
            field = self.field_name(schema, wire_name)
            lines += ["    {", f'        cJSON *member = cJSON_GetObjectItemCaseSensitive(node, "{wire_name}");']
            if wire_name in required:
                lines.append("        if (!member) { status = WF_ERR_INVALID_ARG; goto cleanup; }")
            else:
                lines.append("        if (member) {")
                lines.append(f"            value->has_{field} = true;")
            indent = "            " if wire_name not in required else "        "
            self.emit_decode_value(lines, nsid, name, prop, f"value->{field}", "member", indent, field)
            if wire_name not in required:
                lines.append("        }")
            lines.append("    }")
        lines.append("    return WF_OK;")
        if any("goto cleanup;" in line for line in lines):
            lines += ["cleanup:", f"    wf_lex_clear_{name}(value);",
                      "    return status;"]
        lines += ["}", ""]
        return lines

    def generate_source(self, header_name: str) -> str:
        out = [
            "/* Generated by tools/wf_lexgen.py; do not edit. */",
            f'#include "{header_name}"', "#include <cJSON.h>",
            "#include <openssl/evp.h>", "#include <inttypes.h>",
            "#include <limits.h>", "#include <math.h>", "#include <stdio.h>",
            "#include <stdlib.h>", "#include <string.h>", "",
            "#if defined(__GNUC__) || defined(__clang__)",
            "#define WF_LEX_UNUSED __attribute__((unused))", "#else",
            "#define WF_LEX_UNUSED", "#endif", "",
            "static WF_LEX_UNUSED char *wf_lex_strdup(const char *source) {",
            "    size_t length = strlen(source) + 1; char *copy = malloc(length);",
            "    if (copy) memcpy(copy, source, length);",
            "    return copy;", "}", "",
            "static WF_LEX_UNUSED bool wf_lex_json_integer(cJSON *item, int64_t *out) {",
            "    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||",
            "        item->valuedouble < -9007199254740991.0 || item->valuedouble > 9007199254740991.0 ||",
            "        (double)(int64_t)item->valuedouble != item->valuedouble) return false;",
            "    *out = (int64_t)item->valuedouble; return true;", "}", "",
            "static WF_LEX_UNUSED wf_status wf_lex_json_copy(cJSON *item, wf_lex_json *out) {",
            "    char *json = cJSON_PrintUnformatted(item); if (!json) return WF_ERR_ALLOC;",
            "    out->data = json; out->length = strlen(json); return WF_OK;", "}", "",
            "static WF_LEX_UNUSED wf_status wf_lex_bytes_encode(const wf_lex_bytes *value, cJSON **out) {",
            "    if (!value || !out || (value->length && !value->data) || value->length > INT_MAX)",
            "        return WF_ERR_INVALID_ARG;",
            "    *out = NULL;",
            "    if (value->length > (SIZE_MAX / 4) * 3 - 2) return WF_ERR_INVALID_ARG;",
            "    size_t length = 4 * ((value->length + 2) / 3);",
            "    char *encoded = malloc(length + 1); if (!encoded) return WF_ERR_ALLOC;",
            "    const uint8_t *data = value->length ? value->data : (const uint8_t *)\"\";",
            "    int written = EVP_EncodeBlock((unsigned char *)encoded, data, (int)value->length);",
            "    if (written < 0 || (size_t)written != length) { free(encoded); return WF_ERR_INVALID_ARG; }",
            "    encoded[length] = '\\0'; cJSON *root = cJSON_CreateObject();",
            "    cJSON *tag = cJSON_CreateString(encoded); free(encoded);",
            "    if (!root || !tag || !cJSON_AddItemToObject(root, \"$bytes\", tag)) {",
            "        cJSON_Delete(tag); cJSON_Delete(root); return WF_ERR_ALLOC;",
            "    }",
            "    *out = root; return WF_OK;", "}", "",
            "static WF_LEX_UNUSED wf_status wf_lex_cid_encode(const wf_lex_cid_link *value, cJSON **out) {",
            "    if (!value || !out || !value->cid || !value->cid[0]) return WF_ERR_INVALID_ARG;",
            "    *out = NULL;",
            "    cJSON *root = cJSON_CreateObject(); cJSON *link = cJSON_CreateString(value->cid);",
            "    if (!root || !link || !cJSON_AddItemToObject(root, \"$link\", link)) {",
            "        cJSON_Delete(link); cJSON_Delete(root); return WF_ERR_ALLOC;",
            "    }",
            "    *out = root; return WF_OK;", "}", "",
            "static WF_LEX_UNUSED wf_status wf_lex_blob_encode(const wf_lex_blob *value, cJSON **out) {",
            "    if (!value || !out || !value->cid || !value->cid[0] || !value->mime_type || value->size < 0)",
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
            "        cJSON_Delete(type); cJSON_Delete(ref); cJSON_Delete(mime); cJSON_Delete(size);",
            "        cJSON_Delete(root); return WF_ERR_ALLOC;",
            "    }",
            "    if (!cJSON_AddItemToObject(root, \"$type\", type)) goto blob_fail;",
            "    type = NULL;",
            "    if (!cJSON_AddItemToObject(root, \"ref\", ref)) goto blob_fail;",
            "    ref = NULL;",
            "    if (!cJSON_AddItemToObject(root, \"mimeType\", mime)) goto blob_fail;",
            "    mime = NULL;",
            "    if (!cJSON_AddItemToObject(root, \"size\", size)) goto blob_fail;",
            "    size = NULL;",
            "    *out = root; return WF_OK;",
            "blob_fail:",
            "    cJSON_Delete(type); cJSON_Delete(ref); cJSON_Delete(mime); cJSON_Delete(size);",
            "    cJSON_Delete(root); return WF_ERR_ALLOC;", "}", "",
            "static WF_LEX_UNUSED wf_status wf_lex_cid_decode(cJSON *item, wf_lex_cid_link *out) {",
            "    cJSON *link = cJSON_IsObject(item) ? cJSON_GetObjectItemCaseSensitive(item, \"$link\") : NULL;",
            "    if (!cJSON_IsString(link) || !link->valuestring[0]) return WF_ERR_INVALID_ARG;",
            "    out->cid = wf_lex_strdup(link->valuestring); return out->cid ? WF_OK : WF_ERR_ALLOC;", "}", "",
            "static WF_LEX_UNUSED wf_status wf_lex_bytes_decode(cJSON *item, wf_lex_bytes *out) {",
            "    cJSON *tag = cJSON_IsObject(item) ? cJSON_GetObjectItemCaseSensitive(item, \"$bytes\") : NULL;",
            "    if (!cJSON_IsString(tag)) return WF_ERR_INVALID_ARG;",
            "    size_t encoded = strlen(tag->valuestring);",
            "    if (encoded % 4 != 0 || encoded > (size_t)INT_MAX) return WF_ERR_INVALID_ARG;",
            "    size_t capacity = encoded / 4 * 3; uint8_t *data = capacity ? malloc(capacity) : NULL;",
            "    if (capacity && !data) return WF_ERR_ALLOC;",
            "    int decoded = encoded ? EVP_DecodeBlock(data, (const unsigned char *)tag->valuestring, (int)encoded) : 0;",
            "    if (decoded < 0) { free(data); return WF_ERR_INVALID_ARG; }",
            "    size_t padding = encoded && tag->valuestring[encoded - 1] == '=';",
            "    padding += encoded > 1 && tag->valuestring[encoded - 2] == '=';",
            "    out->data = data; out->length = (size_t)decoded - padding; return WF_OK;", "}", "",
            "static WF_LEX_UNUSED wf_status wf_lex_blob_decode(cJSON *item, wf_lex_blob *out) {",
            "    if (!cJSON_IsObject(item)) return WF_ERR_INVALID_ARG;",
            "    cJSON *type = cJSON_GetObjectItemCaseSensitive(item, \"$type\");",
            "    cJSON *ref = cJSON_GetObjectItemCaseSensitive(item, \"ref\");",
            "    cJSON *mime = cJSON_GetObjectItemCaseSensitive(item, \"mimeType\");",
            "    cJSON *size = cJSON_GetObjectItemCaseSensitive(item, \"size\"); wf_lex_cid_link link = {0};",
            "    if (!cJSON_IsString(type) || strcmp(type->valuestring, \"blob\") != 0 ||",
            "        !cJSON_IsString(mime) || !wf_lex_json_integer(size, &out->size)) return WF_ERR_INVALID_ARG;",
            "    wf_status status = wf_lex_cid_decode(ref, &link); if (status != WF_OK) return status;",
            "    out->mime_type = wf_lex_strdup(mime->valuestring);",
            "    if (!out->mime_type) { free((void *)link.cid); return WF_ERR_ALLOC; }",
            "    out->cid = link.cid; return WF_OK;", "}", "",
        ]
        catalog = self.object_catalog()
        encoders = self.encoder_objects()
        unions = self.collect_unions()
        for name in encoders:
            out.append(f"static WF_LEX_UNUSED wf_status wf_lex_encode_{name}(const {name} *value, cJSON **out);")
        for name in sorted(catalog):
            out.append(f"static WF_LEX_UNUSED void wf_lex_clear_{name}({name} *value);")
            out.append(f"static WF_LEX_UNUSED wf_status wf_lex_decode_{name}(cJSON *node, {name} *value);")
        for name in sorted(unions):
            nsid, schema = unions[name]
            out.append(f"static WF_LEX_UNUSED void wf_lex_clear_{name}({name} *value);")
            out.append(f"static WF_LEX_UNUSED wf_status wf_lex_decode_{name}(cJSON *node, {name} *value);")
        if catalog or unions:
            out.append("")
        for name, (object_nsid, object_schema) in encoders.items():
            out.extend(self.emit_object_encoder(object_nsid, name, object_schema))
        for name in sorted(catalog):
            object_nsid, object_schema = catalog[name]
            out.extend(self.emit_object_decoder(object_nsid, name, object_schema))
        for name in sorted(unions):
            nsid, schema = unions[name]
            out.extend(self.emit_union_decoder(name, nsid, schema))
            out.extend(self.emit_union_clear(name, nsid, schema))
        for doc in self.docs:
            nsid = doc["id"]
            for def_name, definition in doc.get("defs", {}).items():
                if definition.get("type") not in ("query", "procedure"):
                    continue
                base = type_name(nsid, def_name)
                output_schema = definition.get("output", {}).get("schema")
                if output_schema and output_schema.get("type") == "object":
                    out += [f"wf_status {base}_output_decode_json(",
                            f"    const char *json, size_t length, {base}_output **out_value) {{",
                            "    if (!json || !out_value) return WF_ERR_INVALID_ARG;",
                            "    *out_value = NULL; cJSON *root = cJSON_ParseWithLength(json, length);",
                            "    if (!root) return WF_ERR_INVALID_ARG;",
                            f"    {base}_output *value = calloc(1, sizeof(*value));",
                            "    if (!value) { cJSON_Delete(root); return WF_ERR_ALLOC; }",
                            f"    wf_status status = wf_lex_decode_{base}_output(root, value);",
                            "    cJSON_Delete(root);",
                            "    if (status != WF_OK) { free(value); return status; }",
                            "    *out_value = value; return WF_OK;", "}", "",
                            f"void {base}_output_free({base}_output *value) {{",
                            f"    wf_lex_clear_{base}_output(value); free(value);", "}", ""]
                schema = definition.get("input", {}).get("schema")
                input_type = (self.json_input_type(nsid, base, schema)
                              if schema else None)
                if input_type:
                    if schema.get("type") == "object":
                        encoder = base + "_input"
                    else:
                        resolved = (self.resolve_ref(nsid, schema["ref"])
                                    if schema.get("type") == "ref" else None)
                        encoder = (ref_type(nsid, schema["ref"])
                                   if resolved and resolved[1].get("type") == "object"
                                   else None)
                    function = [f"wf_status {base}_input_encode_json(",
                                f"    const {input_type} *value, char **out_json) {{",
                                "    if (!value || !out_json) return WF_ERR_INVALID_ARG;",
                                "    *out_json = NULL; cJSON *root = NULL;",
                                "    wf_status status = WF_OK;", "    (void)status;"]
                    if encoder:
                        function += [f"    status = wf_lex_encode_{encoder}(value, &root);",
                                     "    if (status != WF_OK) return status;"]
                    else:
                        self.add_encoded_value(function, nsid, base, schema, "*value",
                                               "root", "input", "    ")
                    function += ["    *out_json = cJSON_PrintUnformatted(root);", "    cJSON_Delete(root);",
                                 "    return *out_json ? WF_OK : WF_ERR_ALLOC;"]
                    if any("goto invalid;" in line for line in function):
                        function += ["invalid:", "    cJSON_Delete(root); return WF_ERR_INVALID_ARG;"]
                    if any("goto fail;" in line for line in function):
                        function += ["fail:", "    cJSON_Delete(root); return WF_ERR_ALLOC;"]
                    if any("goto status_fail;" in line for line in function):
                        function += ["status_fail:", "    cJSON_Delete(root); return status;"]
                    out += function + ["}", "", f"void {base}_json_free(char *json) {{ cJSON_free(json); }}", ""]
                if definition.get("type") == "procedure" and not definition.get("parameters"):
                    if input_type:
                        out += [f"wf_status {base}_call(wf_xrpc_client *client,",
                                f"    const {input_type} *input, wf_response *response) {{",
                                "    if (!client || !input || !response) return WF_ERR_INVALID_ARG;",
                                "    char *json = NULL;",
                                f"    wf_status status = {base}_input_encode_json(input, &json);",
                                "    if (status != WF_OK) return status;",
                                f'    status = wf_xrpc_procedure(client, "{nsid}", json, response);',
                                "    cJSON_free(json);", "    return status;", "}", ""]
                        out += [f"wf_status {base}_call_auth(wf_auth_client *client,",
                                f"    const {input_type} *input, wf_response *response) {{",
                                "    if (!client || !input || !response) return WF_ERR_INVALID_ARG;",
                                "    char *json = NULL;",
                                f"    wf_status status = {base}_input_encode_json(input, &json);",
                                "    if (status != WF_OK) return status;",
                                f'    status = wf_auth_client_procedure(client, "{nsid}", json, response);',
                                "    cJSON_free(json);", "    return status;", "}", ""]
                    else:
                        out += [f"wf_status {base}_call(wf_xrpc_client *client, wf_response *response) {{",
                                "    if (!client || !response) return WF_ERR_INVALID_ARG;",
                                f'    return wf_xrpc_procedure(client, "{nsid}", NULL, response);', "}", ""]
                        out += [f"wf_status {base}_call_auth(wf_auth_client *client, wf_response *response) {{",
                                "    if (!client || !response) return WF_ERR_INVALID_ARG;",
                                f'    return wf_auth_client_procedure(client, "{nsid}", NULL, response);', "}", ""]
            main = doc.get("defs", {}).get("main", {})
            if main.get("type") == "query":
                base = type_name(nsid, "main")
                params = main.get("parameters")
                if not params:
                    out += [f"wf_status {base}_call(wf_xrpc_client *client, wf_response *response) {{",
                            "    if (!client || !response) return WF_ERR_INVALID_ARG;",
                            f'    return wf_xrpc_query(client, "{nsid}", NULL, response);', "}", ""]
                    out += [f"wf_status {base}_call_auth(wf_auth_client *client, wf_response *response) {{",
                            "    if (!client || !response) return WF_ERR_INVALID_ARG;",
                            f'    return wf_auth_client_query(client, "{nsid}", NULL, response);', "}", ""]
                elif params.get("type") == "params":
                    props = params.get("properties", {})
                    required = set(params.get("required", []))
                    for wire_name, prop in props.items():
                        kind = prop.get("type")
                        if kind == "array":
                            item_kind = prop.get("items", {}).get("type")
                            if item_kind not in ("string", "integer", "boolean"):
                                raise ValueError(
                                    f"query parameter {wire_name} has unsupported array item type {item_kind}"
                                )
                        elif kind not in ("string", "integer", "boolean"):
                            raise ValueError(f"query parameter {wire_name} has unsupported type {kind}")
                    # Build shared param-encoding body
                    call_body: list[str] = [
                        "    if (!params || !response) return WF_ERR_INVALID_ARG;",
                        "    size_t encoded_capacity = 0, number_capacity = 0;"]
                    for wire_name, prop in props.items():
                        field = self.field_name(params, wire_name)
                        condition = None if wire_name in required else f"params->has_{field}"
                        indent = "    "
                        if condition:
                            call_body.append(f"    if ({condition}) {{")
                            indent = "        "
                        kind = prop.get("type")
                        if kind == "array":
                            item_kind = prop["items"]["type"]
                            call_body.append(f"{indent}if (params->{field}.count && !params->{field}.items) return WF_ERR_INVALID_ARG;")
                            if item_kind == "string":
                                call_body.append(f"{indent}for (size_t i = 0; i < params->{field}.count; ++i)")
                                call_body.append(f"{indent}    if (!params->{field}.items[i]) return WF_ERR_INVALID_ARG;")
                            call_body.append(f"{indent}if (params->{field}.count > SIZE_MAX - encoded_capacity) return WF_ERR_INVALID_ARG;")
                            call_body.append(f"{indent}encoded_capacity += params->{field}.count;")
                            if item_kind == "integer":
                                call_body.append(f"{indent}if (params->{field}.count > SIZE_MAX - number_capacity) return WF_ERR_INVALID_ARG;")
                                call_body.append(f"{indent}number_capacity += params->{field}.count;")
                        else:
                            if kind == "string":
                                call_body.append(f"{indent}if (!params->{field}) return WF_ERR_INVALID_ARG;")
                            call_body.append(f"{indent}if (encoded_capacity == SIZE_MAX) return WF_ERR_INVALID_ARG;")
                            call_body.append(f"{indent}++encoded_capacity;")
                            if kind == "integer":
                                call_body.append(f"{indent}if (number_capacity == SIZE_MAX) return WF_ERR_INVALID_ARG;")
                                call_body.append(f"{indent}++number_capacity;")
                        if condition:
                            call_body.append("    }")
                    call_body += ["    if (encoded_capacity > SIZE_MAX / sizeof(wf_xrpc_param) ||",
                                  "        number_capacity > SIZE_MAX / sizeof(char[32])) return WF_ERR_INVALID_ARG;",
                                  "    wf_xrpc_param *encoded = encoded_capacity ? calloc(encoded_capacity, sizeof(*encoded)) : NULL;",
                                  "    char (*number_values)[32] = number_capacity ? malloc(number_capacity * sizeof(*number_values)) : NULL;",
                                  "    if ((encoded_capacity && !encoded) || (number_capacity && !number_values)) {",
                                  "        free(encoded); free(number_values); return WF_ERR_ALLOC;",
                                  "    }", "    size_t count = 0, number_count = 0;",
                                  "    (void)number_count;"]
                    for index, (wire_name, prop) in enumerate(props.items()):
                        field = self.field_name(params, wire_name)
                        condition = None if wire_name in required else f"params->has_{field}"
                        indent = "    "
                        if condition:
                            call_body.append(f"    if ({condition}) {{")
                            indent = "        "
                        kind = prop.get("type")
                        if kind == "string":
                            value = f"params->{field}"
                        elif kind == "boolean":
                            value = f'(params->{field} ? "true" : "false")'
                        elif kind == "integer":
                            call_body.append(f'{indent}snprintf(number_values[number_count], sizeof(number_values[number_count]), "%' + '" PRId64, params->' + field + ");")
                            value = "number_values[number_count++]"
                        else:
                            item_kind = prop["items"]["type"]
                            call_body.append(f"{indent}for (size_t i = 0; i < params->{field}.count; ++i) {{")
                            if item_kind == "string":
                                value = f"params->{field}.items[i]"
                            elif item_kind == "boolean":
                                value = f'(params->{field}.items[i] ? "true" : "false")'
                            else:
                                call_body.append(f'{indent}    snprintf(number_values[number_count], sizeof(number_values[number_count]), "%' + '" PRId64, params->' + field + ".items[i]);")
                                value = "number_values[number_count++]"
                            call_body.append(f'{indent}    encoded[count++] = (wf_xrpc_param){{"{wire_name}", {value}}};')
                            call_body.append(f"{indent}}}")
                            if condition:
                                call_body.append("    }")
                            continue
                        call_body.append(f'{indent}encoded[count++] = (wf_xrpc_param){{"{wire_name}", {value}}};')
                        if condition:
                            call_body.append("    }")
                    # _call variant (wf_xrpc)
                    out += [f"wf_status {base}_call(wf_xrpc_client *client,",
                            f"    const {base}_params *params, wf_response *response) {{",
                            "    if (!client) return WF_ERR_INVALID_ARG;"]
                    out += call_body
                    out += [f'    wf_status status = wf_xrpc_query_params(client, "{nsid}", encoded, count, response);',
                            "    free(encoded); free(number_values); return status;",
                            "}", ""]
                    # _call_auth variant (wf_auth_client)
                    out += [f"wf_status {base}_call_auth(wf_auth_client *client,",
                            f"    const {base}_params *params, wf_response *response) {{",
                            "    if (!client) return WF_ERR_INVALID_ARG;"]
                    out += call_body
                    out += [f'    wf_status status = wf_auth_client_query_params(client, "{nsid}", encoded, count, response);',
                            "    free(encoded); free(number_values); return status;",
                            "}", ""]
        return "\n".join(out)


def load(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as stream:
        doc = json.load(stream)
    if doc.get("lexicon") != 1 or not isinstance(doc.get("id"), str):
        raise ValueError(f"{path}: expected a Lexicon 1 document with an id")
    if not isinstance(doc.get("defs"), dict):
        raise ValueError(f"{path}: expected a defs object")
    return doc


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("lexicons", nargs="+", type=Path)
    parser.add_argument("-o", "--output", type=Path,
                        help="output header (stdout when omitted)")
    parser.add_argument("--source-output", type=Path,
                        help="also write JSON codecs and XRPC wrappers")
    parser.add_argument("--guard", default="WOLFRAM_GENERATED_LEXICONS_H")
    parser.add_argument("--header-rel", default=None,
                        help="include path for the header (e.g. wolfram/foo.h)")
    args = parser.parse_args(argv)
    try:
        generator = Generator([load(path) for path in args.lexicons], args.guard)
        result = generator.generate()
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(result, encoding="utf-8")
        else:
            sys.stdout.write(result)
        if args.source_output:
            if not args.output:
                raise ValueError("--source-output requires --output")
            args.source_output.parent.mkdir(parents=True, exist_ok=True)
            header_name = args.header_rel or args.output.name
            args.source_output.write_text(
                generator.generate_source(header_name), encoding="utf-8")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
