/*
 * moderation_demo.c — self-contained demonstration of the wolfram
 * moderation decision engine.
 *
 * Builds sample labels, preferences, and label definitions entirely in code
 * (no network), computes moderation decisions for a post and an account, and
 * prints the resulting UI actions (blurs / alerts / informs / filters).
 *
 * Also demonstrates the offline JSON ingestion helpers.
 */

#include "wolfram/moderation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build a sample label definition ("porn" -> content/alert/adult). */
static wf_mod_label_def make_porn_def(void) {
    wf_mod_label_def def;
    wf_mod_interpret_label_def(&def, "porn", "did:plc:labeler",
                               "content", "alert", 1, "hide");
    return def;
}

/* Build a sample "spam" label definition. */
static wf_mod_label_def make_spam_def(void) {
    wf_mod_label_def def;
    wf_mod_interpret_label_def(&def, "spam", "did:plc:labeler",
                               "none", "alert", 0, "warn");
    return def;
}

/* Print the UI actions for a computed UI. */
static void print_ui(const char *title, const wf_mod_ui *ui) {
    printf("  %s:\n", title);
    printf("    no_override : %s\n", ui->no_override ? "yes" : "no");
    printf("    blurs      : %zu\n", ui->blur_count);
    printf("    alerts     : %zu\n", ui->alert_count);
    printf("    informs    : %zu\n", ui->inform_count);
    printf("    filters    : %zu\n", ui->filter_count);
}

