/*
 * notification_v2.c — manage v2 notification preferences and activity
 * subscriptions over the generated app.bsky.notification wrappers.
 *
 * Demonstrates:
 *   1. Log in (session.h) to obtain an authenticated XRPC client.
 *   2. Put v2 notification preferences (putPreferencesV2).
 *   3. List activity subscriptions (listActivitySubscriptions).
 *   4. Print the returned preferences and subscriptions.
 *
 * Usage:
 *   notification_v2 <service-url> <handle> <password>
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
                "usage: %s <service-url> <handle> <password>\n", argv[0]);
        return 0;
    }

    const char *service_url = argv[1];
    const char *identifier = argv[2];
    const char *password = argv[3];

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

    printf("Logged in as %s (%s)\n",
           session->data.handle ? session->data.handle : "",
           session->data.did ? session->data.did : "");

    wf_lex_app_bsky_notification_defs_filterable_preference follow_pref = {
        "all", true, true
    };

    wf_lex_app_bsky_notification_put_preferences_v2_main_input put_input = {0};
    put_input.has_follow = true;
    put_input.follow = &follow_pref;
    put_input.has_like = true;
    put_input.like = &follow_pref;
    put_input.has_reply = true;
    put_input.reply = &follow_pref;
    put_input.has_mention = true;
    put_input.mention = &follow_pref;

    wf_response put_res = {0};
    status = wf_lex_app_bsky_notification_put_preferences_v2_main_call(
        session->client, &put_input, &put_res);
    if (status != WF_OK) {
        fprintf(stderr, "putPreferencesV2 failed: %d\n", (int)status);
        wf_response_free(&put_res);
        wf_session_free(session);
        return 1;
    }

    wf_lex_app_bsky_notification_put_preferences_v2_main_output *prefs = NULL;
    status = wf_lex_app_bsky_notification_put_preferences_v2_main_output_decode_json(
        put_res.body, put_res.body_len, &prefs);
    wf_response_free(&put_res);
    if (status != WF_OK || !prefs || !prefs->preferences) {
        fprintf(stderr, "failed to decode putPreferencesV2 response: %d\n", (int)status);
    } else {
        const wf_lex_app_bsky_notification_defs_preferences *p = prefs->preferences;
        printf("putPreferencesV2 OK\n");
        printf("  follow: include=%s list=%d push=%d\n",
               p->follow ? p->follow->include : "",
               p->follow ? p->follow->list : 0,
               p->follow ? p->follow->push : 0);
        printf("  mention: include=%s list=%d push=%d\n",
               p->mention ? p->mention->include : "",
               p->mention ? p->mention->list : 0,
               p->mention ? p->mention->push : 0);
    }
    wf_lex_app_bsky_notification_put_preferences_v2_main_output_free(prefs);

    wf_lex_app_bsky_notification_list_activity_subscriptions_main_params list_params = {0};
    list_params.has_limit = true;
    list_params.limit = 50;

    wf_response list_res = {0};
    status = wf_lex_app_bsky_notification_list_activity_subscriptions_main_call(
        session->client, &list_params, &list_res);
    if (status != WF_OK) {
        fprintf(stderr, "listActivitySubscriptions failed: %d\n", (int)status);
        wf_response_free(&list_res);
        wf_session_free(session);
        return 1;
    }

    wf_lex_app_bsky_notification_list_activity_subscriptions_main_output *subs = NULL;
    status = wf_lex_app_bsky_notification_list_activity_subscriptions_main_output_decode_json(
        list_res.body, list_res.body_len, &subs);
    wf_response_free(&list_res);
    if (status != WF_OK || !subs) {
        fprintf(stderr, "failed to decode listActivitySubscriptions response: %d\n",
                (int)status);
    } else {
        size_t n = subs->subscriptions.count;
        printf("listActivitySubscriptions returned %zu subscription(s)\n", n);
        for (size_t i = 0; i < n; ++i) {
            const wf_lex_app_bsky_actor_defs_profile_view *pv = subs->subscriptions.items[i];
            if (!pv) {
                continue;
            }
            printf("  - did=%s handle=%s display=%s\n",
                   pv->did ? pv->did : "",
                   pv->handle ? pv->handle : "",
                   pv->has_display_name ? pv->display_name : "");
        }
    }
    wf_lex_app_bsky_notification_list_activity_subscriptions_main_output_free(subs);

    wf_session_free(session);
    return status == WF_OK ? 0 : 1;
}
