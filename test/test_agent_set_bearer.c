/*
 * test_agent_set_bearer.c -- wf_agent_set_bearer accepts a valid DID and
 * handle and rejects invalid ones (#124: it used to reject every valid pair,
 * so sign-in through the OAuth node could never complete).
 */

#include "wolfram/agent.h"

#include "test.h"

#include <string.h>

int main(void) {
    wf_agent *a = wf_agent_new("https://auth.example.com");
    WF_CHECK(a != NULL);
    if (!a) WF_TEST_SUMMARY();

    WF_CHECK(wf_agent_set_bearer(a, "token", "alice.example.com",
                                 "did:plc:abcdefghijklmnopqrstuvwx") == WF_OK);
    WF_CHECK(wf_agent_get_did(a) &&
             strcmp(wf_agent_get_did(a), "did:plc:abcdefghijklmnopqrstuvwx") ==
                 0);
    WF_CHECK(wf_agent_get_handle(a) &&
             strcmp(wf_agent_get_handle(a), "alice.example.com") == 0);
    WF_CHECK(wf_agent_set_bearer(a, "token", "bob.example.com",
                                 "did:web:example.com") == WF_OK);

    WF_CHECK(wf_agent_set_bearer(a, "token", "not a handle",
                                 "did:plc:abcdefghijklmnopqrstuvwx") ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_set_bearer(a, "token", "alice.example.com", "plc:abc") ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_set_bearer(a, "", "alice.example.com",
                                 "did:plc:abcdefghijklmnopqrstuvwx") ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_set_bearer(NULL, "t", "alice.example.com",
                                 "did:web:example.com") == WF_ERR_INVALID_ARG);

    wf_agent_free(a);
    WF_TEST_SUMMARY();
}
