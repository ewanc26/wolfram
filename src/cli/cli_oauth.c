/*
 * cli_oauth.c — the `oauth-login` / `oauth-callback` subcommands: the
 * device/browser OAuth authorization flow, split out of main.c as its own
 * self-contained concern.
 */

#include "cli_oauth.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/oauth.h"
#include "wolfram/xrpc.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Return a heap-owned default path for the persisted OAuth pending-state file
 * (~/.wolfram_oauth_state.json), falling back to the cwd when $HOME is unset.
 * Caller frees. */
static char *oauth_default_state_path(void) {
    const char *home = getenv("HOME");
    const char *name = ".wolfram_oauth_state.json";
    if (!home) home = ".";
    size_t n = strlen(home) + 1 + strlen(name) + 1;
    char *p = malloc(n);
    if (p) snprintf(p, n, "%s/%s", home, name);
    return p;
}

/* Return a heap-owned default path for the persisted OAuth session file
 * (~/.wolfram_session.json). Caller frees. */
static char *oauth_default_session_path(void) {
    const char *home = getenv("HOME");
    const char *name = ".wolfram_session.json";
    if (!home) home = ".";
    size_t n = strlen(home) + 1 + strlen(name) + 1;
    char *p = malloc(n);
    if (p) snprintf(p, n, "%s/%s", home, name);
    return p;
}

/* Write `data` to `path` (text mode). Returns 0 on success, -1 on failure. */
static int write_text_file(const char *path, const char *data) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(data, 1, strlen(data), f);
    fclose(f);
    return (n == strlen(data)) ? 0 : -1;
}

/* Parse the query/fragment of a redirect URL into owned callback params.
 * Mirrors examples/oauth_session.c; the caller frees the strdup'd fields. */
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
        if (!eq) {
            q = amp ? amp + 1 : q + pair_len;
            continue;
        }
        size_t name_len = (size_t)(eq - q);
        size_t val_len = pair_len - name_len - 1;
        if (name_len == 5 && memcmp(q, "state", 5) == 0)
            params->state = strndup(eq + 1, val_len);
        else if (name_len == 4 && memcmp(q, "code", 4) == 0)
            params->code = strndup(eq + 1, val_len);
        else if (name_len == 3 && memcmp(q, "iss", 3) == 0)
            params->issuer = strndup(eq + 1, val_len);
        else if (name_len == 5 && memcmp(q, "error", 5) == 0)
            params->error = strndup(eq + 1, val_len);
        q = amp ? amp + 1 : q + pair_len;
    }
}

/* wolfram oauth-callback <service> --url <redirect> --state <state>
 *   [--state-file <path>] [--client-id <id>] [--redirect-uri <uri>]
 *   [--session <path>]
 *
 * Completes the OAuth flow begun by `oauth-login`: validates the callback
 * against the persisted pending state, exchanges the code for tokens, and
 * writes the resulting session to a file (default ~/.wolfram_session.json).
 * No HTTP callback server is run; the user pastes the redirect URL. */
