/*
 * moderation.h — moderation decision engine for the AT Protocol.
 *
 * Computes moderation decisions (blur, alert, inform, filter) for subjects
 * (accounts, profiles, posts, notifications, feed generators, user lists)
 * based on labels, blocks, mutes, hidden posts, and muted words.
 *
 * The engine is self-contained: it takes input data as C structs and produces
 * decisions. No network I/O is performed.
 *
 * Reference: atproto/packages/api/src/moderation/ (TypeScript)
 */

#ifndef WOLFRAM_MODERATION_H
#define WOLFRAM_MODERATION_H

#include <stddef.h>
#include <stdint.h>
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Enums                                                               */
/* ------------------------------------------------------------------ */

/** Moderation cause types. */
typedef enum wf_mod_cause_type {
    WF_MOD_CAUSE_BLOCKING = 0,
    WF_MOD_CAUSE_BLOCKED_BY,
    WF_MOD_CAUSE_BLOCK_OTHER,
    WF_MOD_CAUSE_LABEL,
    WF_MOD_CAUSE_MUTED,
    WF_MOD_CAUSE_MUTE_WORD,
    WF_MOD_CAUSE_HIDDEN
} wf_mod_cause_type;

/** Label target. */
typedef enum wf_mod_label_target {
    WF_MOD_TARGET_ACCOUNT = 0,
    WF_MOD_TARGET_PROFILE,
    WF_MOD_TARGET_CONTENT
} wf_mod_label_target;

/** Label preference (user setting). */
typedef enum wf_mod_label_pref {
    WF_MOD_PREF_IGNORE = 0,
    WF_MOD_PREF_WARN,
    WF_MOD_PREF_HIDE
} wf_mod_label_pref;

/** UI context — which view the decision is being computed for. */
typedef enum wf_mod_context {
    WF_MOD_CTX_PROFILE_LIST = 0,
    WF_MOD_CTX_PROFILE_VIEW,
    WF_MOD_CTX_AVATAR,
    WF_MOD_CTX_BANNER,
    WF_MOD_CTX_DISPLAY_NAME,
    WF_MOD_CTX_CONTENT_LIST,
    WF_MOD_CTX_CONTENT_VIEW,
    WF_MOD_CTX_CONTENT_MEDIA
} wf_mod_context;

/** Action a cause triggers in a given context. */
typedef enum wf_mod_action {
    WF_MOD_ACTION_NONE = 0,
    WF_MOD_ACTION_BLUR,
    WF_MOD_ACTION_ALERT,
    WF_MOD_ACTION_INFORM
} wf_mod_action;

/** Label value definition flags. */
typedef enum wf_mod_label_flag {
    WF_MOD_FLAG_NONE = 0,
    WF_MOD_FLAG_NO_OVERRIDE = 1 << 0,
    WF_MOD_FLAG_ADULT       = 1 << 1,
    WF_MOD_FLAG_UNAUTHED     = 1 << 2,
    WF_MOD_FLAG_NO_SELF      = 1 << 3
} wf_mod_label_flag;

/* ------------------------------------------------------------------ */
/* Label structures                                                    */
/* ------------------------------------------------------------------ */

/** A label as received from the network. */
typedef struct wf_mod_label {
    char *src;   /* DID of the labeler */
    char *uri;   /* URI of the labeled subject */
    char *val;   /* label value string */
    char *cts;   /* creation timestamp */
} wf_mod_label;

/** Behavior for a single target (account/profile/content) across contexts. */
typedef struct wf_mod_behavior {
    wf_mod_action profile_list;
    wf_mod_action profile_view;
    wf_mod_action avatar;
    wf_mod_action banner;
    wf_mod_action display_name;
    wf_mod_action content_list;
    wf_mod_action content_view;
    wf_mod_action content_media;
} wf_mod_behavior;

