#ifndef WOLFRAM_OAUTH_NODE_H
#define WOLFRAM_OAUTH_NODE_H

#include <stdint.h>
#include "wolfram/xrpc_server.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wf_oauth_node wf_oauth_node;

typedef struct wf_oauth_node_config {
    const char *public_base_url;
    const char *client_name;
    const char *scope;
    const char *slingshot_url;
    unsigned int pairing_ttl;
} wf_oauth_node_config;

wf_oauth_node *wf_oauth_node_new(const wf_oauth_node_config *config);
void wf_oauth_node_free(wf_oauth_node *node);

wf_status wf_oauth_node_start(wf_oauth_node *node, const char *listen_address,
                              uint16_t port, unsigned int thread_count);
void wf_oauth_node_stop(wf_oauth_node *node);
uint16_t wf_oauth_node_port(const wf_oauth_node *node);
wf_xrpc_server *wf_oauth_node_server(wf_oauth_node *node);

#ifdef __cplusplus
}
#endif

#endif
