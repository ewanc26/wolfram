/*
 * label_query.c — resolve a handle to a DID and query its labels.
 *
 * Demonstrates the generated com.atproto.identity.resolveHandle and
 * com.atproto.label.queryLabels XRPC wrappers, end to end:
 *   1. Resolve a handle to its DID via the generated resolveHandle wrapper.
 *   2. Query labels for that DID via the generated queryLabels wrapper.
 *   3. Print the returned labels (decoded into owned C structs).
 *
 * Usage:
 *   label_query <service-url> <handle>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/atproto_lex.h"
#include "wolfram/xrpc.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <service-url> <handle>\n", argv[0]);
        return 0;
    }

    const char *service_url = argv[1];
    const char *handle = argv[2];

    wf_xrpc_client *client = wf_xrpc_client_new(service_url);
    if (!client) {
        fprintf(stderr, "failed to create xrpc client\n");
        return 1;
    }

    wf_lex_com_atproto_identity_resolve_handle_main_params resolve_params = {0};
    resolve_params.handle = handle;

    wf_response resolve_res = {0};
    wf_status status = wf_lex_com_atproto_identity_resolve_handle_main_call(
        client, &resolve_params, &resolve_res);
    if (status != WF_OK) {
        fprintf(stderr, "resolveHandle failed: %d\n", (int)status);
        wf_response_free(&resolve_res);
        wf_xrpc_client_free(client);
        return 1;
    }

    wf_lex_com_atproto_identity_resolve_handle_main_output *resolved = NULL;
    status = wf_lex_com_atproto_identity_resolve_handle_main_output_decode_json(
        resolve_res.body, resolve_res.body_len, &resolved);
    wf_response_free(&resolve_res);
    if (status != WF_OK || !resolved || !resolved->did) {
        fprintf(stderr, "failed to decode resolveHandle response: %d\n", (int)status);
        wf_lex_com_atproto_identity_resolve_handle_main_output_free(resolved);
        wf_xrpc_client_free(client);
        return 1;
    }

    printf("Resolved %s -> %s\n", handle, resolved->did);

    const char *patterns[1] = { resolved->did };

    wf_lex_com_atproto_label_query_labels_main_params query_params = {0};
    query_params.uri_patterns.items = patterns;
    query_params.uri_patterns.count = 1;
    query_params.has_limit = true;
    query_params.limit = 50;

    wf_response query_res = {0};
    status = wf_lex_com_atproto_label_query_labels_main_call(
        client, &query_params, &query_res);
    if (status != WF_OK) {
        fprintf(stderr, "queryLabels failed: %d\n", (int)status);
        wf_lex_com_atproto_identity_resolve_handle_main_output_free(resolved);
        wf_response_free(&query_res);
        wf_xrpc_client_free(client);
        return 1;
    }

    wf_lex_com_atproto_label_query_labels_main_output *labels = NULL;
    status = wf_lex_com_atproto_label_query_labels_main_output_decode_json(
        query_res.body, query_res.body_len, &labels);
    wf_response_free(&query_res);

    if (status != WF_OK || !labels) {
        fprintf(stderr, "failed to decode queryLabels response: %d\n", (int)status);
    } else {
        size_t n = labels->labels.count;
        printf("queryLabels returned %zu label(s)\n", n);
        for (size_t i = 0; i < n; ++i) {
            const wf_lex_com_atproto_label_defs_label *l = labels->labels.items[i];
            if (!l) {
                continue;
            }
            printf("  - src=%s uri=%s val=%s cts=%s\n",
                   l->src ? l->src : "",
                   l->uri ? l->uri : "",
                   l->val ? l->val : "",
                   l->cts ? l->cts : "");
        }
    }

    wf_lex_com_atproto_label_query_labels_main_output_free(labels);
    wf_lex_com_atproto_identity_resolve_handle_main_output_free(resolved);
    wf_xrpc_client_free(client);
    return status == WF_OK ? 0 : 1;
}
