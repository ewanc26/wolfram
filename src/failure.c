/* failure.c -- wf_failure_classify and wf_failure_tag (see failure.h). C89. */

#include "wolfram/failure.h"

#include <string.h>

static int name_is(const char *name, const char *want)
{
    return name && strcmp(name, want) == 0;
}

static wf_failure_kind from_xrpc_error(const char *e)
{
    if (!e || !*e) return WF_FAIL_NONE;
    if (name_is(e, "AuthenticationRequired") || name_is(e, "ExpiredToken") ||
        name_is(e, "InvalidToken") || name_is(e, "AuthMissing") ||
        name_is(e, "AuthFactorTokenRequired"))
        return WF_FAIL_BAD_CREDENTIALS;
    if (name_is(e, "RateLimitExceeded")) return WF_FAIL_RATE_LIMIT;
    if (name_is(e, "UpstreamTimeout")) return WF_FAIL_TIMEOUT;
    if (name_is(e, "InternalServerError") || name_is(e, "UpstreamFailure") ||
        name_is(e, "NotEnoughResources"))
        return WF_FAIL_SERVER;
    return WF_FAIL_NONE;
}

static wf_failure_kind from_http(long http)
{
    if (http == 401) return WF_FAIL_BAD_CREDENTIALS;
    if (http == 408 || http == 504) return WF_FAIL_TIMEOUT;
    if (http == 429) return WF_FAIL_RATE_LIMIT;
    if (http >= 500 && http <= 599) return WF_FAIL_SERVER;
    return WF_FAIL_NONE;
}

wf_failure_kind wf_failure_classify(wf_status st, long http_status,
                                    const char *xrpc_error)
{
    wf_failure_kind k;

    switch (st) {
    case WF_OK:
        return WF_FAIL_NONE;
    case WF_ERR_HTTP:
        k = from_xrpc_error(xrpc_error);
        if (k != WF_FAIL_NONE) return k;
        k = from_http(http_status);
        if (k != WF_FAIL_NONE) return k;
        /* A non-2xx the table does not name (400, 403, 404...) is the
         * service answering, not failing: the caller's request was refused. */
        return WF_FAIL_OTHER;
    case WF_ERR_AUTH:
        return WF_FAIL_BAD_CREDENTIALS;
    case WF_ERR_RATE_LIMIT:
        return WF_FAIL_RATE_LIMIT;
    case WF_ERR_TIMEOUT:
        return WF_FAIL_TIMEOUT;
    case WF_ERR_TLS:
    case WF_ERR_CRYPTO:
    case WF_ERR_CONFIG:
        /* The console transports report a failed handshake or a missing
         * certificate store as CRYPTO or CONFIG until WF_ERR_TLS is verified
         * on them; a client already treated these as TLS. */
        return WF_FAIL_TLS;
    case WF_ERR_NETWORK:
    case WF_ERR_DID_RESOLVE:
    case WF_ERR_DID_DOCUMENT_NOT_FOUND:
    case WF_ERR_HANDLE_RESOLVE:
    case WF_ERR_HANDLE_DOCUMENT_NOT_FOUND:
        return WF_FAIL_NETWORK;
    case WF_ERR_PARSE:
        return WF_FAIL_BAD_RESPONSE;
    case WF_ERR_UNSUPPORTED:
    case WF_ERR_NOT_IMPLEMENTED:
        return WF_FAIL_NOT_READY;
    case WF_ERR_INTERNAL:
        return WF_FAIL_SERVER;
    default:
        return WF_FAIL_OTHER;
    }
}

const char *wf_failure_tag(wf_failure_kind kind)
{
    switch (kind) {
    case WF_FAIL_NONE: return "none";
    case WF_FAIL_BAD_CREDENTIALS: return "bad-credentials";
    case WF_FAIL_NETWORK: return "network";
    case WF_FAIL_TIMEOUT: return "timeout";
    case WF_FAIL_TLS: return "tls";
    case WF_FAIL_RATE_LIMIT: return "rate-limit";
    case WF_FAIL_SERVER: return "server";
    case WF_FAIL_BAD_RESPONSE: return "bad-response";
    case WF_FAIL_NOT_READY: return "not-ready";
    case WF_FAIL_OTHER: break;
    }
    return "other";
}
