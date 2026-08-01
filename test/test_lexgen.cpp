// test_lexgen — C++ port of the Python test_lexgen.py suite.
//
// Runs the wf_lexgen codegen tool as a subprocess, asserts on its generated
// output, and for the codec/transport tests compiles and runs small C check
// programs (cc -std=c11 -Wall -Wextra -Werror) that link the generated source
// against cJSON and OpenSSL, exactly as the Python test did.
//
// Paths are injected at build time by CMake:
//   WF_TEST_ROOT             — repository root
//   WF_TEST_GENERATOR        — path to the wf_lexgen_tool binary
//   WF_TEST_CJSON_INCLUDE    — cJSON source dir (cJSON.h)
//   WF_TEST_CJSON_LIB        — cJSON build dir (libcjson)
//   WF_TEST_OPENSSL_INCLUDE  — OpenSSL header dir
//   WF_TEST_OPENSSL_CRYPTO   — OpenSSL crypto library path

#include <cJSON.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

#ifndef WF_TEST_ROOT
#define WF_TEST_ROOT "."
#endif
#ifndef WF_TEST_GENERATOR
#define WF_TEST_GENERATOR "wf_lexgen"
#endif
#ifndef WF_TEST_CJSON_INCLUDE
#define WF_TEST_CJSON_INCLUDE "."
#endif
#ifndef WF_TEST_CJSON_LIB
#define WF_TEST_CJSON_LIB "."
#endif
#ifndef WF_TEST_OPENSSL_INCLUDE
#define WF_TEST_OPENSSL_INCLUDE "."
#endif
#ifndef WF_TEST_OPENSSL_CRYPTO
#define WF_TEST_OPENSSL_CRYPTO ""
#endif

static const std::string ROOT = WF_TEST_ROOT;
static const std::string GENERATOR = WF_TEST_GENERATOR;
static const std::string CJSON_INCLUDE = WF_TEST_CJSON_INCLUDE;
static const std::string CJSON_LIB = WF_TEST_CJSON_LIB;
static const std::string OPENSSL_INCLUDE = WF_TEST_OPENSSL_INCLUDE;
static const std::string OPENSSL_CRYPTO = WF_TEST_OPENSSL_CRYPTO;

// ---------------------------------------------------------------------------
// Subprocess helper: fork/exec with stdout+stderr capture.
// ---------------------------------------------------------------------------

struct CommandResult {
    int status;
    std::string stdout_text;
    std::string stderr_text;
};

