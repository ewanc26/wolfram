/*
 * get_record.c — download a single record via com.atproto.sync.getRecord.
 *
 * Demonstrates wf_sync_get_record: it fetches the CAR containing just the
 * requested record (and its supporting blocks) and prints the root CID plus
 * each block's CID and byte length.
 *
 * Usage:
 *   get_record <service-url> <did> <collection> <record-key>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/wolfram.h"
#include "wolfram/repo/car.h"
#include "wolfram/repo/cid.h"

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr,
                "usage: %s <service-url> <did> <collection> <record-key>\n",
                argv[0]);
        return 1;
    }

    const char *service_url = argv[1];
    const char *did = argv[2];
    const char *collection = argv[3];
    const char *rkey = argv[4];

    wf_xrpc_client *client = wf_xrpc_client_new(service_url);
    if (!client) {
        fprintf(stderr, "failed to create XRPC client\n");
        return 1;
    }

    wf_car car = {0};
    wf_status status = wf_sync_get_record(client, did, collection, rkey, &car);
    if (status != WF_OK) {
        fprintf(stderr, "getRecord failed: status code %d\n", (int)status);
        wf_xrpc_client_free(client);
        return 1;
    }

    printf("Received %zu block(s), %zu root(s)\n",
           car.block_count, car.root_count);

    for (size_t i = 0; i < car.root_count; i++) {
        char *root = wf_cid_to_string(&car.roots[i]);
        printf("root[%zu]: %s\n", i, root ? root : "(null)");
        free(root);
    }

    for (size_t i = 0; i < car.block_count; i++) {
        char *cid = wf_cid_to_string(&car.blocks[i].cid);
        printf("block[%zu]: cid=%s bytes=%zu\n", i,
               cid ? cid : "(null)", car.blocks[i].data_len);
        free(cid);
    }

    wf_car_free(&car);
    wf_xrpc_client_free(client);
    return 0;
}
