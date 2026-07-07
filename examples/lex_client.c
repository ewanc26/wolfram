/*
 * lex_client.c — generated lexicon client example.
 *
 * Demonstrates using wf_lex_* generated wrappers for:
 *   1. No-auth call: com.atproto.server.describeServer with typed output
 *   2. Auth call:    app.bsky.feed.getLikes with param encoding + typed output
 *
 * Usage:
 *   lex_client <service-url>                       (server info only)
 *   lex_client <service-url> <handle> <pwd> <uri>  (+ like list)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/atproto_lex.h"
#include "wolfram/session.h"

/* ── No auth: describeServer ───────────────────────────────────────── */

static void show_server_info(wf_xrpc_client *client) {
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_server_describe_server_main_call(client, &res);
    if (status != WF_OK && status != WF_ERR_HTTP) {
        fprintf(stderr, "describeServer failed: %d\n", (int)status);
        wf_response_free(&res);
        return;
    }

    wf_lex_com_atproto_server_describe_server_main_output *out = NULL;
    status = wf_lex_com_atproto_server_describe_server_main_output_decode_json(
        res.body, res.body_len, &out);
    wf_response_free(&res);
    if (status != WF_OK) { fprintf(stderr, "decode failed: %d\n", (int)status); return; }

    printf("=== Server Info ===\n");
    printf("DID: %s\n", out->did);
    printf("User domains:");
    for (size_t i = 0; i < out->available_user_domains.count; i++)
        printf(" %s", out->available_user_domains.items[i]);
    printf("\n");
    printf("Invite required: %s\n", out->invite_code_required ? "yes" : "no");
    if (out->has_links) {
        if (out->links->has_privacy_policy)
            printf("Privacy: %s\n", out->links->privacy_policy);
        if (out->links->has_terms_of_service)
            printf("ToS: %s\n", out->links->terms_of_service);
    }
    if (out->has_contact && out->contact->has_email)
        printf("Contact: %s\n", out->contact->email);

    wf_lex_com_atproto_server_describe_server_main_output_free(out);
}

/* ── Auth: getLikes (uses session bearer token via _call variant) ──── */

static void show_likes(wf_xrpc_client *client, const char *uri) {
    wf_lex_app_bsky_feed_get_likes_main_params params = {0};
    params.uri = uri;
    params.has_limit = true;
    params.limit = 5;

    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_feed_get_likes_main_call(client, &params, &res);
    if (status != WF_OK && status != WF_ERR_HTTP) {
        fprintf(stderr, "getLikes failed: %d\n", (int)status);
        wf_response_free(&res);
        return;
    }

    wf_lex_app_bsky_feed_get_likes_main_output *out = NULL;
    status = wf_lex_app_bsky_feed_get_likes_main_output_decode_json(
        res.body, res.body_len, &out);
    wf_response_free(&res);
    if (status != WF_OK) { fprintf(stderr, "decode failed: %d\n", (int)status); return; }

    printf("\n=== Likes (first %d) ===\n", (int)out->likes.count);
    for (size_t i = 0; i < out->likes.count; i++) {
        printf("  %s", out->likes.items[i]->actor->handle);
        if (out->likes.items[i]->actor->has_display_name &&
            out->likes.items[i]->actor->display_name)
            printf(" (%s)", out->likes.items[i]->actor->display_name);
        printf("\n");
    }
    if (out->has_cursor)
        printf("Cursor: %s\n", out->cursor);

    wf_lex_app_bsky_feed_get_likes_main_output_free(out);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <service-url> [<handle> <password> <post-uri>]\n", argv[0]);
        return 1;
    }

    wf_session *session = wf_session_new(argv[1]);
    if (!session) { fprintf(stderr, "alloc failed\n"); return 1; }

    show_server_info(session->client);

    if (argc >= 5) {
        wf_status s = wf_session_login(session, argv[2], argv[3]);
        if (s != WF_OK) {
            fprintf(stderr, "login failed: %d\n", (int)s);
        } else {
            printf("\nLogged in as %s (%s)\n", session->data.handle, session->data.did);
            show_likes(session->client, argv[4]);
        }
    }

    wf_session_free(session);
    return 0;
}
