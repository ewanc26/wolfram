/*
 * test_agent_login_discovered.c -- wf_agent_login_discovered's argument checks,
 * and that a handle which cannot be resolved fails before any login is sent,
 * with no PDS reported. The network path is exercised by the consumers' e2e
 * runs.
 */

#include "wolfram/agent.h"

#include "test.h"

#include <stdlib.h>

int main(void) {
    wf_agent *a = wf_agent_new("https://bsky.social");
    char *pds = NULL;

    WF_CHECK(a != NULL);
    if (!a) WF_TEST_SUMMARY();

    WF_CHECK(wf_agent_login_discovered(NULL, "alice.example.com", "pw", &pds) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(pds == NULL);
    WF_CHECK(wf_agent_login_discovered(a, "", "pw", &pds) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_login_discovered(a, "alice.example.com", NULL, &pds) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_login_discovered(a, "alice.example.com", "pw", NULL) ==
             WF_ERR_INVALID_ARG);

    /* `.invalid` never resolves (RFC 2606), so discovery fails and no login is
     * sent: the out-parameter stays NULL. */
    pds = (char *)0x1;
    WF_CHECK(wf_agent_login_discovered(a, "nobody.invalid", "pw", &pds) !=
             WF_OK);
    WF_CHECK(pds == NULL);

    wf_agent_free(a);
    WF_TEST_SUMMARY();
}
