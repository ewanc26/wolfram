/*
 * actor_prefs_typed.c — owned typed parsers + agent wrappers for the
 * app.bsky.actor PREFERENCES union, getSuggestions, and the declaration record
 * shape. See include/wolfram/actor_prefs_typed.h for the public API, the
 * authoritative wire format, and ownership rules.
 *
 * Mirrors labeler_typed.c / actor_typed.c: static strdup/set_string/reset
 * helpers, owned strings, detached `extra` cJSON subtrees, and full cleanup on
 * the first error. The agent wrappers call the generated lex wrappers directly
 * (or wf_agent_put_preferences, which itself drives the generated wrapper)
 * after syncing auth via wf_agent_sync_auth.
 */

#include "wolfram/actor_prefs_typed.h"

#include "agent/_internal.h"
#include "wolfram/atproto_lex.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_actor_prefs_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

static wf_status wf_actor_prefs_set_string(char **dst, const char *src) {
    char *copy = wf_actor_prefs_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

/* Parse a JSON string array into an owned char-pointer array + count. */
static wf_status wf_actor_prefs_parse_string_array(cJSON *arr, char ***out_items,
                                                   size_t *out_count) {
    if (!cJSON_IsArray(arr)) {
        return WF_ERR_PARSE;
    }
    size_t n = (size_t)cJSON_GetArraySize(arr);
    if (n == 0) {
        *out_items = NULL;
        *out_count = 0;
        return WF_OK;
    }
    char **items = (char **)calloc(n, sizeof(char *));
    if (!items) {
        return WF_ERR_ALLOC;
    }
    wf_status status = WF_OK;
    for (size_t i = 0; i < n; ++i) {
        cJSON *it = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsString(it) || !it->valuestring) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_actor_prefs_set_string(&items[i], it->valuestring);
    }
    if (status == WF_OK) {
        *out_items = items;
        *out_count = n;
    } else {
        for (size_t i = 0; i < n; ++i) {
            free(items[i]);
        }
        free(items);
        *out_items = NULL;
        *out_count = 0;
    }
    return status;
}

static void wf_actor_prefs_free_string_array(char **items, size_t count) {
    if (!items) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        free(items[i]);
    }
    free(items);
}

/* ------------------------------------------------------------------ */
/* reset/free for each sub-struct                                     */
/* ------------------------------------------------------------------ */

static void wf_actor_pref_content_label_reset(wf_actor_pref_content_label *p) {
    if (!p) {
        return;
    }
    free(p->labeler_did);
    free(p->label);
    free(p->visibility);
    memset(p, 0, sizeof(*p));
}

static void wf_actor_pref_saved_feed_reset(wf_actor_pref_saved_feed *f) {
    if (!f) {
        return;
    }
    free(f->id);
    free(f->type);
    free(f->value);
    memset(f, 0, sizeof(*f));
}

static void wf_actor_pref_saved_feeds_reset(wf_actor_pref_saved_feeds *p) {
    if (!p) {
        return;
    }
    wf_actor_prefs_free_string_array(p->pinned, p->pinned_count);
    wf_actor_prefs_free_string_array(p->saved, p->saved_count);
    memset(p, 0, sizeof(*p));
}

static void wf_actor_pref_saved_feeds_v2_reset(wf_actor_pref_saved_feeds_v2 *p) {
    if (!p) {
        return;
    }
    for (size_t i = 0; i < p->item_count; ++i) {
        wf_actor_pref_saved_feed_reset(&p->items[i]);
    }
    free(p->items);
    memset(p, 0, sizeof(*p));
}

static void wf_actor_pref_personal_details_reset(wf_actor_pref_personal_details *p) {
    if (!p) {
        return;
    }
    free(p->birth_date);
    memset(p, 0, sizeof(*p));
}

static void wf_actor_pref_feed_view_reset(wf_actor_pref_feed_view *p) {
    if (!p) {
        return;
    }
    free(p->feed);
    memset(p, 0, sizeof(*p));
}

static void wf_actor_pref_thread_view_reset(wf_actor_pref_thread_view *p) {
    if (!p) {
        return;
    }
    free(p->sort);
    memset(p, 0, sizeof(*p));
}

static void wf_actor_pref_interests_reset(wf_actor_pref_interests *p) {
    if (!p) {
        return;
    }
    wf_actor_prefs_free_string_array(p->tags, p->tag_count);
    memset(p, 0, sizeof(*p));
}

static void wf_actor_pref_muted_word_reset(wf_actor_pref_muted_word *p) {
    if (!p) {
        return;
    }
    free(p->id);
    free(p->value);
    wf_actor_prefs_free_string_array(p->targets, p->target_count);
    free(p->actor_target);
    free(p->expires_at);
    memset(p, 0, sizeof(*p));
}

static void wf_actor_pref_hidden_posts_reset(wf_actor_pref_hidden_posts *p) {
    if (!p) {
        return;
    }
    wf_actor_prefs_free_string_array(p->uris, p->uri_count);
    memset(p, 0, sizeof(*p));
}

static void wf_actor_pref_bsky_app_state_reset(wf_actor_pref_bsky_app_state *p) {
    if (!p) {
        return;
    }
    free(p->active_progress_guide);
    wf_actor_prefs_free_string_array(p->queued_nudges, p->queued_nudge_count);
    if (p->nuxs) {
        cJSON_Delete(p->nuxs);
    }
    memset(p, 0, sizeof(*p));
}

static void wf_actor_pref_labelers_reset(wf_actor_pref_labelers *p) {
    if (!p) {
        return;
    }
    wf_actor_prefs_free_string_array(p->labelers, p->labeler_count);
    memset(p, 0, sizeof(*p));
}

static void wf_actor_pref_post_interaction_settings_reset(
    wf_actor_pref_post_interaction_settings *p) {
    if (!p) {
        return;
    }
    if (p->threadgate_allow_rules) {
        cJSON_Delete(p->threadgate_allow_rules);
    }
    if (p->postgate_embedding_rules) {
        cJSON_Delete(p->postgate_embedding_rules);
    }
    memset(p, 0, sizeof(*p));
}

