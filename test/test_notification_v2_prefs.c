/*
 * test_notification_v2_prefs.c — offline tests for the owned typed builder
 * and parser for app.bsky.notification.putPreferencesV2. No network access.
 */

#include "wolfram/notification_v2_typed.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Full #preferences fixture (all 13 slots) per app.bsky.notification.defs. */
static const char *kPrefsFixture =
    "{"
    "  \"chat\": { \"include\": \"all\", \"push\": false },"
    "  \"follow\": { \"include\": \"follows\", \"list\": true, \"push\": true },"
    "  \"like\": { \"include\": \"all\", \"list\": true, \"push\": false },"
    "  \"likeViaRepost\": { \"include\": \"all\", \"list\": false, \"push\": false },"
    "  \"mention\": { \"include\": \"follows\", \"list\": true, \"push\": true },"
    "  \"quote\": { \"include\": \"all\", \"list\": false, \"push\": true },"
    "  \"reply\": { \"include\": \"all\", \"list\": true, \"push\": false },"
    "  \"repost\": { \"include\": \"follows\", \"list\": false, \"push\": false },"
    "  \"repostViaRepost\": { \"include\": \"all\", \"list\": true, \"push\": true },"
    "  \"starterpackJoined\": { \"list\": true, \"push\": false },"
    "  \"subscribedPost\": { \"list\": false, \"push\": true },"
    "  \"unverified\": { \"list\": true, \"push\": false },"
    "  \"verified\": { \"list\": false, \"push\": true }"
    "}";

/* Wrapping output body (as returned by the endpoint). */
static const char *kPrefsOutputFixture =
    "{ \"preferences\": ";

