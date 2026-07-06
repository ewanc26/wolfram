import subprocess
import sys
import tempfile
import unittest
import os
import shlex
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "tools" / "wf_lexgen.py"
FIXTURE = ROOT / "test" / "fixtures" / "lexicons" / "com.example.echo.json"
QUERY_FIXTURE = ROOT / "test" / "fixtures" / "lexicons" / "com.example.get.json"
INLINE_FIXTURE = ROOT / "test" / "fixtures" / "lexicons" / "com.example.inline.json"
REFS_FIXTURE = ROOT / "test" / "fixtures" / "lexicons" / "com.example.refs.json"


class LexgenTests(unittest.TestCase):
    def generate(self, *inputs: Path) -> str:
        result = subprocess.run(
            [sys.executable, str(GENERATOR), *(str(path) for path in inputs)],
            check=True, capture_output=True, text=True,
        )
        return result.stdout

    def test_generates_endpoint_and_named_types(self):
        header = self.generate(FIXTURE)
        self.assertIn('#define WF_LEX_COM_EXAMPLE_ECHO_NSID "com.example.echo"', header)
        self.assertIn('#define WF_LEX_COM_EXAMPLE_ECHO_KIND "procedure"', header)
        self.assertIn("wf_lex_com_example_echo_main_input", header)
        self.assertIn("const char * message;", header)
        self.assertIn("int64_t attempts;", header)
        self.assertIn("bool has_enabled;", header)
        self.assertIn("bool has_tags;", header)
        self.assertIn("WF_LEX_ARRAY(const char *) tags;", header)
        self.assertIn("wf_lex_json metadata;", header)
        self.assertIn("wf_lex_com_example_echo_named", header)

    def test_output_is_deterministic(self):
        self.assertEqual(self.generate(FIXTURE), self.generate(FIXTURE))

    def test_generated_header_compiles_as_c11(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            header = directory / "generated.h"
            source = directory / "check.c"
            subprocess.run(
                [sys.executable, str(GENERATOR), str(FIXTURE), "-o", str(header)],
                check=True,
            )
            source.write_text(
                '#include "generated.h"\n'
                "int main(void) {\n"
                "  wf_lex_com_example_echo_main_input value = {0};\n"
                "  value.message = \"hello\";\n"
                "  return value.message == 0;\n"
                "}\n",
                encoding="utf-8",
            )
            subprocess.run(
                ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                 "-I", str(ROOT / "include"), str(source)], check=True,
            )

    def test_inline_objects_are_dependency_safe_and_deterministic(self):
        header = self.generate(INLINE_FIXTURE)
        nested = "typedef struct wf_lex_com_example_inline_main_input_config_nested {"
        parent = "typedef struct wf_lex_com_example_inline_main_input_config {"
        self.assertLess(header.index(nested), header.index(parent))
        self.assertIn("WF_LEX_ARRAY(wf_lex_com_example_inline_main_input_entries_item) entries;", header)
        self.assertEqual(header, self.generate(INLINE_FIXTURE))

    def test_inline_object_codecs_run(self):
        cjson_include = ROOT / "build" / "_deps" / "cjson-src"
        cjson_lib = ROOT / "build" / "_deps" / "cjson-build"
        if not (cjson_include / "cJSON.h").exists():
            self.skipTest("configured cJSON dependency is not available")
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            header = directory / "generated.h"
            generated = directory / "generated.c"
            check = directory / "check.c"
            executable = directory / "check"
            subprocess.run([sys.executable, str(GENERATOR), str(INLINE_FIXTURE),
                            "-o", str(header), "--source-output", str(generated)], check=True)
            check.write_text(r'''#include "generated.h"
#include <assert.h>
#include <string.h>
wf_status wf_xrpc_procedure(wf_xrpc_client *client, const char *nsid,
                            const char *json, wf_response *out) {
    (void)client; (void)nsid; (void)json; (void)out; return WF_OK;
}
wf_status wf_auth_client_procedure(wf_auth_client *client, const char *nsid,
                                    const char *json, wf_response *out) {
    (void)client; (void)nsid; (void)json; (void)out; return WF_OK;
}
int main(void) {
    wf_lex_com_example_inline_main_input_entries_item entries[] = {{2}, {3}};
    wf_lex_com_example_inline_main_input input = {0};
    input.config.name = "demo"; input.config.nested.enabled = true;
    input.entries.items = entries; input.entries.count = 2;
    char *json = NULL;
    assert(wf_lex_com_example_inline_main_input_encode_json(&input, &json) == WF_OK);
    assert(strcmp(json, "{\"config\":{\"name\":\"demo\",\"nested\":{\"enabled\":true}},\"entries\":[{\"count\":2},{\"count\":3}]}") == 0);
    wf_lex_com_example_inline_main_json_free(json);
    const char body[] = "{\"result\":{\"values\":[{\"label\":\"one\"},{\"label\":\"two\"}]}}";
    wf_lex_com_example_inline_main_output *output = NULL;
    assert(wf_lex_com_example_inline_main_output_decode_json(body, strlen(body), &output) == WF_OK);
    assert(output->result.values.count == 2);
    assert(strcmp(output->result.values.items[1].label, "two") == 0);
    wf_lex_com_example_inline_main_output_free(output);
    return 0;
}
''', encoding="utf-8")
            openssl = shlex.split(subprocess.run(
                ["pkg-config", "--cflags", "--libs", "openssl"], check=True,
                capture_output=True, text=True).stdout)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(directory), "-I", str(ROOT / "include"),
                            "-I", str(cjson_include), str(generated), str(check),
                            "-L", str(cjson_lib), "-lcjson", *openssl,
                            "-o", str(executable)], check=True)
            env = os.environ.copy()
            env["DYLD_LIBRARY_PATH"] = str(cjson_lib)
            subprocess.run([str(executable)], check=True, env=env)

    def test_referenced_inputs_and_all_json_value_kinds_run(self):
        cjson_include = ROOT / "build" / "_deps" / "cjson-src"
        cjson_lib = ROOT / "build" / "_deps" / "cjson-build"
        if not (cjson_include / "cJSON.h").exists():
            self.skipTest("configured cJSON dependency is not available")
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            header = directory / "generated.h"
            generated = directory / "generated.c"
            check = directory / "check.c"
            executable = directory / "check"
            subprocess.run([sys.executable, str(GENERATOR), str(REFS_FIXTURE),
                            "-o", str(header), "--source-output", str(generated)],
                           check=True)
            check.write_text(r'''#include "generated.h"
#include <assert.h>
#include <string.h>

wf_status wf_xrpc_procedure(wf_xrpc_client *client, const char *nsid,
                            const char *json, wf_response *out) {
    (void)client; (void)nsid; (void)json; (void)out; return WF_OK;
}
wf_status wf_auth_client_procedure(wf_auth_client *client, const char *nsid,
                                    const char *json, wf_response *out) {
    (void)client; (void)nsid; (void)json; (void)out; return WF_OK;
}

int main(void) {
    const wf_lex_json metadata = {"{\"n\":1}", 7};
    const wf_lex_com_example_refs_config first = {"one", metadata};
    const wf_lex_com_example_refs_config second = {"two", metadata};
    const wf_lex_com_example_refs_config *configs[] = {&first, &second};
    const char *names[] = {"alice", "bob"};
    const int64_t counts[] = {-1, 2};
    const bool switches[] = {true, false};
    const uint8_t payload[] = {1, 2, 3};
    wf_lex_com_example_refs_main_input input = {0};
    input.config = &first;
    input.configs.items = configs; input.configs.count = 2;
    input.names.items = names; input.names.count = 2;
    input.mode = "fast"; input.token = "com.example.refs#token";
    input.counts.items = counts; input.counts.count = 2;
    input.switches.items = switches; input.switches.count = 2;
    input.payload.data = payload; input.payload.length = sizeof(payload);
    input.link.cid = "bafy-link";
    input.blob = (wf_lex_blob){"bafy-blob", "image/png", 42};
    char *json = NULL;
    assert(wf_lex_com_example_refs_main_input_encode_json(&input, &json) == WF_OK);
    assert(strcmp(json, "{\"config\":{\"name\":\"one\",\"metadata\":{\"n\":1}},\"configs\":[{\"name\":\"one\",\"metadata\":{\"n\":1}},{\"name\":\"two\",\"metadata\":{\"n\":1}}],\"names\":[\"alice\",\"bob\"],\"mode\":\"fast\",\"token\":\"com.example.refs#token\",\"counts\":[-1,2],\"switches\":[true,false],\"payload\":{\"$bytes\":\"AQID\"},\"link\":{\"$link\":\"bafy-link\"},\"blob\":{\"$type\":\"blob\",\"ref\":{\"$link\":\"bafy-blob\"},\"mimeType\":\"image/png\",\"size\":42}}") == 0);
    wf_lex_com_example_refs_main_json_free(json);
    input.config = NULL;
    assert(wf_lex_com_example_refs_main_input_encode_json(&input, &json) == WF_ERR_INVALID_ARG);
    return 0;
}
''', encoding="utf-8")
            openssl = shlex.split(subprocess.run(
                ["pkg-config", "--cflags", "--libs", "openssl"], check=True,
                capture_output=True, text=True).stdout)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(directory), "-I", str(ROOT / "include"),
                            "-I", str(cjson_include), str(generated), str(check),
                            "-L", str(cjson_lib), "-lcjson", *openssl,
                            "-o", str(executable)], check=True)
            env = os.environ.copy()
            env["DYLD_LIBRARY_PATH"] = str(cjson_lib)
            subprocess.run([str(executable)], check=True, env=env)

    def test_generates_json_codec_and_transport_wrapper(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            header = directory / "generated.h"
            source = directory / "generated.c"
            subprocess.run(
                [sys.executable, str(GENERATOR), str(FIXTURE), "-o", str(header),
                 "--source-output", str(source)], check=True,
            )
            generated = source.read_text(encoding="utf-8")
            self.assertIn("cJSON_PrintUnformatted", generated)
            self.assertIn("_output_decode_json", generated)
            self.assertIn('cJSON_GetObjectItemCaseSensitive(item, "$bytes")', generated)
            self.assertIn('cJSON_GetObjectItemCaseSensitive(item, "$link")', generated)
            self.assertIn('wf_xrpc_procedure(client, "com.example.echo"', generated)
            subprocess.run(
                [sys.executable, str(GENERATOR), str(FIXTURE), "-o", str(header),
                 "--source-output", str(source)], check=True,
            )
            self.assertEqual(generated, source.read_text(encoding="utf-8"))

    def test_query_wrapper_uses_encoded_xrpc_parameters(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            header = directory / "generated.h"
            source = directory / "generated.c"
            subprocess.run(
                [sys.executable, str(GENERATOR), str(QUERY_FIXTURE), "-o", str(header),
                 "--source-output", str(source)], check=True,
            )
            generated = source.read_text(encoding="utf-8")
            self.assertIn('wf_xrpc_query_params(client, "com.example.get"', generated)
            self.assertIn('encoded[count++] = (wf_xrpc_param){"limit", number_values[number_count++]}', generated)
            self.assertIn('encoded[count++] = (wf_xrpc_param){"dids", params->dids.items[i]}', generated)
            self.assertIn('encoded[count++] = (wf_xrpc_param){"ids", number_values[number_count++]}', generated)
            self.assertIn('encoded[count++] = (wf_xrpc_param){"flags", (params->flags.items[i] ? "true" : "false")}', generated)

    def test_query_array_wrapper_runs_with_repeated_keys(self):
        cjson_include = ROOT / "build" / "_deps" / "cjson-src"
        cjson_lib = ROOT / "build" / "_deps" / "cjson-build"
        if not (cjson_include / "cJSON.h").exists():
            self.skipTest("configured cJSON dependency is not available")
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            header = directory / "generated.h"
            generated = directory / "generated.c"
            check = directory / "check.c"
            executable = directory / "check"
            subprocess.run(
                [sys.executable, str(GENERATOR), str(QUERY_FIXTURE), "-o", str(header),
                 "--source-output", str(generated)], check=True,
            )
            check.write_text(r'''#include "generated.h"
#include <assert.h>
#include <string.h>

wf_status wf_xrpc_query_params(wf_xrpc_client *client, const char *nsid,
                                const wf_xrpc_param *params, size_t count,
                                wf_response *out) {
    const char *names[] = {"name", "limit", "dids", "dids", "ids", "ids", "flags", "flags"};
    const char *values[] = {"alice", "42", "did:plc:a", "did:plc:b", "-7", "9", "true", "false"};
    assert(client && out && strcmp(nsid, "com.example.get") == 0 && count == 8);
    for (size_t i = 0; i < count; ++i) {
        assert(strcmp(params[i].name, names[i]) == 0);
        assert(strcmp(params[i].value, values[i]) == 0);
    }
    return WF_OK;
}
wf_status wf_auth_client_query_params(wf_auth_client *client, const char *nsid,
                                       const wf_xrpc_param *params, size_t count,
                                       wf_response *out) {
    (void)client; (void)nsid; (void)params; (void)count; (void)out; return WF_OK;
}

int main(void) {
    const char *dids[] = {"did:plc:a", "did:plc:b"};
    int64_t ids[] = {-7, 9};
    bool flags[] = {true, false};
    wf_lex_com_example_get_main_params params = {0};
    params.name = "alice"; params.dids.items = dids; params.dids.count = 2;
    params.has_limit = true; params.limit = 42;
    params.has_ids = true; params.ids.items = ids; params.ids.count = 2;
    params.has_flags = true; params.flags.items = flags; params.flags.count = 2;
    wf_response response = {0};
    assert(wf_lex_com_example_get_main_call((wf_xrpc_client *)1, &params, &response) == WF_OK);
    dids[1] = NULL;
    assert(wf_lex_com_example_get_main_call((wf_xrpc_client *)1, &params, &response) == WF_ERR_INVALID_ARG);
    dids[1] = "did:plc:b";
    params.dids.items = NULL;
    assert(wf_lex_com_example_get_main_call((wf_xrpc_client *)1, &params, &response) == WF_ERR_INVALID_ARG);
    return 0;
}
''', encoding="utf-8")
            openssl = shlex.split(subprocess.run(
                ["pkg-config", "--cflags", "--libs", "openssl"],
                check=True, capture_output=True, text=True,
            ).stdout)
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(directory), "-I", str(ROOT / "include"),
                "-I", str(cjson_include), str(generated), str(check),
                "-L", str(cjson_lib), "-lcjson", *openssl, "-o", str(executable),
            ], check=True)
            env = os.environ.copy()
            env["DYLD_LIBRARY_PATH"] = str(cjson_lib)
            subprocess.run([str(executable)], check=True, env=env)

    def test_generated_codec_and_wrapper_run(self):
        cjson_include = ROOT / "build" / "_deps" / "cjson-src"
        cjson_lib = ROOT / "build" / "_deps" / "cjson-build"
        if not (cjson_include / "cJSON.h").exists():
            self.skipTest("configured cJSON dependency is not available")
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            header = directory / "generated.h"
            generated = directory / "generated.c"
            check = directory / "check.c"
            executable = directory / "check"
            subprocess.run(
                [sys.executable, str(GENERATOR), str(FIXTURE), "-o", str(header),
                 "--source-output", str(generated)], check=True,
            )
            check.write_text(r'''#include "generated.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int called;
wf_status wf_xrpc_procedure(wf_xrpc_client *client, const char *nsid,
                            const char *json, wf_response *out) {
    assert(client != NULL);
    assert(strcmp(nsid, "com.example.echo") == 0);
    assert(strcmp(json, "{\"message\":\"hello\",\"attempts\":2,\"enabled\":true,\"tags\":[\"a\",\"b\"]}") == 0);
    called = 1; out->status = 200; out->body = NULL; out->body_len = 0; return WF_OK;
}
wf_status wf_auth_client_procedure(wf_auth_client *client, const char *nsid,
                                    const char *json, wf_response *out) {
    (void)client; (void)nsid; (void)json; (void)out; return WF_OK;
}
int main(void) {
    const char *tags[] = {"a", "b"};
    wf_lex_com_example_echo_main_input input = {0};
    input.message = "hello"; input.attempts = 2;
    input.has_enabled = true; input.enabled = true;
    input.has_tags = true; input.tags.items = tags; input.tags.count = 2;
    char *json = NULL;
    assert(wf_lex_com_example_echo_main_input_encode_json(&input, &json) == WF_OK);
    assert(json != NULL); wf_lex_com_example_echo_main_json_free(json);
    wf_response response = {0};
    assert(wf_lex_com_example_echo_main_call((wf_xrpc_client *)1, &input, &response) == WF_OK);
    assert(called);
    const char output_json[] = "{\"value\":\"ok\",\"items\":[{\"id\":\"one\",\"payload\":{\"$bytes\":\"AQID\"}}],\"raw\":{\"$bytes\":\"AAE=\"},\"link\":{\"$link\":\"bafytest\"},\"blob\":{\"$type\":\"blob\",\"ref\":{\"$link\":\"bafyblob\"},\"mimeType\":\"image/png\",\"size\":42},\"extra\":{\"x\":1},\"flags\":[true,false]}";
    wf_lex_com_example_echo_main_output *output = NULL;
    assert(wf_lex_com_example_echo_main_output_decode_json(output_json, strlen(output_json), &output) == WF_OK);
    assert(output && strcmp(output->value, "ok") == 0);
    assert(output->items.count == 1 && strcmp(output->items.items[0]->id, "one") == 0);
    assert(output->items.items[0]->payload.length == 3 && output->items.items[0]->payload.data[2] == 3);
    assert(output->raw.length == 2 && output->raw.data[1] == 1);
    assert(strcmp(output->link.cid, "bafytest") == 0);
    assert(strcmp(output->blob.cid, "bafyblob") == 0 && output->blob.size == 42);
    assert(output->has_flags && output->flags.count == 2 && output->flags.items[0]);
    assert(output->extra.length == 7 && strcmp(output->extra.data, "{\"x\":1}") == 0);
    wf_lex_com_example_echo_main_output_free(output);
    output = (void *)1;
    assert(wf_lex_com_example_echo_main_output_decode_json("{}", 2, &output) == WF_ERR_INVALID_ARG);
    assert(output == NULL);
    return 0;
}
''', encoding="utf-8")
            openssl = shlex.split(subprocess.run(
                ["pkg-config", "--cflags", "--libs", "openssl"],
                check=True, capture_output=True, text=True,
            ).stdout)
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(directory), "-I", str(ROOT / "include"),
                "-I", str(cjson_include), str(generated), str(check),
                "-L", str(cjson_lib), "-lcjson", *openssl, "-o", str(executable),
            ], check=True)
            env = os.environ.copy()
            env["DYLD_LIBRARY_PATH"] = str(cjson_lib)
            subprocess.run([str(executable)], check=True, env=env)


if __name__ == "__main__":
    unittest.main()
