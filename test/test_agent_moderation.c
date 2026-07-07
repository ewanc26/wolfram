/*
 * test_agent_moderation.c — offline unit tests for the agent moderation
 * wiring (parsing API-shaped JSON into engine subjects + deciding).
 *
 * No network: we craft profile/post JSON in code, parse it with the
 * wf_agent_mod_*_subject_from_json shims, build a wf_mod_opts by hand, and run
 * the engine's decision functions.
 */

#include "wolfram/agent.h"
#include "wolfram/moderation.h"
#include "test.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ME_DID "did:plc:me"
#define LABELER "did:plc:labeler1"
#define SUBJECT_DID "did:plc:subject"

/* A preferences array with adult content on and labeler1 configured. */
static const char *PREFS_JSON =
    "["
    "  {\"$type\":\"app.bsky.actor.defs#adultContentPref\",\"enabled\":true},"
    "  {\"$type\":\"app.bsky.actor.defs#labelersPref\","
    "     \"labelers\":[{\"did\":\"" LABELER "\"}]},"
    "  {\"$type\":\"app.bsky.actor.defs#mutedWordsPref\","
    "     \"words\":[{\"value\":\"spam\",\"targets\":[\"content\"]}]}"
    "]";

/* Label value definitions as labeler1 would advertise them. The engine's
 * wf_mod_label_defs_from_labeler expects an object wrapping the array. */
static const char *LABEL_DEFS_JSON =
    "{\"labelValueDefinitions\":["
    "  {\"identifier\":\"porn\",\"blurs\":\"content\",\"severity\":\"alert\","
    "     \"adultOnly\":true,\"defaultSetting\":\"warn\"},"
    "  {\"identifier\":\"spam\",\"blurs\":\"none\",\"severity\":\"alert\","
    "     \"adultOnly\":false,\"defaultSetting\":\"warn\"}"
    "]}";

/* Build an opts with prefs + labeler defs. Caller frees prefs/defs. */
static wf_mod_opts build_opts(void) {
    wf_mod_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.user_did = ME_DID;

    wf_mod_prefs_from_json(&opts.prefs, PREFS_JSON);

    wf_mod_label_def *defs = NULL;
    size_t count = 0;
    wf_mod_label_defs_from_labeler(LABELER, LABEL_DEFS_JSON, &defs, &count);
    opts.label_defs = defs;
    opts.label_def_count = count;
    return opts;
}

static int has_cause_type(const wf_mod_decision *d, wf_mod_cause_type t) {
    for (size_t i = 0; i < d->cause_count; i++) {
        if (d->causes[i].type == t) return 1;
    }
    return 0;
}

static int test_profile_subject(void) {
    /* Profile with a blocked-by viewer flag, a profile label (porn) and an
     * account label (spam), both from labeler1. */
    static const char *PROFILE_JSON =
        "{"
        "  \"did\":\"" SUBJECT_DID "\","
        "  \"handle\":\"subj.example\","
        "  \"viewer\":{\"blockedBy\":true,\"muted\":false,\"blocking\":null},"
        "  \"labels\":["
        "    {\"src\":\"" LABELER "\","
        "     \"uri\":\"at://" SUBJECT_DID "/app.bsky.actor.profile/self\","
        "     \"val\":\"porn\",\"cts\":\"2020-01-01T00:00:00Z\"},"
        "    {\"src\":\"" LABELER "\",\"uri\":\"" SUBJECT_DID "\","
        "     \"val\":\"spam\",\"cts\":\"2020-01-01T00:00:00Z\"}"
        "  ]"
        "}";

    cJSON *root = cJSON_Parse(PROFILE_JSON);
    WF_CHECK(root != NULL);

    wf_mod_subject_profile subj = {0};
    wf_mod_label *labels = NULL;
    size_t label_count = 0;
    WF_CHECK(wf_agent_mod_profile_subject_from_json(root, &subj,
            &labels, &label_count) == WF_OK);
    subj.labels = labels;
    subj.label_count = label_count;

    WF_CHECK(subj.did != NULL);
    WF_CHECK(strcmp(subj.did, SUBJECT_DID) == 0);
    WF_CHECK(label_count == 2);
    WF_CHECK(subj.viewer.blocked_by != NULL);
    WF_CHECK(subj.viewer.muted == NULL);

    wf_mod_opts opts = build_opts();

    wf_mod_decision acc = {0}, prof = {0}, merged = {0};
    WF_CHECK(wf_mod_decide_account(&acc, &subj, &opts) == WF_OK);
    WF_CHECK(wf_mod_decide_profile(&prof, &subj, &opts) == WF_OK);
    WF_CHECK(wf_mod_decision_merge(&merged, &acc, &prof) == WF_OK);

    /* blockedBy causes a BLOCKED_BY cause; spam (account) a label cause. */
    WF_CHECK(has_cause_type(&acc, WF_MOD_CAUSE_BLOCKED_BY));
    WF_CHECK(has_cause_type(&acc, WF_MOD_CAUSE_LABEL));
    WF_CHECK(wf_mod_decision_blocked(&acc));

    /* porn (profile) -> a profile label cause. */
    WF_CHECK(has_cause_type(&prof, WF_MOD_CAUSE_LABEL));
    WF_CHECK(prof.cause_count == 1);

    /* merged combines both. */
    WF_CHECK(merged.cause_count == acc.cause_count + prof.cause_count);

    wf_mod_decision_free(&acc);
    wf_mod_decision_free(&prof);
    wf_mod_decision_free(&merged);

    if (labels) wf_mod_labels_free(labels, label_count);
    if (opts.label_def_count) wf_mod_label_defs_free(opts.label_defs, opts.label_def_count);
    wf_mod_prefs_free(&opts.prefs);
    cJSON_Delete(root);
    return 0;
}