/** Interpreted label value definition. */
typedef struct wf_mod_label_def {
    char *identifier;         /* e.g. "porn", "gore", "spam" */
    char *defined_by;         /* DID of labeler, or NULL for global */
    int configurable;         /* 1 if user can configure preference */
    wf_mod_label_pref default_setting; /* default preference */
    uint32_t flags;           /* bitmask of wf_mod_label_flag */
    wf_mod_behavior behaviors[3]; /* indexed by wf_mod_label_target */
} wf_mod_label_def;

/* ------------------------------------------------------------------ */
/* Muted word                                                           */
/* ------------------------------------------------------------------ */

/** A muted word entry from user preferences. */
typedef struct wf_mod_muted_word {
    char *value;         /* the word/phrase to mute */
    char *actor_target;  /* "all" or "exclude-following", or NULL */
    char *expires_at;    /* expiry timestamp, or NULL */
    int targets_content; /* 1 if 'content' is in targets */
    int targets_tag;     /* 1 if 'tag' is in targets */
} wf_mod_muted_word;

/** A mute word match result. */
typedef struct wf_mod_mute_word_match {
    char *value;     /* the muted word that matched */
    char *predicate; /* the text that matched */
} wf_mod_mute_word_match;

/* ------------------------------------------------------------------ */
/* Moderation options                                                   */
/* ------------------------------------------------------------------ */

/** Labeler preference entry. */
typedef struct wf_mod_labeler_pref {
    char *did;
    wf_mod_label_pref *label_prefs; /* array of per-label preferences */
    char **label_identifiers;       /* parallel array of label identifiers */
    size_t label_count;
} wf_mod_labeler_pref;

/** User moderation preferences. */
typedef struct wf_mod_prefs {
    int adult_content_enabled;
    wf_mod_label_pref *global_label_prefs; /* global per-label preferences */
    char **global_label_identifiers;       /* parallel identifiers */
    size_t global_label_count;
    size_t global_label_cap;               /* allocation capacity (internal) */
    wf_mod_labeler_pref *labelers;
    size_t labeler_count;
    size_t labeler_cap;                    /* allocation capacity (internal) */
    wf_mod_muted_word *muted_words;
    size_t muted_word_count;
    size_t muted_word_cap;                 /* allocation capacity (internal) */
    char **hidden_posts;
    size_t hidden_post_count;
    size_t hidden_post_cap;                /* allocation capacity (internal) */
} wf_mod_prefs;

/** Moderation options passed to decision functions. */
typedef struct wf_mod_opts {
    const char *user_did;   /* DID of the current user, or NULL */
    wf_mod_prefs prefs;
    wf_mod_label_def *label_defs; /* known label definitions */
    size_t label_def_count;
} wf_mod_opts;

/* ------------------------------------------------------------------ */
/* Moderation cause and decision                                        */
/* ------------------------------------------------------------------ */

/** A single moderation cause. */
typedef struct wf_mod_cause {
    wf_mod_cause_type type;
    int priority;
    int downgraded;

    /* For label causes */
    wf_mod_label label;          /* copy of the label */
    const wf_mod_label_def *label_def; /* pointer to definition (not owned) */
    wf_mod_label_target target;
    wf_mod_label_pref setting;
    wf_mod_behavior behavior;    /* copy of the behavior for this target */
    int no_override;

    /* For mute-word causes */
    wf_mod_mute_word_match *matches;
    size_t match_count;
} wf_mod_cause;

/** A moderation decision — a collection of causes for a subject. */
typedef struct wf_mod_decision {
    char *did;
    int is_me;
    wf_mod_cause *causes;
    size_t cause_count;
    size_t cause_capacity;
} wf_mod_decision;

/* ------------------------------------------------------------------ */
/* Moderation UI                                                        */
/* ------------------------------------------------------------------ */

