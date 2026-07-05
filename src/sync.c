#include "wolfram/sync.h"

#include <string.h>

wf_status wf_sync_get_repo(wf_xrpc_client *client,
                            const char *did,
                            const char *since,
                            wf_car *out) {
    if (!client || !did || did[0] == '\0' || !out ||
        (since && since[0] == '\0')) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    wf_xrpc_param params[2] = {
        {"did", did},
        {"since", since},
    };
    size_t param_count = since ? 2 : 1;
    wf_response response = {0};
    wf_status status = wf_xrpc_query_params(client,
                                             "com.atproto.sync.getRepo",
                                             params,
                                             param_count,
                                             &response);
    if (status != WF_OK) {
        return status;
    }

    status = wf_car_parse((const unsigned char *)response.body,
                          response.body_len,
                          out);
    wf_response_free(&response);
    return status;
}
