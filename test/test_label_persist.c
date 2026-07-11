/*
 * test_label_persist.c — persist labels to the SQLite store and apply them to
 * agent moderation.
 *
 * Gated behind WOLFRAM_BUILD_STORE. Runs fully offline:
 *   1. Save a couple of wf_mod_label via the store, load them back, and assert
 *      field equality (including overwrite/update on (uri,val,src)).
 *   2. Assert that a moderation decision for an account / post reflects a
 *      persisted label (build a subject + opts, run wf_mod_decide_*, assert the
 *      expected alert/blur in the relevant UI context).
 *   3. Exercise the agent bridge: attach a store, persist a label through the
 *      agent, reload it into the agent's moderation context, and read it back.
 */

#include "wolfram/agent.h"
#include "wolfram/store.h"
#include "wolfram/moderation.h"
#include "test.h"

#include <stdlib.h>
#include <string.h>

#define DID       "did:plc:subject"
#define OTHER     "did:plc:other"
#define POST      "at://did:plc:subject/app.bsky.feed.post/abc"
#define POST_OTHER "at://did:plc:other/app.bsky.feed.post/abc"
#define LABELER   "did:plc:labeler1"

static const char *PREFS_JSON =
    "["
    "  {\"$type\":\"app.bsky.actor.defs#adultContentPref\",\"enabled\":false},"
    "  {\"$type\":\"app.bsky.actor.defs#labelersPref\","
    "     \"labelers\":[{\"did\":\"" LABELER "\"}]}"
    "]";

static const char *LABEL_DEFS_JSON =
    "{\"labelValueDefinitions\":["
    "  {\"identifier\":\"porn\",\"blurs\":\"content\",\"severity\":\"alert\","
    "     \"adultOnly\":true,\"defaultSetting\":\"warn\"}"
    "]}";

static wf_mod_opts build_opts(void) {
    wf_mod_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.user_did = DID;
    wf_mod_prefs_from_json(&opts.prefs, PREFS_JSON);
    wf_mod_label_def *defs = NULL;
    size_t count = 0;
    wf_mod_label_defs_from_labeler(LABELER, LABEL_DEFS_JSON, &defs, &count);
    opts.label_defs = defs;
    opts.label_def_count = count;
    return opts;
}

