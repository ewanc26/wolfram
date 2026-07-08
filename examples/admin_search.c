/*
 * admin_search.c — search accounts as an admin and print the account views.
 *
 * Demonstrates the generated com.atproto.admin.searchAccounts wrapper:
 *   1. Log in as an admin (session.h) to obtain an authenticated XRPC client.
 *   2. Search accounts by email (or with a default limit) via the generated
 *      searchAccounts wrapper.
 *   3. Print the returned account views.
 *
 * Usage:
 *   admin_search <service-url> <admin-handle> <password> [email]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/atproto_lex.h"
#include "wolfram/session.h"
#include "wolfram/xrpc.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <service-url> <admin-handle> <password> [email]\n",
                argv[0]);
        return 0;
    }

    const char *service_url = argv[1];
    const char *identifier = argv[2];
    const char *password = argv[3];
    const char *email = (argc > 4) ? argv[4] : NULL;

    wf_session *session = wf_session_new(service_url);
    if (!session) {
        fprintf(stderr, "failed to create session\n");
        return 1;
    }

    wf_status status = wf_session_login(session, identifier, password);
    if (status != WF_OK) {
        fprintf(stderr, "login failed: %d\n", (int)status);
        wf_session_free(session);
        return 1;
    }

    printf("Logged in as admin %s (%s)\n",
           session->data.handle ? session->data.handle : "",
           session->data.did ? session->data.did : "");

    wf_lex_com_atproto_admin_search_accounts_main_params params = {0};
    if (email && email[0]) {
        params.has_email = true;
        params.email = email;
    }
    params.has_limit = true;
    params.limit = 50;

    wf_response res = {0};
    status = wf_lex_com_atproto_admin_search_accounts_main_call(
        session->client, &params, &res);
    if (status != WF_OK) {
        fprintf(stderr, "admin.searchAccounts failed: %d\n", (int)status);
        wf_response_free(&res);
        wf_session_free(session);
        return 1;
    }

    wf_lex_com_atproto_admin_search_accounts_main_output *out = NULL;
    status = wf_lex_com_atproto_admin_search_accounts_main_output_decode_json(
        res.body, res.body_len, &out);
    wf_response_free(&res);
    if (status != WF_OK || !out) {
        fprintf(stderr, "failed to decode admin.searchAccounts response: %d\n",
                (int)status);
    } else {
        size_t n = out->accounts.count;
        printf("admin.searchAccounts returned %zu account(s)\n", n);
        for (size_t i = 0; i < n; ++i) {
            const wf_lex_com_atproto_admin_defs_account_view *av = out->accounts.items[i];
            if (!av) {
                continue;
            }
            printf("  - did=%s handle=%s email=%s indexedAt=%s\n",
                   av->did ? av->did : "",
                   av->handle ? av->handle : "",
                   av->has_email ? av->email : "",
                   av->indexed_at ? av->indexed_at : "");
        }
    }
    wf_lex_com_atproto_admin_search_accounts_main_output_free(out);

    wf_session_free(session);
    return status == WF_OK ? 0 : 1;
}