static void wf_actor_pref_live_events_reset(wf_actor_pref_live_events *p) {
    if (!p) {
        return;
    }
    wf_actor_prefs_free_string_array(p->hidden_feed_ids,
                                     p->hidden_feed_id_count);
    memset(p, 0, sizeof(*p));
}

/* ------------------------------------------------------------------ */
/* parse for each sub-struct                                          */
/* ------------------------------------------------------------------ */

static wf_status wf_actor_pref_parse_content_label(cJSON *obj,
                                                   wf_actor_pref_content_label *p) {
    cJSON *ld = cJSON_GetObjectItemCaseSensitive(obj, "labelerDid");
    cJSON *label = cJSON_GetObjectItemCaseSensitive(obj, "label");
    cJSON *vis = cJSON_GetObjectItemCaseSensitive(obj, "visibility");
    wf_status s = WF_OK;
    if (cJSON_IsString(ld) && ld->valuestring) {
        s = wf_actor_prefs_set_string(&p->labeler_did, ld->valuestring);
    }
    if (s == WF_OK && cJSON_IsString(label) && label->valuestring) {
        s = wf_actor_prefs_set_string(&p->label, label->valuestring);
    }
    if (s == WF_OK && cJSON_IsString(vis) && vis->valuestring) {
        s = wf_actor_prefs_set_string(&p->visibility, vis->valuestring);
    }
    return s;
}

static wf_status wf_actor_pref_parse_saved_feed(cJSON *obj,
                                                wf_actor_pref_saved_feed *f) {
    cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "id");
    cJSON *type = cJSON_GetObjectItemCaseSensitive(obj, "type");
    cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, "value");
    cJSON *pinned = cJSON_GetObjectItemCaseSensitive(obj, "pinned");
    wf_status s = WF_OK;
    if (cJSON_IsString(id) && id->valuestring) {
        s = wf_actor_prefs_set_string(&f->id, id->valuestring);
    }
    if (s == WF_OK && cJSON_IsString(type) && type->valuestring) {
        s = wf_actor_prefs_set_string(&f->type, type->valuestring);
    }
    if (s == WF_OK && cJSON_IsString(value) && value->valuestring) {
        s = wf_actor_prefs_set_string(&f->value, value->valuestring);
    }
    if (s == WF_OK && cJSON_IsBool(pinned)) {
        f->has_pinned = true;
        f->pinned = cJSON_IsTrue(pinned);
    }
    return s;
}

static wf_status wf_actor_pref_parse_saved_feeds(cJSON *obj,
                                                 wf_actor_pref_saved_feeds *p) {
    cJSON *pinned = cJSON_GetObjectItemCaseSensitive(obj, "pinned");
    cJSON *saved = cJSON_GetObjectItemCaseSensitive(obj, "saved");
    cJSON *ti = cJSON_GetObjectItemCaseSensitive(obj, "timelineIndex");
    wf_status s = WF_OK;
    if (pinned != NULL) {
        s = wf_actor_prefs_parse_string_array(pinned, &p->pinned,
                                              &p->pinned_count);
    }
    if (s == WF_OK && saved != NULL) {
        s = wf_actor_prefs_parse_string_array(saved, &p->saved,
                                              &p->saved_count);
    }
    if (s == WF_OK && cJSON_IsNumber(ti)) {
        p->has_timeline_index = true;
        p->timeline_index = (int64_t)ti->valuedouble;
    } else if (ti != NULL && !cJSON_IsNumber(ti)) {
        s = WF_ERR_PARSE;
    }
    return s;
}

static wf_status wf_actor_pref_parse_saved_feeds_v2(
    cJSON *obj, wf_actor_pref_saved_feeds_v2 *p) {
    cJSON *items = cJSON_GetObjectItemCaseSensitive(obj, "items");
    if (!cJSON_IsArray(items)) {
        return WF_ERR_PARSE;
    }
    size_t n = (size_t)cJSON_GetArraySize(items);
    if (n == 0) {
        p->items = NULL;
        p->item_count = 0;
        return WF_OK;
    }
    wf_actor_pref_saved_feed *arr =
        (wf_actor_pref_saved_feed *)calloc(n, sizeof(*arr));
    if (!arr) {
        return WF_ERR_ALLOC;
    }
    wf_status s = WF_OK;
    for (size_t i = 0; i < n && s == WF_OK; ++i) {
        cJSON *it = cJSON_GetArrayItem(items, (int)i);
        if (!cJSON_IsObject(it)) {
            s = WF_ERR_PARSE;
            break;
        }
        s = wf_actor_pref_parse_saved_feed(it, &arr[i]);
    }
    if (s == WF_OK) {
        p->items = arr;
        p->item_count = n;
    } else {
        for (size_t i = 0; i < n; ++i) {
            wf_actor_pref_saved_feed_reset(&arr[i]);
        }
        free(arr);
    }
    return s;
}

static wf_status wf_actor_pref_parse_personal_details(
    cJSON *obj, wf_actor_pref_personal_details *p) {
    cJSON *bd = cJSON_GetObjectItemCaseSensitive(obj, "birthDate");
    if (cJSON_IsString(bd) && bd->valuestring) {
        return wf_actor_prefs_set_string(&p->birth_date, bd->valuestring);
    }
    return WF_OK;
}

static wf_status wf_actor_pref_parse_declared_age(cJSON *obj,
                                                  wf_actor_pref_declared_age *p) {
    cJSON *o13 = cJSON_GetObjectItemCaseSensitive(obj, "isOverAge13");
    cJSON *o16 = cJSON_GetObjectItemCaseSensitive(obj, "isOverAge16");
    cJSON *o18 = cJSON_GetObjectItemCaseSensitive(obj, "isOverAge18");
    if (o13 != NULL) {
        if (!cJSON_IsBool(o13)) {
            return WF_ERR_PARSE;
        }
        p->has_over_13 = true;
        p->over_13 = cJSON_IsTrue(o13);
    }
    if (o16 != NULL) {
        if (!cJSON_IsBool(o16)) {
            return WF_ERR_PARSE;
        }
        p->has_over_16 = true;
        p->over_16 = cJSON_IsTrue(o16);
    }
    if (o18 != NULL) {
        if (!cJSON_IsBool(o18)) {
            return WF_ERR_PARSE;
        }
        p->has_over_18 = true;
        p->over_18 = cJSON_IsTrue(o18);
    }
    return WF_OK;
}

