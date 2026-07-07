/*
 * lex_client_full.c — full-corpus generated lexicon client example.
 *
 * Exercises representative wrappers generated for the FULL atproto lexicon
 * corpus (all 394 lexicons) from `wolfram/atproto_lex.h`:
 *   1. com.atproto.repo.describeRepo        (no auth, parameterized query)
 *   2. app.bsky.feed.getTimeline            (auth required, typed feed output)
 *   3. tools.ozone.moderation.queryStatuses (auth required, ozone typed output)
 *
 * This file is OFFLINE-SAFE: with no/insufficient arguments it prints usage
 * and exits 0. Network is only attempted when a service URL and credentials
 * are supplied on the command line.
 *
 * Usage:
 *   lex_client_full <service-url> <handle> <password> [<repo>]
 *     <service-url>  base URL of the PDS (e.g. https://bsky.social)
 *     <handle>       account handle or email used to log in
 *     <password>     account password / app password
 *     <repo>         repo handle or DID for describeRepo (defaults to handle)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/atproto_lex.h"
#include "wolfram/session.h"

/* ── No auth: describeRepo ────────────────────────────────────────── */

static void show_describe_repo(wf_xrpc_client *client, const char *repo) {
    wf_lex_com_atproto_repo_describe_repo_main_params params = {0};
    params.repo = repo;

    wf_response res = {0};
    wf_status status =
        wf_lex_com_atproto_repo_describe_repo_main_call(client, &params, &res);
    if (status != WF_OK && status != WF_ERR_HTTP) {
        fprintf(stderr, "describeRepo failed: %d\n", (int)status);
        wf_response_free(&res);
        return;
    }

    wf_lex_com_atproto_repo_describe_repo_main_output *out = NULL;
    status = wf_lex_com_atproto_repo_describe_repo_main_output_decode_json(
        res.body, res.body_len, &out);
    wf_response_free(&res);
    if (status != WF_OK) {
        fprintf(stderr, "describeRepo decode failed: %d\n", (int)status);
        return;
    }

    printf("=== com.atproto.repo.describeRepo ===\n");
    printf("handle: %s\n", out->handle ? out->handle : "(null)");
    printf("did:    %s\n", out->did ? out->did : "(null)");
    printf("handle_is_correct: %s\n", out->handle_is_correct ? "yes" : "no");
    printf("collections: %zu\n", out->collections.count);

    wf_lex_com_atproto_repo_describe_repo_main_output_free(out);
}

/* ── Auth: getTimeline ────────────────────────────────────────────── */

static void show_timeline(wf_xrpc_client *client) {
    wf_lex_app_bsky_feed_get_timeline_main_params params = {0};
    params.has_limit = true;
    params.limit = 3;

    wf_response res = {0};
    wf_status status =
        wf_lex_app_bsky_feed_get_timeline_main_call(client, &params, &res);
    if (status != WF_OK && status != WF_ERR_HTTP) {
        fprintf(stderr, "getTimeline failed: %d\n", (int)status);
        wf_response_free(&res);
        return;
    }

    wf_lex_app_bsky_feed_get_timeline_main_output *out = NULL;
    status = wf_lex_app_bsky_feed_get_timeline_main_output_decode_json(
        res.body, res.body_len, &out);
    wf_response_free(&res);
    if (status != WF_OK) {
        fprintf(stderr, "getTimeline decode failed: %d\n", (int)status);
        return;
    }

    printf("\n=== app.bsky.feed.getTimeline ===\n");
    printf("feed posts: %zu\n", out->feed.count);
    for (size_t i = 0; i < out->feed.count && i < 3; i++) {
        const wf_lex_app_bsky_feed_defs_feed_view_post *fv = out->feed.items[i];
        if (!fv || !fv->post) continue;
        const char *author = fv->post->author ? fv->post->author->handle : "?";
        printf("  [%zu] %s  by @%s\n", i, fv->post->uri, author);
    }
    if (out->has_cursor)
        printf("cursor: %s\n", out->cursor);

    wf_lex_app_bsky_feed_get_timeline_main_output_free(out);
}

/* ── Auth: ozone queryStatuses ────────────────────────────────────── */

static void show_moderation_statuses(wf_xrpc_client *client) {
    wf_lex_tools_ozone_moderation_query_statuses_main_params params = {0};
    params.has_limit = true;
    params.limit = 3;

    wf_response res = {0};
    wf_status status =
        wf_lex_tools_ozone_moderation_query_statuses_main_call(client, &params, &res);
    if (status != WF_OK && status != WF_ERR_HTTP) {
        fprintf(stderr, "queryStatuses failed: %d\n", (int)status);
        wf_response_free(&res);
        return;
    }

    wf_lex_tools_ozone_moderation_query_statuses_main_output *out = NULL;
    status = wf_lex_tools_ozone_moderation_query_statuses_main_output_decode_json(
        res.body, res.body_len, &out);
    wf_response_free(&res);
    if (status != WF_OK) {
        fprintf(stderr, "queryStatuses decode failed: %d\n", (int)status);
        return;
    }

    printf("\n=== tools.ozone.moderation.queryStatuses ===\n");
    printf("subject statuses: %zu\n", out->subject_statuses.count);
    for (size_t i = 0; i < out->subject_statuses.count && i < 3; i++) {
        const wf_lex_tools_ozone_moderation_defs_subject_status_view *s =
            out->subject_statuses.items[i];
        if (!s) continue;
        printf("  [%zu] id=%lld handle=%s\n", i, (long long)s->id,
               s->subject_repo_handle ? s->subject_repo_handle : "?");
    }
    if (out->has_cursor)
        printf("cursor: %s\n", out->cursor);

    wf_lex_tools_ozone_moderation_query_statuses_main_output_free(out);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <service-url> <handle> <password> [<repo>]\n"
                "  (no network is attempted without all required arguments)\n",
                argv[0]);
        return 0;
    }

    const char *service_url = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *repo = (argc >= 5) ? argv[4] : handle;

    wf_session *session = wf_session_new(service_url);
    if (!session) {
        fprintf(stderr, "alloc failed\n");
        return 1;
    }

    show_describe_repo(session->client, repo);

    wf_status s = wf_session_login(session, handle, password);
    if (s != WF_OK) {
        fprintf(stderr, "login failed: %d\n", (int)s);
        wf_session_free(session);
        return 1;
    }
    printf("\nLogged in as %s (%s)\n", session->data.handle, session->data.did);

    show_timeline(session->client);
    show_moderation_statuses(session->client);

    wf_session_free(session);
    return 0;
}