static int test_post_subject(void) {
    static const char *POST_JSON =
        "{"
        "  \"uri\":\"at://" SUBJECT_DID "/app.bsky.feed.post/abc\","
        "  \"cid\":\"bafypost\","
        "  \"author\":{"
        "    \"did\":\"" SUBJECT_DID "\","
        "    \"handle\":\"subj.example\","
        "    \"viewer\":{\"muted\":true,\"blockedBy\":false},"
        "    \"labels\":[{\"src\":\"" LABELER "\",\"uri\":\"" SUBJECT_DID "\","
        "      \"val\":\"spam\",\"cts\":\"2020-01-01T00:00:00Z\"}]"
        "  },"
        "  \"record\":{\"text\":\"this is a spam post\"},"
        "  \"labels\":[{\"src\":\"" LABELER "\","
        "    \"uri\":\"at://" SUBJECT_DID "/app.bsky.feed.post/abc\","
        "    \"val\":\"porn\",\"cts\":\"2020-01-01T00:00:00Z\"}]"
        "}";

    cJSON *root = cJSON_Parse(POST_JSON);
    WF_CHECK(root != NULL);

    cJSON *author = cJSON_GetObjectItemCaseSensitive(root, "author");
    WF_CHECK(author != NULL);

    wf_mod_subject_post subj = {0};
    wf_mod_label *labels = NULL, *author_labels = NULL;
    size_t label_count = 0, author_label_count = 0;
    WF_CHECK(wf_agent_mod_post_subject_from_json(root, author, &subj,
            &labels, &label_count, &author_labels, &author_label_count) == WF_OK);
    subj.labels = labels;
    subj.label_count = label_count;
    subj.author.labels = author_labels;
    subj.author.label_count = author_label_count;

    WF_CHECK(subj.uri != NULL);
    WF_CHECK(subj.text != NULL);
    WF_CHECK(strcmp(subj.text, "this is a spam post") == 0);
    WF_CHECK(label_count == 1);
    WF_CHECK(author_label_count == 1);
    WF_CHECK(subj.author.viewer.muted != NULL);

    wf_mod_opts opts = build_opts();

    wf_mod_decision d = {0};
    WF_CHECK(wf_mod_decide_post(&d, &subj, &opts) == WF_OK);
    WF_CHECK(d.is_me == 0);

    /* Expect: content label (porn) + mute word (spam) + author muted +
     * author account label (spam) = 4 causes. */
    WF_CHECK(d.cause_count == 4);
    WF_CHECK(has_cause_type(&d, WF_MOD_CAUSE_LABEL));
    WF_CHECK(has_cause_type(&d, WF_MOD_CAUSE_MUTE_WORD));
    WF_CHECK(has_cause_type(&d, WF_MOD_CAUSE_MUTED));

    wf_mod_decision_free(&d);

    if (labels) wf_mod_labels_free(labels, label_count);
    if (author_labels) wf_mod_labels_free(author_labels, author_label_count);
    if (opts.label_def_count) wf_mod_label_defs_free(opts.label_defs, opts.label_def_count);
    wf_mod_prefs_free(&opts.prefs);
    cJSON_Delete(root);
    return 0;
}

int main(void) {
    test_profile_subject();
    test_post_subject();
    WF_TEST_SUMMARY();
}