int cmd_oauth_callback(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = NULL;
    const char *url = NULL;
    const char *state = NULL;
    const char *state_file = NULL;
    const char *client_id = NULL;
    const char *redirect_uri = NULL;
    const char *session_path = NULL;

    char **pos = malloc(sizeof(char *) * (size_t)argc);
    if (!pos) {
        fprintf(stderr, "error: out of memory\n");
        return 1;
    }
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--url") == 0 && i + 1 < argc)
            url = argv[++i];
        else if (strcmp(argv[i], "--state") == 0 && i + 1 < argc)
            state = argv[++i];
        else if (strcmp(argv[i], "--state-file") == 0 && i + 1 < argc)
            state_file = argv[++i];
        else if (strcmp(argv[i], "--client-id") == 0 && i + 1 < argc)
            client_id = argv[++i];
        else if (strcmp(argv[i], "--redirect-uri") == 0 && i + 1 < argc)
            redirect_uri = argv[++i];
        else if (strcmp(argv[i], "--session") == 0 && i + 1 < argc)
            session_path = argv[++i];
        else
            pos[pi++] = argv[i];
    }
    if (pi >= 1) service = pos[0];
    free(pos);

    if (!service || !url || !state) {
        fprintf(
            stderr,
            "error: usage: wolfram oauth-callback <service> --url <redirect> "
            "--state <state> [--state-file <path>] [--client-id <id>] "
            "[--redirect-uri <uri>] [--session <path>]\n");
        return 1;
    }

    char *def_state = oauth_default_state_path();
    const char *sf = state_file ? state_file : def_state;
    char *state_json = read_text_file(sf);
    free(def_state);
    if (!state_json) {
        fprintf(stderr,
                "error: could not read pending state file '%s' "
                "(run oauth-login first)\n",
                sf);
        return 1;
    }

    if (!redirect_uri) redirect_uri = "https://localhost/callback";
    if (!client_id) client_id = redirect_uri;

    wf_xrpc_client *transport = wf_xrpc_client_new(service);
    if (!transport) {
        fprintf(stderr, "error: failed to create XRPC client\n");
        free(state_json);
        return 1;
    }

    wf_oauth_resource_metadata resource = {0};
    wf_oauth_server_metadata server = {0};
    wf_status s = wf_oauth_discover(transport, service, &resource, &server);
    if (s != WF_OK) {
        fprintf(stderr, "error: OAuth discovery failed (status %d)\n", (int)s);
        wf_oauth_resource_metadata_free(&resource);
        wf_oauth_server_metadata_free(&server);
        wf_xrpc_client_free(transport);
        free(state_json);
        return 1;
    }

    wf_oauth_client_metadata client = {0};
    s = wf_oauth_client_metadata_get(transport, client_id, &client);
    if (s != WF_OK) {
        fprintf(stderr, "error: client metadata fetch failed (status %d)\n",
                (int)s);
        wf_oauth_client_metadata_free(&client);
        wf_oauth_resource_metadata_free(&resource);
        wf_oauth_server_metadata_free(&server);
        wf_xrpc_client_free(transport);
        free(state_json);
        return 1;
    }

    wf_oauth_client_auth client_auth = {
        .client_id = client_id,
        .authorization_server_issuer = server.issuer,
        .signing_key = NULL,
    };

    wf_oauth_callback_params cb = {0};
    parse_callback_url(url, &cb);

    wf_oauth_authorization_complete_result complete = {0};
    s = wf_oauth_authorization_complete(
        transport, &server, &client, &client_auth, &cb, state, state_json,
        strlen(state_json), redirect_uri, time(NULL), &complete);

    free((void *)cb.state);
    free((void *)cb.code);
    free((void *)cb.issuer);
    free((void *)cb.error);
    wf_oauth_client_metadata_free(&client);
    wf_oauth_resource_metadata_free(&resource);
    wf_oauth_server_metadata_free(&server);
    wf_xrpc_client_free(transport);
    free(state_json);

    if (s != WF_OK) {
        fprintf(stderr, "error: authorization complete failed (status %d)\n",
                (int)s);
        if (complete.error)
            fprintf(stderr, "server error: %s: %s\n", complete.error,
                    complete.error_description ? complete.error_description
                                               : "");
        wf_oauth_authorization_complete_result_free(&complete);
        return 1;
    }

    char *def_session = oauth_default_session_path();
    const char *sp = session_path ? session_path : def_session;
    if (complete.session_json) {
        if (write_text_file(sp, complete.session_json) == 0)
            printf("session saved to %s\n", sp);
        else
            fprintf(stderr, "error: failed to write session file '%s'\n", sp);
    }
    wf_oauth_authorization_complete_result_free(&complete);
    free(def_session);
    return 0;
}

/* OAuth login demonstration — discover the authorization server for the
 * protected resource and begin a PAR flow, printing the authorization URL the
 * user must visit plus the flow state. No callback server is run. */
