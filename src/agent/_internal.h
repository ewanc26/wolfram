#ifndef WOLFRAM_AGENT_INTERNAL_H
#define WOLFRAM_AGENT_INTERNAL_H

#include <stddef.h>
#include <stdio.h>

#include "wolfram/xrpc.h"
#include "wolfram/session.h"
#include "wolfram/repo.h"
#include "wolfram/moderation.h"
#include "wolfram/store.h"

/* Private agent struct for internal use */
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
#ifdef WOLFRAM_BUILD_STORE
    /* Optional persistence target. Caller-owned; never freed by the agent. */
    wf_store *store;
    /* Labels loaded from the store into the agent's moderation context.
     * The agent owns the allocation and frees it on wf_agent_free. */
    wf_mod_label *persisted_labels;
    size_t persisted_label_count;
#endif
} wf_agent;

/* Helper: convert int to string */
static inline int wf_agent_int_to_str(int value, char *buf, size_t buf_len) {
    return snprintf(buf, buf_len, "%d", value) > 0;
}

/* Helper: check if session is logged in */
static inline int wf_agent_is_logged_in(const wf_agent *agent) {
    return agent && agent->session && wf_session_has_session(agent->session) &&
           agent->session->data.did && agent->session->data.access_jwt;
}

/* Helper: set auth on the primary (PDS) XRPC client based on session */
static inline void wf_agent_sync_auth(wf_agent *agent) {
    if (!agent || !agent->client || !agent->session) {
        return;
    }
    wf_xrpc_client_set_auth(agent->client,
        wf_agent_is_logged_in(agent) ? agent->session->data.access_jwt : NULL);
}

/* Helper: set auth on the chat XRPC client based on session. Safe no-op when
 * the chat client has not yet been resolved. */
static inline void wf_agent_sync_chat_auth(wf_agent *agent) {
    if (!agent || !agent->chat_client || !agent->session) {
        return;
    }
    wf_xrpc_client_set_auth(agent->chat_client,
        wf_agent_is_logged_in(agent) ? agent->session->data.access_jwt : NULL);
}

#endif /* WOLFRAM_AGENT_INTERNAL_H */