/** Computed UI result for a given context. */
typedef struct wf_mod_ui {
    int no_override;
    wf_mod_cause *filters;
    size_t filter_count;
    wf_mod_cause *blurs;
    size_t blur_count;
    wf_mod_cause *alerts;
    size_t alert_count;
    wf_mod_cause *informs;
    size_t inform_count;
} wf_mod_ui;

/* ------------------------------------------------------------------ */
/* Subject structures (simplified)                                     */
/* ------------------------------------------------------------------ */

/** Profile viewer state (blocking/muting info). */
typedef struct wf_mod_viewer_state {
    const char *blocking;       /* blocking URI, or NULL */
    const char *blocked_by;      /* 1 if blocked by this actor, 0 otherwise */
    const char *muted;          /* 1 if muted, 0 otherwise */
    const char *muted_by_list;  /* list URI if muted by list, or NULL */
    const char *blocking_by_list; /* list URI if blocking by list, or NULL */
    const char *following;      /* following URI, or NULL */
} wf_mod_viewer_state;

/** A profile subject for moderation. */
typedef struct wf_mod_subject_profile {
    const char *did;
    const char *handle;
    wf_mod_viewer_state viewer;
    wf_mod_label *labels;
    size_t label_count;
} wf_mod_subject_profile;

/** A post subject for moderation. */
typedef struct wf_mod_subject_post {
    const char *uri;
    const char *cid;
    wf_mod_subject_profile author;
    wf_mod_label *labels;
    size_t label_count;
    const char *text;          /* post text */
    const char *embed_type;    /* embed $type, or NULL */
    const char *embed_uri;     /* URI of embedded/quoted post, or NULL */
} wf_mod_subject_post;

/** A feed generator subject. */
typedef struct wf_mod_subject_feed_gen {
    wf_mod_subject_profile creator;
    wf_mod_label *labels;
    size_t label_count;
} wf_mod_subject_feed_gen;

/** A user list subject. */
typedef struct wf_mod_subject_user_list {
    const char *uri;
    wf_mod_subject_profile creator;
    wf_mod_label *labels;
    size_t label_count;
} wf_mod_subject_user_list;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */
/* ------------------------------------------------------------------ */

/** Initialize a decision to empty. Returns WF_OK or WF_ERR_ALLOC. */
wf_status wf_mod_decision_init(wf_mod_decision *d);

/** Free a decision and all owned causes. */
void wf_mod_decision_free(wf_mod_decision *d);

/** Merge two decisions into a new one. The result must be freed by caller. */
wf_status wf_mod_decision_merge(wf_mod_decision *out,
                                const wf_mod_decision *a,
                                const wf_mod_decision *b);

/** Mark all causes in a decision as downgraded. */
void wf_mod_decision_downgrade(wf_mod_decision *d);

/** Check if a decision has a blocking cause. */
int wf_mod_decision_blocked(const wf_mod_decision *d);

/** Check if a decision has a mute cause. */
int wf_mod_decision_muted(const wf_mod_decision *d);

/** Compute the UI for a given context. Free with wf_mod_ui_free. */
wf_status wf_mod_decision_ui(const wf_mod_decision *d,
                             wf_mod_context ctx,
                             wf_mod_ui *out);

/** Free a UI struct. */
void wf_mod_ui_free(wf_mod_ui *ui);

/* ------------------------------------------------------------------ */
/* Cause adders (used internally by subject deciders)                   */
/* ------------------------------------------------------------------ */

wf_status wf_mod_add_blocking(wf_mod_decision *d, const char *blocking_uri);
wf_status wf_mod_add_blocked_by(wf_mod_decision *d, int blocked_by);
wf_status wf_mod_add_block_other(wf_mod_decision *d, int block_other);
wf_status wf_mod_add_muted(wf_mod_decision *d, int muted);
wf_status wf_mod_add_hidden(wf_mod_decision *d, int hidden);
wf_status wf_mod_add_muted_word(wf_mod_decision *d,
                                const wf_mod_mute_word_match *matches,
                                size_t match_count);