int main(void) {
    /* ---- Build a representative subset: follow + like filterable + verified. */
    wf_notification_v2_preferences prefs = {0};
    prefs.has_follow = 1;
    prefs.follow.has_include = 1;
    prefs.follow.include = WF_NOTIF_V2_INCLUDE_FOLLOWS;
    prefs.follow.has_list = 1;
    prefs.follow.list = 1;
    prefs.follow.has_push = 1;
    prefs.follow.push = 0;

    prefs.has_like = 1;
    prefs.like.has_include = 1;
    prefs.like.include = WF_NOTIF_V2_INCLUDE_ALL;
    prefs.like.has_push = 1;
    prefs.like.push = 1; /* note: deliberately omit `list` on like */

    prefs.has_verified = 1;
    prefs.verified.has_list = 1;
    prefs.verified.list = 0;
    prefs.verified.has_push = 1;
    prefs.verified.push = 1;

    char *json = NULL;
    WF_CHECK(wf_notification_v2_preferences_build(&prefs, &json) == WF_OK);
    WF_CHECK(json != NULL);

    if (json) {
        cJSON *root = cJSON_Parse(json);
        WF_CHECK(root && cJSON_IsObject(root));

        /* Set fields present with correct values. */
        cJSON *follow = cJSON_GetObjectItemCaseSensitive(root, "follow");
        WF_CHECK(cJSON_IsObject(follow));
        cJSON *finc = cJSON_GetObjectItemCaseSensitive(follow, "include");
        WF_CHECK(cJSON_IsString(finc) && strcmp(finc->valuestring, "follows") == 0);
        cJSON *flist = cJSON_GetObjectItemCaseSensitive(follow, "list");
        WF_CHECK(cJSON_IsBool(flist) && cJSON_IsTrue(flist));
        cJSON *fpush = cJSON_GetObjectItemCaseSensitive(follow, "push");
        WF_CHECK(cJSON_IsBool(fpush) && !cJSON_IsTrue(fpush));

        cJSON *like = cJSON_GetObjectItemCaseSensitive(root, "like");
        WF_CHECK(cJSON_IsObject(like));
        cJSON *linc = cJSON_GetObjectItemCaseSensitive(like, "include");
        WF_CHECK(cJSON_IsString(linc) && strcmp(linc->valuestring, "all") == 0);
        cJSON *lpush = cJSON_GetObjectItemCaseSensitive(like, "push");
        WF_CHECK(cJSON_IsBool(lpush) && cJSON_IsTrue(lpush));
        /* `list` was intentionally omitted on like. */
        WF_CHECK(cJSON_GetObjectItemCaseSensitive(like, "list") == NULL);

        cJSON *verified = cJSON_GetObjectItemCaseSensitive(root, "verified");
        WF_CHECK(cJSON_IsObject(verified));
        cJSON *vlist = cJSON_GetObjectItemCaseSensitive(verified, "list");
        WF_CHECK(cJSON_IsBool(vlist) && !cJSON_IsTrue(vlist));
        cJSON *vpush = cJSON_GetObjectItemCaseSensitive(verified, "push");
        WF_CHECK(cJSON_IsBool(vpush) && cJSON_IsTrue(vpush));

        /* Unset top-level slots are NOT present. */
        WF_CHECK(cJSON_GetObjectItemCaseSensitive(root, "chat") == NULL);
        WF_CHECK(cJSON_GetObjectItemCaseSensitive(root, "mention") == NULL);
        WF_CHECK(cJSON_GetObjectItemCaseSensitive(root, "quote") == NULL);
        WF_CHECK(cJSON_GetObjectItemCaseSensitive(root, "reply") == NULL);
        WF_CHECK(cJSON_GetObjectItemCaseSensitive(root, "repost") == NULL);
        WF_CHECK(cJSON_GetObjectItemCaseSensitive(root, "repostViaRepost") == NULL);
        WF_CHECK(cJSON_GetObjectItemCaseSensitive(root, "starterpackJoined") == NULL);
        WF_CHECK(cJSON_GetObjectItemCaseSensitive(root, "subscribedPost") == NULL);
        WF_CHECK(cJSON_GetObjectItemCaseSensitive(root, "unverified") == NULL);

        cJSON_Delete(root);
        free(json);
    }
    wf_notification_v2_preferences_free(&prefs);

    /* ---- Invalid include value is rejected by the builder. ---- */
    wf_notification_v2_preferences bad = {0};
    bad.has_follow = 1;
    bad.follow.has_include = 1;
    bad.follow.include = WF_NOTIF_V2_INCLUDE_UNSET;
    char *bad_json = NULL;
    WF_CHECK(wf_notification_v2_preferences_build(&bad, &bad_json) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(bad_json == NULL);
    wf_notification_v2_preferences_free(&bad);

    /* ---- Round-trip: parse the full #preferences fixture. ---- */
    wf_notification_v2_preferences parsed = {0};
    WF_CHECK(wf_notification_v2_preferences_parse(
                 kPrefsFixture, strlen(kPrefsFixture), &parsed) == WF_OK);

    WF_CHECK(parsed.has_chat == 1);
    WF_CHECK(parsed.chat.has_include == 1);
    WF_CHECK(parsed.chat.include == WF_NOTIF_V2_INCLUDE_ALL);
    WF_CHECK(parsed.chat.has_push == 1);
    WF_CHECK(parsed.chat.push == 0);

    WF_CHECK(parsed.has_follow == 1);
    WF_CHECK(parsed.follow.has_include == 1 &&
             parsed.follow.include == WF_NOTIF_V2_INCLUDE_FOLLOWS);
    WF_CHECK(parsed.follow.has_list == 1 && parsed.follow.list == 1);
    WF_CHECK(parsed.follow.has_push == 1 && parsed.follow.push == 1);

    WF_CHECK(parsed.has_like == 1);
    WF_CHECK(parsed.like.has_include == 1 &&
             parsed.like.include == WF_NOTIF_V2_INCLUDE_ALL);
    WF_CHECK(parsed.like.has_list == 1 && parsed.like.list == 1);
    WF_CHECK(parsed.like.has_push == 1 && parsed.like.push == 0);

    WF_CHECK(parsed.has_like_via_repost == 1);
    WF_CHECK(parsed.like_via_repost.include == WF_NOTIF_V2_INCLUDE_ALL);
    WF_CHECK(parsed.like_via_repost.list == 0 && parsed.like_via_repost.push == 0);

    WF_CHECK(parsed.has_mention == 1);
    WF_CHECK(parsed.mention.include == WF_NOTIF_V2_INCLUDE_FOLLOWS);
    WF_CHECK(parsed.mention.list == 1 && parsed.mention.push == 1);

    WF_CHECK(parsed.has_quote == 1);
    WF_CHECK(parsed.quote.include == WF_NOTIF_V2_INCLUDE_ALL);
    WF_CHECK(parsed.quote.list == 0 && parsed.quote.push == 1);

    WF_CHECK(parsed.has_reply == 1);
    WF_CHECK(parsed.reply.include == WF_NOTIF_V2_INCLUDE_ALL);
    WF_CHECK(parsed.reply.list == 1 && parsed.reply.push == 0);

    WF_CHECK(parsed.has_repost == 1);
    WF_CHECK(parsed.repost.include == WF_NOTIF_V2_INCLUDE_FOLLOWS);
    WF_CHECK(parsed.repost.list == 0 && parsed.repost.push == 0);

    WF_CHECK(parsed.has_repost_via_repost == 1);
    WF_CHECK(parsed.repost_via_repost.include == WF_NOTIF_V2_INCLUDE_ALL);
    WF_CHECK(parsed.repost_via_repost.list == 1 &&
             parsed.repost_via_repost.push == 1);

    WF_CHECK(parsed.has_starterpack_joined == 1);
    WF_CHECK(parsed.starterpack_joined.list == 1 &&
             parsed.starterpack_joined.push == 0);
    WF_CHECK(parsed.has_subscribed_post == 1);
    WF_CHECK(parsed.subscribed_post.list == 0 &&
             parsed.subscribed_post.push == 1);
    WF_CHECK(parsed.has_unverified == 1);
    WF_CHECK(parsed.unverified.list == 1 && parsed.unverified.push == 0);
    WF_CHECK(parsed.has_verified == 1);
    WF_CHECK(parsed.verified.list == 0 && parsed.verified.push == 1);

    wf_notification_v2_preferences_free(&parsed);

    /* ---- The wrapping output body also parses (preferences key). ---- */
    size_t out_len =
        strlen(kPrefsOutputFixture) + strlen(kPrefsFixture) + 2;
    char *wrapped = (char *)malloc(out_len);
    WF_CHECK(wrapped != NULL);
    if (wrapped) {
        snprintf(wrapped, out_len, "%s%s}", kPrefsOutputFixture, kPrefsFixture);
        wf_notification_v2_preferences wrapped_parsed = {0};
        WF_CHECK(wf_notification_v2_preferences_parse(
                     wrapped, strlen(wrapped), &wrapped_parsed) == WF_OK);
        WF_CHECK(wrapped_parsed.has_verified == 1 &&
                 wrapped_parsed.verified.push == 1);
        wf_notification_v2_preferences_free(&wrapped_parsed);
        free(wrapped);
    }

    /* ---- Invalid input yields WF_ERR_PARSE. ---- */
    wf_notification_v2_preferences bad_parse = {0};
    WF_CHECK(wf_notification_v2_preferences_parse(
                 "not json", 8, &bad_parse) == WF_ERR_PARSE);
    WF_CHECK(wf_notification_v2_preferences_parse(NULL, 0, &bad_parse) ==
             WF_ERR_INVALID_ARG);
    wf_notification_v2_preferences_free(&bad_parse);

    /* ---- Agent wrapper rejects NULL arguments without network access. ---- */
    static int sentinel;
    wf_agent *fake = (wf_agent *)&sentinel;
    WF_CHECK(wf_agent_put_notification_preferences_v2_typed(
                 NULL, &prefs, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_put_notification_preferences_v2_typed(
                 fake, NULL, NULL) == WF_ERR_INVALID_ARG);

    printf("notification_v2_prefs_typed: all checks passed\n");
    WF_TEST_SUMMARY();
}