static wf_status wf_actor_pref_parse_feed_view(cJSON *obj,
                                               wf_actor_pref_feed_view *p) {
    cJSON *feed = cJSON_GetObjectItemCaseSensitive(obj, "feed");
    cJSON *hr = cJSON_GetObjectItemCaseSensitive(obj, "hideReplies");
    cJSON *hrbu = cJSON_GetObjectItemCaseSensitive(obj, "hideRepliesByUnfollowed");
    cJSON *hrbl = cJSON_GetObjectItemCaseSensitive(obj, "hideRepliesByLikeCount");
    cJSON *hrep = cJSON_GetObjectItemCaseSensitive(obj, "hideReposts");
    cJSON *hq = cJSON_GetObjectItemCaseSensitive(obj, "hideQuotePosts");
    wf_status s = WF_OK;
    if (cJSON_IsString(feed) && feed->valuestring) {
        s = wf_actor_prefs_set_string(&p->feed, feed->valuestring);
    }
    if (s == WF_OK && hr != NULL) {
        if (!cJSON_IsBool(hr)) {
            return WF_ERR_PARSE;
        }
        p->has_hide_replies = true;
        p->hide_replies = cJSON_IsTrue(hr);
    }
    if (s == WF_OK && hrbu != NULL) {
        if (!cJSON_IsBool(hrbu)) {
            return WF_ERR_PARSE;
        }
        p->has_hide_replies_by_unfollowed = true;
        p->hide_replies_by_unfollowed = cJSON_IsTrue(hrbu);
    }
    if (s == WF_OK && hrbl != NULL) {
        if (!cJSON_IsNumber(hrbl)) {
            return WF_ERR_PARSE;
        }
        p->has_hide_replies_by_like_count = true;
        p->hide_replies_by_like_count = (int64_t)hrbl->valuedouble;
    }
    if (s == WF_OK && hrep != NULL) {
        if (!cJSON_IsBool(hrep)) {
            return WF_ERR_PARSE;
        }
        p->has_hide_reposts = true;
        p->hide_reposts = cJSON_IsTrue(hrep);
    }
    if (s == WF_OK && hq != NULL) {
        if (!cJSON_IsBool(hq)) {
            return WF_ERR_PARSE;
        }
        p->has_hide_quote_posts = true;
        p->hide_quote_posts = cJSON_IsTrue(hq);
    }
    return s;
}

static wf_status wf_actor_pref_parse_thread_view(cJSON *obj,
                                                 wf_actor_pref_thread_view *p) {
    cJSON *sort = cJSON_GetObjectItemCaseSensitive(obj, "sort");
    if (cJSON_IsString(sort) && sort->valuestring) {
        return wf_actor_prefs_set_string(&p->sort, sort->valuestring);
    }
    return WF_OK;
}

static wf_status wf_actor_pref_parse_interests(cJSON *obj,
                                               wf_actor_pref_interests *p) {
    cJSON *tags = cJSON_GetObjectItemCaseSensitive(obj, "tags");
    if (!cJSON_IsArray(tags)) {
        return WF_ERR_PARSE;
    }
    return wf_actor_prefs_parse_string_array(tags, &p->tags, &p->tag_count);
}

static wf_status wf_actor_pref_parse_muted_word(cJSON *obj,
                                                wf_actor_pref_muted_word *p) {
    cJSON *id = cJSON_GetObjectItemCaseSensitive(obj, "id");
    cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, "value");
    cJSON *targets = cJSON_GetObjectItemCaseSensitive(obj, "targets");
    cJSON *at = cJSON_GetObjectItemCaseSensitive(obj, "actorTarget");
    cJSON *exp = cJSON_GetObjectItemCaseSensitive(obj, "expiresAt");
    wf_status s = WF_OK;
    if (cJSON_IsString(id) && id->valuestring) {
        s = wf_actor_prefs_set_string(&p->id, id->valuestring);
    }
    if (s == WF_OK && cJSON_IsString(value) && value->valuestring) {
        s = wf_actor_prefs_set_string(&p->value, value->valuestring);
    }
    if (s == WF_OK && targets != NULL) {
        s = wf_actor_prefs_parse_string_array(targets, &p->targets,
                                              &p->target_count);
    }
    if (s == WF_OK && cJSON_IsString(at) && at->valuestring) {
        s = wf_actor_prefs_set_string(&p->actor_target, at->valuestring);
    }
    if (s == WF_OK && cJSON_IsString(exp) && exp->valuestring) {
        s = wf_actor_prefs_set_string(&p->expires_at, exp->valuestring);
    }
    return s;
}

static wf_status wf_actor_pref_parse_hidden_posts(cJSON *obj,
                                                  wf_actor_pref_hidden_posts *p) {
    cJSON *items = cJSON_GetObjectItemCaseSensitive(obj, "items");
    if (!cJSON_IsArray(items)) {
        return WF_ERR_PARSE;
    }
    return wf_actor_prefs_parse_string_array(items, &p->uris, &p->uri_count);
}

