# Moderation engine (`moderation.h`)

The moderation engine computes blur/alert/inform/filter decisions for subjects
(accounts, profiles, posts, notifications, feed generators, and user lists) from
labels, blocks, mutes, hidden posts, and muted words. It is fully **offline**:
it takes input data as C structs and produces decisions. No network I/O is
performed — you fetch the raw records/labels yourself (e.g. with the
[agent](agent.md) or [sync](sync.md) layers) and hand them to the engine.

Reference implementation: `atproto/packages/api/src/moderation/` (TypeScript).

Conventions used throughout:

- Success is `WF_OK` (the `wf_status` enum); any other value is an error.
- Functions that allocate an owned struct come with a matching `_free`
  (e.g. `wf_mod_prefs_free`, `wf_mod_labels_free`, `wf_mod_label_defs_free`,
  `wf_mod_decision_free`, `wf_mod_ui_free`). Call it when done.
- Three JSON-ingestion helpers parse API-shaped JSON into engine structs so you
  don't have to build the structs by hand.

## Building preferences from JSON

A user's `app.bsky.actor.defs#preferences` array (or a bundled
`app.bsky.actor.defs#moderationPrefs` object) parses into `wf_mod_prefs`:

```c
#include "wolfram/moderation.h"

/* `json` is the preferences array/object as returned by the API. */
wf_mod_prefs prefs = {0};
wf_status st = wf_mod_prefs_from_json(&prefs, json);
if (st != WF_OK) { /* handle error */ }

/* `prefs` now owns adult_content_enabled, labeler list, global label prefs,
   hidden posts, and muted words. Free when done: */
wf_mod_prefs_free(&prefs);
```

Recognized shapes: `adultContentPref`, `labelersPref`, `contentLabelPrefs`,
`hiddenPostsPref`, and `mutedWordsPref` (plus their `moderationPrefs.*`
wrappers).

## Interpreting a labeler's definitions

Each labeler publishes `labelValueDefinitions` under
`policies.labelValueDefinitions` of its `app.bsky.labeler.service` record.
Parse them into `wf_mod_label_def` (with computed behaviors/flags/defaults)
via `wf_mod_label_defs_from_labeler`:

```c
/* `labeler_json` is the labelValueDefinitions array; `labeler_did` is recorded
   as `defined_by` on each definition. */
wf_mod_label_def *defs = NULL;
size_t def_count = 0;
wf_status st = wf_mod_label_defs_from_labeler(labeler_did, labeler_json,
                                              &defs, &def_count);
if (st != WF_OK) { /* handle error */ }

/* Later: re-interpret a single raw definition if needed:
   wf_mod_interpret_label_def(&out, identifier, defined_by,
                              blurs, severity, adult_only, default_setting); */

/* Free the array when done: */
wf_mod_label_defs_free(defs, def_count);
```

You typically repeat this for every labeler the user subscribes to (from
`prefs.labelers`) and collect all definitions into one array for `wf_mod_opts`.

## Parsing labels

A `labels` array (each item with `src`, `uri`, `val`, `cts`) parses into
`wf_mod_label` via `wf_mod_labels_from_json`:

```c
wf_mod_label *labels = NULL;
size_t label_count = 0;
wf_status st = wf_mod_labels_from_json(&labels, &label_count, labels_json);
if (st != WF_OK) { /* handle error */ }

/* `labels` is now a caller-owned array. Free when done: */
wf_mod_labels_free(labels, label_count);
```

Labels are attached to subject structs (see below) when computing a decision.

## Computing a decision

Build a `wf_mod_opts` carrying the user's preferences and the known label
definitions, then call a subject decider. Available deciders:

- `wf_mod_decide_account` — account (block/mute/account-labels)
- `wf_mod_decide_profile` — profile (profile-labels only)
- `wf_mod_decide_post` — post (content labels + embeds + author)
- `wf_mod_decide_notification` — notification (account + profile + content)
- `wf_mod_decide_feed_generator` — feed generator
- `wf_mod_decide_user_list` — user list
- `wf_mod_decide_status` — account status (deactivated/takendown)

```c
/* Assemble the options once per user/session. */
wf_mod_opts opts = {
    .user_did    = my_did,
    .prefs       = prefs,          /* from wf_mod_prefs_from_json */
    .label_defs  = defs,           /* from wf_mod_label_defs_from_labeler */
    .label_def_count = def_count,
};

/* Describe the post to be moderated. */
wf_mod_subject_post subject = {
    .uri     = post_uri,
    .cid     = post_cid,
    .author  = { .did = author_did, .handle = author_handle,
                 .labels = author_labels, .label_count = author_label_count },
    .labels  = post_labels,
    .label_count = post_label_count,
    .text    = post_text,
    .embed_type = NULL,            /* or "app.bsky.embed.record" etc. */
    .embed_uri  = NULL,
};

wf_mod_decision decision = {0};
wf_status st = wf_mod_decide_post(&decision, &subject, &opts);
if (st != WF_OK) { /* handle error */ }

/* Inspect the decision. */
if (wf_mod_decision_blocked(&decision)) { /* block the post */ }
if (wf_mod_decision_muted(&decision))   { /* mute the post   */ }

/* Map to a UI for a view context. */
wf_mod_ui ui = {0};
wf_status st2 = wf_mod_decision_ui(&decision, WF_MOD_CTX_CONTENT_LIST, &ui);
/* ui.blurs / ui.alerts / ui.informs point at owned cause subsets. */
wf_mod_ui_free(&ui);

wf_mod_decision_free(&decision);
```

Decisions can be merged (`wf_mod_decision_merge`), downgraded
(`wf_mod_decision_downgrade`), and queried for blocking/mute causes. Use
`wf_mod_find_label_def`, `wf_mod_get_label_pref`, and `wf_mod_behavior_get` to
resolve individual labels, and `wf_mod_match_mute_words` to test post text/tags
against the user's muted words.

## Persisted labels (`store.h`)

When the library is built with `WOLFRAM_BUILD_STORE=ON`, the optional SQLite
store (`store.h`) lets you persist the labels you've seen so the engine can
moderate content offline:

```c
#include "wolfram/store.h"

wf_store *store = NULL;
wf_store_open(&store, "moderation.db");

/* Persist a label for a subject (uri + cid + the raw label JSON blob). */
wf_store_save_label(store, post_uri, post_cid, label_src, label_val, label_cts);

/* Load all labels attached to a subject. */
wf_mod_label *loaded = NULL;
size_t loaded_count = 0;
wf_store_load_labels(store, post_uri, &loaded, &loaded_count);
/* ... feed `loaded`/`loaded_count` into a wf_mod_subject_* ... */
wf_mod_labels_free(loaded, loaded_count);   /* store copies its own; free the parse */

wf_store_close(store);
```

See [modules.md](modules.md) for the build-gating details and the optional
`WOLFRAM_BUILD_STORE_CRYPTO=ON` at-rest encryption (libsodium).
