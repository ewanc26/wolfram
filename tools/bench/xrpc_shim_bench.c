/*
 * Load target for the XRPC server shim benchmark recorded in
 * docs/xrpc-shim-benchmark.md.
 *
 * Starts one wf_xrpc_server on 127.0.0.1:PORT with EXTRA dummy GET routes
 * registered ahead of a single /hit route, then runs until killed. Drive it
 * with ApacheBench, e.g. `ab -n 5000 -c 8 http://127.0.0.1:PORT/hit`, and add
 * `-k` for HTTP keep-alive.
 *
 *   usage: xrpc_shim_bench PORT EXTRA_ROUTES
 *
 * Build against a library configured with -DWOLFRAM_MHD_SHIM=ON and
 * -DWOLFRAM_BUILD_SERVER=ON (see the benchmark doc for the exact commands).
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "wolfram/xrpc_server.h"

static wf_status hit_handler(void *ctx, const wf_xrpc_request *req,
                             wf_xrpc_response *resp) {
    (void)ctx;
    (void)req;
    wf_xrpc_response_set_body(resp, "ok", 2);
    return WF_OK;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s PORT EXTRA_ROUTES\n", argv[0]);
        return 2;
    }
    unsigned int port = (unsigned int)strtoul(argv[1], NULL, 10);
    long extra = strtol(argv[2], NULL, 10);

    wf_xrpc_server *server =
        wf_xrpc_server_start("127.0.0.1", (uint16_t)port, 0);
    if (!server) {
        fprintf(stderr, "failed to start server on port %u\n", port);
        return 1;
    }

    for (long i = 0; i < extra; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/r/%ld", i);
        if (wf_xrpc_server_register_http_route(server, "GET", path, hit_handler,
                                               NULL) != WF_OK) {
            fprintf(stderr, "failed to register %s\n", path);
            return 1;
        }
    }
    if (wf_xrpc_server_register_http_route(server, "GET", "/hit", hit_handler,
                                           NULL) != WF_OK) {
        fprintf(stderr, "failed to register /hit\n");
        return 1;
    }

    printf("listening on 127.0.0.1:%u with %ld extra routes\n", port, extra);
    fflush(stdout);
    for (;;) pause();
}