static wf_status wf_actor_pref_parse_bsky_app_state(
    cJSON *obj, wf_actor_pref_bsky_app_state *p) {
    cJSON *apg = cJSON_GetObjectItemCaseSensitive(obj, "activeProgressGuide");
    cJSON *qn = cJSON_GetObjectItemCaseSensitive(obj, "queuedNudges");
    cJSON *nuxs = cJSON_GetObjectItemCaseSensitive(obj, "nuxs");
    wf_status s = WF_OK;
    if (cJSON_IsObject(apg)) {
        cJSON *guide = cJSON_GetObjectItemCaseSensitive(apg, "guide");
        if (cJSON_IsString(guide) && guide->valuestring) {
            s = wf_actor_prefs_set_string(&p->active_progress_guide,
                                          guide->valuestring);
        }
    } else if (apg != NULL) {
        return WF_ERR_PARSE;
    }
    if (s == WF_OK && qn != NULL) {
        s = wf_actor_prefs_parse_string_array(qn, &p->queued_nudges,
                                              &p->queued_nudge_count);
    }
    if (s == WF_OK && nuxs != NULL) {
        if (!cJSON_IsArray(nuxs)) {
            return WF_ERR_PARSE;
        }
        p->nuxs = cJSON_DetachItemFromObject(obj, "nuxs");
    }
    return s;
}

static wf_status wf_actor_pref_parse_labelers(cJSON *obj,
                                              wf_actor_pref_labelers *p) {
    cJSON *labelers = cJSON_GetObjectItemCaseSensitive(obj, "labelers");
    if (!cJSON_IsArray(labelers)) {
        return WF_ERR_PARSE;
    }
    return wf_actor_prefs_parse_string_array(labelers, &p->labelers,
                                             &p->labeler_count);
}

static wf_status wf_actor_pref_parse_post_interaction_settings(
    cJSON *obj, wf_actor_pref_post_interaction_settings *p) {
    cJSON *tg = cJSON_GetObjectItemCaseSensitive(obj, "threadgateAllowRules");
    cJSON *pg = cJSON_GetObjectItemCaseSensitive(obj, "postgateEmbeddingRules");
    if (tg != NULL) {
        if (!cJSON_IsArray(tg)) {
            return WF_ERR_PARSE;
        }
        p->threadgate_allow_rules = cJSON_DetachItemFromObject(
            obj, "threadgateAllowRules");
    }
    if (pg != NULL) {
        if (!cJSON_IsArray(pg)) {
            return WF_ERR_PARSE;
        }
        p->postgate_embedding_rules = cJSON_DetachItemFromObject(
            obj, "postgateEmbeddingRules");
    }
    return WF_OK;
}

static wf_status wf_actor_pref_parse_verification(cJSON *obj,
                                                  wf_actor_pref_verification *p) {
    cJSON *hb = cJSON_GetObjectItemCaseSensitive(obj, "hideBadges");
    if (hb != NULL) {
        if (!cJSON_IsBool(hb)) {
            return WF_ERR_PARSE;
        }
        p->has_hide_badges = true;
        p->hide_badges = cJSON_IsTrue(hb);
    }
    return WF_OK;
}

static wf_status wf_actor_pref_parse_live_events(cJSON *obj,
                                                 wf_actor_pref_live_events *p) {
    cJSON *hf = cJSON_GetObjectItemCaseSensitive(obj, "hiddenFeedIds");
    cJSON *ha = cJSON_GetObjectItemCaseSensitive(obj, "hideAllFeeds");
    wf_status s = WF_OK;
    if (hf != NULL) {
        s = wf_actor_prefs_parse_string_array(hf, &p->hidden_feed_ids,
                                              &p->hidden_feed_id_count);
    }
    if (s == WF_OK && ha != NULL) {
        if (!cJSON_IsBool(ha)) {
            return WF_ERR_PARSE;
        }
        p->has_hide_all_feeds = true;
        p->hide_all_feeds = cJSON_IsTrue(ha);
    }
    return s;
}

/* Return the short type name (the part after '#', or the whole string if there
 * is no '#') for a `$type` value. */
static const char *wf_actor_prefs_short_type(const char *type) {
    if (!type) {
        return NULL;
    }
    const char *h = strrchr(type, '#');
    return h ? h + 1 : type;
}

/* Dispatch one known preference object into the aggregator. Returns WF_OK on
 * success, an error code on failure (after which `out` must be reset by the
 * caller). Returns 1 (true) via `*consumed` when the type was recognized. */
