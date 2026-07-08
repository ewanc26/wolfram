/**
 * identity_mgmt.c — agent convenience wrappers for the com.atproto.identity
 * and com.atproto.server account/identity-management endpoints that are not
 * already present elsewhere in the agent sources. Each uses the agent's
 * authenticated PDS client and delegates to the typed low-level wrappers
 * in identity.h / server.h.
 */

#include "wolfram/agent.h"
#include "wolfram/identity.h"
#include "wolfram/server.h"
#include "_internal.h"

#include <stdlib.h>
#include <string.h>

static int agent_ready(wf_agent *agent) {
    return agent && agent->client && wf_agent_is_logged_in(agent);
}

wf_status wf_agent_check_handle(wf_agent *agent,
                                const wf_identity_check_handle_input *input,
                                wf_identity_check_handle_result *out) {
    if (!agent_ready(agent) || !input || !out || !input->handle ||
        input->handle[0] == '\0') {
        return WF_ERR_INVALID_ARG;
    }
    wf_agent_sync_auth(agent);
    return wf_identity_check_handle(agent->client, input, out);
}

wf_status wf_agent_verify_handle(wf_agent *agent, const char *handle,
                                 int *out_valid) {
    if (!agent || !agent->client || !handle || handle[0] == '\0' ||
        !out_valid) {
        return WF_ERR_INVALID_ARG;
    }
    /* verify_handle is a local resolution check; no auth required. */
    return wf_identity_verify_handle(agent->client, handle, out_valid);
}

wf_status wf_agent_revoke_invite_codes(
    wf_agent *agent, const wf_server_revoke_invite_codes_input *input) {
    if (!agent_ready(agent) || !input) {
        return WF_ERR_INVALID_ARG;
    }
    wf_agent_sync_auth(agent);
    return wf_server_revoke_invite_codes(agent->client, input);
}
