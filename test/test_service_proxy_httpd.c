/*
 * test_service_proxy_httpd.c — reaching another service through, or with a
 * token from, the account's PDS, over a real localhost socket.
 *
 * Two mock servers stand in for a PDS and for a service that is called
 * directly. The checks read what each server received off the wire:
 *   - wf_agent_query_via / wf_agent_procedure_via put the given atproto-proxy
 *     on one request and leave the client's own setting alone;
 *   - getServiceAuth goes to the PDS with aud/exp/lxm and no atproto-proxy,
 *     even when the agent's client proxies every other request;
 *   - wf_agent_service_client_new sends the returned token as the Bearer to
 *     the service, and when the PDS refuses, or replies with no token, it
 *     returns no client and nothing reaches the service.
 *
 * Built only when WOLFRAM_BUILD_TEST_HTTPD=ON.
 */

#include "wolfram/agent.h"
#include "wolfram/server_typed.h"
#include "wolfram/xrpc.h"

#include "mock_pds.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHAT_PROXY "did:web:api.bsky.chat#bsky_chat"
#define LABELER_DID "did:plc:ar7c4by46qjdydhdevvrndac"
#define SERVICE_DID "did:web:feed.example"
#define SERVICE_LXM "app.bsky.feed.getFeedSkeleton"

static int arg_eq(wf_mock_pds *pds, const char *name, const char *expect) {
    char *v = wf_mock_pds_get_last_arg(pds, name);
    int ok = expect ? (v && strcmp(v, expect) == 0) : v == NULL;
    free(v);
    return ok;
}