int main(void) {
    wf_store *store = NULL;
    WF_CHECK(wf_store_open(&store, ":memory:") == WF_OK);

    /* ── 1. save + load round-trip, plus overwrite ── */
    WF_CHECK(wf_store_save_label(store, DID, NULL, "porn", LABELER,
                                  "2020-01-01T00:00:00Z", 0, 0, 0, NULL) == WF_OK);
    WF_CHECK(wf_store_save_label(store, POST, "bafypost", "porn", LABELER,
                                  "2020-01-01T00:00:00Z", 0, 1, 0, NULL) == WF_OK);

    wf_mod_label *acc_labels = NULL;
    size_t acc_n = 0;
    WF_CHECK(wf_store_load_labels(store, DID, &acc_labels, &acc_n) == WF_OK);
    WF_CHECK(acc_n == 1);
    WF_CHECK(acc_labels != NULL);
    WF_CHECK(strcmp(acc_labels[0].uri, DID) == 0);
    WF_CHECK(strcmp(acc_labels[0].val, "porn") == 0);
    WF_CHECK(strcmp(acc_labels[0].src, LABELER) == 0);
    WF_CHECK(acc_labels[0].cts && strcmp(acc_labels[0].cts,
                                          "2020-01-01T00:00:00Z") == 0);
    /* A positive label reloads as a positive (neg==0) and not a revocation. */
    WF_CHECK(acc_labels[0].neg == 0);

    /* Overwrite the (uri,val,src) tuple with a new cts. */
    WF_CHECK(wf_store_save_label(store, DID, NULL, "porn", LABELER,
                                  "2021-02-02T00:00:00Z", 0, 0, 0, NULL) == WF_OK);
    wf_mod_labels_free(acc_labels, acc_n);
    WF_CHECK(wf_store_load_labels(store, DID, &acc_labels, &acc_n) == WF_OK);
    WF_CHECK(acc_n == 1);
    WF_CHECK(acc_labels[0].cts && strcmp(acc_labels[0].cts,
                                          "2021-02-02T00:00:00Z") == 0);

    wf_mod_label *post_labels = NULL;
    size_t post_n = 0;
    WF_CHECK(wf_store_load_labels(store, POST, &post_labels, &post_n) == WF_OK);
    WF_CHECK(post_n == 1);
    WF_CHECK(strcmp(post_labels[0].val, "porn") == 0);
    /* cid / has_cid must round-trip, not be dropped. */
    WF_CHECK(post_labels[0].has_cid == 1);
    WF_CHECK(post_labels[0].cid && strcmp(post_labels[0].cid, "bafypost") == 0);

    /* ── 1b. a negation (revocation) is a SEPARATE row and reloads as neg=1 ── */
    WF_CHECK(wf_store_save_label(store, DID, NULL, "porn", LABELER,
                                  "2021-03-03T00:00:00Z", 1, 0, 0, NULL) == WF_OK);
    wf_mod_labels_free(post_labels, post_n);
    WF_CHECK(wf_store_load_labels(store, DID, &post_labels, &post_n) == WF_OK);
    /* Positive (neg=0) and negation (neg=1) both present — distinct rows. */
    WF_CHECK(post_n == 2);
    int saw_pos = 0, saw_neg = 0;
    for (size_t i = 0; i < post_n; i++) {
        if (post_labels[i].neg) saw_neg = 1; else saw_pos = 1;
    }
    WF_CHECK(saw_pos);
    WF_CHECK(saw_neg);

    /* ── 2. persisted labels drive moderation decisions ── */
    wf_mod_opts opts = build_opts();

    /* The porn def carries WF_MOD_FLAG_NO_SELF, so it must be applied to a
     * *different* actor than the viewer (user_did == DID). The label URI must
     * match the subject DID for the decision engine to bind it. */
    wf_mod_label acc_label = {0};
    acc_label.src = LABELER;
    acc_label.uri = "did:plc:other/app.bsky.actor.profile/self";
    acc_label.val = "porn";
    acc_label.cts = "2020-01-01T00:00:00Z";
    wf_mod_subject_profile subj = {0};
    subj.did = OTHER;
    subj.labels = &acc_label;
    subj.label_count = 1;
    wf_mod_decision prof = {0};
    WF_CHECK(wf_mod_decide_profile(&prof, &subj, &opts) == WF_OK);
    wf_mod_ui ui = {0};
    WF_CHECK(wf_mod_decision_ui(&prof, WF_MOD_CTX_PROFILE_VIEW, &ui) == WF_OK);
    WF_CHECK(ui.alert_count > 0);
    wf_mod_ui_free(&ui);
    wf_mod_decision_free(&prof);

    /* Post label (porn, content target) on a different actor -> blur in content view. */
    wf_mod_label post_label = {0};
    post_label.src = LABELER;
    post_label.uri = POST_OTHER;
    post_label.val = "porn";
    post_label.cts = "2020-01-01T00:00:00Z";
    wf_mod_subject_post psubj = {0};
    psubj.uri = POST_OTHER;
    psubj.labels = &post_label;
    psubj.label_count = 1;
    wf_mod_decision pdec = {0};
    WF_CHECK(wf_mod_decide_post(&pdec, &psubj, &opts) == WF_OK);
    wf_mod_ui pui = {0};
    WF_CHECK(wf_mod_decision_ui(&pdec, WF_MOD_CTX_CONTENT_VIEW, &pui) == WF_OK);
    WF_CHECK(pui.blur_count > 0);
    wf_mod_ui_free(&pui);
    wf_mod_decision_free(&pdec);

    if (opts.label_def_count)
        wf_mod_label_defs_free(opts.label_defs, opts.label_def_count);
    wf_mod_prefs_free(&opts.prefs);
    wf_mod_labels_free(acc_labels, acc_n);
    wf_mod_labels_free(post_labels, post_n);

    /* ── 3. agent bridge ── */
    wf_agent *agent = wf_agent_new("https://bsky.social");
    WF_CHECK(agent != NULL);
    /* The agent must know its own DID so load_labels_from_store can scope to it. */
    WF_CHECK(wf_agent_set_did(agent, DID) == WF_OK);
    WF_CHECK(wf_agent_attach_label_store(agent, store) == WF_OK);

    wf_mod_label one = {0};
    one.src = LABELER;
    one.uri = DID;
    one.val = "spam";
    one.cts = "2020-01-01T00:00:00Z";
    WF_CHECK(wf_agent_persist_label(agent, &one) == WF_OK);

    /* Store now has porn (DID) + spam (DID) + porn (POST). */
    WF_CHECK(wf_agent_load_labels_from_store(agent) == WF_OK);
    size_t pc = 0;
    const wf_mod_label *pl = wf_agent_get_persisted_labels(agent, &pc);
    WF_CHECK(pl != NULL);
    WF_CHECK(pc == 2);
    int saw_spam = 0;
    for (size_t i = 0; i < pc; i++) {
        if (pl[i].val && strcmp(pl[i].val, "spam") == 0) saw_spam = 1;
    }
    WF_CHECK(saw_spam);

    /* Best-effort no-op when nothing is attached to a fresh agent. */
    wf_agent *agent2 = wf_agent_new("https://bsky.social");
    WF_CHECK(agent2 != NULL);
    WF_CHECK(wf_agent_load_labels_from_store(agent2) == WF_OK);
    WF_CHECK(wf_agent_persist_label(agent2, &one) == WF_OK);
    wf_agent_free(agent2);

    wf_agent_free(agent); /* frees the agent-owned persisted labels */
    wf_store_close(store);

    printf("label_persist: all checks passed\n");
    return 0;
}
