/*
 * actor_prefs_typed.h — owned typed parsers + agent wrappers for the
 * app.bsky.actor PREFERENCES union, the getSuggestions endpoint, and the
 * (DECLARATION) record shape. See src/actor_prefs_typed.c for ownership rules.
 *
 * This module is DISTRICT from actor_typed.h (which covers profile/profiles/
 * search/likes/status). It centralizes the rich app.bsky.actor.defs#preferences
 * union (every known preference type) plus a builder that round-trips the
 * owned structs back to a JSON array.
 *
 * Conventions mirror labeler_typed.h / actor_typed.h: wf_status error codes,
 * static strdup/set_string/reset helpers, ownership via cJSON_DetachItemFrom*,
 * and a matching `_free` for every owned struct (a freed/zeroed struct frees
 * safely). Every owned string is heap-allocated; the `extra` field holds an
 * owned cJSON subtree of unknown/unbounded fields.
 *
 * NSIDs covered:
 *   - app.bsky.actor.getPreferences  (full preferences union)
 *   - app.bsky.actor.putPreferences  (input = array of preference objects)
 *   - app.bsky.actor.getSuggestions  (actor profile views + cursor)
 *
 * NSIDs referenced but with NO generated lex wrapper in this tree (honest
 * stubs, see report):
 *   - app.bsky.actor.declareActor     (no declaration.json in local lexicons)
 *   - app.bsky.actor.getSuggestionV2  (absent from local lexicons)
 */

#ifndef WOLFRAM_ACTOR_PREFS_TYPED_H
#define WOLFRAM_ACTOR_PREFS_TYPED_H

/* Provides wf_agent_actor_list + wf_agent_parse_actor_search (reused for
 * getSuggestions), and the shared strdup/reset conventions. */
#include "wolfram/actor_typed.h"
#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Preference sub-structs (one per known app.bsky.actor.defs#* pref)  */
/* ------------------------------------------------------------------ */

/* adultContentPref: { enabled } */
typedef struct wf_actor_pref_adult_content {
    bool has_enabled;
    bool enabled;
} wf_actor_pref_adult_content;

/* contentLabelPref: { labelerDid?, label, visibility } — appears as multiple */
typedef struct wf_actor_pref_content_label {
    char *labeler_did;
    char *label;
    char *visibility;
} wf_actor_pref_content_label;

/* savedFeed (item of savedFeedsPrefV2#items). */
typedef struct wf_actor_pref_saved_feed {
    char *id;
    char *type;
    char *value;
    bool has_pinned;
    bool pinned;
} wf_actor_pref_saved_feed;

/* savedFeedsPref: { pinned[], saved[], timelineIndex? } */
typedef struct wf_actor_pref_saved_feeds {
    char **pinned;
    size_t pinned_count;
    char **saved;
    size_t saved_count;
    bool has_timeline_index;
    int64_t timeline_index;
} wf_actor_pref_saved_feeds;

/* savedFeedsPrefV2: { items[] } */
typedef struct wf_actor_pref_saved_feeds_v2 {
    wf_actor_pref_saved_feed *items;
    size_t item_count;
} wf_actor_pref_saved_feeds_v2;

/* personalDetailsPref: { birthDate? } */
typedef struct wf_actor_pref_personal_details {
    char *birth_date;
} wf_actor_pref_personal_details;

/* declaredAgePref: { isOverAge13?, isOverAge16?, isOverAge18? } */
typedef struct wf_actor_pref_declared_age {
    bool has_over_13;
    bool over_13;
    bool has_over_16;
    bool over_16;
    bool has_over_18;
    bool over_18;
} wf_actor_pref_declared_age;

