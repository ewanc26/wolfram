/*
 * proxy_id.c — build the "<did>#<service id>" value of an atproto-proxy
 * header (see wf_xrpc_proxy_identifier in xrpc.h).
 *
 * Strict C89 with nothing but libc: the Mac OS 9 transport target links it
 * and console clients do too (tools/embedded-sources.txt).
 */
#include "wolfram/xrpc.h"

#include <stdlib.h>
#include <string.h>

#include "proxy_id.h"

wf_status wf_xrpc_proxy_identifier(const char *did, const char *service_id,
                                   char **out) {
    size_t did_len;
    size_t id_len;
    char *value;

    if (out == NULL) return WF_ERR_INVALID_ARG;
    *out = NULL;
    if (did == NULL || service_id == NULL) return WF_ERR_INVALID_ARG;
    /* A leading '#' is how service ids are written in a DID document; accept
     * it rather than make every caller strip it. */
    if (service_id[0] == '#') service_id++;
    did_len = strlen(did);
    id_len = strlen(service_id);
    value = (char *)malloc(did_len + 1 + id_len + 1);
    if (value == NULL) return WF_ERR_ALLOC;
    memcpy(value, did, did_len);
    value[did_len] = '#';
    memcpy(value + did_len + 1, service_id, id_len);
    value[did_len + 1 + id_len] = '\0';
    /* The validator sees the joined value, so a '#' inside either part (two
     * separators) is refused along with control characters and spaces. */
    if (!wf_proxy_id_valid(value)) {
        free(value);
        return WF_ERR_INVALID_ARG;
    }
    *out = value;
    return WF_OK;
}

int wf_xrpc_proxy_identifier_valid(const char *value) {
    return wf_proxy_id_valid(value);
}