int main(void) {
    const char *me = "did:plc:me";
    const char *labeler = "did:plc:labeler";
    const char *author = "did:plc:author";

    /* ---- Label definitions (offline, in code) ---- */
    wf_mod_label_def defs[2];
    defs[0] = make_porn_def();
    defs[1] = make_spam_def();

    /* ---- Preferences (offline, in code) ---- */
    wf_mod_prefs prefs;
    memset(&prefs, 0, sizeof(prefs));
    prefs.adult_content_enabled = 0; /* adult content OFF -> porn hides */

    /* Global label preference: spam -> warn */
    prefs.global_label_identifiers = malloc(sizeof(char *));
    prefs.global_label_prefs = malloc(sizeof(wf_mod_label_pref));
    prefs.global_label_identifiers[0] = strdup("spam");
    prefs.global_label_prefs[0] = WF_MOD_PREF_WARN;
    prefs.global_label_count = 1;

    /* Configure the labeler so its labels are honored. */
    prefs.labelers = malloc(sizeof(wf_mod_labeler_pref));
    prefs.labelers[0].did = strdup(labeler);
    prefs.labelers[0].label_prefs = NULL;
    prefs.labelers[0].label_identifiers = NULL;
    prefs.labelers[0].label_count = 0;
    prefs.labeler_count = 1;

    /* Mute the word "buy now" in content. */
    prefs.muted_words = malloc(sizeof(wf_mod_muted_word));
    prefs.muted_words[0].value = strdup("buy now");
    prefs.muted_words[0].targets_content = 1;
    prefs.muted_words[0].targets_tag = 0;
    prefs.muted_words[0].actor_target = NULL;
    prefs.muted_words[0].expires_at = NULL;
    prefs.muted_word_count = 1;

    /* ---- Options ---- */
    wf_mod_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.user_did = me;
    opts.prefs = prefs;
    opts.label_defs = defs;
    opts.label_def_count = 2;

    /* ---- Decide an account (blocked by + labeled spam) ---- */
    wf_mod_label acct_labels[1];
    memset(&acct_labels[0], 0, sizeof(acct_labels[0]));
    acct_labels[0].src = (char *)labeler;
    acct_labels[0].uri = (char *)"at://did:plc:author";
    acct_labels[0].val = (char *)"spam";

    wf_mod_subject_profile profile;
    memset(&profile, 0, sizeof(profile));
    profile.did = author;
    profile.viewer.blocked_by = "1";
    profile.labels = acct_labels;
    profile.label_count = 1;

    wf_mod_decision acct_dec;
    if (wf_mod_decide_account(&acct_dec, &profile, &opts) != WF_OK) {
        fprintf(stderr, "decide_account failed\n");
        return 1;
    }
    wf_mod_ui acct_ui;
    wf_mod_decision_ui(&acct_dec, WF_MOD_CTX_PROFILE_VIEW, &acct_ui);
    print_ui("account (profile view)", &acct_ui);
    wf_mod_ui_free(&acct_ui);
    wf_mod_decision_free(&acct_dec);

    /* ---- Decide a post (porn content + muted word + hidden) ---- */
    wf_mod_label post_labels[1];
    memset(&post_labels[0], 0, sizeof(post_labels[0]));
    post_labels[0].src = (char *)labeler;
    post_labels[0].uri = (char *)"at://did:plc:author/app.bsky.feed.post/abc";
    post_labels[0].val = (char *)"porn";

    wf_mod_subject_post post;
    memset(&post, 0, sizeof(post));
    post.uri = "at://did:plc:author/app.bsky.feed.post/abc";
    post.author.did = author;
    post.author.viewer.blocked_by = "1";
    post.text = "Check out this buy now offer!";
    post.labels = post_labels;
    post.label_count = 1;

    wf_mod_decision post_dec;
    if (wf_mod_decide_post(&post_dec, &post, &opts) != WF_OK) {
        fprintf(stderr, "decide_post failed\n");
        return 1;
    }
    wf_mod_ui content_ui;
    wf_mod_decision_ui(&post_dec, WF_MOD_CTX_CONTENT_VIEW, &content_ui);
    print_ui("post (content view)", &content_ui);
    wf_mod_ui_free(&content_ui);
    wf_mod_decision_free(&post_dec);

    /* ---- Demonstrate offline JSON ingestion helpers ---- */
    const char *prefs_json =
        "{"
        "  \"preferences\": ["
        "    {\"$type\":\"app.bsky.actor.defs#adultContentPref\",\"enabled\":true},"
        "    {\"$type\":\"app.bsky.actor.defs#labelersPref\","
        "       \"labelers\":[{\"did\":\"did:plc:labeler\"}]},"
        "    {\"$type\":\"app.bsky.actor.defs#contentLabelPrefs\","
        "       \"values\":[{\"label\":\"spam\",\"visibility\":\"ignore\"}]},"
        "    {\"$type\":\"app.bsky.actor.defs#hiddenPostsPref\","
        "       \"posts\":[\"at://did:plc:author/app.bsky.feed.post/x\"]},"
        "    {\"$type\":\"app.bsky.actor.defs#mutedWordsPref\","
        "       \"words\":[{\"value\":\"buy now\",\"targets\":[\"content\"],"
        "                   \"actorTarget\":\"all\"}]}"
        "  ]"
        "}";
    wf_mod_prefs json_prefs;
    if (wf_mod_prefs_from_json(&json_prefs, prefs_json) == WF_OK) {
        printf("  json prefs: adult=%d labelers=%zu global_labels=%zu"
               " muted_words=%zu hidden=%zu\n",
               json_prefs.adult_content_enabled,
               json_prefs.labeler_count,
               json_prefs.global_label_count,
               json_prefs.muted_word_count,
               json_prefs.hidden_post_count);
        wf_mod_prefs_free(&json_prefs);
    } else {
        fprintf(stderr, "prefs_from_json failed\n");
    }

    const char *labeler_json =
        "{\"policies\":{\"labelValueDefinitions\":["
        "  {\"identifier\":\"porn\",\"blurs\":\"content\",\"severity\":\"alert\","
        "   \"adultOnly\":true,\"defaultSetting\":\"hide\"}"
        "]}}";
    wf_mod_label_def *json_defs = NULL;
    size_t json_def_count = 0;
    if (wf_mod_label_defs_from_labeler(labeler, labeler_json,
                                       &json_defs, &json_def_count) == WF_OK) {
        printf("  json label defs: %zu\n", json_def_count);
        if (json_def_count > 0) {
            printf("    first def: %s adult=%d default=%d\n",
                   json_defs[0].identifier,
                   (json_defs[0].flags & WF_MOD_FLAG_ADULT) ? 1 : 0,
                   json_defs[0].default_setting);
        }
        wf_mod_label_defs_free(json_defs, json_def_count);
    } else {
        fprintf(stderr, "label_defs_from_labeler failed\n");
    }

    const char *labels_json =
        "{\"labels\":["
        "  {\"src\":\"did:plc:labeler\","
        "   \"uri\":\"at://did:plc:author/app.bsky.feed.post/abc\","
        "   \"val\":\"porn\",\"cts\":\"2024-01-01T00:00:00Z\"}"
        "]}";
    wf_mod_label *json_labels = NULL;
    size_t json_label_count = 0;
    if (wf_mod_labels_from_json(&json_labels, &json_label_count,
                                labels_json) == WF_OK) {
        printf("  json labels: %zu (val=%s)\n", json_label_count,
               json_label_count ? json_labels[0].val : "?");
        wf_mod_labels_free(json_labels, json_label_count);
    } else {
        fprintf(stderr, "labels_from_json failed\n");
    }

    /* Cleanup in-code prefs. */
    free(prefs.global_label_identifiers[0]);
    free(prefs.global_label_identifiers);
    free(prefs.global_label_prefs);
    free(prefs.labelers[0].did);
    free(prefs.labelers);
    free(prefs.muted_words[0].value);
    free(prefs.muted_words);
    wf_mod_label_def_free(&defs[0]);
    wf_mod_label_def_free(&defs[1]);

    printf("moderation_demo: OK\n");
    return 0;
}