/* feedViewPref: { feed, hideReplies?, hideRepliesByUnfollowed?, ... } — multiple */
typedef struct wf_actor_pref_feed_view {
    char *feed;
    bool has_hide_replies;
    bool hide_replies;
    bool has_hide_replies_by_unfollowed;
    bool hide_replies_by_unfollowed;
    bool has_hide_replies_by_like_count;
    int64_t hide_replies_by_like_count;
    bool has_hide_reposts;
    bool hide_reposts;
    bool has_hide_quote_posts;
    bool hide_quote_posts;
} wf_actor_pref_feed_view;

/* threadViewPref: { sort? } */
typedef struct wf_actor_pref_thread_view {
    char *sort;
} wf_actor_pref_thread_view;

/* interestsPref: { tags[] } */
typedef struct wf_actor_pref_interests {
    char **tags;
    size_t tag_count;
} wf_actor_pref_interests;

/* mutedWord (item of mutedWordsPref#items). */
typedef struct wf_actor_pref_muted_word {
    char *id;
    char *value;
    char **targets;
    size_t target_count;
    char *actor_target;
    char *expires_at;
} wf_actor_pref_muted_word;

/* hiddenPostsPref: { items[] } */
typedef struct wf_actor_pref_hidden_posts {
    char **uris;
    size_t uri_count;
} wf_actor_pref_hidden_posts;

/* bskyAppStatePref: { activeProgressGuide?, queuedNudges[]?, nuxs[]? } */
typedef struct wf_actor_pref_bsky_app_state {
    char *active_progress_guide;
    char **queued_nudges;
    size_t queued_nudge_count;
    cJSON *nuxs;          /* owned detached array of nux objects; NULL absent */
} wf_actor_pref_bsky_app_state;

/* labelersPref: { labelers[] } */
typedef struct wf_actor_pref_labelers {
    char **labelers;
    size_t labeler_count;
} wf_actor_pref_labelers;

/* postInteractionSettingsPref: { threadgateAllowRules[]?, postgateEmbeddingRules[]? }
 * Both are unions of other record shapes; kept as owned detached cJSON arrays. */
typedef struct wf_actor_pref_post_interaction_settings {
    cJSON *threadgate_allow_rules;     /* owned array; NULL absent */
    cJSON *postgate_embedding_rules;   /* owned array; NULL absent */
} wf_actor_pref_post_interaction_settings;

/* verificationPrefs: { hideBadges? } */
typedef struct wf_actor_pref_verification {
    bool has_hide_badges;
    bool hide_badges;
} wf_actor_pref_verification;

/* liveEventPreferences: { hiddenFeedIds[]?, hideAllFeeds? } */
typedef struct wf_actor_pref_live_events {
    char **hidden_feed_ids;
    size_t hidden_feed_id_count;
    bool has_hide_all_feeds;
    bool hide_all_feeds;
} wf_actor_pref_live_events;

/* ------------------------------------------------------------------ */
/* Aggregator: full preferences union                                 */
/* ------------------------------------------------------------------ */

/* An owned aggregation of a parsed app.bsky.actor.defs#preferences array.
 * Each known $type is classified into its sub-struct; unknown $types are kept
 * verbatim in `extra` (an owned cJSON array). On error `out` is fully reset. */
typedef struct wf_actor_preferences {
    wf_actor_pref_adult_content adult_content;
    wf_actor_pref_content_label *content_labels;
    size_t content_label_count;
    wf_actor_pref_saved_feeds saved_feeds;
    wf_actor_pref_saved_feeds_v2 saved_feeds_v2;
    wf_actor_pref_personal_details personal_details;
    wf_actor_pref_declared_age declared_age;
    wf_actor_pref_feed_view *feed_views;
    size_t feed_view_count;
    wf_actor_pref_thread_view thread_view;
    wf_actor_pref_interests interests;
    wf_actor_pref_muted_word *muting_keywords;
    size_t muting_keyword_count;
    wf_actor_pref_hidden_posts hidden_posts;
    wf_actor_pref_bsky_app_state bsky_app_state;
    wf_actor_pref_labelers labelers;
    wf_actor_pref_post_interaction_settings post_interaction_settings;
    wf_actor_pref_verification verification;
    wf_actor_pref_live_events live_events;
    cJSON *extra;          /* owned detached array of unknown raw pref objects */
} wf_actor_preferences;

