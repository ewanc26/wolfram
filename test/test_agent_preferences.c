/*
 * test_agent_preferences.c — unit tests for preferences and push API argument handling.
 */

#include "wolfram/agent.h"
#include "test.h"

int main(void) {
    wf_agent *agent = wf_agent_new("https://example.com");
    WF_CHECK(agent != NULL);

    wf_response res = {0};

    /* Invalid argument cases */
    WF_CHECK(wf_agent_put_preferences(NULL, "[]", &res) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_put_preferences(agent, NULL, &res) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_put_preferences(agent, "[]", NULL) == WF_ERR_INVALID_ARG);
    /* JSON parsing test omitted to avoid network/hanging */

    /* Network-dependent call skipped in unit test to avoid hanging. */

    /* Register push invalid args */
    WF_CHECK(wf_agent_register_push(NULL, "did:plc:test", "tok", &res) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_register_push(agent, NULL, "tok", &res) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_register_push(agent, "did:plc:test", NULL, &res) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_register_push(agent, "did:plc:test", "tok", NULL) == WF_ERR_INVALID_ARG);

    /* Unregister push invalid args */
    WF_CHECK(wf_agent_unregister_push(NULL, "did:plc:test", "tok", &res) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_unregister_push(agent, NULL, "tok", &res) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_unregister_push(agent, "did:plc:test", NULL, &res) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_unregister_push(agent, "did:plc:test", "tok", NULL) == WF_ERR_INVALID_ARG);

    wf_agent_free(agent);
    WF_TEST_SUMMARY();
    return 0; /* unreachable */
}