static wf_status wf_actor_prefs_classify(cJSON *obj, wf_actor_preferences *out,
                                         bool *consumed) {
    cJSON *type = cJSON_GetObjectItemCaseSensitive(obj, "$type");
    const char *ts = (cJSON_IsString(type) && type->valuestring)
                         ? type->valuestring
                         : NULL;
    const char *short_type = wf_actor_prefs_short_type(ts);

    *consumed = true;
    if (short_type == NULL) {
        return WF_ERR_PARSE;
    } else if (strcmp(short_type, "adultContentPref") == 0) {
        cJSON *e = cJSON_GetObjectItemCaseSensitive(obj, "enabled");
        if (!cJSON_IsBool(e)) {
            return WF_ERR_PARSE;
        }
        out->adult_content.has_enabled = true;
        out->adult_content.enabled = cJSON_IsTrue(e);
        return WF_OK;
    } else if (strcmp(short_type, "contentLabelPref") == 0) {
        wf_actor_pref_content_label *nl =
            (wf_actor_pref_content_label *)realloc(
                out->content_labels,
                (out->content_label_count + 1) * sizeof(*nl));
        if (!nl) {
            return WF_ERR_ALLOC;
        }
        out->content_labels = nl;
        wf_actor_pref_content_label *p =
            &out->content_labels[out->content_label_count];
        memset(p, 0, sizeof(*p));
        wf_status s = wf_actor_pref_parse_content_label(obj, p);
        if (s != WF_OK) {
            wf_actor_pref_content_label_reset(p);
            return s;
        }
        out->content_label_count++;
        return WF_OK;
    } else if (strcmp(short_type, "savedFeedsPref") == 0) {
        return wf_actor_pref_parse_saved_feeds(obj, &out->saved_feeds);
    } else if (strcmp(short_type, "savedFeedsPrefV2") == 0) {
        return wf_actor_pref_parse_saved_feeds_v2(obj, &out->saved_feeds_v2);
    } else if (strcmp(short_type, "personalDetailsPref") == 0) {
        return wf_actor_pref_parse_personal_details(obj, &out->personal_details);
    } else if (strcmp(short_type, "declaredAgePref") == 0) {
        return wf_actor_pref_parse_declared_age(obj, &out->declared_age);
    } else if (strcmp(short_type, "feedViewPref") == 0) {
        wf_actor_pref_feed_view *nl =
            (wf_actor_pref_feed_view *)realloc(
                out->feed_views,
                (out->feed_view_count + 1) * sizeof(*nl));
        if (!nl) {
            return WF_ERR_ALLOC;
        }
        out->feed_views = nl;
        wf_actor_pref_feed_view *p = &out->feed_views[out->feed_view_count];
        memset(p, 0, sizeof(*p));
        wf_status s = wf_actor_pref_parse_feed_view(obj, p);
        if (s != WF_OK) {
            wf_actor_pref_feed_view_reset(p);
            return s;
        }
        out->feed_view_count++;
        return WF_OK;
    } else if (strcmp(short_type, "threadViewPref") == 0) {
        return wf_actor_pref_parse_thread_view(obj, &out->thread_view);
    } else if (strcmp(short_type, "interestsPref") == 0) {
        return wf_actor_pref_parse_interests(obj, &out->interests);
    } else if (strcmp(short_type, "mutedWordsPref") == 0) {
        cJSON *items = cJSON_GetObjectItemCaseSensitive(obj, "items");
        if (!cJSON_IsArray(items)) {
            return WF_ERR_PARSE;
        }
        size_t n = (size_t)cJSON_GetArraySize(items);
        wf_actor_pref_muted_word *nl =
            (wf_actor_pref_muted_word *)realloc(
                out->muting_keywords,
                (out->muting_keyword_count + n) * sizeof(*nl));
        if (!nl) {
            return WF_ERR_ALLOC;
        }
        out->muting_keywords = nl;
        wf_status s = WF_OK;
        for (size_t i = 0; i < n && s == WF_OK; ++i) {
            cJSON *it = cJSON_GetArrayItem(items, (int)i);
            if (!cJSON_IsObject(it)) {
                s = WF_ERR_PARSE;
                break;
            }
            wf_actor_pref_muted_word *p =
                &out->muting_keywords[out->muting_keyword_count + i];
            memset(p, 0, sizeof(*p));
            s = wf_actor_pref_parse_muted_word(it, p);
        }
        if (s == WF_OK) {
            out->muting_keyword_count += n;
        } else {
            for (size_t i = 0; i < n; ++i) {
                wf_actor_pref_muted_word_reset(
                    &out->muting_keywords[out->muting_keyword_count + i]);
            }
        }
        return s;
    } else if (strcmp(short_type, "hiddenPostsPref") == 0) {
        return wf_actor_pref_parse_hidden_posts(obj, &out->hidden_posts);
    } else if (strcmp(short_type, "bskyAppStatePref") == 0) {
        return wf_actor_pref_parse_bsky_app_state(obj, &out->bsky_app_state);
    } else if (strcmp(short_type, "labelersPref") == 0) {
        return wf_actor_pref_parse_labelers(obj, &out->labelers);
    } else if (strcmp(short_type, "postInteractionSettingsPref") == 0) {
        return wf_actor_pref_parse_post_interaction_settings(
            obj, &out->post_interaction_settings);
    } else if (strcmp(short_type, "verificationPrefs") == 0) {
        return wf_actor_pref_parse_verification(obj, &out->verification);
    } else if (strcmp(short_type, "liveEventPreferences") == 0) {
        return wf_actor_pref_parse_live_events(obj, &out->live_events);
    }

    *consumed = false;
    return WF_OK;
}

/* ------------------------------------------------------------------ */
/* top-level preferences parse + free                                 */
/* ------------------------------------------------------------------ */

wf_status wf_actor_parse_preferences(const char *json, size_t json_len,
                                     wf_actor_preferences *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    /* Accept either {"preferences":[...]} or a bare array. */
    cJSON *arr = cJSON_IsArray(root)
                     ? root
                     : cJSON_GetObjectItemCaseSensitive(root, "preferences");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *extra = cJSON_CreateArray();
    if (!extra) {
        cJSON_Delete(root);
        return WF_ERR_ALLOC;
    }

    int i = 0;
    while (i < cJSON_GetArraySize(arr)) {
        cJSON *obj = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        bool consumed = false;
        wf_status s = wf_actor_prefs_classify(obj, out, &consumed);
        if (s != WF_OK) {
            status = s;
            break;
        }
        if (consumed) {
            cJSON_Delete(cJSON_DetachItemFromArray(arr, i));
        } else {
            /* Unknown $type: keep the raw object in `extra`. */
            cJSON *raw = cJSON_DetachItemFromArray(arr, i);
            cJSON_AddItemToArray(extra, raw);
        }
        /* No index increment: DetachItemFromArray shifted the tail in. */
    }

    if (status == WF_OK) {
        out->extra = (cJSON_GetArraySize(extra) > 0) ? extra : NULL;
        if (out->extra == NULL) {
            cJSON_Delete(extra);
        }
    } else {
        wf_actor_preferences_free(out);
        cJSON_Delete(extra);
    }
    cJSON_Delete(root);
    return status;
}

