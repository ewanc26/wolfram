#ifndef WOLFRAM_AGENT_STRUCT_H
#define WOLFRAM_AGENT_STRUCT_H

/* The single definition of struct wf_agent. Every translation unit that needs
 * the layout includes this header, so there is no second copy to drift. */

#include <stddef.h>

#include "wolfram/xrpc.h"
#include "wolfram/session.h"
#include "wolfram/repo.h"
#include "wolfram/moderation.h"
#include "wolfram/store.h"

typedef struct wf_agent {
    wf_xrpc_client *client;
    /* Separate XRPC client for the Bluesky chat service
     * (chat.bsky.convo.*). In production these endpoints are served by a
     * distinct chat service, NOT the user's PDS, so they must be routed
     * through this client. Lazily created via wf_agent_chat_service_resolve. */
    wf_xrpc_client *chat_client;
    wf_session *session;
    char *service_url;
    char *mirror_did;
    char *mirror_signing_key;
    wf_car mirror;
    /* TLS settings, remembered rather than applied once: an agent owns three
     * clients (data plane, session, and the lazily-created chat client), and a
     * platform that needs a CA bundle or its own handshake RNG needs every one
     * of them configured, including ones that do not exist yet at the time the
     * application sets this. */
    char *ca_bundle;
    wf_tls_rng_fn tls_rng;
    void *tls_rng_userdata;
    /* Default BCP-47 tags written to new posts' `langs` (comma-separated,
     * max 3). NULL means none. Owned by the agent. */
    char *post_langs;
#ifdef WOLFRAM_BUILD_STORE
    /* Optional persistence target. Caller-owned; never freed by the agent. */
    wf_store *store;
    /* Labels loaded from the store into the agent's moderation context.
     * The agent owns the allocation and frees it on wf_agent_free. */
    wf_mod_label *persisted_labels;
    size_t persisted_label_count;
#endif
} wf_agent;

#endif /* WOLFRAM_AGENT_STRUCT_H */
