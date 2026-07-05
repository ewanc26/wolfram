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


def snake(value: str) -> str:
    value = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", value)
    value = re.sub(r"[^A-Za-z0-9]+", "_", value).strip("_").lower()
    if not value:
        value = "value"
    if value[0].isdigit() or value in C_KEYWORDS or keyword.iskeyword(value):
        value += "_"
    return value


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
        base = snake(wire_name)
        required = set(schema.get("required", []))
        presence = {"has_" + snake(name) for name in schema.get("properties", {})
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
        # Unions and unknown values retain their encoded JSON representation.
        if kind in ("union", "unknown"):
            return "wf_lex_json", False
        if kind == "array":
            item_type, _ = self.c_type(nsid, owner, field + "_item",
                                       schema.get("items", {"type": "unknown"}))
            return f"WF_LEX_ARRAY({item_type})", False
        return "wf_lex_json", False

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

        def visit(name: str, schema: dict[str, Any]) -> None:
            if name in wanted:
                return
            wanted.add(name)
            for wire_name, prop in schema.get("properties", {}).items():
                field = self.field_name(schema, wire_name)
                value = prop
                suffix = field
                while value.get("type") == "array":
                    value = value.get("items", {"type": "unknown"})
                    suffix += "_item"
                if value.get("type") == "object":
                    child = f"{name}_{snake(suffix)}"
                    visit(child, catalog[child][1])

        for doc in self.docs:
            for def_name, definition in doc.get("defs", {}).items():
                schema = definition.get("input", {}).get("schema", {})
                if definition.get("type") in ("query", "procedure") and schema.get("type") == "object":
                    root = type_name(doc["id"], def_name) + "_input"
                    visit(root, schema)
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
            "#include <wolfram/xrpc.h>", "",
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
        declarations = set(objects) | self.referenced_types()
        for name in sorted(declarations):
            out.append(f"typedef struct {name} {name};")
        if declarations:
            out.append("")
        for name, (nsid, schema) in objects.items():
            out.extend(self.emit_object(nsid, name, schema))
            out.append("")
        for doc in self.docs:
            nsid = doc["id"]
            for def_name, definition in doc.get("defs", {}).items():
                if definition.get("type") not in ("query", "procedure"):
                    continue
                base = type_name(nsid, def_name)
                input_schema = definition.get("input", {}).get("schema")
                if input_schema and input_schema.get("type") == "object":
                    out.append(f"wf_status {base}_input_encode_json(")
                    out.append(f"    const {base}_input *value, char **out_json);")
                    out.append("/** Free JSON returned by the matching encoder. */")
                    out.append(f"void {base}_json_free(char *json);")
                output_schema = definition.get("output", {}).get("schema")
                if output_schema and output_schema.get("type") == "object":
                    out.append("/** Decode an owning output value; free it with the matching function. */")
                    out.append(f"wf_status {base}_output_decode_json(")
                    out.append(f"    const char *json, size_t length, {base}_output **out_value);")
                    out.append(f"void {base}_output_free({base}_output *value);")
                if (definition.get("type") == "procedure" and
                        not definition.get("parameters") and input_schema and
                        input_schema.get("type") == "object"):
                    out.append(f"wf_status {base}_call(wf_xrpc_client *client,")
                    out.append(f"    const {base}_input *input, wf_response *out);")
                if definition.get("type") == "query":
                    params = definition.get("parameters")
                    out.append(f"wf_status {base}_call(wf_xrpc_client *client,")
                    if params and params.get("type") == "params":
                        out.append(f"    const {base}_params *params, wf_response *out);")
                    else:
                        out.append("    wf_response *out);")
                out.append("")
        out.extend(["#undef WF_LEX_ARRAY", "", "#ifdef __cplusplus", "}",
                    "#endif", "", f"#endif /* {self.guard} */", ""])
        return "\n".join(out)

    def add_value(self, lines: list[str], nsid: str, owner: str,
                  prop: dict[str, Any], field: str, wire_name: str,
                  indent: str = "    ") -> None:
        kind = prop.get("type")
        target = f"value->{field}"
        if kind == "string":
            lines.append(f"{indent}if (!{target}) goto invalid;")
            expr = f"cJSON_CreateString({target})"
        elif kind == "integer":
            expr = f"cJSON_CreateNumber((double){target})"
        elif kind == "boolean":
            expr = f"cJSON_CreateBool({target})"
        elif kind in ("unknown", "union"):
            lines.append(f"{indent}if (!{target}.data) goto invalid;")
            lines.append(f'{indent}item = cJSON_ParseWithLength({target}.data, {target}.length);')
            expr = None
        elif kind == "object":
            inline = f"{owner}_{snake(field)}"
            lines.append(f"{indent}if (wf_lex_encode_{inline}(&{target}, &item) != WF_OK) goto invalid;")
            expr = None
        elif kind == "array" and prop.get("items", {}).get("type") in ("string", "object"):
            item_schema = prop.get("items", {})
            lines.append(f"{indent}if ({target}.count && !{target}.items) goto invalid;")
            lines.append(f"{indent}item = cJSON_CreateArray();")
            lines.append(f"{indent}if (item) for (size_t i = 0; i < {target}.count; ++i) {{")
            if item_schema.get("type") == "string":
                lines.append(f"{indent}    if (!{target}.items[i]) {{ cJSON_Delete(item); item = NULL; break; }}")
                lines.append(f"{indent}    cJSON *element = cJSON_CreateString({target}.items[i]);")
            else:
                inline = f"{owner}_{snake(field)}_item"
                lines.append(f"{indent}    cJSON *element = NULL;")
                lines.append(f"{indent}    if (wf_lex_encode_{inline}(&{target}.items[i], &element) != WF_OK) {{ cJSON_Delete(item); item = NULL; break; }}")
            lines.append(f"{indent}    if (!element || !cJSON_AddItemToArray(item, element)) {{ cJSON_Delete(element); cJSON_Delete(item); item = NULL; break; }}")
            lines.append(f"{indent}}}")
            expr = None
        else:
            raise ValueError(f"JSON encoding is not supported for field {wire_name} ({kind})")
        if expr:
            lines.append(f"{indent}item = {expr};")
        lines.append(f'{indent}if (!item || !cJSON_AddItemToObject(root, "{wire_name}", item)) {{ cJSON_Delete(item); goto fail; }}')

    def emit_object_encoder(self, nsid: str, name: str,
                            schema: dict[str, Any]) -> list[str]:
        lines = [f"static wf_status wf_lex_encode_{name}(const {name} *value, cJSON **out) {{",
                 "    if (!value || !out) return WF_ERR_INVALID_ARG;", "    *out = NULL;",
                 "    cJSON *root = cJSON_CreateObject();", "    cJSON *item = NULL;",
                 "    if (!root) return WF_ERR_ALLOC;"]
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
            lines += ["invalid:", "    cJSON_Delete(root); return WF_ERR_INVALID_ARG;"]
        if any("goto fail;" in line for line in lines):
            lines += ["fail:", "    cJSON_Delete(root); return WF_ERR_ALLOC;"]
        lines += ["}", ""]
        return lines

    def emit_decode_value(self, lines: list[str], nsid: str, owner: str,
                          schema: dict[str, Any], target: str,
                          source: str, indent: str) -> None:
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
        elif kind in ("unknown", "union"):
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
                                       target, source, indent)
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
            item_type, _ = self.c_type(nsid, owner, array_field + "_item", item_schema)
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
                self.emit_decode_value(lines, nsid, owner, item_schema, "items[i]", "element", indent + "        ")
            lines.append(f"{indent}    }}")
            lines.append(f"{indent}}}")
        else:
            raise ValueError(f"JSON decoding is not supported for {owner} ({kind})")

    def emit_clear_value(self, lines: list[str], nsid: str, owner: str,
                         schema: dict[str, Any], target: str,
                         indent: str) -> None:
        kind = schema.get("type")
        if kind == "string":
            lines.append(f"{indent}free((void *){target});")
        elif kind in ("unknown", "union"):
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
                                      target, indent)
            else:
                lines.append(f"{indent}if ({target}) {{ wf_lex_clear_{ref}(({ref} *){target}); free((void *){target}); }}")
        elif kind == "array":
            item = schema.get("items", {"type": "unknown"})
            array_field = target.rsplit("->", 1)[-1].split(".")[-1]
            item_type, _ = self.c_type(nsid, owner, array_field + "_item", item)
            lines.append(f"{indent}for (size_t i = 0; i < {target}.count; ++i) {{")
            if item.get("type") == "object":
                lines.append(f"{indent}    wf_lex_clear_{item_type}(({item_type} *)&{target}.items[i]);")
            else:
                self.emit_clear_value(lines, nsid, owner, item, f"{target}.items[i]", indent + "    ")
            lines.append(f"{indent}}}")
            lines.append(f"{indent}free((void *){target}.items);")

    def emit_object_decoder(self, nsid: str, name: str,
                            schema: dict[str, Any]) -> list[str]:
        lines = [f"static void wf_lex_clear_{name}({name} *value) {{",
                 "    if (!value) return;"]
        for wire_name, prop in schema.get("properties", {}).items():
            self.emit_clear_value(lines, nsid, name, prop,
                                  f"value->{self.field_name(schema, wire_name)}", "    ")
        lines += ["    memset(value, 0, sizeof(*value));", "}", "",
                  f"static wf_status wf_lex_decode_{name}(cJSON *node, {name} *value) {{",
                  "    wf_status status = WF_OK;",
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
            self.emit_decode_value(lines, nsid, name, prop, f"value->{field}", "member", indent)
            if wire_name not in required:
                lines.append("        }")
            lines.append("    }")
        lines += ["    return WF_OK;", "cleanup:", f"    wf_lex_clear_{name}(value);",
                  "    return status;", "}", ""]
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
            "        trunc(item->valuedouble) != item->valuedouble) return false;",
            "    *out = (int64_t)item->valuedouble; return true;", "}", "",
            "static WF_LEX_UNUSED wf_status wf_lex_json_copy(cJSON *item, wf_lex_json *out) {",
            "    char *json = cJSON_PrintUnformatted(item); if (!json) return WF_ERR_ALLOC;",
            "    out->data = json; out->length = strlen(json); return WF_OK;", "}", "",
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
        for name in encoders:
            out.append(f"static WF_LEX_UNUSED wf_status wf_lex_encode_{name}(const {name} *value, cJSON **out);")
        for name in sorted(catalog):
            out.append(f"static WF_LEX_UNUSED void wf_lex_clear_{name}({name} *value);")
            out.append(f"static WF_LEX_UNUSED wf_status wf_lex_decode_{name}(cJSON *node, {name} *value);")
        if catalog:
            out.append("")
        for name, (object_nsid, object_schema) in encoders.items():
            out.extend(self.emit_object_encoder(object_nsid, name, object_schema))
        for name in sorted(catalog):
            object_nsid, object_schema = catalog[name]
            out.extend(self.emit_object_decoder(object_nsid, name, object_schema))
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
                if not schema or schema.get("type") != "object":
                    continue
                out += [f"wf_status {base}_input_encode_json(",
                        f"    const {base}_input *value, char **out_json) {{",
                        "    if (!value || !out_json) return WF_ERR_INVALID_ARG;",
                        "    *out_json = NULL;", "    cJSON *root = cJSON_CreateObject();",
                        "    cJSON *item = NULL;", "    if (!root) return WF_ERR_ALLOC;"]
                required = set(schema.get("required", []))
                for wire_name, prop in schema.get("properties", {}).items():
                    field = self.field_name(schema, wire_name)
                    if wire_name not in required:
                        out.append(f"    if (value->has_{field}) {{")
                        self.add_value(out, nsid, base + "_input", prop, field,
                                       wire_name, "        ")
                        out.append("    }")
                    else:
                        self.add_value(out, nsid, base + "_input", prop, field,
                                       wire_name)
                out += ["    *out_json = cJSON_PrintUnformatted(root);", "    cJSON_Delete(root);",
                        "    return *out_json ? WF_OK : WF_ERR_ALLOC;", "invalid:",
                        "    cJSON_Delete(root);", "    return WF_ERR_INVALID_ARG;", "fail:",
                        "    cJSON_Delete(root);", "    return WF_ERR_ALLOC;", "}", "",
                        f"void {base}_json_free(char *json) {{ cJSON_free(json); }}", ""]
                if definition.get("type") == "procedure" and not definition.get("parameters"):
                    out += [f"wf_status {base}_call(wf_xrpc_client *client,",
                            f"    const {base}_input *input, wf_response *response) {{",
                            "    if (!client || !input || !response) return WF_ERR_INVALID_ARG;",
                            "    char *json = NULL;",
                            f"    wf_status status = {base}_input_encode_json(input, &json);",
                            "    if (status != WF_OK) return status;",
                            f'    status = wf_xrpc_procedure(client, "{nsid}", json, response);',
                            "    cJSON_free(json);", "    return status;", "}", ""]
            main = doc.get("defs", {}).get("main", {})
            if main.get("type") == "query":
                base = type_name(nsid, "main")
                params = main.get("parameters")
                if not params:
                    out += [f"wf_status {base}_call(wf_xrpc_client *client, wf_response *response) {{",
                            "    if (!client || !response) return WF_ERR_INVALID_ARG;",
                            f'    return wf_xrpc_query(client, "{nsid}", NULL, response);', "}", ""]
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
                    out += [f"wf_status {base}_call(wf_xrpc_client *client,",
                            f"    const {base}_params *params, wf_response *response) {{",
                            "    if (!client || !params || !response) return WF_ERR_INVALID_ARG;",
                            "    size_t encoded_capacity = 0, number_capacity = 0;"]
                    # Determine exact storage before populating it. Array parameters
                    # become repeated name/value pairs, matching URLSearchParams and
                    # the reference atproto XRPC client.
                    for wire_name, prop in props.items():
                        field = self.field_name(params, wire_name)
                        condition = None if wire_name in required else f"params->has_{field}"
                        indent = "    "
                        if condition:
                            out.append(f"    if ({condition}) {{")
                            indent = "        "
                        kind = prop.get("type")
                        if kind == "array":
                            item_kind = prop["items"]["type"]
                            out.append(f"{indent}if (params->{field}.count && !params->{field}.items) return WF_ERR_INVALID_ARG;")
                            if item_kind == "string":
                                out.append(f"{indent}for (size_t i = 0; i < params->{field}.count; ++i)")
                                out.append(f"{indent}    if (!params->{field}.items[i]) return WF_ERR_INVALID_ARG;")
                            out.append(f"{indent}if (params->{field}.count > SIZE_MAX - encoded_capacity) return WF_ERR_INVALID_ARG;")
                            out.append(f"{indent}encoded_capacity += params->{field}.count;")
                            if item_kind == "integer":
                                out.append(f"{indent}if (params->{field}.count > SIZE_MAX - number_capacity) return WF_ERR_INVALID_ARG;")
                                out.append(f"{indent}number_capacity += params->{field}.count;")
                        else:
                            if kind == "string":
                                out.append(f"{indent}if (!params->{field}) return WF_ERR_INVALID_ARG;")
                            out.append(f"{indent}if (encoded_capacity == SIZE_MAX) return WF_ERR_INVALID_ARG;")
                            out.append(f"{indent}++encoded_capacity;")
                            if kind == "integer":
                                out.append(f"{indent}if (number_capacity == SIZE_MAX) return WF_ERR_INVALID_ARG;")
                                out.append(f"{indent}++number_capacity;")
                        if condition:
                            out.append("    }")
                    out += ["    if (encoded_capacity > SIZE_MAX / sizeof(wf_xrpc_param) ||",
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
                            out.append(f"    if ({condition}) {{")
                            indent = "        "
                        kind = prop.get("type")
                        if kind == "string":
                            value = f"params->{field}"
                        elif kind == "boolean":
                            value = f'(params->{field} ? "true" : "false")'
                        elif kind == "integer":
                            out.append(f'{indent}snprintf(number_values[number_count], sizeof(number_values[number_count]), "%' + '" PRId64, params->' + field + ");")
                            value = "number_values[number_count++]"
                        else:
                            item_kind = prop["items"]["type"]
                            out.append(f"{indent}for (size_t i = 0; i < params->{field}.count; ++i) {{")
                            if item_kind == "string":
                                value = f"params->{field}.items[i]"
                            elif item_kind == "boolean":
                                value = f'(params->{field}.items[i] ? "true" : "false")'
                            else:
                                out.append(f'{indent}    snprintf(number_values[number_count], sizeof(number_values[number_count]), "%' + '" PRId64, params->' + field + ".items[i]);")
                                value = "number_values[number_count++]"
                            out.append(f'{indent}    encoded[count++] = (wf_xrpc_param){{"{wire_name}", {value}}};')
                            out.append(f"{indent}}}")
                            if condition:
                                out.append("    }")
                            continue
                        out.append(f'{indent}encoded[count++] = (wf_xrpc_param){{"{wire_name}", {value}}};')
                        if condition:
                            out.append("    }")
                    out += [f'    wf_status status = wf_xrpc_query_params(client, "{nsid}", encoded, count, response);',
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
            args.source_output.write_text(generator.generate_source(args.output.name), encoding="utf-8")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