void wf_actor_preferences_free(wf_actor_preferences *out) {
    if (!out) {
        return;
    }
    for (size_t i = 0; i < out->content_label_count; ++i) {
        wf_actor_pref_content_label_reset(&out->content_labels[i]);
    }
    free(out->content_labels);
    wf_actor_pref_saved_feeds_reset(&out->saved_feeds);
    wf_actor_pref_saved_feeds_v2_reset(&out->saved_feeds_v2);
    wf_actor_pref_personal_details_reset(&out->personal_details);
    for (size_t i = 0; i < out->feed_view_count; ++i) {
        wf_actor_pref_feed_view_reset(&out->feed_views[i]);
    }
    free(out->feed_views);
    wf_actor_pref_thread_view_reset(&out->thread_view);
    wf_actor_pref_interests_reset(&out->interests);
    for (size_t i = 0; i < out->muting_keyword_count; ++i) {
        wf_actor_pref_muted_word_reset(&out->muting_keywords[i]);
    }
    free(out->muting_keywords);
    wf_actor_pref_hidden_posts_reset(&out->hidden_posts);
    wf_actor_pref_bsky_app_state_reset(&out->bsky_app_state);
    wf_actor_pref_labelers_reset(&out->labelers);
    wf_actor_pref_post_interaction_settings_reset(&out->post_interaction_settings);
    wf_actor_pref_live_events_reset(&out->live_events);
    if (out->extra) {
        cJSON_Delete(out->extra);
    }
    memset(out, 0, sizeof(*out));
}

/* ------------------------------------------------------------------ */
/* preferences builder                                                */
/* ------------------------------------------------------------------ */

static void wf_actor_prefs_add_string_array(cJSON *arr, char **items,
                                            size_t count) {
    for (size_t i = 0; i < count; ++i) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(items[i]));
    }
}

static void wf_actor_prefs_build_saved_feed(cJSON *obj,
                                            const wf_actor_pref_saved_feed *f) {
    if (f->id) {
        cJSON_AddStringToObject(obj, "id", f->id);
    }
    if (f->type) {
        cJSON_AddStringToObject(obj, "type", f->type);
    }
    if (f->value) {
        cJSON_AddStringToObject(obj, "value", f->value);
    }
    if (f->has_pinned) {
        cJSON_AddBoolToObject(obj, "pinned", f->pinned);
    }
}

