#include "wolfram/oauth_node.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t stop_requested;

static void on_signal(int sig) {
    (void)sig;
    stop_requested = 1;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s --public-base-url URL [--listen ADDRESS] [--port PORT]\n"
            "       [--client-name NAME] [--scope SCOPE] [--ttl SECONDS]\n",
            argv0);
}

int main(int argc, char **argv) {
    const char *public_url = NULL;
    const char *listen_address = "127.0.0.1";
    const char *client_name = "Wolfram OAuth Node";
    const char *scope = "atproto repo:* blob:*/*";
    unsigned long port = 8080;
    unsigned long ttl = 600;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--public-base-url") == 0 && i + 1 < argc) {
            public_url = argv[++i];
        } else if (strcmp(argv[i], "--listen") == 0 && i + 1 < argc) {
            listen_address = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--client-name") == 0 && i + 1 < argc) {
            client_name = argv[++i];
        } else if (strcmp(argv[i], "--scope") == 0 && i + 1 < argc) {
            scope = argv[++i];
        } else if (strcmp(argv[i], "--ttl") == 0 && i + 1 < argc) {
            ttl = strtoul(argv[++i], NULL, 10);
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (!public_url || !public_url[0] || port > 65535 || ttl < 60) {
        usage(argv[0]);
        return 2;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    wf_oauth_node_config config = {.public_base_url = public_url,
                                   .client_name = client_name,
                                   .scope = scope,
                                   .slingshot_url =
                                       "https://slingshot.micocosm.blue",
                                   .pairing_ttl = (unsigned int)ttl};

    wf_oauth_node *node = wf_oauth_node_new(&config);
    if (!node) {
        fprintf(stderr, "wolfram-oauth-node: could not initialise\n");
        return 1;
    }

    wf_status status =
        wf_oauth_node_start(node, listen_address, (uint16_t)port, 4);
    if (status != WF_OK) {
        fprintf(stderr, "wolfram-oauth-node: failed to start (%d)\n",
                (int)status);
        wf_oauth_node_free(node);
        return 1;
    }

    fprintf(stderr, "wolfram-oauth-node: listening on %s:%u\n", listen_address,
            (unsigned)wf_oauth_node_port(node));

    while (!stop_requested) pause();
    wf_oauth_node_free(node);
    return 0;
}
