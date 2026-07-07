#include "wolfram/agent.h"
#include "wolfram/identity.h"
#include <stdlib.h>
#include <string.h>

#include "_internal.h"

wf_status wf_get_notif_endpoint(wf_agent *agent, const char *service_did, char **out_endpoint) {
    if (!agent || !service_did || !out_endpoint) return WF_ERR_INVALID_ARG;
    wf_did_document doc = {0};
    wf_status status = wf_did_resolve(agent->client, service_did, &doc);
    if (status != WF_OK) return status;
    if (doc.notif_endpoint) {
        *out_endpoint = strdup(doc.notif_endpoint);
        if (!*out_endpoint) {
            wf_did_document_free(&doc);
            return WF_ERR_ALLOC;
        }
        wf_did_document_free(&doc);
        return WF_OK;
    }
    wf_did_document_free(&doc);
    return WF_ERR_NOT_FOUND;
}