static CommandResult run_command(const std::vector<std::string>& args,
                                 const std::vector<std::string>& extra_env = {}) {
    int out_pipe[2];
    int err_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0)
        throw std::runtime_error("test_lexgen: pipe() failed");
    pid_t pid = fork();
    if (pid < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        throw std::runtime_error("test_lexgen: fork() failed");
    }
    if (pid == 0) {
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);
        for (const std::string& entry : extra_env) {
            std::string::size_type eq = entry.find('=');
            if (eq != std::string::npos)
                setenv(entry.substr(0, eq).c_str(), entry.substr(eq + 1).c_str(), 1);
        }
        std::vector<char*> argv;
        for (const std::string& a : args)
            argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        std::string err = std::string("test_lexgen: execvp(") + args[0] + ") failed: " +
                          strerror(errno) + "\n";
        write(STDERR_FILENO, err.data(), err.size());
        _exit(127);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);
    std::string outputs[2];
    bool open_fds[2] = {true, true};
    while (open_fds[0] || open_fds[1]) {
        struct pollfd pfd[2];
        int index[2];
        int nfds = 0;
        for (int i = 0; i < 2; ++i) {
            if (open_fds[i]) {
                pfd[nfds].fd = i == 0 ? out_pipe[0] : err_pipe[0];
                pfd[nfds].events = POLLIN;
                pfd[nfds].revents = 0;
                index[nfds] = i;
                ++nfds;
            }
        }
        int polled = poll(pfd, nfds, -1);
        if (polled < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        for (int i = 0; i < nfds; ++i) {
            if (pfd[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                char buf[8192];
                ssize_t n = read(pfd[i].fd, buf, sizeof(buf));
                if (n > 0) {
                    outputs[index[i]].append(buf, static_cast<size_t>(n));
                } else if (n == 0) {
                    open_fds[index[i]] = false;
                } else if (errno != EINTR) {
                    open_fds[index[i]] = false;
                }
            }
        }
    }
    close(out_pipe[0]);
    close(err_pipe[0]);

    int wstatus = 0;
    waitpid(pid, &wstatus, 0);
    int status;
    if (WIFEXITED(wstatus))
        status = WEXITSTATUS(wstatus);
    else if (WIFSIGNALED(wstatus))
        status = 128 + WTERMSIG(wstatus);
    else
        status = 1;
    return CommandResult{status, std::move(outputs[0]), std::move(outputs[1])};
}

// ---------------------------------------------------------------------------
// File / directory helpers.
// ---------------------------------------------------------------------------

static std::string read_file(const fs::path& path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("test_lexgen: cannot read " + path.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void write_file(const fs::path& path, const std::string& content) {
    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("test_lexgen: cannot write " + path.string());
    out << content;
}

class TempDir {
public:
    fs::path path;

    TempDir() {
        static int counter = 0;
        path = fs::temp_directory_path() /
               ("wf_lexgen_test_" + std::to_string(::getpid()) + "_" +
                std::to_string(counter++));
        fs::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// ---------------------------------------------------------------------------
// Generator invocation helpers (mirror the Python subprocess.run calls).
// ---------------------------------------------------------------------------

static std::string generate_to_stdout(const std::vector<std::string>& lexicons) {
    std::vector<std::string> args = {GENERATOR};
    for (const std::string& path : lexicons)
        args.push_back(path);
    CommandResult res = run_command(args);
    if (res.status != 0)
        throw std::runtime_error("generator exited " + std::to_string(res.status) + ":\n" +
                                 res.stdout_text + res.stderr_text);
    return res.stdout_text;
}

// Generate a header into `header` and, when `source` is non-empty, also a
// source file via --source-output, mirroring the Python argument order.
static void generate(const std::string& fixture, const fs::path& header,
                     const fs::path& source) {
    std::vector<std::string> args = {GENERATOR, fixture, "-o", header.string()};
    if (!source.empty()) {
        args.push_back("--source-output");
        args.push_back(source.string());
    }
    CommandResult res = run_command(args);
    if (res.status != 0)
        throw std::runtime_error("generator exited " + std::to_string(res.status) + ":\n" +
                                 res.stdout_text + res.stderr_text);
}

// ---------------------------------------------------------------------------
// Compile + run helpers (cc -std=c11 -Wall -Wextra -Werror, linking the
// generated source against cJSON and OpenSSL like the Python test did).
// ---------------------------------------------------------------------------

static void compile_and_run(const fs::path& dir, const fs::path& generated_c,
                            const fs::path& check_c, const fs::path& executable) {
    std::vector<std::string> args = {
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", dir.string(),
        "-I", ROOT + "/include",
        "-I", CJSON_INCLUDE,
        "-I", OPENSSL_INCLUDE,
        generated_c.string(), check_c.string(),
        "-L", CJSON_LIB, "-lcjson",
        OPENSSL_CRYPTO,
        "-o", executable.string(),
    };
    CommandResult compile = run_command(args);
    if (compile.status != 0)
        throw std::runtime_error("cc failed:\n" + compile.stdout_text + compile.stderr_text);

    std::vector<std::string> env = {
        "DYLD_LIBRARY_PATH=" + CJSON_LIB,
        "LD_LIBRARY_PATH=" + CJSON_LIB,
    };
    CommandResult run = run_command({executable.string()}, env);
    if (run.status != 0)
        throw std::runtime_error("check binary exited " + std::to_string(run.status) + ":\n" +
                                 run.stdout_text + run.stderr_text);
}

// ---------------------------------------------------------------------------
// Assertion helpers.
// ---------------------------------------------------------------------------

static void assert_true(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

static void assert_contains(const std::string& haystack, const std::string& needle,
                            const std::string& what) {
    if (haystack.find(needle) == std::string::npos)
        throw std::runtime_error(what + ": generated output is missing:\n" + needle);
}

// ---------------------------------------------------------------------------
// Naming helpers mirroring the Python generator (needed to compute the
// endpoint symbols asserted against include/wolfram/atproto_lex.h).
// ---------------------------------------------------------------------------

static const std::set<std::string> C_KEYWORDS = {
    "auto", "break", "case", "char", "const", "continue", "default",
    "do", "double", "else", "enum", "extern", "float", "for", "goto",
    "if", "inline", "int", "long", "register", "restrict", "return",
    "short", "signed", "sizeof", "static", "struct", "switch", "typedef",
    "union", "unsigned", "void", "volatile", "while", "_Alignas",
    "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary",
    "_Noreturn", "_Static_assert", "_Thread_local",
};

static const std::set<std::string> PY_KEYWORDS = {
    "False", "None", "True", "and", "as", "assert", "async", "await",
    "break", "class", "continue", "def", "del", "elif", "else", "except",
    "finally", "for", "from", "global", "if", "import", "in", "is",
    "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try",
    "while", "with", "yield",
};

static bool is_lower_or_digit(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return std::islower(uc) != 0 || std::isdigit(uc) != 0;
}

static std::string snake(const std::string& value) {
    std::string camel;
    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];
        if (i > 0 && is_lower_or_digit(value[i - 1]) &&
            std::isupper(static_cast<unsigned char>(c)) != 0)
            camel += '_';
        camel += c;
    }
    std::string out;
    bool pending = false;
    for (char c : camel) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            if (pending) {
                out += '_';
                pending = false;
            }
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else {
            pending = true;
        }
    }
    if (out.empty())
        out = "value";
    if (std::isdigit(static_cast<unsigned char>(out[0])) != 0 ||
        C_KEYWORDS.count(out) != 0 || PY_KEYWORDS.count(out) != 0)
        out += '_';
    return out;
}

static std::string type_name(const std::string& nsid, const std::string& suffix) {
    return "wf_lex_" + snake(nsid) + (suffix.empty() ? "" : "_" + snake(suffix));
}

// ---------------------------------------------------------------------------
// The ported test methods.
// ---------------------------------------------------------------------------

static void test_generates_endpoint_and_named_types() {
    std::string header = generate_to_stdout(
        {ROOT + "/test/fixtures/lexicons/com.example.echo.json"});
    assert_contains(header, "#define WF_LEX_COM_EXAMPLE_ECHO_NSID \"com.example.echo\"",
                    "NSID define");
    assert_contains(header, "#define WF_LEX_COM_EXAMPLE_ECHO_KIND \"procedure\"",
                    "KIND define");
    assert_contains(header, "wf_lex_com_example_echo_main_input", "main input type");
    assert_contains(header, "const char * message;", "message field");
    assert_contains(header, "int64_t attempts;", "attempts field");
    assert_contains(header, "bool has_enabled;", "has_enabled field");
    assert_contains(header, "bool has_tags;", "has_tags field");
    assert_contains(header, "WF_LEX_ARRAY(const char *) tags;", "tags field");
    assert_contains(header, "wf_lex_json metadata;", "metadata field");
    assert_contains(header, "wf_lex_com_example_echo_named", "named type");
}

static void test_output_is_deterministic() {
    std::string fixture = ROOT + "/test/fixtures/lexicons/com.example.echo.json";
    assert_true(generate_to_stdout({fixture}) == generate_to_stdout({fixture}),
                "generator output is not deterministic");
}

static void test_generated_client_covers_bundled_endpoint_lexicons() {
    std::string header = read_file(ROOT + "/include/wolfram/atproto_lex.h");
    std::set<std::string> subscriptions;
    int endpoint_count = 0;
    int file_count = 0;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(ROOT + "/lexicons")) {
        if (!entry.is_regular_file() || entry.path().extension().string() != ".json")
            continue;
        ++file_count;
        std::string text = read_file(entry.path());
        cJSON* document = cJSON_Parse(text.c_str());
        if (!document)
            throw std::runtime_error("cannot parse " + entry.path().string());
        cJSON* id_item = cJSON_GetObjectItemCaseSensitive(document, "id");
        std::string nsid = (id_item && cJSON_IsString(id_item) && id_item->valuestring)
                               ? id_item->valuestring
                               : "";
        cJSON* defs = cJSON_GetObjectItemCaseSensitive(document, "defs");
        if (defs && cJSON_IsObject(defs)) {
            for (cJSON* def = defs->child; def; def = def->next) {
                if (!def->string)
                    continue;
                cJSON* type_item = cJSON_GetObjectItemCaseSensitive(def, "type");
                std::string kind = (type_item && cJSON_IsString(type_item) && type_item->valuestring)
                                       ? type_item->valuestring
                                       : "";
                if (kind == "subscription") {
                    subscriptions.insert(nsid);
                    continue;
                }
                if (kind != "query" && kind != "procedure")
                    continue;
                ++endpoint_count;
                std::string symbol = type_name(nsid, def->string);
                assert_contains(header, "wf_status " + symbol + "_call(",
                                "endpoint call " + symbol);
                assert_contains(header, "wf_status " + symbol + "_call_auth(",
                                "endpoint auth call " + symbol);
            }
        }
        cJSON_Delete(document);
    }
    assert_true(endpoint_count == 314,
                "endpoint count is " + std::to_string(endpoint_count) + ", expected 314");
    std::set<std::string> expected = {
        "chat.bsky.moderation.subscribeModEvents",
        "com.atproto.label.subscribeLabels",
        "com.atproto.sync.subscribeRepos",
    };
    assert_true(subscriptions == expected, "subscription NSID set does not match");
    std::string subscription_apis =
        read_file(ROOT + "/include/wolfram/sync_subscribe.h") +
        read_file(ROOT + "/include/wolfram/label.h") +
        read_file(ROOT + "/include/wolfram/chat_typed.h");
    assert_contains(subscription_apis, "wf_subscribe_start(", "wf_subscribe_start");
    assert_contains(subscription_apis, "wf_label_subscribe_start(", "wf_label_subscribe_start");
    assert_contains(subscription_apis, "wf_agent_chat_subscribe_mod_events_typed(",
                    "wf_agent_chat_subscribe_mod_events_typed");
    std::cout << "    bundled lexicons: " << file_count << " files, " << endpoint_count
              << " endpoints\n";
}

static void test_generated_header_compiles_as_c11() {
    TempDir dir;
    fs::path header = dir.path / "generated.h";
    fs::path check = dir.path / "check.c";
    generate(ROOT + "/test/fixtures/lexicons/com.example.echo.json", header, {});
    write_file(check, "#include \"generated.h\"\n"
                      "int main(void) {\n"
                      "  wf_lex_com_example_echo_main_input value = {0};\n"
                      "  value.message = \"hello\";\n"
                      "  return value.message == 0;\n"
                      "}\n");
    std::vector<std::string> args = {
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
        "-I", ROOT + "/include", check.string(),
    };
    CommandResult res = run_command(args);
    if (res.status != 0)
        throw std::runtime_error("header C11 compile failed:\n" + res.stdout_text +
                                 res.stderr_text);
}

static void test_union_array_cleanup_casts_away_borrowed_view_const() {
    TempDir dir;
    fs::path fixture = dir.path / "com.example.union-array.json";
    fs::path header = dir.path / "generated.h";
    fs::path source = dir.path / "generated.c";
    write_file(fixture, R"lexchk({
  "lexicon": 1,
  "id": "com.example.unionArray",
  "defs": {
    "main": {
      "type": "query",
      "output": {
        "encoding": "application/json",
        "schema": {
          "type": "object",
          "required": ["items"],
          "properties": {
            "items": {
              "type": "array",
              "items": {
                "type": "union",
                "refs": ["#entry"],
                "closed": true
              }
            }
          }
        }
      }
    },
    "entry": {
      "type": "object",
      "required": ["value"],
      "properties": {"value": {"type": "string"}}
    }
  }
})lexchk");
    generate(fixture, header, source);
    std::string generated = read_file(source);
    std::string union_type = "wf_lex_com_example_union_array_main_output_items_item_union";
    assert_contains(generated, "wf_lex_clear_" + union_type + "((" + union_type + " *)&(",
                    "union-array cleanup cast");
}

static void test_inline_objects_are_dependency_safe_and_deterministic() {
    std::string fixture = ROOT + "/test/fixtures/lexicons/com.example.inline.json";
    std::string header = generate_to_stdout({fixture});
    std::string nested = "typedef struct wf_lex_com_example_inline_main_input_config_nested {";
    std::string parent = "typedef struct wf_lex_com_example_inline_main_input_config {";
    size_t nested_pos = header.find(nested);
    size_t parent_pos = header.find(parent);
    assert_true(nested_pos != std::string::npos, "nested inline typedef missing");
    assert_true(parent_pos != std::string::npos, "parent inline typedef missing");
    assert_true(nested_pos < parent_pos, "nested typedef must precede parent typedef");
    assert_contains(header,
                    "WF_LEX_ARRAY(wf_lex_com_example_inline_main_input_entries_item) entries;",
                    "entries array type");
    assert_true(header == generate_to_stdout({fixture}),
                "inline output is not deterministic");
}

static void test_inline_object_codecs_run() {
    TempDir dir;
    fs::path header = dir.path / "generated.h";
    fs::path generated = dir.path / "generated.c";
    fs::path check = dir.path / "check.c";
    fs::path executable = dir.path / "check";
    generate(ROOT + "/test/fixtures/lexicons/com.example.inline.json", header, generated);
    write_file(check, R"lexchk(#include "generated.h"
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
)lexchk");
    compile_and_run(dir.path, generated, check, executable);
}

static void test_referenced_inputs_and_all_json_value_kinds_run() {
    TempDir dir;
    fs::path header = dir.path / "generated.h";
    fs::path generated = dir.path / "generated.c";
    fs::path check = dir.path / "check.c";
    fs::path executable = dir.path / "check";
    generate(ROOT + "/test/fixtures/lexicons/com.example.refs.json", header, generated);
    write_file(check, R"lexchk(#include "generated.h"
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
)lexchk");
    compile_and_run(dir.path, generated, check, executable);
}

static void test_generates_json_codec_and_transport_wrapper() {
    TempDir dir;
    fs::path header = dir.path / "generated.h";
    fs::path source = dir.path / "generated.c";
    generate(ROOT + "/test/fixtures/lexicons/com.example.echo.json", header, source);
    std::string generated = read_file(source);
    assert_contains(generated, "cJSON_PrintUnformatted", "cJSON_PrintUnformatted");
    assert_contains(generated, "_output_decode_json", "output decode json");
    assert_contains(generated, "cJSON_GetObjectItemCaseSensitive(item, \"$bytes\")",
                    "$bytes lookup");
    assert_contains(generated, "cJSON_GetObjectItemCaseSensitive(item, \"$link\")",
                    "$link lookup");
    assert_contains(generated, "wf_xrpc_procedure(client, \"com.example.echo\"",
                    "procedure wrapper");
    generate(ROOT + "/test/fixtures/lexicons/com.example.echo.json", header, source);
    assert_true(generated == read_file(source), "regenerated source differs");
}

static void test_query_wrapper_uses_encoded_xrpc_parameters() {
    TempDir dir;
    fs::path header = dir.path / "generated.h";
    fs::path source = dir.path / "generated.c";
    generate(ROOT + "/test/fixtures/lexicons/com.example.get.json", header, source);
    std::string generated = read_file(source);
    assert_contains(generated, "wf_xrpc_query_params(client, \"com.example.get\"",
                    "query params wrapper");
    assert_contains(generated,
                    "encoded[count++] = (wf_xrpc_param){\"limit\", number_values[number_count++]}",
                    "limit param");
    assert_contains(generated,
                    "encoded[count++] = (wf_xrpc_param){\"dids\", params->dids.items[i]}",
                    "dids param");
    assert_contains(generated,
                    "encoded[count++] = (wf_xrpc_param){\"ids\", number_values[number_count++]}",
                    "ids param");
    assert_contains(generated,
                    "encoded[count++] = (wf_xrpc_param){\"flags\", (params->flags.items[i] ? \"true\" : \"false\")}",
                    "flags param");
}

static void test_query_array_wrapper_runs_with_repeated_keys() {
    TempDir dir;
    fs::path header = dir.path / "generated.h";
    fs::path generated = dir.path / "generated.c";
    fs::path check = dir.path / "check.c";
    fs::path executable = dir.path / "check";
    generate(ROOT + "/test/fixtures/lexicons/com.example.get.json", header, generated);
    write_file(check, R"lexchk(#include "generated.h"
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
)lexchk");
    compile_and_run(dir.path, generated, check, executable);
}

static void test_generated_codec_and_wrapper_run() {
    TempDir dir;
    fs::path header = dir.path / "generated.h";
    fs::path generated = dir.path / "generated.c";
    fs::path check = dir.path / "check.c";
    fs::path executable = dir.path / "check";
    generate(ROOT + "/test/fixtures/lexicons/com.example.echo.json", header, generated);
    write_file(check, R"lexchk(#include "generated.h"
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
)lexchk");
    compile_and_run(dir.path, generated, check, executable);
}

// ---------------------------------------------------------------------------
// Main: run every ported test method and report.
// ---------------------------------------------------------------------------

struct TestEntry {
    const char* name;
    void (*run)();
};

int main() {
    const TestEntry tests[] = {
        {"generates_endpoint_and_named_types", test_generates_endpoint_and_named_types},
        {"output_is_deterministic", test_output_is_deterministic},
        {"generated_client_covers_bundled_endpoint_lexicons",
         test_generated_client_covers_bundled_endpoint_lexicons},
        {"generated_header_compiles_as_c11", test_generated_header_compiles_as_c11},
        {"union_array_cleanup_casts_away_borrowed_view_const",
         test_union_array_cleanup_casts_away_borrowed_view_const},
        {"inline_objects_are_dependency_safe_and_deterministic",
         test_inline_objects_are_dependency_safe_and_deterministic},
        {"inline_object_codecs_run", test_inline_object_codecs_run},
        {"referenced_inputs_and_all_json_value_kinds_run",
         test_referenced_inputs_and_all_json_value_kinds_run},
        {"generates_json_codec_and_transport_wrapper",
         test_generates_json_codec_and_transport_wrapper},
        {"query_wrapper_uses_encoded_xrpc_parameters",
         test_query_wrapper_uses_encoded_xrpc_parameters},
        {"query_array_wrapper_runs_with_repeated_keys",
         test_query_array_wrapper_runs_with_repeated_keys},
        {"generated_codec_and_wrapper_run", test_generated_codec_and_wrapper_run},
    };

    int failures = 0;
    for (const TestEntry& test : tests) {
        try {
            test.run();
            std::cout << "ok " << test.name << "\n";
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << test.name << ": " << error.what() << "\n";
            ++failures;
        }
    }
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all lexgen tests passed (" << sizeof(tests) / sizeof(tests[0])
              << " tests)\n";
    return 0;
}
