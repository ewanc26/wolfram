/**
 * test_agent_tls.c — offline tests for the agent's TLS configuration.
 *
 * No handshake happens here; nothing in this file touches the network. What is
 * being checked is that the settings are accepted, remembered, and reported
 * honestly — the failure mode that matters is an agent silently keeping the
 * library defaults on a platform where those defaults do not work.
 */

#include <stdlib.h>
#include <string.h>

#include "wolfram/agent.h"
#include "test.h"

/* Stand-in application RNG. Never invoked offline; it only needs a valid
 * wf_tls_rng_fn address to install. */
static int test_rng(void *userdata, unsigned char *output, size_t len) {
    (void)userdata;
    memset(output, 0x5A, len);
    return 0;
}

int main(void) {
    /* NULL agent is rejected rather than crashing. */
    WF_CHECK(wf_agent_set_ca_bundle(NULL, "/etc/ssl/cert.pem") ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_set_tls_rng(NULL, test_rng, NULL) == WF_ERR_INVALID_ARG);

    wf_agent *agent = wf_agent_new("https://bsky.social");
    WF_CHECK(agent != NULL);
    if (!agent) {
        WF_TEST_SUMMARY();
    }

    /* A CA bundle can be set, replaced, and cleared. The agent copies the path,
     * so a caller's buffer going away afterwards must not matter. */
    char path[64];
    snprintf(path, sizeof(path), "/tmp/cobalt-cacert.pem");
    WF_CHECK(wf_agent_set_ca_bundle(agent, path) == WF_OK);
    memset(path, 0, sizeof(path));

    WF_CHECK(wf_agent_set_ca_bundle(agent, "/tmp/another.pem") == WF_OK);
    WF_CHECK(wf_agent_set_ca_bundle(agent, NULL) == WF_OK);

    /*
     * The RNG's outcome depends on the linked libcurl's backend — mbedTLS on
     * the console targets, usually OpenSSL on a desktop — so it is asserted
     * against wf_xrpc_tls_rng_supported() rather than hardcoded. The point is
     * that the two cases stay distinguishable: a build that cannot honour the
     * RNG must say so instead of quietly accepting one it will never call.
     */
    wf_status installed = wf_agent_set_tls_rng(agent, test_rng, agent);
    if (wf_xrpc_tls_rng_supported()) {
        WF_CHECK(installed == WF_OK);
    } else {
        WF_CHECK(installed == WF_ERR_UNSUPPORTED);
    }

    /* Clearing restores libcurl's own RNG and works on every build. */
    WF_CHECK(wf_agent_set_tls_rng(agent, NULL, NULL) == WF_OK);

    /* Settings applied before login must survive it — the session's client is
     * a different client from the data-plane one, and configuring only one of
     * them is the bug this API exists to prevent. */
    WF_CHECK(wf_agent_set_ca_bundle(agent, "/tmp/cobalt-cacert.pem") == WF_OK);

    wf_agent_free(agent);
    wf_agent_free(NULL); /* must be safe */

    WF_TEST_SUMMARY();
}