int main(void) {
    wf_mock_pds *pds = NULL;
    wf_mock_pds *svc = NULL;
    int pds_port = 0;
    int svc_port = 0;
    WF_CHECK(wf_mock_pds_start(&pds, &pds_port) == WF_OK);
    WF_CHECK(wf_mock_pds_start(&svc, &svc_port) == WF_OK);
    if (!pds || !svc) {
        wf_mock_pds_free(pds);
        wf_mock_pds_free(svc);
        WF_TEST_SUMMARY();
    }

    /* wf_agent_resume refreshes the session it is given. */
    WF_CHECK(wf_mock_pds_register(
                 pds, "com.atproto.server.refreshSession",
                 "{\"did\":\"did:plc:abc123\",\"handle\":\"alice.test\","
                 "\"accessJwt\":\"eyJ.fake.access\","
                 "\"refreshJwt\":\"eyJ.fake.refresh\",\"active\":true}") ==
             WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "tools.ozone.moderation.getRecord",
                                  "{\"uri\":\"at://x\"}") == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "chat.bsky.convo.sendMessage",
                                  "{\"id\":\"m1\"}") == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "chat.bsky.convo.listConvos",
                                  "{\"convos\":[]}") == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "com.atproto.server.getServiceAuth",
                                  "{\"token\":\"svc.jwt.token\"}") == WF_OK);
    WF_CHECK(wf_mock_pds_register(svc, SERVICE_LXM, "{\"feed\":[]}") == WF_OK);

    char pds_url[64];
    char svc_url[64];
    snprintf(pds_url, sizeof pds_url, "http://127.0.0.1:%d", pds_port);
    snprintf(svc_url, sizeof svc_url, "http://127.0.0.1:%d", svc_port);

    wf_agent *agent = wf_agent_new(pds_url);
    WF_CHECK(agent != NULL);
    wf_session_data data;
    memset(&data, 0, sizeof data);
    data.did = "did:plc:abc123";
    data.handle = "alice.test";
    data.access_jwt = "eyJ.fake.access";
    data.refresh_jwt = "eyJ.fake.refresh";
    data.active = 1;
    WF_CHECK(agent && wf_agent_resume(agent, &data) == WF_OK);
    if (!agent) {
        wf_mock_pds_free(pds);
        wf_mock_pds_free(svc);
        WF_TEST_SUMMARY();
    }

    char *labeler = NULL;
    WF_CHECK(wf_xrpc_proxy_identifier(LABELER_DID, WF_PROXY_SERVICE_LABELER,
                                      &labeler) == WF_OK);

    /* ---- one proxied request, built from the labeler's DID ---- */
    {
        wf_xrpc_param p[] = {{"uri", "at://did:plc:x/app.bsky.feed.post/1"}};
        wf_response res = {0};
        WF_CHECK(wf_agent_query_via(agent, labeler,
                                    "tools.ozone.moderation.getRecord", p, 1,
                                    &res) == WF_OK);
        WF_CHECK(res.status == 200);
        wf_response_free(&res);
        const char *seen = wf_mock_pds_get_last_proxy(pds);
        WF_CHECK(seen && labeler && strcmp(seen, labeler) == 0);
        WF_CHECK(wf_mock_pds_get_last_authorization(pds) &&
                 strcmp(wf_mock_pds_get_last_authorization(pds),
                        "Bearer eyJ.fake.access") == 0);
        WF_CHECK(arg_eq(pds, "uri", "at://did:plc:x/app.bsky.feed.post/1"));
        /* The client has no proxy of its own afterwards. */
        WF_CHECK(wf_xrpc_client_get_proxy(wf_agent_get_xrpc_client(agent)) ==
                 NULL);
    }
    {
        wf_response res = {0};
        WF_CHECK(wf_agent_procedure_via(agent, CHAT_PROXY,
                                        "chat.bsky.convo.sendMessage",
                                        "{\"convoId\":\"c\"}", &res) == WF_OK);
        wf_response_free(&res);
        const char *seen = wf_mock_pds_get_last_proxy(pds);
        WF_CHECK(seen && strcmp(seen, CHAT_PROXY) == 0);
    }
    {
        wf_response res = {0};
        WF_CHECK(wf_agent_query_via(agent, "did:web:x#a\r\nX-Evil: 1",
                                    "chat.bsky.convo.listConvos", NULL, 0,
                                    &res) == WF_ERR_INVALID_ARG);
    }

    /* ---- getServiceAuth stays on the PDS under a session-wide proxy ---- */
    WF_CHECK(wf_xrpc_client_set_proxy(wf_agent_get_xrpc_client(agent),
                                      CHAT_PROXY) == WF_OK);
    {
        wf_response res = {0};
        WF_CHECK(wf_agent_query_via(agent, CHAT_PROXY,
                                    "chat.bsky.convo.listConvos", NULL, 0,
                                    &res) == WF_OK);
        wf_response_free(&res);
        WF_CHECK(wf_mock_pds_get_last_proxy(pds) != NULL);
    }
    {
        wf_server_auth_token tok = {0};
        WF_CHECK(wf_agent_get_service_auth_typed(agent, SERVICE_DID, 1900000000,
                                                 SERVICE_LXM, &tok) == WF_OK);
        WF_CHECK(tok.token && strcmp(tok.token, "svc.jwt.token") == 0);
        wf_server_auth_token_free(&tok);
        const char *nsid = NULL, *method = NULL, *body = NULL;
        WF_CHECK(wf_mock_pds_get_last_request(pds, &nsid, &method, &body) ==
                 WF_OK);
        WF_CHECK(nsid &&
                 strcmp(nsid, "com.atproto.server.getServiceAuth") == 0);
        WF_CHECK(method && strcmp(method, "GET") == 0);
        WF_CHECK(wf_mock_pds_get_last_proxy(pds) == NULL);
        WF_CHECK(arg_eq(pds, "aud", SERVICE_DID));
        WF_CHECK(arg_eq(pds, "exp", "1900000000"));
        WF_CHECK(arg_eq(pds, "lxm", SERVICE_LXM));
    }

    /* ---- the token is the Bearer on a client for the service ---- */
    {
        wf_xrpc_client *sc = NULL;
        WF_CHECK(wf_agent_service_client_new(agent, svc_url, SERVICE_DID,
                                             SERVICE_LXM, 0, &sc) == WF_OK);
        WF_CHECK(sc != NULL);
        WF_CHECK(arg_eq(pds, "exp", NULL));
        if (sc) {
            wf_xrpc_param p[] = {{"feed", "at://did:plc:x/g/1"}};
            wf_response res = {0};
            WF_CHECK(wf_xrpc_query_params(sc, SERVICE_LXM, p, 1, &res) ==
                     WF_OK);
            WF_CHECK(res.status == 200);
            wf_response_free(&res);
            const char *az = wf_mock_pds_get_last_authorization(svc);
            WF_CHECK(az && strcmp(az, "Bearer svc.jwt.token") == 0);
            /* A direct call: the service is not asked to forward anything. */
            WF_CHECK(wf_mock_pds_get_last_proxy(svc) == NULL);
            wf_xrpc_client_free(sc);
        }
    }

    /* ---- refusals: no client, and nothing reaches the service ---- */
    {
        const char *nsid = NULL, *method = NULL, *body = NULL;
        wf_xrpc_client *sc = (wf_xrpc_client *)1;

        WF_CHECK(wf_mock_pds_register_status(
                     pds, "com.atproto.server.getServiceAuth", 400,
                     "{\"error\":\"BadExpiration\",\"message\":\"no\"}") ==
                 WF_OK);
        WF_CHECK(wf_agent_service_client_new(agent, svc_url, SERVICE_DID,
                                             SERVICE_LXM, 0,
                                             &sc) == WF_ERR_HTTP);
        WF_CHECK(sc == NULL);

        WF_CHECK(wf_mock_pds_register_status(
                     pds, "com.atproto.server.getServiceAuth", 401,
                     "{\"error\":\"AuthRequired\",\"message\":\"no\"}") ==
                 WF_OK);
        sc = (wf_xrpc_client *)1;
        wf_status st = wf_agent_service_client_new(agent, svc_url, SERVICE_DID,
                                                   NULL, 0, &sc);
        WF_CHECK(st == WF_ERR_AUTH || st == WF_ERR_HTTP);
        WF_CHECK(sc == NULL);

        /* A 200 that carries no token is not a token. */
        WF_CHECK(wf_mock_pds_register(pds, "com.atproto.server.getServiceAuth",
                                      "{}") == WF_OK);
        sc = (wf_xrpc_client *)1;
        WF_CHECK(wf_agent_service_client_new(agent, svc_url, SERVICE_DID, NULL,
                                             0, &sc) == WF_ERR_PARSE);
        WF_CHECK(sc == NULL);

        /* The service saw only the one successful call above. */
        WF_CHECK(wf_mock_pds_get_last_request(svc, &nsid, &method, &body) ==
                 WF_OK);
        WF_CHECK(nsid && strcmp(nsid, SERVICE_LXM) == 0);

        sc = (wf_xrpc_client *)1;
        WF_CHECK(wf_agent_service_client_new(agent, NULL, SERVICE_DID, NULL, 0,
                                             &sc) == WF_ERR_INVALID_ARG);
        WF_CHECK(sc == NULL);
        WF_CHECK(wf_agent_service_client_new(agent, svc_url, "", NULL, 0,
                                             &sc) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_service_client_new(agent, svc_url, SERVICE_DID, NULL,
                                             0, NULL) == WF_ERR_INVALID_ARG);
    }

    free(labeler);
    wf_agent_free(agent);
    wf_mock_pds_free(svc);
    wf_mock_pds_free(pds);
    WF_TEST_SUMMARY();
}
