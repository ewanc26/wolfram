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

wf_status wf_sync_verify_diff_car(const wf_car *base,
                                  const wf_cid *base_commit,
                                  const unsigned char *bytes,
                                  size_t len,
                                  const wf_repo_verify_options *options,
                                  wf_repo_diff *out) {
    if (!base || !base_commit || !bytes || len == 0 || !options || !out)
        return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    wf_car update = {0};
    wf_status status = wf_car_parse(bytes, len, &update);
    if (status != WF_OK) return status;
    status = wf_repo_diff_verify(base, base_commit, &update, options, out);
    wf_car_free(&update);
    return status;
}