wf_status wf_actor_build_preferences(const wf_actor_preferences *prefs,
                                     char **out_json) {
    if (!prefs || !out_json) {
        return WF_ERR_INVALID_ARG;
    }
    *out_json = NULL;

    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        return WF_ERR_ALLOC;
    }

    /* adultContentPref */
    if (prefs->adult_content.has_enabled) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "$type",
                                "app.bsky.actor.defs#adultContentPref");
        cJSON_AddBoolToObject(o, "enabled", prefs->adult_content.enabled);
        cJSON_AddItemToArray(arr, o);
    }
    /* contentLabelPref[] */
    for (size_t i = 0; i < prefs->content_label_count; ++i) {
        const wf_actor_pref_content_label *p = &prefs->content_labels[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "$type",
                                "app.bsky.actor.defs#contentLabelPref");
        if (p->labeler_did) {
            cJSON_AddStringToObject(o, "labelerDid", p->labeler_did);
        }
        if (p->label) {
            cJSON_AddStringToObject(o, "label", p->label);
        }
        if (p->visibility) {
            cJSON_AddStringToObject(o, "visibility", p->visibility);
        }
        cJSON_AddItemToArray(arr, o);
    }
    /* savedFeedsPref */
    if (prefs->saved_feeds.pinned_count > 0 ||
        prefs->saved_feeds.saved_count > 0 || prefs->saved_feeds.has_timeline_index) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "$type",
                                "app.bsky.actor.defs#savedFeedsPref");
        cJSON *pinned = cJSON_CreateArray();
        wf_actor_prefs_add_string_array(pinned, prefs->saved_feeds.pinned,
                                        prefs->saved_feeds.pinned_count);
        cJSON_AddItemToObject(o, "pinned", pinned);
        cJSON *saved = cJSON_CreateArray();
        wf_actor_prefs_add_string_array(saved, prefs->saved_feeds.saved,
                                        prefs->saved_feeds.saved_count);
        cJSON_AddItemToObject(o, "saved", saved);
        if (prefs->saved_feeds.has_timeline_index) {
            cJSON_AddNumberToObject(o, "timelineIndex",
                                    (double)prefs->saved_feeds.timeline_index);
        }
        cJSON_AddItemToArray(arr, o);
    }
    /* savedFeedsPrefV2 */
    if (prefs->saved_feeds_v2.item_count > 0) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "$type",
                                "app.bsky.actor.defs#savedFeedsPrefV2");
        cJSON *items = cJSON_CreateArray();
        for (size_t i = 0; i < prefs->saved_feeds_v2.item_count; ++i) {
            cJSON *it = cJSON_CreateObject();
            wf_actor_prefs_build_saved_feed(it, &prefs->saved_feeds_v2.items[i]);
            cJSON_AddItemToArray(items, it);
        }
        cJSON_AddItemToObject(o, "items", items);
        cJSON_AddItemToArray(arr, o);
    }
    /* personalDetailsPref */
    if (prefs->personal_details.birth_date) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "$type",
                                "app.bsky.actor.defs#personalDetailsPref");
        cJSON_AddStringToObject(o, "birthDate",
                                prefs->personal_details.birth_date);
        cJSON_AddItemToArray(arr, o);
    }
    /* declaredAgePref */
    if (prefs->declared_age.has_over_13 || prefs->declared_age.has_over_16 ||
        prefs->declared_age.has_over_18) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "$type",
                                "app.bsky.actor.defs#declaredAgePref");
        if (prefs->declared_age.has_over_13) {
            cJSON_AddBoolToObject(o, "isOverAge13", prefs->declared_age.over_13);
        }
        if (prefs->declared_age.has_over_16) {
            cJSON_AddBoolToObject(o, "isOverAge16", prefs->declared_age.over_16);
        }
        if (prefs->declared_age.has_over_18) {
            cJSON_AddBoolToObject(o, "isOverAge18", prefs->declared_age.over_18);
        }
        cJSON_AddItemToArray(arr, o);
    }
    /* feedViewPref[] */
    for (size_t i = 0; i < prefs->feed_view_count; ++i) {
        const wf_actor_pref_feed_view *p = &prefs->feed_views[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "$type",
                                "app.bsky.actor.defs#feedViewPref");
        if (p->feed) {
            cJSON_AddStringToObject(o, "feed", p->feed);
        }
        if (p->has_hide_replies) {
            cJSON_AddBoolToObject(o, "hideReplies", p->hide_replies);
        }
        if (p->has_hide_replies_by_unfollowed) {
            cJSON_AddBoolToObject(o, "hideRepliesByUnfollowed",
                                  p->hide_replies_by_unfollowed);
        }
        if (p->has_hide_replies_by_like_count) {
            cJSON_AddNumberToObject(o, "hideRepliesByLikeCount",
                                    (double)p->hide_replies_by_like_count);
        }
        if (p->has_hide_reposts) {
            cJSON_AddBoolToObject(o, "hideReposts", p->hide_reposts);
        }
        if (p->has_hide_quote_posts) {
            cJSON_AddBoolToObject(o, "hideQuotePosts", p->hide_quote_posts);
        }
        cJSON_AddItemToArray(arr, o);
    }
    /* threadViewPref */
    if (prefs->thread_view.sort) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "$type",
                                "app.bsky.actor.defs#threadViewPref");
        cJSON_AddStringToObject(o, "sort", prefs->thread_view.sort);
        cJSON_AddItemToArray(arr, o);
    }
    /* interestsPref */
    if (prefs->interests.tag_count > 0) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "$type",
                                "app.bsky.actor.defs#interestsPref");
        cJSON *tags = cJSON_CreateArray();
        wf_actor_prefs_add_string_array(tags, prefs->interests.tags,
                                        prefs->interests.tag_count);
        cJSON_AddItemToObject(o, "tags", tags);
        cJSON_AddItemToArray(arr, o);
    }
    /* mutedWordsPref */
    if (prefs->muting_keyword_count > 0) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "$type",
                                "app.bsky.actor.defs#mutedWordsPref");
        cJSON *items = cJSON_CreateArray();
        for (size_t i = 0; i < prefs->muting_keyword_count; ++i) {
            const wf_actor_pref_muted_word *p = &prefs->muting_keywords[i];
            cJSON *it = cJSON_CreateObject();
            if (p->id) {
                cJSON_AddStringToObject(it, "id", p->id);
            }
            if (p->value) {
                cJSON_AddStringToObject(it, "value", p->value);
            }
            cJSON *targets = cJSON_CreateArray();
            wf_actor_prefs_add_string_array(targets, p->targets,
                                            p->target_count);
            cJSON_AddItemToObject(it, "targets", targets);
            if (p->actor_target) {
                cJSON_AddStringToObject(it, "actorTarget", p->actor_target);
            }
            if (p->expires_at) {
                cJSON_AddStringToObject(it, "expiresAt", p->expires_at);
            }
            cJSON_AddItemToArray(items, it);
        }
        cJSON_AddItemToObject(o, "items", items);
        cJSON_AddItemToArray(arr, o);
    }
    /* hiddenPostsPref */
    if (prefs->hidden_posts.uri_count > 0) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "$type",
                                "app.bsky.actor.defs#hiddenPostsPref");
        cJSON *items = cJSON_CreateArray();
        wf_actor_prefs_add_string_array(items, prefs->hidden_posts.uris,
                                        prefs->hidden_posts.uri_count);
        cJSON_AddItemToObject(o, "items", items);
        cJSON_AddItemToArray(arr, o);
    }
    /* bskyAppStatePref */
    if (prefs->bsky_app_state.active_progress_guide ||
        prefs->bsky_app_state.queued_nudge_count > 0 ||
        prefs->bsky_app_state.nuxs) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "$type",
                                "app.bsky.actor.defs#bskyAppStatePref");
        if (prefs->bsky_app_state.active_progress_guide) {
            cJSON *apg = cJSON_CreateObject();
            cJSON_AddStringToObject(apg, "guide",
                                    prefs->bsky_app_state.active_progress_guide);
            cJSON_AddItemToObject(o, "activeProgressGuide", apg);
        }
        if (prefs->bsky_app_state.queued_nudge_count > 0) {
            cJSON *qn = cJSON_CreateArray();
            wf_actor_prefs_add_string_array(qn, prefs->bsky_app_state.queued_nudges,
                                            prefs->bsky_app_state.queued_nudge_count);
            cJSON_AddItemToObject(o, "queuedNudges", qn);
        }
        if (prefs->bsky_app_state.nuxs) {
            cJSON_AddItemToObject(o, "nuxs",
                                  cJSON_Duplicate(prefs->bsky_app_state.nuxs,
                                                 1));
        }
        cJSON_AddItemToArray(arr, o);
    }
    /* labelersPref */
    if (prefs->labelers.labeler_count > 0) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "$type",
                                "app.bsky.actor.defs#labelersPref");
        cJSON *labelers = cJSON_CreateArray();
        wf_actor_prefs_add_string_array(labelers, prefs->labelers.labelers,
                                        prefs->labelers.labeler_count);
        cJSON_AddItemToObject(o, "labelers", labelers);
        cJSON_AddItemToArray(arr, o);
    }
    /* postInteractionSettingsPref */
    if (prefs->post_interaction_settings.threadgate_allow_rules ||
        prefs->post_interaction_settings.postgate_embedding_rules) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "$type",
                                "app.bsky.actor.defs#postInteractionSettingsPref");
        if (prefs->post_interaction_settings.threadgate_allow_rules) {
            cJSON_AddItemToObject(
                o, "threadgateAllowRules",
                cJSON_Duplicate(prefs->post_interaction_settings.threadgate_allow_rules, 1));
        }
        if (prefs->post_interaction_settings.postgate_embedding_rules) {
            cJSON_AddItemToObject(
                o, "postgateEmbeddingRules",
                cJSON_Duplicate(prefs->post_interaction_settings.postgate_embedding_rules, 1));
        }
        cJSON_AddItemToArray(arr, o);
    }
    /* verificationPrefs */
    if (prefs->verification.has_hide_badges) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "$type",
                                "app.bsky.actor.defs#verificationPrefs");
        cJSON_AddBoolToObject(o, "hideBadges", prefs->verification.hide_badges);
        cJSON_AddItemToArray(arr, o);
    }
    /* liveEventPreferences */
    if (prefs->live_events.hidden_feed_id_count > 0 ||
        prefs->live_events.has_hide_all_feeds) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "$type",
                                "app.bsky.actor.defs#liveEventPreferences");
        if (prefs->live_events.hidden_feed_id_count > 0) {
            cJSON *hf = cJSON_CreateArray();
            wf_actor_prefs_add_string_array(hf, prefs->live_events.hidden_feed_ids,
                                            prefs->live_events.hidden_feed_id_count);
            cJSON_AddItemToObject(o, "hiddenFeedIds", hf);
        }
        if (prefs->live_events.has_hide_all_feeds) {
            cJSON_AddBoolToObject(o, "hideAllFeeds",
                                  prefs->live_events.hide_all_feeds);
        }
        cJSON_AddItemToArray(arr, o);
    }
    /* unknown prefs, verbatim */
    if (prefs->extra) {
        cJSON *e = prefs->extra;
        int n = cJSON_GetArraySize(e);
        for (int i = 0; i < n; ++i) {
            cJSON_AddItemToArray(arr, cJSON_Duplicate(cJSON_GetArrayItem(e, i), 1));
        }
    }

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!json) {
        return WF_ERR_ALLOC;
    }
    *out_json = json; /* freed by caller with free() */
    return WF_OK;
}