wf_status wf_mod_add_label(wf_mod_decision *d,
                           wf_mod_label_target target,
                           const wf_mod_label *label,
                           const wf_mod_opts *opts);

/* ------------------------------------------------------------------ */
/* Subject deciders                                                     */
/* ------------------------------------------------------------------ */

/** Decide moderation for an account (block/mute/account-labels). */
wf_status wf_mod_decide_account(wf_mod_decision *out,
                                const wf_mod_subject_profile *subject,
                                const wf_mod_opts *opts);

/** Decide moderation for a profile (profile-labels only). */
wf_status wf_mod_decide_profile(wf_mod_decision *out,
                                const wf_mod_subject_profile *subject,
                                const wf_mod_opts *opts);

/** Decide moderation for a post (content labels + embeds + author). */
wf_status wf_mod_decide_post(wf_mod_decision *out,
                             const wf_mod_subject_post *subject,
                             const wf_mod_opts *opts);

/** Decide moderation for a notification (account + profile + content). */
wf_status wf_mod_decide_notification(wf_mod_decision *out,
                                     const wf_mod_subject_post *subject,
                                     const wf_mod_opts *opts);

/** Decide moderation for a feed generator. */
wf_status wf_mod_decide_feed_generator(wf_mod_decision *out,
                                       const wf_mod_subject_feed_gen *subject,
                                       const wf_mod_opts *opts);

/** Decide moderation for a user list. */
wf_status wf_mod_decide_user_list(wf_mod_decision *out,
                                  const wf_mod_subject_user_list *subject,
                                  const wf_mod_opts *opts);

/** Decide moderation for account status (deactivated/takendown). */
wf_status wf_mod_decide_status(wf_mod_decision *out,
                               const wf_mod_subject_profile *subject,
                               const wf_mod_opts *opts);

/* ------------------------------------------------------------------ */
/* Label interpretation                                                 */
/* ------------------------------------------------------------------ */

/**
 * Interpret a raw label value definition (from JSON) into a structured
 * definition with computed behaviors, flags, and default setting.
 *
 * `identifier` is the label value string (e.g. "porn", "gore").
 * `blurs` is one of "content", "media", "none".
 * `severity` is one of "alert", "inform", or NULL/empty for none.
 * `adult_only` is 1 if the label is adult-only.
 * `default_setting` is one of "hide", "warn", "ignore", or NULL for "warn".
 */
wf_status wf_mod_interpret_label_def(wf_mod_label_def *out,
                                     const char *identifier,
                                     const char *defined_by,
                                     const char *blurs,
                                     const char *severity,
                                     int adult_only,
                                     const char *default_setting);

/** Free a label def. */
void wf_mod_label_def_free(wf_mod_label_def *def);

/* ------------------------------------------------------------------ */
/* Mute word matching                                                   */
/* ------------------------------------------------------------------ */

/**
 * Match muted words against text and tags.
 *
 * `text` is the post text (UTF-8).
 * `tags` is an array of tag strings (lowercased), or NULL.
 * `tag_count` is the number of tags.
 * `lang` is the primary language code, or NULL (for language exceptions).
 * `following` is 1 if the author is followed by the user (for exclude-following).
 *
 * Returns WF_OK and populates `out_matches`/`out_count` if matches found.
 * If no matches, out_count is set to 0.
 */
wf_status wf_mod_match_mute_words(wf_mod_mute_word_match **out_matches,
                                  size_t *out_count,
                                  const wf_mod_muted_word *muted_words,
                                  size_t muted_word_count,
                                  const char *text,
                                  const char *const *tags,
                                  size_t tag_count,
                                  const char *lang,
                                  int following);

/** Free an array of mute word matches. */
void wf_mod_mute_word_matches_free(wf_mod_mute_word_match *matches,
                                   size_t count);

/* ------------------------------------------------------------------ */
/* Utility                                                              */
/* ------------------------------------------------------------------ */

