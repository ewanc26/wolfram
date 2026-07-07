/**
 * test_oauth_refresh.c — offline tests for transparent token refresh and
 * DPoP nonce retry in wf_auth_client, plus wf_auth_client_ensure_valid.
 *
 * No real network is used. The transport's injected handler
 * (wf_xrpc_set_handler) serves a small fake XRPC/metadata backend so the real
 * refresh and DPoP-nonce state machines are exercised deterministically.
 */

#include "test.h"
#include "wolfram/auth_client.h"
#include "wolfram/oauth.h"
#include "wolfram/xrpc.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    int resource_calls;
    int token_calls;
    int refresh_fail;
    int nonce_on_401;
    char observed_token[128];
} fake_state;

static wf_status fake_handler(void *ud, const char *method, const char *url,
                              const char *content_type, const char *body,
                              size_t body_len, const wf_http_header *headers,
                              size_t header_count, wf_response *out) {
    (void)method;
    (void)content_type;
    (void)body;
    (void)body_len;
    fake_state *st = ud;

    /* OAuth token endpoint: return a freshly minted token (or fail). */
    if (strstr(url, "/token")) {
        st->token_calls++;
        if (st->refresh_fail) {
            static const char err[] = "{\"error\":\"invalid_grant\"}";
            out->status = 400;
            out->body = strdup(err);
            out->body_len = strlen(err);
            return WF_ERR_HTTP;
        }
        static const char tok[] =
            "{\"access_token\":\"NEWTOKEN\",\"token_type\":\"DPoP\","
            "\"sub\":\"did:plc:alice\",\"scope\":\"atproto\","
            "\"refresh_token\":\"refresh2\",\"expires_in\":3600}";
        out->status = 200;
        out->body = strdup(tok);
        out->body_len = strlen(tok);
        return WF_OK;
    }

    /* DID resolution (did:plc). */
    if (strstr(url, "plc.directory")) {
        static const char did[] =
            "{\"id\":\"did:plc:alice\",\"service\":[{\"type\":"
            "\"AtprotoPersonalDataServer\",\"serviceEndpoint\":"
            "\"https://pds.example\"}]}";
        out->status = 200;
        out->body = strdup(did);
        out->body_len = strlen(did);
        return WF_OK;
    }

    /* Protected-resource metadata. */
    if (strstr(url, "oauth-protected-resource")) {
        static const char rm[] =
            "{\"resource\":\"https://pds.example\","
            "\"authorization_servers\":[\"https://auth.example\"]}";
        out->status = 200;
        out->body = strdup(rm);
        out->body_len = strlen(rm);
        return WF_OK;
    }

    /* Authorization-server metadata. */
    if (strstr(url, "oauth-authorization-server")) {
        static const char sm[] =
            "{\"issuer\":\"https://auth.example\","
            "\"authorization_endpoint\":\"https://auth.example/authorize\","
            "\"token_endpoint\":\"https://auth.example/token\","
            "\"pushed_authorization_request_endpoint\":\"https://auth.example/par\","
            "\"response_types_supported\":[\"code\"],"
            "\"grant_types_supported\":[\"authorization_code\",\"refresh_token\"],"
            "\"code_challenge_methods_supported\":[\"S256\"],"
            "\"token_endpoint_auth_methods_supported\":[\"none\",\"private_key_jwt\"],"
            "\"token_endpoint_auth_signing_alg_values_supported\":[\"ES256\"],"
            "\"scopes_supported\":[\"atproto\"],"
            "\"dpop_signing_alg_values_supported\":[\"ES256\"],"
            "\"authorization_response_iss_parameter_supported\":true,"
            "\"require_pushed_authorization_requests\":true,"
            "\"client_id_metadata_document_supported\":true}";
        out->status = 200;
        out->body = strdup(sm);
        out->body_len = strlen(sm);
        return WF_OK;
    }

    /* The protected XRPC resource itself. */
    st->resource_calls++;
    for (size_t i = 0; i < header_count; i++) {
        if (headers[i].name && strcmp(headers[i].name, "Authorization") == 0 &&
            headers[i].value && strncmp(headers[i].value, "Bearer ", 7) == 0) {
            snprintf(st->observed_token, sizeof(st->observed_token), "%s",
                     headers[i].value + 7);
        }
    }
    if (st->resource_calls == 1) {
        out->status = 401;
        if (st->nonce_on_401) {
            out->dpop_nonce = strdup("server-nonce-123");
        }
        return WF_ERR_HTTP;
    }
    static const char ok[] = "{\"ok\":true}";
    out->status = 200;
    out->body = strdup(ok);
    out->body_len = strlen(ok);
    return WF_OK;
}

typedef struct {
    wf_xrpc_client *client;
    wf_auth_client *ac;
    wf_oauth_session_state session;
    wf_oauth_server_metadata server;
    wf_oauth_client_auth auth;
    wf_oauth_dpop_key *key;
} scenario;

static void scenario_init(scenario *sc, fake_state *st) {
    memset(sc, 0, sizeof(*sc));
    sc->client = wf_xrpc_client_new("https://pds.example");
    wf_xrpc_set_handler(sc->client, fake_handler, st);
    wf_oauth_dpop_key_generate(&sc->key);

    sc->server.issuer = "https://auth.example";
    sc->server.token_endpoint = "https://auth.example/token";

    sc->auth.client_id = "https://app.example/client.json";

    sc->session.issuer = strdup("https://auth.example");
    sc->session.subject = strdup("did:plc:alice");
    sc->session.audience = strdup("https://pds.example");
    sc->session.access_token = strdup("OLDTOKEN");
    sc->session.refresh_token = strdup("refresh");
    sc->session.expires_at = 2000000000; /* far future: not preemptively refreshed */
    sc->session.dpop_key = sc->key;

    sc->ac = wf_auth_client_new(sc->client, &sc->session, &sc->server, &sc->auth);
}

