/**
 * sync_head.c — query a repo's current head CID.
 *
 * Demonstrates:
 *   1. Creating an XRPC client
 *   2. Calling com.atproto.sync.getHead
 *   3. Printing the resulting root CID and rev
 *
 * Usage:
 *   sync_head <pds-base-url> <did>
 *   sync_head https://eurosky.social did:plc:ofrbh253gwicbkc5nktqepol
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/sync.h"

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <pds-base-url> <did>\n", argv[0]);
        return 1;
    }

    const char *base_url = argv[1];
    const char *did = argv[2];

    wf_xrpc_client *client = wf_xrpc_client_new(base_url);
    if (!client) {
        fprintf(stderr, "failed to create client\n");
        return 1;
    }

    wf_sync_head head = {0};
    wf_status status = wf_sync_get_head(client, did, &head);

    if (status != WF_OK) {
        fprintf(stderr, "getHead failed: %d\n", (int)status);
        wf_xrpc_client_free(client);
        return 1;
    }

    printf("root: %s\nrev:   %s\n",
           head.root ? head.root : "(none)",
           head.rev ? head.rev : "(none)");

    wf_sync_head_free(&head);
    wf_xrpc_client_free(client);
    return 0;
}