int cmd_oauth_login(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = NULL;
    const char *handle = NULL;
    const char *client_id = NULL;
    const char *redirect_uri = "https://localhost/callback";
    const char *state_file = NULL;

    char **pos = malloc(sizeof(char *) * (size_t)argc);
    if (!pos) {
        fprintf(stderr, "error: out of memory\n");
        return 1;
    }
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--state-file") == 0 && i + 1 < argc) {
            state_file = argv[++i];
        } else {
            pos[pi++] = argv[i];
        }
    }
    if (pi < 2) {
        fprintf(stderr, "error: usage: wolfram oauth-login <service> <handle> "
                        "[client-id] [redirect-uri] [--state-file <path>]\n");
        free(pos);
        return 1;
    }
    service = pos[0];
    handle = pos[1];
    if (pi >= 3) client_id = pos[2];
    if (pi >= 4) redirect_uri = pos[3];
    free(pos);

    wf_xrpc_client *transport = wf_xrpc_client_new(service);
    if (!transport) {
        fprintf(stderr, "error: failed to create XRPC client\n");
        return 1;
    }

    wf_oauth_resource_metadata resource = {0};
    wf_oauth_server_metadata server = {0};
    wf_status s = wf_oauth_discover(transport, service, &resource, &server);
    if (s != WF_OK) {
        fprintf(stderr, "error: OAuth discovery failed (status %d)\n", (int)s);
        wf_oauth_resource_metadata_free(&resource);
        wf_oauth_server_metadata_free(&server);
        wf_xrpc_client_free(transport);
        return 1;
    }

    printf("authorization server: %s\n",
           server.authorization_endpoint ? server.authorization_endpoint : "?");
    if (server.issuer) {
        printf("issuer: %s\n", server.issuer);
    }

    if (!client_id) {
        printf("\nOAuth discovery complete. Provide a client-id "
               "(and redirect-uri) to begin the PAR flow:\n"
               "  wolfram oauth-login %s %s <client-id> [redirect-uri] "
               "[--state-file <path>]\n",
               service, handle);
        wf_oauth_resource_metadata_free(&resource);
        wf_oauth_server_metadata_free(&server);
        wf_xrpc_client_free(transport);
        return 0;
    }

    wf_oauth_client_metadata client = {0};
    s = wf_oauth_client_metadata_get(transport, client_id, &client);
    if (s != WF_OK) {
        fprintf(stderr, "error: client metadata fetch failed (status %d)\n",
                (int)s);
        wf_oauth_resource_metadata_free(&resource);
        wf_oauth_server_metadata_free(&server);
        wf_xrpc_client_free(transport);
        return 1;
    }

    wf_oauth_client_auth client_auth = {
        .client_id = client_id,
        .authorization_server_issuer = server.issuer,
        .signing_key = NULL,
    };

    wf_oauth_authorization_begin_options opts = {
        .redirect_uri = redirect_uri,
        .scope = "atproto",
        .login_hint = handle,
        .now = time(NULL),
        .state_ttl = 600,
    };

    wf_oauth_authorization_begin_result begin = {0};
    s = wf_oauth_authorization_begin(transport, &server, &client, &client_auth,
                                     &opts, &begin);
    wf_oauth_client_metadata_free(&client);
    wf_oauth_resource_metadata_free(&resource);
    wf_oauth_server_metadata_free(&server);
    wf_xrpc_client_free(transport);

    if (s != WF_OK) {
        fprintf(stderr, "error: authorization begin failed (status %d)\n",
                (int)s);
        wf_oauth_authorization_begin_result_free(&begin);
        return 1;
    }

    /* Persist the pending authorization state so oauth-callback can finish the
     * flow. The state file holds the serialized PKCE/DPoP material; the printed
     * `state` is the opaque CSRF token the callback must echo back. */
    char *def_state = oauth_default_state_path();
    const char *sf = state_file ? state_file : def_state;
    if (begin.state_json) {
        if (write_text_file(sf, begin.state_json) == 0)
            printf("\npending state saved to %s\n", sf);
        else
            fprintf(stderr, "warning: failed to write state file '%s'\n", sf);
    }
    free(def_state);

    printf("\nOpen this URL in your browser to authorize:\n%s\n",
           begin.authorization_url ? begin.authorization_url : "(none)");
    printf("\nstate: %s\n", begin.state ? begin.state : "(none)");
    printf(
        "\nAfter authorizing, run:\n"
        "  wolfram oauth-callback %s --url \"<redirect-url>\" --state %s%s%s\n",
        service, begin.state ? begin.state : "",
        state_file ? " --state-file " : "", state_file ? state_file : "");

    wf_oauth_authorization_begin_result_free(&begin);
    return 0;
}
