#!/usr/bin/env python3
"""Generate XRPC-level convenience wrappers for remaining app.bsky.unspecced endpoints.

Usage:
    python3 tools/wf_gen_unspecced_wrappers.py --header   # header declarations
    python3 tools/wf_gen_unspecced_wrappers.py --source   # source implementations
"""

import re
import sys

HEADER_PATH = "include/wolfram/atproto_lex.h"

# Endpoints that already have wrappers in unspecced_typed.c (skip these)
EXISTING = frozenset({
    "get_age_assurance_state",
    "get_config",
    "get_onboarding_suggested_starter_packs",
    "get_onboarding_suggested_starter_packs_skeleton",
    "get_suggestions_skeleton",
    "get_tagged_suggestions",
    "get_trending_topics",
    "search_starter_packs_skeleton",
})


def snake_to_nsid_method(snake):
    """Convert snake_case method name to NSID method name (camelCase).
    E.g. 'get_popular_feed_generators' -> 'getPopularFeedGenerators'
    """
    parts = snake.split("_")
    result = parts[0]
    for p in parts[1:]:
        result += p.capitalize()
    return result


def read_endpoints():
    """Read all unspecced _main_call endpoint names from atproto_lex.h."""
    with open(HEADER_PATH, "r") as f:
        content = f.read()
    endpoints = set()
    for m in re.finditer(
        r'wf_status\s+wf_lex_app_bsky_unspecced_(\w+)_main_call\s*\(', content
    ):
        name = m.group(1)
        if name not in EXISTING:
            endpoints.add(name)
    return sorted(endpoints)


def output_header(endpoints):
    """Output header declarations."""
    print("/* ------------------------------------------------------------------ */")
    print("/* Unspecced — XRPC-level convenience wrappers                        */")
    print("/* ------------------------------------------------------------------ */")
    print()
    # NSID defines
    for ep in endpoints:
        method = snake_to_nsid_method(ep)
        define_name = f"WF_UNSPECCED_{method.upper()}_NSID"
        nsid = f"app.bsky.unspecced.{method}"
        print(f"#define {define_name:60s} \"{nsid}\"")
    print()
    # Wrapper declarations
    for ep in endpoints:
        lex_name = f"app_bsky_unspecced_{ep}"
        method = snake_to_nsid_method(ep)
        has_input = ep == "init_age_assurance"
        has_output = ep != "init_age_assurance"
        if has_input:
            params_type = f"wf_lex_{lex_name}_main_input"
        else:
            params_type = f"wf_lex_{lex_name}_main_params"
        func = f"wf_unspecced_{ep}"
        if has_input:
            print(f"wf_status {func}(wf_xrpc_client *client, const {params_type} *input, wf_response *out);")
        else:
            print(f"wf_status {func}(wf_xrpc_client *client, const {params_type} *params, wf_response *out);")
        if has_output:
            output_type = f"wf_lex_{lex_name}_main_output"
            print(f"wf_status {func}_parse(const wf_response *resp, {output_type} **out);")
        print()


def output_source(endpoints):
    """Output source implementations."""
    print("/* ================================================================== */")
    print("/* Unspecced — generated XRPC-level convenience wrappers              */")
    print("/* ================================================================== */")
    print()

    for ep in endpoints:
        lex_name = f"app_bsky_unspecced_{ep}"
        has_input = ep == "init_age_assurance"
        has_output = ep != "init_age_assurance"

        if has_input:
            params_type = f"wf_lex_{lex_name}_main_input"
            call_func = f"wf_lex_{lex_name}_main_call"
        else:
            params_type = f"wf_lex_{lex_name}_main_params"
            call_func = f"wf_lex_{lex_name}_main_call"

        func = f"wf_unspecced_{ep}"

        # Call function
        arg_name = "input" if has_input else "params"
        print(f"wf_status {func}(")
        print(f"    wf_xrpc_client *client,")
        print(f"    const {params_type} *{arg_name},")
        print(f"    wf_response *out) {{")
        print(f"    if (!client || !{arg_name} || !out) {{")
        print(f"        return WF_ERR_INVALID_ARG;")
        print(f"    }}")
        print(f"    return {call_func}(client, {arg_name}, out);")
        print(f"}}")
        print()

        # Parse function
        if has_output:
            output_type = f"wf_lex_{lex_name}_main_output"
            decode_func = f"wf_lex_{lex_name}_main_output_decode_json"
            print(f"wf_status {func}_parse(")
            print(f"    const wf_response *resp,")
            print(f"    {output_type} **out) {{")
            print(f"    if (!resp || !out) {{")
            print(f"        return WF_ERR_INVALID_ARG;")
            print(f"    }}")
            print(f"    *out = NULL;")
            print(f"    if (!resp->body) {{")
            print(f"        return WF_ERR_INVALID_ARG;")
            print(f"    }}")
            print(f"    return {decode_func}(")
            print(f"        resp->body, resp->body_len, out);")
            print(f"}}")
            print()


def main():
    endpoints = read_endpoints()
    if not endpoints:
        print("No remaining unspecced endpoints found.", file=sys.stderr)
        sys.exit(1)

    if "--header" in sys.argv:
        output_header(endpoints)
    elif "--source" in sys.argv:
        output_source(endpoints)
    else:
        print(f"Usage: {sys.argv[0]} [--header | --source]", file=sys.stderr)
        print(f"Found {len(endpoints)} endpoints", file=sys.stderr)

if __name__ == "__main__":
    main()
