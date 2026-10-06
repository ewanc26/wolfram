/*
 * failure.h -- one answer to "what kind of failure was that?" for every client.
 *
 * A client decides what to tell the person: "wrong password", "no network", "the
 * secure connection failed", "slow down". `wf_status` alone cannot say, because
 * a rejected login can arrive as WF_ERR_AUTH or as an HTTP 401 with an XRPC
 * error name, and a rate limit as WF_ERR_RATE_LIMIT or an HTTP 429. This maps
 * the three things a caller has (the status, the HTTP status, the XRPC error
 * name) to one kind, the same in every client. Message text stays with the
 * client, because wording and language differ per platform.
 *
 * Pure C89, no allocation, builds everywhere including the Mac OS 9 target.
 * The mapping is documented in docs/failure.md and pinned by
 * test/vectors/failure.json, which every client can read.
 */

#ifndef WOLFRAM_FAILURE_H
#define WOLFRAM_FAILURE_H

#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum wf_failure_kind {
    WF_FAIL_NONE = 0,
    WF_FAIL_BAD_CREDENTIALS, /* the account, password or token was refused */
    WF_FAIL_NETWORK,         /* could not reach the service */
    WF_FAIL_TIMEOUT,         /* the service took too long */
    WF_FAIL_TLS,             /* the secure connection could not be set up */
    WF_FAIL_RATE_LIMIT,      /* too many requests; wait */
    WF_FAIL_SERVER,          /* the service failed (5xx, upstream, internal) */
    WF_FAIL_BAD_RESPONSE,    /* the reply could not be understood */
    WF_FAIL_NOT_READY,       /* this build or transport cannot do it */
    WF_FAIL_OTHER            /* anything else */
} wf_failure_kind;

/*
 * Classify a failed call. Total: every input gives a kind, and WF_OK gives
 * WF_FAIL_NONE whatever the other arguments are.
 *
 *  - `st` is the call's wf_status.
 *  - `http_status` is the HTTP status of the response, or 0 if there was none.
 *  - `xrpc_error` is the XRPC error name from the response body (see
 *    wf_xrpc_error), or NULL.
 *
 * When `st` is WF_ERR_HTTP the XRPC error name decides first, then the HTTP
 * status; for any other `st` the status decides and the other two are ignored,
 * because a transport failure has no response to read. See docs/failure.md for
 * the table.
 */
wf_failure_kind wf_failure_classify(wf_status st, long http_status,
                                    const char *xrpc_error);

/* A stable machine tag for logs and tests: "none", "bad-credentials",
 * "network", "timeout", "tls", "rate-limit", "server", "bad-response",
 * "not-ready", "other". Never NULL; an out-of-range kind gives "other". */
const char *wf_failure_tag(wf_failure_kind kind);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_FAILURE_H */