/** Find a label definition by identifier. Returns NULL if not found. */
const wf_mod_label_def *wf_mod_find_label_def(const wf_mod_opts *opts,
                                               const char *identifier,
                                               const char *labeler_did);

/** Get the label preference for a label, considering user prefs and label defs. */
wf_mod_label_pref wf_mod_get_label_pref(const wf_mod_opts *opts,
                                        const wf_mod_label_def *def,
                                        const char *labeler_did);

/** Get the action for a behavior in a given context. */
wf_mod_action wf_mod_behavior_get(const wf_mod_behavior *beh,
                                   wf_mod_context ctx);

/* ------------------------------------------------------------------ */
/* JSON ingestion (offline — parses API-shaped JSON into engine structs) */
/* ------------------------------------------------------------------ */

/**
 * Parse a user's moderation `preferences` JSON (the
 * `app.bsky.actor.defs#preferences` array, or a bundled
 * `app.bsky.actor.defs#moderationPrefs` object) into `wf_mod_prefs`.
 *
 * Recognized preference shapes:
 *  - adultContentPref / moderationPrefs.adultContentEnabled -> adult_content_enabled
 *  - labelersPref / moderationPrefs.labelers -> prefs.labelers (DID list)
 *  - contentLabelPrefs / moderationPrefs.labels -> global per-label preferences
 *  - hiddenPostsPref / moderationPrefs.hiddenPosts -> prefs.hidden_posts
 *  - mutedWordsPref / moderationPrefs.mutedWords -> prefs.muted_words
 *
 * On success, `out` owns all pointed-to memory; free it with
 * wf_mod_prefs_free. On failure, `out` is left zeroed.
 */
wf_status wf_mod_prefs_from_json(wf_mod_prefs *out, const char *json);

/** Free a wf_mod_prefs produced by wf_mod_prefs_from_json. */
void wf_mod_prefs_free(wf_mod_prefs *prefs);

/**
 * Parse a labeler's `labelValueDefinitions` JSON (the
 * `com.atproto.label.defs#labelValueDefinition` array, found under
 * `policies.labelValueDefinitions` of an `app.bsky.labeler.service` record)
 * into `wf_mod_label_def` via wf_mod_interpret_label_def.
 *
 * `labeler_did` is recorded as `defined_by` on each definition. On success,
 * `*out` points to a caller-owned array of `out_count` defs; free with
 * wf_mod_label_defs_free. On failure, `*out` is set to NULL.
 */
wf_status wf_mod_label_defs_from_labeler(const char *labeler_did,
                                         const char *json,
                                         wf_mod_label_def **out,
                                         size_t *out_count);

/** Free an array of label defs produced by wf_mod_label_defs_from_labeler. */
void wf_mod_label_defs_free(wf_mod_label_def *defs, size_t count);

/**
 * Parse a `labels` array (each item with `src`, `uri`, `val`, `cts`) into
 * `wf_mod_label`. On success, `*out` points to a caller-owned array of
 * `out_count` labels; free with wf_mod_labels_free. On failure, `*out` is
 * set to NULL.
 */
wf_status wf_mod_labels_from_json(wf_mod_label **out,
                                  size_t *out_count,
                                  const char *json);

/** Free an array of labels produced by wf_mod_labels_from_json. */
void wf_mod_labels_free(wf_mod_label *labels, size_t count);

/** Built-in behavior constants. */
extern const wf_mod_behavior WF_MOD_BLOCK_BEHAVIOR;
extern const wf_mod_behavior WF_MOD_MUTE_BEHAVIOR;
extern const wf_mod_behavior WF_MOD_MUTEWORD_BEHAVIOR;
extern const wf_mod_behavior WF_MOD_HIDE_BEHAVIOR;
extern const wf_mod_behavior WF_MOD_NOOP_BEHAVIOR;

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_MODERATION_H */