/* Parse a getPreferences body. Accepts either the full
 * {"preferences":[...]} envelope or a bare JSON array of preference objects.
 * Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or a
 * missing/invalid shape, WF_ERR_ALLOC on allocation failure, WF_OK on success.
 * On any error `out` is left fully reset. */
wf_status wf_actor_parse_preferences(const char *json, size_t json_len,
                                     wf_actor_preferences *out);

/* Free the owned contents of `out` (also safe on a reset/zeroed struct). */
void wf_actor_preferences_free(wf_actor_preferences *out);

/* Build a JSON array string from `prefs`. Each known sub-struct emits one
 * preference object with its `$type`; unknown `extra` objects are re-emitted
 * verbatim. On success `*out_json` is owned by the caller and must be released
 * with free(). Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_ALLOC on
 * allocation failure, WF_OK on success. */
wf_status wf_actor_build_preferences(const wf_actor_preferences *prefs,
                                     char **out_json);

/* ------------------------------------------------------------------ */
/* Declaration (app.bsky.actor.declaration record shape)              */
/* ------------------------------------------------------------------ */

/* The authoritative app.bsky.actor.declaration lexicon is NOT present in this
 * tree, so this struct mirrors the documented record shape: an actorType enum
 * string (e.g. "app.bsky.actor.declaration#appUser"), a `since` datetime, and a
 * `password` string. Extra fields remain in owned `extra`. */
typedef struct wf_actor_declaration {
    char *actor_type;
    char *since;
    char *password;
    cJSON *extra;          /* owned detached subtree of unknown fields; NULL absent */
} wf_actor_declaration;

/* Parse an app.bsky.actor.declaration record JSON body. Same ownership/error
 * rules as wf_actor_parse_preferences. */
wf_status wf_actor_parse_declaration(const char *json, size_t json_len,
                                     wf_actor_declaration *out);

/* Free the owned contents of `out` (also safe on a reset/zeroed struct). */
void wf_actor_declaration_free(wf_actor_declaration *out);

/* Build a declaration JSON object string from `decl`. On success `*out_json` is
 * owned by the caller and must be released with free(). Returns WF_ERR_INVALID_ARG
 * on NULL inputs, WF_ERR_ALLOC on allocation failure, WF_OK on success. */
wf_status wf_actor_build_declaration(const wf_actor_declaration *decl,
                                     char **out_json);

/* ------------------------------------------------------------------ */
/* Agent convenience wrappers                                          */
/* ------------------------------------------------------------------ */

/* app.bsky.actor.getPreferences — fetch and parse the full preferences union.
 * NOTE: distinct from the existing wf_agent_get_preferences_typed in agent.h
 * (which returns the generated lex output); this is the richer typed variant. */
wf_status wf_agent_get_actor_prefs_typed(wf_agent *agent,
                                         wf_actor_preferences *out);

/* app.bsky.actor.putPreferences — serialize `prefs` and PUT them. */
wf_status wf_agent_put_actor_prefs_typed(wf_agent *agent,
                                         const wf_actor_preferences *prefs);

/* app.bsky.actor.getSuggestions — actor profile views (+ cursor). Reuses
 * wf_agent_actor_list from actor_typed.h. */
wf_status wf_agent_get_suggestions_typed(wf_agent *agent, int limit,
                                         const char *cursor,
                                         wf_agent_actor_list *out);

/* app.bsky.actor.declareActor — STUB: no generated lex wrapper exists in this
 * tree (declaration.json is absent from the local lexicons). */
wf_status wf_agent_declare_actor_typed(wf_agent *agent,
                                       const wf_actor_declaration *decl);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_ACTOR_PREFS_TYPED_H */