/* ------------------------------------------------------------------ */
/* declaration parse / free / build                                  */
/* ------------------------------------------------------------------ */

wf_status wf_actor_parse_declaration(const char *json, size_t json_len,
                                     wf_actor_declaration *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *at = cJSON_GetObjectItemCaseSensitive(root, "actorType");
    cJSON *since = cJSON_GetObjectItemCaseSensitive(root, "since");
    cJSON *pw = cJSON_GetObjectItemCaseSensitive(root, "password");

    if (cJSON_IsString(at) && at->valuestring) {
        status = wf_actor_prefs_set_string(&out->actor_type, at->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(since) && since->valuestring) {
        status = wf_actor_prefs_set_string(&out->since, since->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(pw) && pw->valuestring) {
        status = wf_actor_prefs_set_string(&out->password, pw->valuestring);
    }
    if (status == WF_OK) {
        cJSON_DetachItemFromObject(root, "actorType");
        cJSON_DetachItemFromObject(root, "since");
        cJSON_DetachItemFromObject(root, "password");
        out->extra = root;
    } else {
        wf_actor_declaration_free(out);
        cJSON_Delete(root);
    }
    return status;
}

void wf_actor_declaration_free(wf_actor_declaration *out) {
    if (!out) {
        return;
    }
    free(out->actor_type);
    free(out->since);
    free(out->password);
    if (out->extra) {
        cJSON_Delete(out->extra);
    }
    memset(out, 0, sizeof(*out));
}

wf_status wf_actor_build_declaration(const wf_actor_declaration *decl,
                                     char **out_json) {
    if (!decl || !out_json) {
        return WF_ERR_INVALID_ARG;
    }
    *out_json = NULL;

    cJSON *o = cJSON_CreateObject();
    if (!o) {
        return WF_ERR_ALLOC;
    }
    if (decl->actor_type) {
        cJSON_AddStringToObject(o, "actorType", decl->actor_type);
    }
    if (decl->since) {
        cJSON_AddStringToObject(o, "since", decl->since);
    }
    if (decl->password) {
        cJSON_AddStringToObject(o, "password", decl->password);
    }
    if (decl->extra) {
        /* Copy every remaining extra key/value pair into the built object. */
        cJSON *key = NULL;
        cJSON *val = NULL;
        cJSON_ArrayForEach(key, decl->extra) {
            (void)val;
            cJSON *dup = cJSON_Duplicate(key, 1);
            if (!dup) {
                cJSON_Delete(o);
                return WF_ERR_ALLOC;
            }
            cJSON_AddItemToObject(o, key->string, dup);
        }
    }

    char *json = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!json) {
        return WF_ERR_ALLOC;
    }
    *out_json = json;
    return WF_OK;
}

/* ------------------------------------------------------------------ */
/* Agent convenience wrappers                                         */
/* ------------------------------------------------------------------ */

wf_status wf_agent_get_actor_prefs_typed(wf_agent *agent,
                                         wf_actor_preferences *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_actor_preferences prefs = {0};
    wf_lex_app_bsky_actor_get_preferences_main_params params = {0};

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_actor_get_preferences_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_actor_parse_preferences(res.body, res.body_len, &prefs);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = prefs;
    }
    return status;
}

wf_status wf_agent_put_actor_prefs_typed(wf_agent *agent,
                                         const wf_actor_preferences *prefs) {
    if (!agent || !agent->client || !prefs) {
        return WF_ERR_INVALID_ARG;
    }

    char *json = NULL;
    wf_status status = wf_actor_build_preferences(prefs, &json);
    if (status != WF_OK) {
        return status;
    }

    wf_response res = {0};
    status = wf_agent_put_preferences(agent, json, &res);
    free(json);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_suggestions_typed(wf_agent *agent, int limit,
                                         const char *cursor,
                                         wf_agent_actor_list *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_actor_list list = {0};
    wf_lex_app_bsky_actor_get_suggestions_main_params params = {0};
    params.has_limit = true;
    params.limit = limit > 0 ? (int64_t)limit : 50;
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_actor_get_suggestions_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_agent_parse_actor_search(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_declare_actor_typed(wf_agent *agent,
                                       const wf_actor_declaration *decl) {
    /* TODO: app.bsky.actor.declareActor has no generated lex wrapper in this
     * tree (declaration.json is absent from the local lexicons), so the typed
     * declaration parser/builder exist but the network call cannot be issued.
     * Implement once a generated wf_lex_app_bsky_actor_declare_actor_* wrapper
     * is available. */
    (void)agent;
    (void)decl;
    return WF_ERR_INVALID_ARG;
}
