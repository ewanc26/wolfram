/*
 * test_moderation.c — unit tests for the moderation decision engine.
 *
 * Tests decision init/free, merge, downgrade, cause addition, UI computation,
 * label interpretation, mute word matching, and subject deciders.
 */

#include "wolfram/moderation.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

#define MOD_CHECK(expr) \
    do { if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; } } while (0)

static wf_mod_label make_label(const char *src, const char *uri,
                               const char *val) {
    wf_mod_label l = {0};
    l.src = (char *)src;
    l.uri = (char *)uri;
    l.val = (char *)val;
    l.cts = NULL;
    return l;
}

__attribute__((unused))
static wf_mod_subject_profile make_profile(const char *did,
                                           wf_mod_label *labels,
                                           size_t label_count) {
    wf_mod_subject_profile p = {0};
    p.did = did;
    p.labels = labels;
    p.label_count = label_count;
    return p;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static int test_decision_init_free(void) {
    wf_mod_decision d;
    MOD_CHECK(wf_mod_decision_init(&d) == WF_OK);
    MOD_CHECK(d.cause_count == 0);
    MOD_CHECK(d.causes != NULL);
    MOD_CHECK(d.did == NULL);
    MOD_CHECK(d.is_me == 0);
    wf_mod_decision_free(&d);
    return 0;
}

static int test_decision_merge(void) {
    wf_mod_decision a, b, merged;
    MOD_CHECK(wf_mod_decision_init(&a) == WF_OK);
    MOD_CHECK(wf_mod_decision_init(&b) == WF_OK);

    a.did = strdup("did:plc:abc");
    a.is_me = 0;
    wf_mod_add_blocking(&a, "at://did:plc:abc/block");
    wf_mod_add_muted(&a, 1);

    b.did = strdup("did:plc:abc");
    b.is_me = 0;
    wf_mod_add_blocked_by(&b, 1);

    MOD_CHECK(wf_mod_decision_merge(&merged, &a, &b) == WF_OK);
    MOD_CHECK(merged.cause_count == 3);
    MOD_CHECK(merged.did != NULL);
    MOD_CHECK(strcmp(merged.did, "did:plc:abc") == 0);

    MOD_CHECK(wf_mod_decision_blocked(&merged));
    MOD_CHECK(wf_mod_decision_muted(&merged));

    wf_mod_decision_free(&a);
    wf_mod_decision_free(&b);
    wf_mod_decision_free(&merged);
    return 0;
}

static int test_decision_downgrade(void) {
    wf_mod_decision d;
    MOD_CHECK(wf_mod_decision_init(&d) == WF_OK);
    wf_mod_add_muted(&d, 1);
    wf_mod_add_blocking(&d, "at://block");

    MOD_CHECK(d.causes[0].downgraded == 0);
    MOD_CHECK(d.causes[1].downgraded == 0);

    wf_mod_decision_downgrade(&d);

    MOD_CHECK(d.causes[0].downgraded == 1);
    MOD_CHECK(d.causes[1].downgraded == 1);

    wf_mod_decision_free(&d);
    return 0;
}

static int test_ui_block(void) {
    wf_mod_decision d;
    wf_mod_ui ui;
    MOD_CHECK(wf_mod_decision_init(&d) == WF_OK);
    d.is_me = 0;
    wf_mod_add_blocking(&d, "at://block");

    /* content_list should blur and filter */
    MOD_CHECK(wf_mod_decision_ui(&d, WF_MOD_CTX_CONTENT_LIST, &ui) == WF_OK);
    MOD_CHECK(ui.blur_count > 0);
    MOD_CHECK(ui.filter_count > 0);
    MOD_CHECK(ui.no_override == 1);
    wf_mod_ui_free(&ui);

    /* profile_view should alert */
    MOD_CHECK(wf_mod_decision_ui(&d, WF_MOD_CTX_PROFILE_VIEW, &ui) == WF_OK);
    MOD_CHECK(ui.alert_count > 0);
    MOD_CHECK(ui.blur_count == 0);
    wf_mod_ui_free(&ui);

    wf_mod_decision_free(&d);
    return 0;
}

static int test_ui_mute(void) {
    wf_mod_decision d;
    wf_mod_ui ui;
    MOD_CHECK(wf_mod_decision_init(&d) == WF_OK);
    d.is_me = 0;
    wf_mod_add_muted(&d, 1);

    /* content_list should blur and filter */
    MOD_CHECK(wf_mod_decision_ui(&d, WF_MOD_CTX_CONTENT_LIST, &ui) == WF_OK);
    MOD_CHECK(ui.blur_count > 0);
    MOD_CHECK(ui.filter_count > 0);
    wf_mod_ui_free(&ui);

    /* content_view should inform */
    MOD_CHECK(wf_mod_decision_ui(&d, WF_MOD_CTX_CONTENT_VIEW, &ui) == WF_OK);
    MOD_CHECK(ui.inform_count > 0);
    wf_mod_ui_free(&ui);

    wf_mod_decision_free(&d);
    return 0;
}

static int test_ui_is_me(void) {
    wf_mod_decision d;
    wf_mod_ui ui;
    MOD_CHECK(wf_mod_decision_init(&d) == WF_OK);
    d.is_me = 1;
    wf_mod_add_blocking(&d, "at://block");
    wf_mod_add_muted(&d, 1);

    /* When is_me, block and mute should not produce UI actions */
    MOD_CHECK(wf_mod_decision_ui(&d, WF_MOD_CTX_CONTENT_LIST, &ui) == WF_OK);
    MOD_CHECK(ui.blur_count == 0);
    MOD_CHECK(ui.alert_count == 0);
    MOD_CHECK(ui.inform_count == 0);
    MOD_CHECK(ui.filter_count == 0);
    wf_mod_ui_free(&ui);

    wf_mod_decision_free(&d);
    return 0;
}

static int test_label_interpretation(void) {
    wf_mod_label_def def;

    /* blurs=content, severity=alert, adult_only=1 */
    MOD_CHECK(wf_mod_interpret_label_def(&def, "porn", NULL, "content", "alert", 1, "hide") == WF_OK);
    MOD_CHECK(strcmp(def.identifier, "porn") == 0);
    MOD_CHECK(def.configurable == 1);
    MOD_CHECK(def.default_setting == WF_MOD_PREF_HIDE);
    MOD_CHECK(def.flags & WF_MOD_FLAG_ADULT);
    MOD_CHECK(def.flags & WF_MOD_FLAG_NO_SELF);
    /* content target should blur content_list and content_view (adult) */
    MOD_CHECK(def.behaviors[WF_MOD_TARGET_CONTENT].content_list == WF_MOD_ACTION_BLUR);
    MOD_CHECK(def.behaviors[WF_MOD_TARGET_CONTENT].content_view == WF_MOD_ACTION_BLUR);
    /* account target should alert profile_list and profile_view */
    MOD_CHECK(def.behaviors[WF_MOD_TARGET_ACCOUNT].profile_list == WF_MOD_ACTION_ALERT);
    MOD_CHECK(def.behaviors[WF_MOD_TARGET_ACCOUNT].profile_view == WF_MOD_ACTION_ALERT);
    wf_mod_label_def_free(&def);

    /* blurs=media, severity=inform, adult_only=0 */
    MOD_CHECK(wf_mod_interpret_label_def(&def, "gore", NULL, "media", "inform", 0, "warn") == WF_OK);
    MOD_CHECK(def.default_setting == WF_MOD_PREF_WARN);
    MOD_CHECK(!(def.flags & WF_MOD_FLAG_ADULT));
    MOD_CHECK(def.behaviors[WF_MOD_TARGET_ACCOUNT].avatar == WF_MOD_ACTION_BLUR);
    MOD_CHECK(def.behaviors[WF_MOD_TARGET_ACCOUNT].banner == WF_MOD_ACTION_BLUR);
    MOD_CHECK(def.behaviors[WF_MOD_TARGET_CONTENT].content_media == WF_MOD_ACTION_BLUR);
    wf_mod_label_def_free(&def);

    /* blurs=none, severity=NULL, default=ignore */
    MOD_CHECK(wf_mod_interpret_label_def(&def, "spam", NULL, "none", NULL, 0, "ignore") == WF_OK);
    MOD_CHECK(def.default_setting == WF_MOD_PREF_IGNORE);
    MOD_CHECK(def.behaviors[WF_MOD_TARGET_ACCOUNT].profile_list == WF_MOD_ACTION_NONE);
    MOD_CHECK(def.behaviors[WF_MOD_TARGET_CONTENT].content_list == WF_MOD_ACTION_NONE);
    wf_mod_label_def_free(&def);

    return 0;
}

static int test_mute_word_matching(void) {
    wf_mod_muted_word words[2] = {0};
    wf_mod_mute_word_match *matches = NULL;
    size_t match_count = 0;

    /* Basic word match */
    words[0].value = strdup("spam");
    words[0].targets_content = 1;
    words[1].value = strdup("hello");
    words[1].targets_content = 1;

    MOD_CHECK(wf_mod_match_mute_words(&matches, &match_count,
                                      words, 2,
                                      "this is spam content", NULL, 0, NULL, 0) == WF_OK);
    MOD_CHECK(match_count == 1);
    MOD_CHECK(strcmp(matches[0].value, "spam") == 0);
    wf_mod_mute_word_matches_free(matches, match_count);

    /* Tag match */
    const char *tags[] = { "spam" };
    MOD_CHECK(wf_mod_match_mute_words(&matches, &match_count,
                                      words, 2,
                                      "some text", tags, 1, NULL, 0) == WF_OK);
    MOD_CHECK(match_count == 1);
    MOD_CHECK(strcmp(matches[0].value, "spam") == 0);
    wf_mod_mute_word_matches_free(matches, match_count);

    /* No match */
    MOD_CHECK(wf_mod_match_mute_words(&matches, &match_count,
                                      words, 2,
                                      "nothing here", NULL, 0, NULL, 0) == WF_OK);
    MOD_CHECK(match_count == 0);

    /* Language exception (Japanese) - substring match */
    free(words[0].value);
    words[0].value = strdup("test");
    MOD_CHECK(wf_mod_match_mute_words(&matches, &match_count,
                                      words, 1,
                                      "testing", NULL, 0, "ja", 0) == WF_OK);
    MOD_CHECK(match_count == 1);
    wf_mod_mute_word_matches_free(matches, match_count);

    /* exclude-following with following=1 should skip */
    free(words[0].value);
    words[0].value = strdup("spam");
    words[0].actor_target = strdup("exclude-following");
    MOD_CHECK(wf_mod_match_mute_words(&matches, &match_count,
                                      words, 1,
                                      "spam here", NULL, 0, NULL, 1) == WF_OK);
    MOD_CHECK(match_count == 0);

    /* Timed mute words remain active until their RFC 3339 expiry. */
    free(words[0].actor_target);
    words[0].actor_target = NULL;
    words[0].expires_at = strdup("2999-01-01T00:00:00.000Z");
    MOD_CHECK(wf_mod_match_mute_words(&matches, &match_count,
                                      words, 1,
                                      "spam here", NULL, 0, NULL, 0) == WF_OK);
    MOD_CHECK(match_count == 1);
    wf_mod_mute_word_matches_free(matches, match_count);

    free(words[0].expires_at);
    words[0].expires_at = strdup("2000-01-01T00:00:00.000Z");
    MOD_CHECK(wf_mod_match_mute_words(&matches, &match_count,
                                      words, 1,
                                      "spam here", NULL, 0, NULL, 0) == WF_OK);
    MOD_CHECK(match_count == 0);

    free(words[0].value);
    free(words[0].expires_at);
    free(words[1].value);

    return 0;
}

static int test_decide_account(void) {
    wf_mod_opts opts = {0};
    opts.user_did = "did:plc:me";

    wf_mod_label labels[1];
    labels[0] = make_label("did:plc:labeler", "at://did:plc:other/post", "spam");

    /* Set up a label def */
    wf_mod_label_def def;
    wf_mod_interpret_label_def(&def, "spam", NULL, "none", "alert", 0, "warn");
    opts.label_defs = &def;
    opts.label_def_count = 1;

    /* Set up labeler pref */
    wf_mod_labeler_pref lp = {0};
    lp.did = strdup("did:plc:labeler");
    opts.prefs.labelers = &lp;
    opts.prefs.labeler_count = 1;

    wf_mod_subject_profile subject = {0};
    subject.did = "did:plc:other";
    subject.viewer.muted = "1";
    subject.viewer.blocking = "at://block";
    subject.labels = labels;
    subject.label_count = 1;

    wf_mod_decision d;
    MOD_CHECK(wf_mod_decide_account(&d, &subject, &opts) == WF_OK);
    MOD_CHECK(d.is_me == 0);
    MOD_CHECK(wf_mod_decision_muted(&d));
    MOD_CHECK(wf_mod_decision_blocked(&d));
    MOD_CHECK(d.cause_count >= 2); /* at least mute + blocking */

    wf_mod_decision_free(&d);
    free(lp.did);
    wf_mod_label_def_free(&def);
    return 0;
}

static int test_decide_post(void) {
    wf_mod_opts opts = {0};
    opts.user_did = "did:plc:me";

    wf_mod_subject_post subject = {0};
    subject.uri = "at://did:plc:other/app.bsky.feed.post/123";
    subject.cid = "cid123";
    subject.author.did = "did:plc:other";
    subject.text = "hello world";
    subject.labels = NULL;
    subject.label_count = 0;

    wf_mod_decision d;
    MOD_CHECK(wf_mod_decide_post(&d, &subject, &opts) == WF_OK);
    MOD_CHECK(d.is_me == 0);
    /* No labels, no blocks, no mutes — should have no causes */
    MOD_CHECK(d.cause_count == 0);

    wf_mod_decision_free(&d);
    return 0;
}

static int test_decide_post_with_mute_word(void) {
    wf_mod_opts opts = {0};
    opts.user_did = "did:plc:me";

    wf_mod_muted_word mw[1] = {0};
    mw[0].value = strdup("spam");
    mw[0].targets_content = 1;
    opts.prefs.muted_words = mw;
    opts.prefs.muted_word_count = 1;

    wf_mod_subject_post subject = {0};
    subject.uri = "at://did:plc:other/app.bsky.feed.post/123";
    subject.author.did = "did:plc:other";
    subject.text = "this is spam content";

    /* Test mute word matching directly first */
    wf_mod_mute_word_match *matches = NULL;
    size_t match_count = 0;
    MOD_CHECK(wf_mod_match_mute_words(&matches, &match_count,
                                      mw, 1, "this is spam content",
                                      NULL, 0, NULL, 0) == WF_OK);
    MOD_CHECK(match_count == 1);
    wf_mod_mute_word_matches_free(matches, match_count);

    wf_mod_decision d;
    MOD_CHECK(wf_mod_decide_post(&d, &subject, &opts) == WF_OK);
    MOD_CHECK(d.is_me == 0);
    /* Should have a mute-word cause */
    int found_mw = 0;
    for (size_t i = 0; i < d.cause_count; i++) {
        if (d.causes[i].type == WF_MOD_CAUSE_MUTE_WORD) found_mw = 1;
    }
    MOD_CHECK(found_mw);

    wf_mod_decision_free(&d);
    free(mw[0].value);
    return 0;
}

static int test_decide_post_hidden(void) {
    wf_mod_opts opts = {0};
    opts.user_did = "did:plc:me";

    const char *hidden[] = { "at://did:plc:other/app.bsky.feed.post/123" };
    opts.prefs.hidden_posts = (char **)hidden;
    opts.prefs.hidden_post_count = 1;

    wf_mod_subject_post subject = {0};
    subject.uri = "at://did:plc:other/app.bsky.feed.post/123";
    subject.author.did = "did:plc:other";
    subject.text = "hello";

    wf_mod_decision d;
    MOD_CHECK(wf_mod_decide_post(&d, &subject, &opts) == WF_OK);
    int found_hidden = 0;
    for (size_t i = 0; i < d.cause_count; i++) {
        if (d.causes[i].type == WF_MOD_CAUSE_HIDDEN) found_hidden = 1;
    }
    MOD_CHECK(found_hidden);

    wf_mod_decision_free(&d);
    return 0;
}

static int test_behavior_get(void) {
    MOD_CHECK(wf_mod_behavior_get(&WF_MOD_BLOCK_BEHAVIOR, WF_MOD_CTX_CONTENT_LIST) == WF_MOD_ACTION_BLUR);
    MOD_CHECK(wf_mod_behavior_get(&WF_MOD_BLOCK_BEHAVIOR, WF_MOD_CTX_PROFILE_VIEW) == WF_MOD_ACTION_ALERT);
    MOD_CHECK(wf_mod_behavior_get(&WF_MOD_MUTE_BEHAVIOR, WF_MOD_CTX_CONTENT_VIEW) == WF_MOD_ACTION_INFORM);
    MOD_CHECK(wf_mod_behavior_get(&WF_MOD_MUTE_BEHAVIOR, WF_MOD_CTX_CONTENT_LIST) == WF_MOD_ACTION_BLUR);
    MOD_CHECK(wf_mod_behavior_get(&WF_MOD_NOOP_BEHAVIOR, WF_MOD_CTX_CONTENT_LIST) == WF_MOD_ACTION_NONE);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(void) {
    int failed = 0;
    #define RUN(t) do { printf("  " #t "..."); fflush(stdout); failed += t(); printf("ok\n"); } while(0)
    RUN(test_decision_init_free);
    RUN(test_decision_merge);
    RUN(test_decision_downgrade);
    RUN(test_ui_block);
    RUN(test_ui_mute);
    RUN(test_ui_is_me);
    RUN(test_label_interpretation);
    RUN(test_mute_word_matching);
    RUN(test_decide_account);
    RUN(test_decide_post);
    RUN(test_decide_post_with_mute_word);
    RUN(test_decide_post_hidden);
    RUN(test_behavior_get);

    if (failed == 0) {
        printf("All moderation tests passed\n");
        return 0;
    } else {
        printf("%d moderation test(s) failed\n", failed);
        return 1;
    }
}
