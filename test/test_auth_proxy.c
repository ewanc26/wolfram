/**
 * test_auth_proxy.c — the atproto-proxy header on a real HTTP request.
 *
 * test_oauth_refresh.c checks the header at an injected transport handler.
 * This test goes further: an authenticated wf_auth_client call is sent over a
 * real localhost socket to the mock PDS, and the server reads the header off
 * the wire. That is the request a PDS would see, so it is the check that
 * matters for chat.bsky.* (which the PDS forwards only when it is named).
 */
#include "test.h"
#include "mock_pds.h"
#include "wolfram/auth_client.h"
#include "wolfram/oauth.h"
#include "wolfram/xrpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHAT_PROXY "did:web:api.bsky.chat#bsky_chat"

int main(void) {
    wf_mock_pds *pds = NULL;
    int port = 0;
    WF_CHECK(wf_mock_pds_start(&pds, &port) == WF_OK);
    WF_CHECK(pds != NULL && port > 0);
    if (!pds || port <= 0) {
        WF_TEST_SUMMARY();
    }

    WF_CHECK(wf_mock_pds_register(pds, "chat.bsky.convo.listConvos",
                                  "{\"convos\":[]}") == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "app.bsky.feed.getTimeline",
                                  "{\"feed\":[]}") == WF_OK);

    char base_url[64];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%d", port);

    wf_xrpc_client *client = wf_xrpc_client_new(base_url);
    WF_CHECK(client != NULL);

    wf_oauth_dpop_key *key = NULL;
    WF_CHECK(wf_oauth_dpop_key_generate(&key) == WF_OK);

    wf_oauth_server_metadata server = {0};
    server.issuer = "https://auth.example";
    server.token_endpoint = "https://auth.example/token";

    wf_oauth_client_auth auth = {0};
    auth.client_id = "https://app.example/client.json";

    /* Far-future expiry: the client never tries to refresh during the test. */
    wf_oauth_session_state session = {0};
    session.issuer = strdup("https://auth.example");
    session.subject = strdup("did:plc:alice");
    session.audience = strdup(base_url);
    session.access_token = strdup("TOKEN");
    session.refresh_token = strdup("refresh");
    session.expires_at = 2000000000;
    session.dpop_key = key;

    wf_auth_client *ac =
        client ? wf_auth_client_new(client, &session, &server, &auth) : NULL;
    WF_CHECK(ac != NULL);

    if (ac && client) {
        /* A chat call through a client that names the chat service: the header
         * is on the request the server receives. */
        WF_CHECK(wf_xrpc_client_set_proxy(client, CHAT_PROXY) == WF_OK);
        {
            wf_response out = {0};
            WF_CHECK(wf_auth_client_query(ac, "chat.bsky.convo.listConvos",
                                          NULL, &out) == WF_OK);
            WF_CHECK(out.status == 200);
            wf_response_free(&out);
        }
        {
            const char *seen = wf_mock_pds_get_last_proxy(pds);
            WF_CHECK(seen != NULL && strcmp(seen, CHAT_PROXY) == 0);
        }

        /* With no identifier set on the client, the same kind of call goes out
         * without the header. */
        WF_CHECK(wf_xrpc_client_set_proxy(client, NULL) == WF_OK);
        {
            wf_response out = {0};
            WF_CHECK(wf_auth_client_query(ac, "app.bsky.feed.getTimeline", NULL,
                                          &out) == WF_OK);
            WF_CHECK(out.status == 200);
            wf_response_free(&out);
        }
        WF_CHECK(wf_mock_pds_get_last_proxy(pds) == NULL);
    }

    wf_auth_client_free(ac);
    wf_xrpc_client_free(client);
    wf_oauth_session_state_free(&session);
    wf_mock_pds_free(pds);
    WF_TEST_SUMMARY();
}
