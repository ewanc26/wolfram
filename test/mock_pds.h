/**
 * mock_pds.h — a tiny embedded mock XRPC/PDS server for offline tests.
 *
 * Wraps GNU libmicrohttpd to serve canned XRPC JSON responses keyed by
 * NSID. Intended for integration tests that need a real local HTTP server
 * to exercise the SDK's transport end-to-end without touching the network.
 *
 * This is test-only scaffolding (built only when
 * WOLFRAM_BUILD_TEST_HTTPD=ON). It performs no crypto and is not part of
 * the shipped library.
 */

#ifndef WOLFRAM_TEST_MOCK_PDS_H
#define WOLFRAM_TEST_MOCK_PDS_H

#include <stddef.h>

#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque mock PDS. The server runs an internal MHD polling thread; the
 *  map of canned responses is consulted live on every request, so it is
 *  safe to register responses before issuing client requests. */
typedef struct wf_mock_pds wf_mock_pds;

/**
 * Start a mock PDS on an ephemeral localhost port.
 *
 * On WF_OK, `*out` is set to a started server and `*out_port` is set to the
 * bound TCP port. The caller owns `*out` and must release it with
 * wf_mock_pds_free. The server initially serves no NSIDs; register canned
 * responses with wf_mock_pds_register.
 */
wf_status wf_mock_pds_start(wf_mock_pds **out, int *out_port);

/**
 * Register a canned XRPC response for `nsid`. The server returns `json`
 * (as application/json, HTTP 200) for any GET/POST to `/xrpc/<nsid>`.
 * The strings are copied; neither the NSID nor the JSON need outlive the
 * call. Registering the same NSID twice replaces the previous response.
 */
wf_status wf_mock_pds_register(wf_mock_pds *pds,
                               const char *nsid,
                               const char *json);

/** Stop the HTTP daemon without discarding registered responses or the
 *  struct. Safe to call more than once. */
wf_status wf_mock_pds_stop(wf_mock_pds *pds);

/** Stop the daemon (if running) and free all memory owned by the server.
 *  Safe to call with NULL. */
void wf_mock_pds_free(wf_mock_pds *pds);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_TEST_MOCK_PDS_H */
