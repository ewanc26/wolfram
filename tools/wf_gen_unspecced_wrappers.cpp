// C++ replacement for tools/wf_gen_unspecced_wrappers.py
// Generate XRPC-level convenience wrappers for remaining app.bsky.unspecced endpoints

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static const char* HEADER_PATH = "include/wolfram/atproto_lex.h";

// Endpoints that already have wrappers in unspecced_typed.c (skip these)
static const std::set<std::string> EXISTING = {
    "get_age_assurance_state",
    "get_config",
    "get_onboarding_suggested_starter_packs",
    "get_onboarding_suggested_starter_packs_skeleton",
    "get_suggestions_skeleton",
    "get_tagged_suggestions",
    "get_trending_topics",
    "search_starter_packs_skeleton",
};

std::string snake_to_nsid_method(const std::string& snake) {
    std::string result;
    bool first_segment = true;
    for (size_t i = 0; i < snake.size();) {
        size_t end = snake.find('_', i);
        if (end == std::string::npos)
            end = snake.size();
        std::string part = snake.substr(i, end - i);
        if (first_segment) {
            result += part;
            first_segment = false;
        } else {
            result += static_cast<char>(std::toupper(static_cast<unsigned char>(part[0])));
            for (size_t j = 1; j < part.size(); ++j)
                result += static_cast<char>(std::tolower(static_cast<unsigned char>(part[j])));
        }
        i = end + 1;
    }
    return result;
}

std::vector<std::string> read_endpoints() {
    std::ifstream file(HEADER_PATH);
    if (!file) {
        std::cerr << "error: cannot open " << HEADER_PATH << "\n";
        std::exit(1);
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    std::set<std::string> found;
    size_t pos = 0;
    const std::string marker = "wf_status wf_lex_app_bsky_unspecced_";
    const std::string suffix = "_main_call";
    while (true) {
        pos = content.find(marker, pos);
        if (pos == std::string::npos)
            break;
        size_t name_start = pos + marker.size();
        size_t name_end = name_start;
        while (name_end < content.size() &&
               (std::isalnum(static_cast<unsigned char>(content[name_end])) ||
                content[name_end] == '_'))
            ++name_end;
        if (content.compare(name_end, 1, "(") == 0 &&
            name_end >= name_start + suffix.size() &&
            content.compare(name_end - suffix.size(), suffix.size(), suffix) == 0) {
            std::string name = content.substr(name_start, name_end - name_start - suffix.size());
            if (EXISTING.find(name) == EXISTING.end())
                found.insert(name);
        }
        pos = name_end;
    }

    return std::vector<std::string>(found.begin(), found.end());
}

void output_header(const std::vector<std::string>& endpoints) {
    std::cout << "/* ------------------------------------------------------------------ */\n";
    std::cout << "/* Unspecced — XRPC-level convenience wrappers                        */\n";
    std::cout << "/* ------------------------------------------------------------------ */\n";
    std::cout << "\n";
    for (const auto& ep : endpoints) {
        std::string method = snake_to_nsid_method(ep);
        std::string define_name = "WF_UNSPECCED_" + [&method]() {
            std::string upper = method;
            for (auto& c : upper)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return upper;
        }() + "_NSID";
        std::string nsid = "app.bsky.unspecced." + method;
        std::cout << "#define " << std::left << std::setw(60) << define_name << " \"" << nsid << "\"\n";
    }
    std::cout << "\n";
    for (const auto& ep : endpoints) {
        std::string lex_name = "app_bsky_unspecced_" + ep;
        std::string method = snake_to_nsid_method(ep);
        bool has_input = ep == "init_age_assurance";
        bool has_output = ep != "init_age_assurance";
        std::string params_type = has_input ? "wf_lex_" + lex_name + "_main_input"
                                            : "wf_lex_" + lex_name + "_main_params";
        std::string func = "wf_unspecced_" + ep;
        if (has_input) {
            std::cout << "wf_status " << func
                      << "(wf_xrpc_client *client, const " << params_type
                      << " *input, wf_response *out);\n";
        } else {
            std::cout << "wf_status " << func
                      << "(wf_xrpc_client *client, const " << params_type
                      << " *params, wf_response *out);\n";
        }
        if (has_output) {
            std::string output_type = "wf_lex_" + lex_name + "_main_output";
            std::cout << "wf_status " << func << "_parse(const wf_response *resp, "
                      << output_type << " **out);\n";
        }
        std::cout << "\n";
    }
}

void output_source(const std::vector<std::string>& endpoints) {
    std::cout << "/* ================================================================== */\n";
    std::cout << "/* Unspecced — generated XRPC-level convenience wrappers              */\n";
    std::cout << "/* ================================================================== */\n";
    std::cout << "\n";

    for (const auto& ep : endpoints) {
        std::string lex_name = "app_bsky_unspecced_" + ep;
        bool has_input = ep == "init_age_assurance";
        bool has_output = ep != "init_age_assurance";
        std::string params_type = has_input ? "wf_lex_" + lex_name + "_main_input"
                                            : "wf_lex_" + lex_name + "_main_params";
        std::string call_func = "wf_lex_" + lex_name + "_main_call";
        std::string func = "wf_unspecced_" + ep;
        std::string arg_name = has_input ? "input" : "params";

        std::cout << "wf_status " << func << "(\n";
        std::cout << "    wf_xrpc_client *client,\n";
        std::cout << "    const " << params_type << " *" << arg_name << ",\n";
        std::cout << "    wf_response *out) {\n";
        std::cout << "    if (!client || !" << arg_name << " || !out) {\n";
        std::cout << "        return WF_ERR_INVALID_ARG;\n";
        std::cout << "    }\n";
        std::cout << "    return " << call_func << "(client, " << arg_name << ", out);\n";
        std::cout << "}\n";
        std::cout << "\n";

        if (has_output) {
            std::string output_type = "wf_lex_" + lex_name + "_main_output";
            std::string decode_func = "wf_lex_" + lex_name + "_main_output_decode_json";
            std::cout << "wf_status " << func << "_parse(\n";
            std::cout << "    const wf_response *resp,\n";
            std::cout << "    " << output_type << " **out) {\n";
            std::cout << "    if (!resp || !out) {\n";
            std::cout << "        return WF_ERR_INVALID_ARG;\n";
            std::cout << "    }\n";
            std::cout << "    *out = NULL;\n";
            std::cout << "    if (!resp->body) {\n";
            std::cout << "        return WF_ERR_INVALID_ARG;\n";
            std::cout << "    }\n";
            std::cout << "    return " << decode_func << "(\n";
            std::cout << "        resp->body, resp->body_len, out);\n";
            std::cout << "}\n";
            std::cout << "\n";
        }
    }
}

int main(int argc, char* argv[]) {
    std::vector<std::string> endpoints = read_endpoints();
    if (endpoints.empty()) {
        std::cerr << "No remaining unspecced endpoints found.\n";
        return 1;
    }

    bool header_mode = false;
    bool source_mode = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--header") header_mode = true;
        if (arg == "--source") source_mode = true;
    }

    if (header_mode) {
        output_header(endpoints);
    } else if (source_mode) {
        output_source(endpoints);
    } else {
        std::cerr << "Usage: " << argv[0] << " [--header | --source]\n";
        std::cerr << "Found " << endpoints.size() << " endpoints\n";
    }
    return 0;
}
