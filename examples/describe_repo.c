/**
 * describe_repo.c — smallest possible end-to-end example.
 *
 * Calls com.atproto.repo.describeRepo against a PDS for a given
 * handle-or-DID and prints the raw JSON response. No JSON parsing
 * yet — that's the point; this only exercises the XRPC transport.
 *
 * Usage:
 *   describe_repo <pds-base-url> <repo>
 *   describe_repo https://eurosky.social did:plc:ofrbh253gwicbkc5nktqepol
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/wolfram.h"

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <pds-base-url> <repo>\n", argv[0]);
        return 1;
    }

    const char *base_url = argv[1];
    const char *repo = argv[2];

    wf_xrpc_client *client = wf_xrpc_client_new(base_url);
    if (!client) {
        fprintf(stderr, "failed to create client\n");
        return 1;
    }

    char query[512];
    snprintf(query, sizeof(query), "repo=%s", repo);

    wf_response res = {0};
    wf_status status = wf_xrpc_query(client, "com.atproto.repo.describeRepo", query, &res);

    if (status != WF_OK && status != WF_ERR_HTTP) {
        fprintf(stderr, "request failed: status code %d\n", (int)status);
        wf_xrpc_client_free(client);
        return 1;
    }

    printf("HTTP %ld\n%s\n", res.status, res.body ? res.body : "(empty body)");

    wf_response_free(&res);
    wf_xrpc_client_free(client);
    return status == WF_OK ? 0 : 1;
}
