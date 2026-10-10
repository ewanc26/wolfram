#ifndef WOLFRAM_AGENT_INTERNAL_H
#define WOLFRAM_AGENT_INTERNAL_H

#include <stddef.h>
#include <stdio.h>

#include "wolfram/xrpc.h"
#include "wolfram/session.h"
#include "wolfram/repo.h"
#include "wolfram/moderation.h"
#include "wolfram/store.h"

#include "_agent_struct.h"

/* Apply the agent's remembered TLS settings to one of its clients. Called for
 * each client the agent creates, so a lazily-created one is not left with the
 * library defaults. */
void wf_agent_apply_tls(wf_agent *agent, wf_xrpc_client *client);

/* Resolve the chat service's own endpoint URL (the host the moderation
 * WebSocket dials), independent of the PDS route used for XRPC. Best-effort:
 * falls back to WF_CHAT_DEFAULT_ENDPOINT. Caller frees the result. */
char *wf_agent_chat_service_endpoint(wf_agent *agent);

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
    wf_xrpc_client_set_auth(agent->client, wf_agent_is_logged_in(agent)
                                               ? agent->session->data.access_jwt
                                               : NULL);
}

/* Helper: set auth on the chat XRPC client based on session. Safe no-op when
 * the chat client has not yet been resolved. */
static inline void wf_agent_sync_chat_auth(wf_agent *agent) {
    if (!agent || !agent->chat_client || !agent->session) {
        return;
    }
    wf_xrpc_client_set_auth(
        agent->chat_client,
        wf_agent_is_logged_in(agent) ? agent->session->data.access_jwt : NULL);
}

#endif /* WOLFRAM_AGENT_INTERNAL_H */