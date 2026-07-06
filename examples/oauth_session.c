/**
 * oauth_session.c — OAuth authorization flow with session persistence.
 *
 * Demonstrates:
 *   1. Discovery (resource + server metadata)
 *   2. Client metadata fetch
 *   3. Authorization begin (PKCE, DPoP, PAR)
 *   4. Callback completion (state validation, code exchange, DID verification)
 *   5. Session serialization/deserialization
 *   6. Authenticated XRPC call with auto-refresh
 *
 * Usage:
 *   oauth_session <pds-base-url> <handle-or-did> [session-file]
 *
 * The session file defaults to "oauth_session.json" and is persistent:
 * subsequent runs reuse the saved session.
 *
 * For the initial run, the program prints an authorization URL. Open it
 * in a browser, authorize, and paste the full callback URL back into
 * this program.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wolfram/wolfram.h"
#include "wolfram/auth_client.h"

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) { fclose(f); return NULL; }
    char *data = malloc((size_t)len + 1);
    if (!data) { fclose(f); return NULL; }
    size_t n = fread(data, 1, (size_t)len, f);
    fclose(f);
    data[n] = '\0';
    return data;
}

static int write_file(const char *path, const char *data) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(data, 1, strlen(data), f);
    fclose(f);
    return (int)n;
}

static void parse_callback_url(const char *url,
                               wf_oauth_callback_params *params) {
    memset(params, 0, sizeof(*params));
    const char *q = strchr(url, '?');
    if (!q) q = strchr(url, '#');
    if (!q) return;
    q++;

    while (*q) {
        const char *amp = strchr(q, '&');
        size_t pair_len = amp ? (size_t)(amp - q) : strlen(q);

        const char *eq = memchr(q, '=', pair_len);
        if (!eq) { q = amp ? amp + 1 : q + pair_len; continue; }

        size_t name_len = (size_t)(eq - q);
        size_t val_len = pair_len - name_len - 1;

        if (name_len == 5 && memcmp(q, "state", 5) == 0) {
            params->state = strndup(eq + 1, val_len);
        } else if (name_len == 4 && memcmp(q, "code", 4) == 0) {
            params->code = strndup(eq + 1, val_len);
        } else if (name_len == 3 && memcmp(q, "iss", 3) == 0) {
            params->issuer = strndup(eq + 1, val_len);
        } else if (name_len == 5 && memcmp(q, "error", 5) == 0) {
            params->error = strndup(eq + 1, val_len);
        }

        q = amp ? amp + 1 : q + pair_len;
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <pds-base-url> <handle-or-did> [session-file]\n", argv[0]);
        return 1;
    }

    const char *base_url = argv[1];
    const char *handle_or_did = argv[2];
    const char *session_path = (argc > 3) ? argv[3] : "oauth_session.json";
    const char *redirect_uri = "http://127.0.0.1:8080/callback";
    const char *client_id = redirect_uri;

    wf_xrpc_client *transport = wf_xrpc_client_new(base_url);
    if (!transport) {
        fprintf(stderr, "failed to create transport\n");
        return 1;
    }

    /* Try to load persisted session first */
    char *session_json = read_file(session_path);
    if (session_json) {
        printf("Restoring session from %s...\n", session_path);

        wf_oauth_session_state session = {0};
        wf_status s = wf_oauth_session_state_parse(
            session_json, strlen(session_json), NULL, &session);
        free(session_json);

        if (s != WF_OK) {
            fprintf(stderr, "session parse failed: %d, re-authenticating\n", s);
        } else {
            /* Resolve server and client metadata for the session's issuer */
            wf_oauth_resource_metadata resource = {0};
            wf_oauth_server_metadata server = {0};
            wf_oauth_client_metadata client = {0};

            s = wf_oauth_discover(transport, session.issuer, &resource, &server);
            if (s == WF_OK)
                s = wf_oauth_client_metadata_get(transport, client_id, &client);

            if (s != WF_OK) {
                fprintf(stderr, "metadata discovery failed: %d\n", s);
                wf_oauth_session_state_free(&session);
                goto done;
            }

            wf_oauth_client_auth client_auth = {
                .client_id = client_id,
                .signing_key = NULL,
            };

            int64_t now = time(NULL);
            if (session.expires_at > 0 && now >= session.expires_at) {
                printf("Token expired, refreshing...\n");
                s = wf_oauth_session_refresh(transport, &server, &client_auth,
                                             &session, now);
                if (s != WF_OK) {
                    fprintf(stderr, "session refresh failed: %d\n", s);
                    wf_oauth_session_state_free(&session);
                    wf_oauth_resource_metadata_free(&resource);
                    wf_oauth_server_metadata_free(&server);
                    wf_oauth_client_metadata_free(&client);
                    goto done;
                }
                char *fresh_json = NULL;
                wf_oauth_session_state_serialize(&session, &fresh_json);
                if (fresh_json) {
                    write_file(session_path, fresh_json);
                    free(fresh_json);
                }
                printf("Session refreshed.\n");
            }

            wf_auth_client *auth = wf_auth_client_new(transport, &session,
                                                       &server, &client_auth);
            if (!auth) {
                fprintf(stderr, "failed to create auth client\n");
                wf_oauth_session_state_free(&session);
                wf_oauth_resource_metadata_free(&resource);
                wf_oauth_server_metadata_free(&server);
                wf_oauth_client_metadata_free(&client);
                goto done;
            }

            wf_response res = {0};
            s = wf_auth_client_query(auth, "com.atproto.repo.describeRepo",
                                     "repo=did:plc:test", &res);
            if (s == WF_OK || s == WF_ERR_HTTP)
                printf("HTTP %ld\n%s\n", res.status, res.body ? res.body : "");
            else
                fprintf(stderr, "query failed: %d\n", s);
            wf_response_free(&res);
            wf_auth_client_free(auth);
            wf_oauth_session_state_free(&session);
            wf_oauth_resource_metadata_free(&resource);
            wf_oauth_server_metadata_free(&server);
            wf_oauth_client_metadata_free(&client);
            goto done;
        }
    }

    /* No valid session — start full OAuth flow */
    printf("No session found. Starting OAuth authorization...\n");

    wf_oauth_resource_metadata resource = {0};
    wf_oauth_server_metadata server = {0};
    wf_oauth_client_metadata client = {0};

    wf_status s = wf_oauth_discover(transport, base_url, &resource, &server);
    if (s != WF_OK) {
        fprintf(stderr, "metadata discovery failed: %d\n", s);
        goto done;
    }

    s = wf_oauth_client_metadata_get(transport, client_id, &client);
    if (s != WF_OK) {
        fprintf(stderr, "client metadata fetch failed: %d\n", s);
        goto done;
    }

    wf_oauth_client_auth client_auth = {
        .client_id = client_id,
        .signing_key = NULL,
    };

    wf_oauth_authorization_begin_options opts = {
        .redirect_uri = redirect_uri,
        .scope = "atproto",
        .login_hint = handle_or_did,
        .now = time(NULL),
        .state_ttl = 600,
    };

    wf_oauth_authorization_begin_result begin = {0};
    s = wf_oauth_authorization_begin(transport, &server, &client,
                                     &client_auth, &opts, &begin);
    if (s != WF_OK) {
        fprintf(stderr, "authorization begin failed: %d\n", s);
        goto done;
    }

    printf("\nOpen this URL in your browser:\n%s\n", begin.authorization_url);
    printf("\nAfter authorizing, paste the full callback URL here:\n> ");

    char cb_url[8192];
    if (!fgets(cb_url, sizeof(cb_url), stdin)) {
        fprintf(stderr, "failed to read callback URL\n");
        wf_oauth_authorization_begin_result_free(&begin);
        goto done;
    }
    size_t cb_len = strlen(cb_url);
    if (cb_len > 0 && cb_url[cb_len - 1] == '\n') cb_url[cb_len - 1] = '\0';

    write_file("oauth_state.json", begin.state_json);

    wf_oauth_callback_params cb_params = {0};
    parse_callback_url(cb_url, &cb_params);

    wf_oauth_authorization_complete_result complete = {0};
    s = wf_oauth_authorization_complete(
        transport, &server, &client, &client_auth,
        &cb_params, begin.state, begin.state_json, strlen(begin.state_json),
        redirect_uri, time(NULL), &complete);

    wf_oauth_authorization_begin_result_free(&begin);
    free(cb_params.state);
    free(cb_params.code);
    free(cb_params.issuer);
    free(cb_params.error);

    if (s != WF_OK) {
        fprintf(stderr, "authorization complete failed: %d\n", s);
        if (complete.error)
            fprintf(stderr, "server error: %s: %s\n",
                    complete.error, complete.error_description ? complete.error_description : "");
        wf_oauth_authorization_complete_result_free(&complete);
        goto done;
    }

    if (complete.session_json) {
        write_file(session_path, complete.session_json);
        printf("Session saved to %s\n", session_path);
    }

    wf_auth_client *auth = wf_auth_client_new(transport, &complete.session,
                                               &server, &client_auth);
    if (!auth) {
        fprintf(stderr, "failed to create auth client\n");
        wf_oauth_authorization_complete_result_free(&complete);
        goto done;
    }

    /* Resolve the handle/DID first */
    char did_buf[256];
    const char *repo_did;
    if (strncmp(handle_or_did, "did:", 4) == 0) {
        repo_did = handle_or_did;
    } else {
        char *resolved = NULL;
        s = wf_handle_resolve(transport, handle_or_did, &resolved);
        if (s == WF_OK && resolved) {
            snprintf(did_buf, sizeof(did_buf), "%s", resolved);
            repo_did = did_buf;
            free(resolved);
        } else {
            repo_did = handle_or_did;
        }
    }

    wf_response res = {0};
    char query[512];
    snprintf(query, sizeof(query), "repo=%s", repo_did);
    s = wf_auth_client_query(auth, "com.atproto.repo.describeRepo", query, &res);

    if (s == WF_OK || s == WF_ERR_HTTP)
        printf("HTTP %ld\n%s\n", res.status, res.body ? res.body : "(empty)");
    else
        fprintf(stderr, "query failed: status %d\n", s);

    wf_response_free(&res);
    wf_auth_client_free(auth);
    wf_oauth_authorization_complete_result_free(&complete);

done:
    wf_xrpc_client_free(transport);
    remove("oauth_state.json");
    return 0;
}