static void scenario_free(scenario *sc) {
    wf_auth_client_free(sc->ac);
    wf_xrpc_client_free(sc->client);
    wf_oauth_session_state_free(&sc->session); /* frees strings and dpop_key */
}

/* The access token was rejected (401, no nonce): the client must refresh once
 * and re-issue with the new token, succeeding in the end. */
static void test_refresh_on_401(void) {
    fake_state st = {0};
    scenario sc;
    scenario_init(&sc, &st);

    wf_response out = {0};
    wf_status s = wf_auth_client_query(sc.ac, "com.atproto.repo.describeRepo",
                                       NULL, &out);

    WF_CHECK(s == WF_OK);
    WF_CHECK(st.resource_calls == 2);
    WF_CHECK(st.token_calls == 1);
    WF_CHECK(strcmp(st.observed_token, "NEWTOKEN") == 0);
    WF_CHECK(strcmp(sc.session.access_token, "NEWTOKEN") == 0);
    WF_CHECK(out.status == 200);

    wf_response_free(&out);
    scenario_free(&sc);
}

/* A DPoP nonce rotation (401 + fresh dpop-nonce) must re-issue once without
 * refreshing the token. */
static void test_dpop_nonce_retry(void) {
    fake_state st = {0};
    st.nonce_on_401 = 1;
    scenario sc;
    scenario_init(&sc, &st);

    wf_response out = {0};
    wf_status s = wf_auth_client_query(sc.ac, "com.atproto.repo.describeRepo",
                                       NULL, &out);

    WF_CHECK(s == WF_OK);
    WF_CHECK(st.resource_calls == 2);
    WF_CHECK(st.token_calls == 0);
    WF_CHECK(strcmp(sc.session.access_token, "OLDTOKEN") == 0);
    WF_CHECK(out.status == 200);

    wf_response_free(&out);
    scenario_free(&sc);
}

/* When the refresh is rejected, the original 401 error is propagated and only
 * a single resource attempt is made. */
static void test_refresh_failure_propagates(void) {
    fake_state st = {0};
    st.refresh_fail = 1;
    scenario sc;
    scenario_init(&sc, &st);

    wf_response out = {0};
    wf_status s = wf_auth_client_query(sc.ac, "com.atproto.repo.describeRepo",
                                       NULL, &out);

    WF_CHECK(s == WF_ERR_HTTP);
    WF_CHECK(st.resource_calls == 1);
    WF_CHECK(st.token_calls == 1);
    WF_CHECK(strcmp(sc.session.access_token, "OLDTOKEN") == 0);

    wf_response_free(&out);
    scenario_free(&sc);
}

/* The same transparent refresh applies to procedures. */
static void test_refresh_on_401_procedure(void) {
    fake_state st = {0};
    scenario sc;
    scenario_init(&sc, &st);

    wf_response out = {0};
    wf_status s = wf_auth_client_procedure(sc.ac, "com.atproto.repo.createRecord",
                                          "{}", &out);

    WF_CHECK(s == WF_OK);
    WF_CHECK(st.resource_calls == 2);
    WF_CHECK(st.token_calls == 1);

    wf_response_free(&out);
    scenario_free(&sc);
}

/* wf_auth_client_ensure_valid refreshes only when missing/expired, and leaves
 * a still-valid token untouched (no network). */
static void test_ensure_valid(void) {
    /* Valid token: no network, token unchanged. */
    {
        fake_state st = {0};
        scenario sc;
        scenario_init(&sc, &st);
        wf_status s = wf_auth_client_ensure_valid(sc.ac);
        WF_CHECK(s == WF_OK);
        WF_CHECK(st.token_calls == 0);
        WF_CHECK(strcmp(sc.session.access_token, "OLDTOKEN") == 0);
        scenario_free(&sc);
    }

    /* Expired token: refresh in place. */
    {
        fake_state st = {0};
        scenario sc;
        scenario_init(&sc, &st);
        sc.session.expires_at = 1000000000; /* firmly in the past */
        wf_status s = wf_auth_client_ensure_valid(sc.ac);
        WF_CHECK(s == WF_OK);
        WF_CHECK(st.token_calls == 1);
        WF_CHECK(strcmp(sc.session.access_token, "NEWTOKEN") == 0);
        scenario_free(&sc);
    }

    /* Missing token and no refresh token: cannot refresh. */
    {
        fake_state st = {0};
        scenario sc;
        scenario_init(&sc, &st);
        free(sc.session.access_token);
        sc.session.access_token = NULL;
        free(sc.session.refresh_token);
        sc.session.refresh_token = NULL;
        wf_status s = wf_auth_client_ensure_valid(sc.ac);
        WF_CHECK(s == WF_ERR_NOT_FOUND);
        scenario_free(&sc);
    }
}

int main(void) {
    test_refresh_on_401();
    test_dpop_nonce_retry();
    test_refresh_failure_propagates();
    test_refresh_on_401_procedure();
    test_ensure_valid();
    WF_TEST_SUMMARY();
}
