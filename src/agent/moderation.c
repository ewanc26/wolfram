/*
 * moderation.c — moderation decision engine implementation.
 *
 * Computes moderation decisions based on labels, blocks, mutes, hidden posts,
 * and muted words. Self-contained: no network I/O.
 *
 * Reference: atproto/packages/api/src/moderation/ (TypeScript)
 */

#include "wolfram/moderation.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>
#include <cJSON.h>

/* ------------------------------------------------------------------ */
/* Built-in behavior constants                                          */
/* ------------------------------------------------------------------ */

const wf_mod_behavior WF_MOD_BLOCK_BEHAVIOR = {
    .profile_list  = WF_MOD_ACTION_BLUR,
    .profile_view  = WF_MOD_ACTION_ALERT,
    .avatar        = WF_MOD_ACTION_BLUR,
    .banner        = WF_MOD_ACTION_BLUR,
    .display_name  = WF_MOD_ACTION_NONE,
    .content_list  = WF_MOD_ACTION_BLUR,
    .content_view  = WF_MOD_ACTION_BLUR,
    .content_media = WF_MOD_ACTION_NONE
};

const wf_mod_behavior WF_MOD_MUTE_BEHAVIOR = {
    .profile_list  = WF_MOD_ACTION_INFORM,
    .profile_view  = WF_MOD_ACTION_ALERT,
    .avatar        = WF_MOD_ACTION_NONE,
    .banner        = WF_MOD_ACTION_NONE,
    .display_name  = WF_MOD_ACTION_NONE,
    .content_list  = WF_MOD_ACTION_BLUR,
    .content_view  = WF_MOD_ACTION_INFORM,
    .content_media = WF_MOD_ACTION_NONE
};

const wf_mod_behavior WF_MOD_MUTEWORD_BEHAVIOR = {
    .profile_list  = WF_MOD_ACTION_NONE,
    .profile_view  = WF_MOD_ACTION_NONE,
    .avatar        = WF_MOD_ACTION_NONE,
    .banner        = WF_MOD_ACTION_NONE,
    .display_name  = WF_MOD_ACTION_NONE,
    .content_list  = WF_MOD_ACTION_BLUR,
    .content_view  = WF_MOD_ACTION_BLUR,
    .content_media = WF_MOD_ACTION_NONE
};

const wf_mod_behavior WF_MOD_HIDE_BEHAVIOR = {
    .profile_list  = WF_MOD_ACTION_NONE,
    .profile_view  = WF_MOD_ACTION_NONE,
    .avatar        = WF_MOD_ACTION_NONE,
    .banner        = WF_MOD_ACTION_NONE,
    .display_name  = WF_MOD_ACTION_NONE,
    .content_list  = WF_MOD_ACTION_BLUR,
    .content_view  = WF_MOD_ACTION_BLUR,
    .content_media = WF_MOD_ACTION_NONE
};

const wf_mod_behavior WF_MOD_NOOP_BEHAVIOR = {
    .profile_list  = WF_MOD_ACTION_NONE,
    .profile_view  = WF_MOD_ACTION_NONE,
    .avatar        = WF_MOD_ACTION_NONE,
    .banner        = WF_MOD_ACTION_NONE,
    .display_name  = WF_MOD_ACTION_NONE,
    .content_list  = WF_MOD_ACTION_NONE,
    .content_view  = WF_MOD_ACTION_NONE,
    .content_media = WF_MOD_ACTION_NONE
};

/* ------------------------------------------------------------------ */
/* Utility functions                                                    */
/* ------------------------------------------------------------------ */

static char *dup_str(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *r = malloc(len + 1);
    if (r) memcpy(r, s, len + 1);
    return r;
}

/* Match the reference SDK's comparison against new Date().toISOString().
 * Lexicon datetime values are RFC 3339 strings; the API emits canonical UTC
 * timestamps, whose fixed-width representation sorts chronologically. */
static int mute_word_has_expired(const char *expires_at) {
    time_t now;
    struct tm utc;
    char now_iso[25];

    if (!expires_at || !expires_at[0]) return 0;
    now = time(NULL);
#if defined(_WIN32)
    if (gmtime_s(&utc, &now) != 0) return 0;
#else
    if (!gmtime_r(&now, &utc)) return 0;
#endif
    if (strftime(now_iso, sizeof(now_iso), "%Y-%m-%dT%H:%M:%S", &utc) == 0)
        return 0;
    snprintf(now_iso + 19, sizeof(now_iso) - 19, ".000Z");
    return strcmp(expires_at, now_iso) < 0;
}

static int str_eq(const char *a, const char *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

__attribute__((unused))
static int str_starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    size_t lp = strlen(prefix);
    return strncmp(s, prefix, lp) == 0;
}

static int str_ends_with(const char *s, const char *suffix) {
    if (!s || !suffix) return 0;
    size_t ls = strlen(s), lf = strlen(suffix);
    if (lf > ls) return 0;
    return strcmp(s + ls - lf, suffix) == 0;
}

wf_mod_action wf_mod_behavior_get(const wf_mod_behavior *beh,
                                  wf_mod_context ctx) {
    if (!beh) return WF_MOD_ACTION_NONE;
    switch (ctx) {
        case WF_MOD_CTX_PROFILE_LIST:  return beh->profile_list;
        case WF_MOD_CTX_PROFILE_VIEW:  return beh->profile_view;
        case WF_MOD_CTX_AVATAR:        return beh->avatar;
        case WF_MOD_CTX_BANNER:        return beh->banner;
        case WF_MOD_CTX_DISPLAY_NAME:  return beh->display_name;
        case WF_MOD_CTX_CONTENT_LIST:  return beh->content_list;
        case WF_MOD_CTX_CONTENT_VIEW:  return beh->content_view;
        case WF_MOD_CTX_CONTENT_MEDIA: return beh->content_media;
        default: return WF_MOD_ACTION_NONE;
    }
}

/* ------------------------------------------------------------------ */
/* Label definition lookup                                              */
/* ------------------------------------------------------------------ */

const wf_mod_label_def *wf_mod_find_label_def(const wf_mod_opts *opts,
                                               const char *identifier,
                                               const char *labeler_did) {
    if (!opts || !identifier) return NULL;

    /* First, check labeler-specific definitions */
    if (labeler_did) {
        for (size_t i = 0; i < opts->label_def_count; i++) {
            const wf_mod_label_def *def = &opts->label_defs[i];
            if (def->defined_by && str_eq(def->defined_by, labeler_did) &&
                str_eq(def->identifier, identifier)) {
                return def;
            }
        }
    }

    /* Then check global definitions (defined_by == NULL) */
    for (size_t i = 0; i < opts->label_def_count; i++) {
        const wf_mod_label_def *def = &opts->label_defs[i];
        if (def->defined_by == NULL && str_eq(def->identifier, identifier)) {
            return def;
        }
    }

    return NULL;
}

wf_mod_label_pref wf_mod_get_label_pref(const wf_mod_opts *opts,
                                        const wf_mod_label_def *def,
                                        const char *labeler_did) {
    if (!def) return WF_MOD_PREF_IGNORE;

    /* If not configurable, use default setting */
    if (!def->configurable) {
        return def->default_setting;
    }

    /* If adult and adult content not enabled, hide */
    if ((def->flags & WF_MOD_FLAG_ADULT) && !opts->prefs.adult_content_enabled) {
        return WF_MOD_PREF_HIDE;
    }

    /* Check labeler-specific preference */
    if (labeler_did && !str_eq(labeler_did, opts->user_did)) {
        for (size_t i = 0; i < opts->prefs.labeler_count; i++) {
            const wf_mod_labeler_pref *lp = &opts->prefs.labelers[i];
            if (str_eq(lp->did, labeler_did)) {
                for (size_t j = 0; j < lp->label_count; j++) {
                    if (str_eq(lp->label_identifiers[j], def->identifier)) {
                        return lp->label_prefs[j];
                    }
                }
            }
        }
    }

    /* Check global preference */
    for (size_t i = 0; i < opts->prefs.global_label_count; i++) {
        if (str_eq(opts->prefs.global_label_identifiers[i], def->identifier)) {
            return opts->prefs.global_label_prefs[i];
        }
    }

    /* Fall back to default setting */
    return def->default_setting;
}

/* ------------------------------------------------------------------ */
/* Label interpretation                                                 */
/* ------------------------------------------------------------------ */

wf_status wf_mod_interpret_label_def(wf_mod_label_def *out,
                                     const char *identifier,
                                     const char *defined_by,
                                     const char *blurs,
                                     const char *severity,
                                     int adult_only,
                                     const char *default_setting) {
    if (!out || !identifier) return WF_ERR_INVALID_ARG;

    memset(out, 0, sizeof(*out));
    out->identifier = dup_str(identifier);
    out->defined_by = dup_str(defined_by);
    out->configurable = 1;

    /* Default setting */
    if (default_setting && str_eq(default_setting, "hide")) {
        out->default_setting = WF_MOD_PREF_HIDE;
    } else if (default_setting && str_eq(default_setting, "ignore")) {
        out->default_setting = WF_MOD_PREF_IGNORE;
    } else {
        out->default_setting = WF_MOD_PREF_WARN;
    }

    /* Flags */
    out->flags = WF_MOD_FLAG_NO_SELF;
    if (adult_only) {
        out->flags |= WF_MOD_FLAG_ADULT;
    }

    /* Determine alert/inform action from severity */
    wf_mod_action alert_or_inform = WF_MOD_ACTION_NONE;
    if (severity && str_eq(severity, "alert")) {
        alert_or_inform = WF_MOD_ACTION_ALERT;
    } else if (severity && str_eq(severity, "inform")) {
        alert_or_inform = WF_MOD_ACTION_INFORM;
    }

    /* Initialize all behaviors to none */
    for (int i = 0; i < 3; i++) {
        out->behaviors[i] = WF_MOD_NOOP_BEHAVIOR;
    }

    /* Compute behaviors based on blurs target */
    if (blurs && str_eq(blurs, "content")) {
        /* target=account */
        out->behaviors[WF_MOD_TARGET_ACCOUNT].profile_list  = alert_or_inform;
        out->behaviors[WF_MOD_TARGET_ACCOUNT].profile_view  = alert_or_inform;
        out->behaviors[WF_MOD_TARGET_ACCOUNT].content_list  = WF_MOD_ACTION_BLUR;
        out->behaviors[WF_MOD_TARGET_ACCOUNT].content_view  = adult_only ? WF_MOD_ACTION_BLUR : alert_or_inform;
        /* target=profile */
        out->behaviors[WF_MOD_TARGET_PROFILE].profile_list  = alert_or_inform;
        out->behaviors[WF_MOD_TARGET_PROFILE].profile_view  = alert_or_inform;
        /* target=content */
        out->behaviors[WF_MOD_TARGET_CONTENT].content_list  = WF_MOD_ACTION_BLUR;
        out->behaviors[WF_MOD_TARGET_CONTENT].content_view  = adult_only ? WF_MOD_ACTION_BLUR : alert_or_inform;
    } else if (blurs && str_eq(blurs, "media")) {
        /* target=account */
        out->behaviors[WF_MOD_TARGET_ACCOUNT].profile_list  = alert_or_inform;
        out->behaviors[WF_MOD_TARGET_ACCOUNT].profile_view  = alert_or_inform;
        out->behaviors[WF_MOD_TARGET_ACCOUNT].avatar        = WF_MOD_ACTION_BLUR;
        out->behaviors[WF_MOD_TARGET_ACCOUNT].banner        = WF_MOD_ACTION_BLUR;
        /* target=profile */
        out->behaviors[WF_MOD_TARGET_PROFILE].profile_list  = alert_or_inform;
        out->behaviors[WF_MOD_TARGET_PROFILE].profile_view  = alert_or_inform;
        out->behaviors[WF_MOD_TARGET_PROFILE].avatar        = WF_MOD_ACTION_BLUR;
        out->behaviors[WF_MOD_TARGET_PROFILE].banner        = WF_MOD_ACTION_BLUR;
        /* target=content */
        out->behaviors[WF_MOD_TARGET_CONTENT].content_media = WF_MOD_ACTION_BLUR;
    } else {
        /* blurs=none */
        /* target=account */
        out->behaviors[WF_MOD_TARGET_ACCOUNT].profile_list  = alert_or_inform;
        out->behaviors[WF_MOD_TARGET_ACCOUNT].profile_view  = alert_or_inform;
        out->behaviors[WF_MOD_TARGET_ACCOUNT].content_list  = alert_or_inform;
        out->behaviors[WF_MOD_TARGET_ACCOUNT].content_view  = alert_or_inform;
        /* target=profile */
        out->behaviors[WF_MOD_TARGET_PROFILE].profile_list  = alert_or_inform;
        out->behaviors[WF_MOD_TARGET_PROFILE].profile_view  = alert_or_inform;
        /* target=content */
        out->behaviors[WF_MOD_TARGET_CONTENT].content_list  = alert_or_inform;
        out->behaviors[WF_MOD_TARGET_CONTENT].content_view  = alert_or_inform;
    }

    return WF_OK;
}

void wf_mod_label_def_free(wf_mod_label_def *def) {
    if (!def) return;
    free(def->identifier);
    free(def->defined_by);
    def->identifier = NULL;
    def->defined_by = NULL;
}

/* ------------------------------------------------------------------ */
/* Decision lifecycle                                                   */
/* ------------------------------------------------------------------ */

wf_status wf_mod_decision_init(wf_mod_decision *d) {
    if (!d) return WF_ERR_INVALID_ARG;
    memset(d, 0, sizeof(*d));
    d->cause_capacity = 8;
    d->causes = calloc(d->cause_capacity, sizeof(wf_mod_cause));
    if (!d->causes) {
        d->cause_capacity = 0;
        return WF_ERR_ALLOC;
    }
    return WF_OK;
}

static void cause_free(wf_mod_cause *c) {
    if (!c) return;
    if (c->type == WF_MOD_CAUSE_LABEL) {
        free(c->label.src);
        free(c->label.uri);
        free(c->label.val);
        free(c->label.cts);
    }
    if (c->type == WF_MOD_CAUSE_MUTE_WORD && c->matches) {
        for (size_t i = 0; i < c->match_count; i++) {
            free(c->matches[i].value);
            free(c->matches[i].predicate);
        }
        free(c->matches);
    }
    memset(c, 0, sizeof(*c));
}

void wf_mod_decision_free(wf_mod_decision *d) {
    if (!d) return;
    for (size_t i = 0; i < d->cause_count; i++) {
        cause_free(&d->causes[i]);
    }
    free(d->causes);
    free(d->did);
    memset(d, 0, sizeof(*d));
}

static wf_status ensure_capacity(wf_mod_decision *d) {
    if (d->cause_count < d->cause_capacity) return WF_OK;
    size_t new_cap = d->cause_capacity * 2;
    if (new_cap == 0) new_cap = 8;
    wf_mod_cause *new_arr = realloc(d->causes, new_cap * sizeof(wf_mod_cause));
    if (!new_arr) return WF_ERR_ALLOC;
    /* Zero the new slots */
    memset(new_arr + d->cause_count, 0,
           (new_cap - d->cause_count) * sizeof(wf_mod_cause));
    d->causes = new_arr;
    d->cause_capacity = new_cap;
    return WF_OK;
}

static wf_status add_cause(wf_mod_decision *d, const wf_mod_cause *cause) {
    wf_status st = ensure_capacity(d);
    if (st != WF_OK) return st;
    d->causes[d->cause_count++] = *cause;
    return WF_OK;
}

/* Deep-copy a cause into dst, taking ownership of any copied heap data.
 * The copy is independent: freeing src does not affect dst. */
static wf_status cause_copy_deep(wf_mod_cause *dst, const wf_mod_cause *src) {
    *dst = *src;
    if (src->type == WF_MOD_CAUSE_LABEL) {
        dst->label.src = dup_str(src->label.src);
        dst->label.uri = dup_str(src->label.uri);
        dst->label.val = dup_str(src->label.val);
        dst->label.cts = dup_str(src->label.cts);
    } else if (src->type == WF_MOD_CAUSE_MUTE_WORD && src->match_count > 0) {
        dst->matches = calloc(src->match_count, sizeof(wf_mod_mute_word_match));
        if (!dst->matches) return WF_ERR_ALLOC;
        for (size_t i = 0; i < src->match_count; i++) {
            dst->matches[i].value = dup_str(src->matches[i].value);
            dst->matches[i].predicate = dup_str(src->matches[i].predicate);
        }
    }
    return WF_OK;
}

wf_status wf_mod_decision_merge(wf_mod_decision *out,
                                const wf_mod_decision *a,
                                const wf_mod_decision *b) {
    if (!out) return WF_ERR_INVALID_ARG;
    wf_status st = wf_mod_decision_init(out);
    if (st != WF_OK) return st;

    const wf_mod_decision *first = a ? a : b;
    if (first) {
        out->did = dup_str(first->did);
        out->is_me = first->is_me;
    }

    if (a) {
        for (size_t i = 0; i < a->cause_count; i++) {
            wf_mod_cause c;
            wf_status cs = cause_copy_deep(&c, &a->causes[i]);
            if (cs != WF_OK) {
                wf_mod_decision_free(out);
                return cs;
            }
            st = add_cause(out, &c);
            if (st != WF_OK) {
                cause_free(&c);
                wf_mod_decision_free(out);
                return st;
            }
        }
    }
    if (b) {
        for (size_t i = 0; i < b->cause_count; i++) {
            wf_mod_cause c;
            wf_status cs = cause_copy_deep(&c, &b->causes[i]);
            if (cs != WF_OK) {
                wf_mod_decision_free(out);
                return cs;
            }
            st = add_cause(out, &c);
            if (st != WF_OK) {
                cause_free(&c);
                wf_mod_decision_free(out);
                return st;
            }
        }
    }
    return WF_OK;
}

void wf_mod_decision_downgrade(wf_mod_decision *d) {
    if (!d) return;
    for (size_t i = 0; i < d->cause_count; i++) {
        d->causes[i].downgraded = 1;
    }
}

int wf_mod_decision_blocked(const wf_mod_decision *d) {
    if (!d) return 0;
    for (size_t i = 0; i < d->cause_count; i++) {
        if (d->causes[i].type == WF_MOD_CAUSE_BLOCKING ||
            d->causes[i].type == WF_MOD_CAUSE_BLOCKED_BY ||
            d->causes[i].type == WF_MOD_CAUSE_BLOCK_OTHER) {
            return 1;
        }
    }
    return 0;
}

int wf_mod_decision_muted(const wf_mod_decision *d) {
    if (!d) return 0;
    for (size_t i = 0; i < d->cause_count; i++) {
        if (d->causes[i].type == WF_MOD_CAUSE_MUTED) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* UI computation                                                       */
/* ------------------------------------------------------------------ */

static int cause_is_block(const wf_mod_cause *c) {
    return c->type == WF_MOD_CAUSE_BLOCKING ||
           c->type == WF_MOD_CAUSE_BLOCKED_BY ||
           c->type == WF_MOD_CAUSE_BLOCK_OTHER;
}

static wf_status ui_append(wf_mod_cause **arr, size_t *count, size_t *cap,
                           const wf_mod_cause *cause) {
    if (*count >= *cap) {
        size_t nc = (*cap == 0) ? 4 : *cap * 2;
        wf_mod_cause *na = realloc(*arr, nc * sizeof(wf_mod_cause));
        if (!na) return WF_ERR_ALLOC;
        *arr = na;
        *cap = nc;
    }
    (*arr)[(*count)++] = *cause;
    return WF_OK;
}

wf_status wf_mod_decision_ui(const wf_mod_decision *d,
                             wf_mod_context ctx,
                             wf_mod_ui *out) {
    if (!d || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    for (size_t i = 0; i < d->cause_count; i++) {
        const wf_mod_cause *c = &d->causes[i];

        if (cause_is_block(c)) {
            if (d->is_me) continue;
            if (ctx == WF_MOD_CTX_PROFILE_LIST || ctx == WF_MOD_CTX_CONTENT_LIST) {
                ui_append(&out->filters, &out->filter_count, &(size_t){0}, c);
            }
            if (!c->downgraded) {
                wf_mod_action a = wf_mod_behavior_get(&WF_MOD_BLOCK_BEHAVIOR, ctx);
                if (a == WF_MOD_ACTION_BLUR) {
                    out->no_override = 1;
                    ui_append(&out->blurs, &out->blur_count, &(size_t){0}, c);
                } else if (a == WF_MOD_ACTION_ALERT) {
                    ui_append(&out->alerts, &out->alert_count, &(size_t){0}, c);
                } else if (a == WF_MOD_ACTION_INFORM) {
                    ui_append(&out->informs, &out->inform_count, &(size_t){0}, c);
                }
            }
        } else if (c->type == WF_MOD_CAUSE_MUTED) {
            if (d->is_me) continue;
            if (ctx == WF_MOD_CTX_PROFILE_LIST || ctx == WF_MOD_CTX_CONTENT_LIST) {
                ui_append(&out->filters, &out->filter_count, &(size_t){0}, c);
            }
            if (!c->downgraded) {
                wf_mod_action a = wf_mod_behavior_get(&WF_MOD_MUTE_BEHAVIOR, ctx);
                if (a == WF_MOD_ACTION_BLUR) {
                    ui_append(&out->blurs, &out->blur_count, &(size_t){0}, c);
                } else if (a == WF_MOD_ACTION_ALERT) {
                    ui_append(&out->alerts, &out->alert_count, &(size_t){0}, c);
                } else if (a == WF_MOD_ACTION_INFORM) {
                    ui_append(&out->informs, &out->inform_count, &(size_t){0}, c);
                }
            }
        } else if (c->type == WF_MOD_CAUSE_MUTE_WORD) {
            if (d->is_me) continue;
            if (ctx == WF_MOD_CTX_CONTENT_LIST) {
                ui_append(&out->filters, &out->filter_count, &(size_t){0}, c);
            }
            if (!c->downgraded) {
                wf_mod_action a = wf_mod_behavior_get(&WF_MOD_MUTEWORD_BEHAVIOR, ctx);
                if (a == WF_MOD_ACTION_BLUR) {
                    ui_append(&out->blurs, &out->blur_count, &(size_t){0}, c);
                } else if (a == WF_MOD_ACTION_ALERT) {
                    ui_append(&out->alerts, &out->alert_count, &(size_t){0}, c);
                } else if (a == WF_MOD_ACTION_INFORM) {
                    ui_append(&out->informs, &out->inform_count, &(size_t){0}, c);
                }
            }
        } else if (c->type == WF_MOD_CAUSE_HIDDEN) {
            if (ctx == WF_MOD_CTX_PROFILE_LIST || ctx == WF_MOD_CTX_CONTENT_LIST) {
                ui_append(&out->filters, &out->filter_count, &(size_t){0}, c);
            }
            if (!c->downgraded) {
                wf_mod_action a = wf_mod_behavior_get(&WF_MOD_HIDE_BEHAVIOR, ctx);
                if (a == WF_MOD_ACTION_BLUR) {
                    ui_append(&out->blurs, &out->blur_count, &(size_t){0}, c);
                } else if (a == WF_MOD_ACTION_ALERT) {
                    ui_append(&out->alerts, &out->alert_count, &(size_t){0}, c);
                } else if (a == WF_MOD_ACTION_INFORM) {
                    ui_append(&out->informs, &out->inform_count, &(size_t){0}, c);
                }
            }
        } else if (c->type == WF_MOD_CAUSE_LABEL) {
            /* Filter logic for hide settings */
            if (ctx == WF_MOD_CTX_PROFILE_LIST && c->target == WF_MOD_TARGET_ACCOUNT) {
                if (c->setting == WF_MOD_PREF_HIDE && !d->is_me) {
                    ui_append(&out->filters, &out->filter_count, &(size_t){0}, c);
                }
            } else if (ctx == WF_MOD_CTX_CONTENT_LIST &&
                       (c->target == WF_MOD_TARGET_ACCOUNT || c->target == WF_MOD_TARGET_CONTENT)) {
                if (c->setting == WF_MOD_PREF_HIDE && !d->is_me) {
                    ui_append(&out->filters, &out->filter_count, &(size_t){0}, c);
                }
            }
            if (!c->downgraded) {
                wf_mod_action a = wf_mod_behavior_get(&c->behavior, ctx);
                if (a == WF_MOD_ACTION_BLUR) {
                    ui_append(&out->blurs, &out->blur_count, &(size_t){0}, c);
                    if (c->no_override && !d->is_me) {
                        out->no_override = 1;
                    }
                } else if (a == WF_MOD_ACTION_ALERT) {
                    ui_append(&out->alerts, &out->alert_count, &(size_t){0}, c);
                } else if (a == WF_MOD_ACTION_INFORM) {
                    ui_append(&out->informs, &out->inform_count, &(size_t){0}, c);
                }
            }
        }
    }

    return WF_OK;
}

void wf_mod_ui_free(wf_mod_ui *ui) {
    if (!ui) return;
    free(ui->filters);
    free(ui->blurs);
    free(ui->alerts);
    free(ui->informs);
    memset(ui, 0, sizeof(*ui));
}

/* ------------------------------------------------------------------ */
/* Cause adders                                                         */
/* ------------------------------------------------------------------ */

wf_status wf_mod_add_blocking(wf_mod_decision *d, const char *blocking_uri) {
    if (!d) return WF_ERR_INVALID_ARG;
    if (!blocking_uri) return WF_OK;
    wf_mod_cause c = {0};
    c.type = WF_MOD_CAUSE_BLOCKING;
    c.priority = 3;
    return add_cause(d, &c);
}

wf_status wf_mod_add_blocked_by(wf_mod_decision *d, int blocked_by) {
    if (!d) return WF_ERR_INVALID_ARG;
    if (!blocked_by) return WF_OK;
    wf_mod_cause c = {0};
    c.type = WF_MOD_CAUSE_BLOCKED_BY;
    c.priority = 4;
    return add_cause(d, &c);
}

wf_status wf_mod_add_block_other(wf_mod_decision *d, int block_other) {
    if (!d) return WF_ERR_INVALID_ARG;
    if (!block_other) return WF_OK;
    wf_mod_cause c = {0};
    c.type = WF_MOD_CAUSE_BLOCK_OTHER;
    c.priority = 4;
    return add_cause(d, &c);
}

wf_status wf_mod_add_muted(wf_mod_decision *d, int muted) {
    if (!d) return WF_ERR_INVALID_ARG;
    if (!muted) return WF_OK;
    wf_mod_cause c = {0};
    c.type = WF_MOD_CAUSE_MUTED;
    c.priority = 6;
    return add_cause(d, &c);
}

wf_status wf_mod_add_hidden(wf_mod_decision *d, int hidden) {
    if (!d) return WF_ERR_INVALID_ARG;
    if (!hidden) return WF_OK;
    wf_mod_cause c = {0};
    c.type = WF_MOD_CAUSE_HIDDEN;
    c.priority = 6;
    return add_cause(d, &c);
}

wf_status wf_mod_add_muted_word(wf_mod_decision *d,
                                const wf_mod_mute_word_match *matches,
                                size_t match_count) {
    if (!d) return WF_ERR_INVALID_ARG;
    if (!matches || match_count == 0) return WF_OK;

    wf_mod_cause c = {0};
    c.type = WF_MOD_CAUSE_MUTE_WORD;
    c.priority = 6;
    c.match_count = match_count;
    c.matches = calloc(match_count, sizeof(wf_mod_mute_word_match));
    if (!c.matches) return WF_ERR_ALLOC;
    for (size_t i = 0; i < match_count; i++) {
        c.matches[i].value = dup_str(matches[i].value);
        c.matches[i].predicate = dup_str(matches[i].predicate);
    }
    return add_cause(d, &c);
}

wf_status wf_mod_add_label(wf_mod_decision *d,
                           wf_mod_label_target target,
                           const wf_mod_label *label,
                           const wf_mod_opts *opts) {
    if (!d || !label || !opts) return WF_ERR_INVALID_ARG;

    /* Find label definition */
    const wf_mod_label_def *def = wf_mod_find_label_def(opts, label->val, label->src);
    if (!def) {
        /* Ignore labels we don't understand */
        return WF_OK;
    }

    /* Check if self-labeled */
    int is_self = opts->user_did && str_eq(label->src, opts->user_did);

    /* Skip labelers not configured by the user (unless self) */
    if (!is_self) {
        int found = 0;
        for (size_t i = 0; i < opts->prefs.labeler_count; i++) {
            if (str_eq(opts->prefs.labelers[i].did, label->src)) {
                found = 1;
                break;
            }
        }
        if (!found) return WF_OK;
    }

    /* Skip self-labels with no-self flag */
    if (is_self && (def->flags & WF_MOD_FLAG_NO_SELF)) {
        return WF_OK;
    }

    /* Get label preference */
    wf_mod_label_pref pref = wf_mod_get_label_pref(opts, def, label->src);

    /* Ignore labels the user has asked to ignore */
    if (pref == WF_MOD_PREF_IGNORE) {
        return WF_OK;
    }

    /* Ignore 'unauthed' labels when the user is authed */
    if ((def->flags & WF_MOD_FLAG_UNAUTHED) && opts->user_did) {
        return WF_OK;
    }

    /* Compute priority */
    int priority;
    int no_override = 0;

    if ((def->flags & WF_MOD_FLAG_NO_OVERRIDE) ||
        ((def->flags & WF_MOD_FLAG_ADULT) && !opts->prefs.adult_content_enabled)) {
        priority = 1;
        no_override = 1;
    } else if (pref == WF_MOD_PREF_HIDE) {
        priority = 2;
    } else {
        /* Determine severity from behavior */
        const wf_mod_behavior *beh = &def->behaviors[target];
        if (beh->profile_view == WF_MOD_ACTION_BLUR || beh->content_view == WF_MOD_ACTION_BLUR) {
            priority = 5;
        } else if (beh->content_list == WF_MOD_ACTION_BLUR || beh->content_media == WF_MOD_ACTION_BLUR) {
            priority = 7;
        } else {
            priority = 8;
        }
    }

    /* Add the cause */
    wf_mod_cause c = {0};
    c.type = WF_MOD_CAUSE_LABEL;
    c.priority = (uint32_t)priority;
    c.downgraded = 0;
    c.label.src = dup_str(label->src);
    c.label.uri = dup_str(label->uri);
    c.label.val = dup_str(label->val);
    c.label.cts = dup_str(label->cts);
    c.label_def = def;
    c.target = target;
    c.setting = pref;
    c.behavior = def->behaviors[target];
    c.no_override = no_override;

    return add_cause(d, &c);
}

/* ------------------------------------------------------------------ */
/* Subject deciders                                                     */
/* ------------------------------------------------------------------ */

static void set_did_and_me(wf_mod_decision *d, const char *did,
                           const wf_mod_opts *opts) {
    d->did = dup_str(did);
    d->is_me = (opts->user_did && str_eq(did, opts->user_did)) ? 1 : 0;
}

static int is_account_label(const wf_mod_label *label) {
    /* Account labels are those NOT on the profile/self URI */
    if (!label->uri) return 1;
    return !str_ends_with(label->uri, "/app.bsky.actor.profile/self") ||
           str_eq(label->val, "!no-unauthenticated");
}

static int is_profile_label(const wf_mod_label *label) {
    if (!label->uri) return 0;
    return str_ends_with(label->uri, "/app.bsky.actor.profile/self");
}

wf_status wf_mod_decide_account(wf_mod_decision *out,
                                const wf_mod_subject_profile *subject,
                                const wf_mod_opts *opts) {
    if (!out || !subject || !opts) return WF_ERR_INVALID_ARG;

    wf_status st = wf_mod_decision_init(out);
    if (st != WF_OK) return st;

    set_did_and_me(out, subject->did, opts);

    /* Mute */
    if (subject->viewer.muted) {
        wf_mod_add_muted(out, 1);
    }
    /* Blocking */
    if (subject->viewer.blocking) {
        wf_mod_add_blocking(out, subject->viewer.blocking);
    }
    /* Blocked by */
    if (subject->viewer.blocked_by) {
        wf_mod_add_blocked_by(out, 1);
    }

    /* Account labels */
    for (size_t i = 0; i < subject->label_count; i++) {
        if (is_account_label(&subject->labels[i])) {
            wf_mod_add_label(out, WF_MOD_TARGET_ACCOUNT, &subject->labels[i], opts);
        }
    }

    return WF_OK;
}

wf_status wf_mod_decide_profile(wf_mod_decision *out,
                                const wf_mod_subject_profile *subject,
                                const wf_mod_opts *opts) {
    if (!out || !subject || !opts) return WF_ERR_INVALID_ARG;

    wf_status st = wf_mod_decision_init(out);
    if (st != WF_OK) return st;

    set_did_and_me(out, subject->did, opts);

    /* Profile labels only */
    for (size_t i = 0; i < subject->label_count; i++) {
        if (is_profile_label(&subject->labels[i])) {
            wf_mod_add_label(out, WF_MOD_TARGET_PROFILE, &subject->labels[i], opts);
        }
    }

    return WF_OK;
}

wf_status wf_mod_decide_post(wf_mod_decision *out,
                             const wf_mod_subject_post *subject,
                             const wf_mod_opts *opts) {
    if (!out || !subject || !opts) return WF_ERR_INVALID_ARG;

    /* Build subject decision (content labels + hidden + mute words) */
    wf_mod_decision subject_d;
    wf_status st = wf_mod_decision_init(&subject_d);
    if (st != WF_OK) return st;

    set_did_and_me(&subject_d, subject->author.did, opts);

    /* Content labels */
    for (size_t i = 0; i < subject->label_count; i++) {
        wf_mod_add_label(&subject_d, WF_MOD_TARGET_CONTENT, &subject->labels[i], opts);
    }

    /* Check hidden posts */
    int hidden = 0;
    for (size_t i = 0; i < opts->prefs.hidden_post_count; i++) {
        if (str_eq(opts->prefs.hidden_posts[i], subject->uri)) {
            hidden = 1;
            break;
        }
    }
    wf_mod_add_hidden(&subject_d, hidden);

    /* Mute word matching */
    if (!subject_d.is_me && opts->prefs.muted_word_count > 0 && subject->text) {
        wf_mod_mute_word_match *matches = NULL;
        size_t match_count = 0;
        wf_mod_match_mute_words(&matches, &match_count,
                                opts->prefs.muted_words,
                                opts->prefs.muted_word_count,
                                subject->text, NULL, 0, NULL, 0);
        if (match_count > 0) {
            wf_mod_add_muted_word(&subject_d, matches, match_count);
            wf_mod_mute_word_matches_free(matches, match_count);
        }
    }

    /* Build account decision */
    wf_mod_decision account_d;
    st = wf_mod_decide_account(&account_d, &subject->author, opts);
    if (st != WF_OK) {
        wf_mod_decision_free(&subject_d);
        return st;
    }

    /* Build profile decision */
    wf_mod_decision profile_d;
    st = wf_mod_decide_profile(&profile_d, &subject->author, opts);
    if (st != WF_OK) {
        wf_mod_decision_free(&subject_d);
        wf_mod_decision_free(&account_d);
        return st;
    }

    /* Merge: subject + account + profile */
    wf_mod_decision merged1;
    st = wf_mod_decision_merge(&merged1, &subject_d, &account_d);
    if (st != WF_OK) {
        wf_mod_decision_free(&subject_d);
        wf_mod_decision_free(&account_d);
        wf_mod_decision_free(&profile_d);
        return st;
    }

    wf_mod_decision merged2;
    st = wf_mod_decision_merge(&merged2, &merged1, &profile_d);
    if (st != WF_OK) {
        wf_mod_decision_free(&subject_d);
        wf_mod_decision_free(&account_d);
        wf_mod_decision_free(&profile_d);
        wf_mod_decision_free(&merged1);
        return st;
    }

    *out = merged2;

    wf_mod_decision_free(&subject_d);
    wf_mod_decision_free(&account_d);
    wf_mod_decision_free(&profile_d);
    wf_mod_decision_free(&merged1);

    return WF_OK;
}

wf_status wf_mod_decide_notification(wf_mod_decision *out,
                                     const wf_mod_subject_post *subject,
                                     const wf_mod_opts *opts) {
    /* Notification moderation is the same as post moderation for now */
    return wf_mod_decide_post(out, subject, opts);
}

wf_status wf_mod_decide_feed_generator(wf_mod_decision *out,
                                       const wf_mod_subject_feed_gen *subject,
                                       const wf_mod_opts *opts) {
    if (!out || !subject || !opts) return WF_ERR_INVALID_ARG;

    /* Merge account + profile + content labels */
    wf_mod_decision account_d;
    wf_status st = wf_mod_decide_account(&account_d, &subject->creator, opts);
    if (st != WF_OK) return st;

    wf_mod_decision profile_d;
    st = wf_mod_decide_profile(&profile_d, &subject->creator, opts);
    if (st != WF_OK) {
        wf_mod_decision_free(&account_d);
        return st;
    }

    /* Content labels on the feed generator */
    wf_mod_decision content_d;
    st = wf_mod_decision_init(&content_d);
    if (st != WF_OK) {
        wf_mod_decision_free(&account_d);
        wf_mod_decision_free(&profile_d);
        return st;
    }
    set_did_and_me(&content_d, subject->creator.did, opts);
    for (size_t i = 0; i < subject->label_count; i++) {
        wf_mod_add_label(&content_d, WF_MOD_TARGET_CONTENT, &subject->labels[i], opts);
    }

    wf_mod_decision merged1;
    st = wf_mod_decision_merge(&merged1, &account_d, &profile_d);
    if (st != WF_OK) {
        wf_mod_decision_free(&account_d);
        wf_mod_decision_free(&profile_d);
        wf_mod_decision_free(&content_d);
        return st;
    }

    st = wf_mod_decision_merge(out, &merged1, &content_d);
    wf_mod_decision_free(&account_d);
    wf_mod_decision_free(&profile_d);
    wf_mod_decision_free(&content_d);
    wf_mod_decision_free(&merged1);

    return st;
}

wf_status wf_mod_decide_user_list(wf_mod_decision *out,
                                  const wf_mod_subject_user_list *subject,
                                  const wf_mod_opts *opts) {
    if (!out || !subject || !opts) return WF_ERR_INVALID_ARG;

    /* Merge account + profile + content labels */
    wf_mod_decision account_d;
    wf_status st = wf_mod_decide_account(&account_d, &subject->creator, opts);
    if (st != WF_OK) return st;

    wf_mod_decision profile_d;
    st = wf_mod_decide_profile(&profile_d, &subject->creator, opts);
    if (st != WF_OK) {
        wf_mod_decision_free(&account_d);
        return st;
    }

    /* Content labels on the list */
    wf_mod_decision content_d;
    st = wf_mod_decision_init(&content_d);
    if (st != WF_OK) {
        wf_mod_decision_free(&account_d);
        wf_mod_decision_free(&profile_d);
        return st;
    }
    set_did_and_me(&content_d, subject->creator.did, opts);
    for (size_t i = 0; i < subject->label_count; i++) {
        wf_mod_add_label(&content_d, WF_MOD_TARGET_CONTENT, &subject->labels[i], opts);
    }

    wf_mod_decision merged1;
    st = wf_mod_decision_merge(&merged1, &account_d, &profile_d);
    if (st != WF_OK) {
        wf_mod_decision_free(&account_d);
        wf_mod_decision_free(&profile_d);
        wf_mod_decision_free(&content_d);
        return st;
    }

    st = wf_mod_decision_merge(out, &merged1, &content_d);
    wf_mod_decision_free(&account_d);
    wf_mod_decision_free(&profile_d);
    wf_mod_decision_free(&content_d);
    wf_mod_decision_free(&merged1);

    return st;
}

wf_status wf_mod_decide_status(wf_mod_decision *out,
                               const wf_mod_subject_profile *subject,
                               const wf_mod_opts *opts) {
    /* Status moderation is account-level only for now */
    return wf_mod_decide_account(out, subject, opts);
}

/* ------------------------------------------------------------------ */
/* Mute word matching                                                   */
/* ------------------------------------------------------------------ */

/* Language exceptions — languages that don't use spaces */
static int is_language_exception(const char *lang) {
    if (!lang) return 0;
    return str_eq(lang, "ja") || str_eq(lang, "zh") ||
           str_eq(lang, "ko") || str_eq(lang, "th") ||
           str_eq(lang, "vi");
}

/* Check if a character is a punctuation character (simplified) */
static int is_punct(char c) {
    return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') ||
           (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

/* Check if a character is whitespace */
static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/* Lowercase a string in-place */
static void to_lower(char *s) {
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

/* Check if word contains any punctuation */
static int word_has_punct(const char *w) {
    for (; *w; w++) if (is_punct(*w)) return 1;
    return 0;
}

/* Check if word contains space or punctuation */
static int word_has_space_or_punct(const char *w) {
    for (; *w; w++) if (is_space(*w) || is_punct(*w)) return 1;
    return 0;
}

/* Trim leading/trailing punctuation from a word (returns pointer into s) */
static const char *trim_punct(const char *s, size_t *out_len) {
    size_t len = strlen(s);
    while (len > 0 && is_punct(s[0])) { s++; len--; }
    while (len > 0 && is_punct(s[len - 1])) len--;
    *out_len = len;
    return s;
}

wf_status wf_mod_match_mute_words(wf_mod_mute_word_match **out_matches,
                                  size_t *out_count,
                                  const wf_mod_muted_word *muted_words,
                                  size_t muted_word_count,
                                  const char *text,
                                  const char *const *tags,
                                  size_t tag_count,
                                  const char *lang,
                                  int following) {
    if (!out_matches || !out_count) return WF_ERR_INVALID_ARG;
    *out_matches = NULL;
    *out_count = 0;

    if (!muted_words || muted_word_count == 0 || !text) return WF_OK;

    int exception = is_language_exception(lang);

    /* Lowercase the text */
    size_t text_len = strlen(text);
    char *post_text = malloc(text_len + 1);
    if (!post_text) return WF_ERR_ALLOC;
    memcpy(post_text, text, text_len + 1);
    to_lower(post_text);

    /* Collect lowercase tags */
    char **lower_tags = NULL;
    if (tag_count > 0) {
        lower_tags = calloc(tag_count, sizeof(char *));
        if (!lower_tags) { free(post_text); return WF_ERR_ALLOC; }
        for (size_t i = 0; i < tag_count; i++) {
            if (tags[i]) {
                lower_tags[i] = dup_str(tags[i]);
                if (lower_tags[i]) to_lower(lower_tags[i]);
            }
        }
    }

    /* Allocate result array (max = muted_word_count) */
    wf_mod_mute_word_match *results = calloc(muted_word_count, sizeof(wf_mod_mute_word_match));
    if (!results) {
        free(post_text);
        for (size_t i = 0; i < tag_count; i++) free(lower_tags ? lower_tags[i] : NULL);
        free(lower_tags);
        return WF_ERR_ALLOC;
    }
    size_t result_count = 0;

    for (size_t i = 0; i < muted_word_count; i++) {
        const wf_mod_muted_word *mw = &muted_words[i];
        if (!mw->value) continue;

        char *muted_word = dup_str(mw->value);
        if (!muted_word) continue;
        to_lower(muted_word);
        size_t mw_len = strlen(muted_word);

        /* Expired mute words are ignored; future timed mutes remain active. */
        if (mute_word_has_expired(mw->expires_at)) {
            free(muted_word);
            continue;
        }

        /* Check actor target */
        if (mw->actor_target && str_eq(mw->actor_target, "exclude-following") && following) {
            free(muted_word);
            continue;
        }

        /* Check tags */
        int tag_match = 0;
        for (size_t t = 0; t < tag_count; t++) {
            if (lower_tags && lower_tags[t] && str_eq(lower_tags[t], muted_word)) {
                tag_match = 1;
                break;
            }
        }
        if (tag_match) {
            results[result_count].value = dup_str(mw->value);
            results[result_count].predicate = dup_str(mw->value);
            result_count++;
            free(muted_word);
            continue;
        }

        /* Rest of checks are for content target only */
        if (!mw->targets_content) {
            free(muted_word);
            continue;
        }

        /* Single character or language exception: use substring match */
        if (mw_len == 1 || exception) {
            if (strstr(post_text, muted_word)) {
                results[result_count].value = dup_str(mw->value);
                results[result_count].predicate = dup_str(mw->value);
                result_count++;
            }
            free(muted_word);
            continue;
        }

        /* Too long */
        if (mw_len > text_len) {
            free(muted_word);
            continue;
        }

        /* Exact match */
        if (strcmp(muted_word, post_text) == 0) {
            results[result_count].value = dup_str(mw->value);
            results[result_count].predicate = dup_str(mw->value);
            result_count++;
            free(muted_word);
            continue;
        }

        /* Muted phrase with space or punctuation */
        if (word_has_space_or_punct(muted_word) && strstr(post_text, muted_word)) {
            results[result_count].value = dup_str(mw->value);
            results[result_count].predicate = dup_str(mw->value);
            result_count++;
            free(muted_word);
            continue;
        }

        /* Split text into words and compare */
        {
            char *p = post_text;
            int found = 0;
            while (*p && !found) {
                /* Skip whitespace */
                while (*p && is_space(*p)) p++;
                if (!*p) break;

                /* Find end of word */
                char *word_start = p;
                while (*p && !is_space(*p)) p++;
                size_t word_len = (size_t)(p - word_start);

                /* Compare exact word */
                if (word_len == mw_len && strncmp(word_start, muted_word, mw_len) == 0) {
                    results[result_count].value = dup_str(mw->value);
                    /* Copy the matched word */
                    char *pred = malloc(word_len + 1);
                    if (pred) {
                        memcpy(pred, word_start, word_len);
                        pred[word_len] = '\0';
                        results[result_count].predicate = pred;
                    }
                    result_count++;
                    found = 1;
                    break;
                }

                /* Trim punctuation and compare */
                size_t trimmed_len;
                const char *trimmed = trim_punct(word_start, &trimmed_len);
                if (trimmed_len > 0) {
                    if (trimmed_len == mw_len && strncmp(trimmed, muted_word, mw_len) == 0) {
                        results[result_count].value = dup_str(mw->value);
                        char *pred = malloc(word_len + 1);
                        if (pred) {
                            memcpy(pred, word_start, word_len);
                            pred[word_len] = '\0';
                            results[result_count].predicate = pred;
                        }
                        result_count++;
                        found = 1;
                        break;
                    }

                    /* If word has internal punctuation, try splitting */
                    if (trimmed_len > 0 && word_has_punct(trimmed)) {
                        /* Try replacing punctuation with spaces */
                        char *spaced = malloc(trimmed_len + 1);
                        if (spaced) {
                            memcpy(spaced, trimmed, trimmed_len);
                            spaced[trimmed_len] = '\0';
                            for (size_t c = 0; c < trimmed_len; c++) {
                                if (is_punct(spaced[c])) spaced[c] = ' ';
                            }
                            if (strcmp(spaced, muted_word) == 0) {
                                results[result_count].value = dup_str(mw->value);
                                char *pred = malloc(word_len + 1);
                                if (pred) {
                                    memcpy(pred, word_start, word_len);
                                    pred[word_len] = '\0';
                                    results[result_count].predicate = pred;
                                }
                                result_count++;
                                found = 1;
                                free(spaced);
                                break;
                            }

                            /* Try contiguous (remove spaces) */
                            char *contig = malloc(trimmed_len + 1);
                            if (contig) {
                                size_t ci = 0;
                                for (size_t c = 0; c < trimmed_len; c++) {
                                    if (!is_space(spaced[c])) contig[ci++] = spaced[c];
                                }
                                contig[ci] = '\0';
                                if (strcmp(contig, muted_word) == 0) {
                                    results[result_count].value = dup_str(mw->value);
                                    char *pred = malloc(word_len + 1);
                                    if (pred) {
                                        memcpy(pred, word_start, word_len);
                                        pred[word_len] = '\0';
                                        results[result_count].predicate = pred;
                                    }
                                    result_count++;
                                    found = 1;
                                    free(contig);
                                    free(spaced);
                                    break;
                                }
                                free(contig);
                            }
                            free(spaced);
                        }
                    }
                }
            }
        }

        free(muted_word);
    }

    free(post_text);
    for (size_t i = 0; i < tag_count; i++) free(lower_tags ? lower_tags[i] : NULL);
    free(lower_tags);

    if (result_count > 0) {
        *out_matches = results;
        *out_count = result_count;
    } else {
        free(results);
    }

    return WF_OK;
}

void wf_mod_mute_word_matches_free(wf_mod_mute_word_match *matches, size_t count) {
    if (!matches) return;
    for (size_t i = 0; i < count; i++) {
        free(matches[i].value);
        free(matches[i].predicate);
    }
    free(matches);
}

/* ------------------------------------------------------------------ */
/* JSON ingestion                                                       */
/* ------------------------------------------------------------------ */

/* Forward declarations for the mutually-referential pref parsers. */
static wf_status parse_pref_item(wf_mod_prefs *p, const cJSON *item);
static wf_status parse_moderation_prefs(wf_mod_prefs *p, const cJSON *mp);

/* Append a duplicated string to a growable char* array. */
static wf_status json_strarr_push(char ***arr, size_t *count, size_t *cap,
                                  const char *s) {
    if (!s) return WF_OK;
    if (*count >= *cap) {
        size_t nc = (*cap == 0) ? 4 : *cap * 2;
        char **na = realloc(*arr, nc * sizeof(char *));
        if (!na) return WF_ERR_ALLOC;
        *arr = na;
        *cap = nc;
    }
    (*arr)[*count] = dup_str(s);
    if (!(*arr)[*count]) return WF_ERR_ALLOC;
    (*count)++;
    return WF_OK;
}

/* Append a labeler preference (DID only) to a growable labeler array. */
static wf_status json_labeler_push(wf_mod_labeler_pref **arr, size_t *count,
                                   size_t *cap, const char *did) {
    if (!did) return WF_OK;
    if (*count >= *cap) {
        size_t nc = (*cap == 0) ? 2 : *cap * 2;
        wf_mod_labeler_pref *na = realloc(*arr, nc * sizeof(wf_mod_labeler_pref));
        if (!na) return WF_ERR_ALLOC;
        *arr = na;
        *cap = nc;
    }
    wf_mod_labeler_pref *lp = &(*arr)[*count];
    memset(lp, 0, sizeof(*lp));
    lp->did = dup_str(did);
    if (!lp->did) return WF_ERR_ALLOC;
    (*count)++;
    return WF_OK;
}

/* Append a muted word to a growable muted-word array. */
static wf_status json_muted_word_push(wf_mod_muted_word **arr, size_t *count,
                                      size_t *cap,
                                      const wf_mod_muted_word *mw) {
    if (*count >= *cap) {
        size_t nc = (*cap == 0) ? 4 : *cap * 2;
        wf_mod_muted_word *na = realloc(*arr, nc * sizeof(wf_mod_muted_word));
        if (!na) return WF_ERR_ALLOC;
        *arr = na;
        *cap = nc;
    }
    wf_mod_muted_word *dst = &(*arr)[*count];
    memset(dst, 0, sizeof(*dst));
    dst->value = dup_str(mw->value);
    dst->actor_target = dup_str(mw->actor_target);
    dst->expires_at = dup_str(mw->expires_at);
    dst->targets_content = mw->targets_content;
    dst->targets_tag = mw->targets_tag;
    if (!dst->value) return WF_ERR_ALLOC;
    (*count)++;
    return WF_OK;
}

static const char *json_str(const cJSON *item) {
    if (item && cJSON_IsString(item) && item->valuestring) {
        return item->valuestring;
    }
    return NULL;
}

/* Map a visibility string to a label preference. */
static wf_mod_label_pref json_visibility_to_pref(const char *vis) {
    if (!vis) return WF_MOD_PREF_WARN;
    if (strcmp(vis, "hide") == 0) return WF_MOD_PREF_HIDE;
    if (strcmp(vis, "ignore") == 0) return WF_MOD_PREF_IGNORE;
    return WF_MOD_PREF_WARN;
}

/* Apply one content-label / global label entry to prefs. */
static wf_status apply_global_label(wf_mod_prefs *p, const char *label,
                                    const char *vis) {
    if (!label) return WF_OK;
    wf_mod_label_pref pref = json_visibility_to_pref(vis);
    /* Overwrite if the label already exists. */
    for (size_t i = 0; i < p->global_label_count; i++) {
        if (str_eq(p->global_label_identifiers[i], label)) {
            p->global_label_prefs[i] = pref;
            return WF_OK;
        }
    }
    if (p->global_label_count >= p->global_label_cap) {
        size_t nc = (p->global_label_cap == 0) ? 4 : p->global_label_cap * 2;
        char **ids = realloc(p->global_label_identifiers, nc * sizeof(char *));
        wf_mod_label_pref *prefs = realloc(p->global_label_prefs, nc * sizeof(wf_mod_label_pref));
        if (!ids || !prefs) { free(ids); free(prefs); return WF_ERR_ALLOC; }
        p->global_label_identifiers = ids;
        p->global_label_prefs = prefs;
        p->global_label_cap = nc;
    }
    p->global_label_identifiers[p->global_label_count] = dup_str(label);
    p->global_label_prefs[p->global_label_count] = pref;
    if (!p->global_label_identifiers[p->global_label_count]) return WF_ERR_ALLOC;
    p->global_label_count++;
    return WF_OK;
}

/* Apply one muted-word object (with value/targets/actorTarget/expiresAt). */
static wf_status apply_muted_word(wf_mod_prefs *p, const cJSON *word) {
    const char *value = json_str(cJSON_GetObjectItemCaseSensitive(word, "value"));
    if (!value) return WF_OK;
    wf_mod_muted_word mw;
    memset(&mw, 0, sizeof(mw));
    mw.value = (char *)value;
    mw.actor_target = (char *)json_str(
        cJSON_GetObjectItemCaseSensitive(word, "actorTarget"));
    mw.expires_at = (char *)json_str(
        cJSON_GetObjectItemCaseSensitive(word, "expiresAt"));
    cJSON *targets = cJSON_GetObjectItemCaseSensitive(word, "targets");
    if (cJSON_IsArray(targets)) {
        cJSON *t;
        cJSON_ArrayForEach(t, targets) {
            const char *ts = json_str(t);
            if (!ts) continue;
            if (strcmp(ts, "content") == 0) mw.targets_content = 1;
            else if (strcmp(ts, "tag") == 0) mw.targets_tag = 1;
        }
    }
    return json_muted_word_push(&p->muted_words, &p->muted_word_count,
                                &p->muted_word_cap, &mw);
}

/* Parse a single preference item (by its $type). */
static wf_status parse_pref_item(wf_mod_prefs *p, const cJSON *item) {
    const char *type = json_str(cJSON_GetObjectItemCaseSensitive(item, "$type"));
    if (!type) return WF_OK;

    if (str_ends_with(type, "#adultContentPref")) {
        cJSON *enabled = cJSON_GetObjectItemCaseSensitive(item, "enabled");
        if (cJSON_IsBool(enabled)) {
            p->adult_content_enabled = cJSON_IsTrue(enabled) ? 1 : 0;
        }
        return WF_OK;
    }
    if (str_ends_with(type, "#labelersPref")) {
        cJSON *labelers = cJSON_GetObjectItemCaseSensitive(item, "labelers");
        if (cJSON_IsArray(labelers)) {
            cJSON *l;
            cJSON_ArrayForEach(l, labelers) {
                const char *did = json_str(cJSON_GetObjectItemCaseSensitive(l, "did"));
                wf_status st = json_labeler_push(&p->labelers, &p->labeler_count,
                                                 &p->labeler_cap, did);
                if (st != WF_OK) return st;
            }
        }
        return WF_OK;
    }
    if (str_ends_with(type, "#contentLabelPrefs")) {
        cJSON *values = cJSON_GetObjectItemCaseSensitive(item, "values");
        if (cJSON_IsArray(values)) {
            cJSON *v;
            cJSON_ArrayForEach(v, values) {
                const char *label = json_str(cJSON_GetObjectItemCaseSensitive(v, "label"));
                const char *vis = json_str(cJSON_GetObjectItemCaseSensitive(v, "visibility"));
                wf_status st = apply_global_label(p, label, vis);
                if (st != WF_OK) return st;
            }
        }
        return WF_OK;
    }
    if (str_ends_with(type, "#hiddenPostsPref")) {
        cJSON *posts = cJSON_GetObjectItemCaseSensitive(item, "posts");
        if (cJSON_IsArray(posts)) {
            cJSON *pp;
            cJSON_ArrayForEach(pp, posts) {
                wf_status st = json_strarr_push(&p->hidden_posts,
                                                &p->hidden_post_count,
                                                &p->hidden_post_cap,
                                                json_str(pp));
                if (st != WF_OK) return st;
            }
        }
        return WF_OK;
    }
    if (str_ends_with(type, "#mutedWordsPref")) {
        cJSON *words = cJSON_GetObjectItemCaseSensitive(item, "words");
        if (cJSON_IsArray(words)) {
            cJSON *w;
            cJSON_ArrayForEach(w, words) {
                wf_status st = apply_muted_word(p, w);
                if (st != WF_OK) return st;
            }
        }
        return WF_OK;
    }
    if (str_ends_with(type, "#moderationPrefs")) {
        return parse_moderation_prefs(p, item);
    }
    return WF_OK;
}

/* Parse a bundled moderationPrefs object (or the sub-object of one). */
static wf_status parse_moderation_prefs(wf_mod_prefs *p, const cJSON *mp) {
    cJSON *adult = cJSON_GetObjectItemCaseSensitive(mp, "adultContentEnabled");
    if (cJSON_IsBool(adult)) {
        p->adult_content_enabled = cJSON_IsTrue(adult) ? 1 : 0;
    }
    cJSON *labelers = cJSON_GetObjectItemCaseSensitive(mp, "labelers");
    if (cJSON_IsArray(labelers)) {
        cJSON *l;
        cJSON_ArrayForEach(l, labelers) {
            const char *did = json_str(cJSON_GetObjectItemCaseSensitive(l, "did"));
            wf_status st = json_labeler_push(&p->labelers, &p->labeler_count,
                                             &p->labeler_cap, did);
            if (st != WF_OK) return st;
        }
    }
    cJSON *labels = cJSON_GetObjectItemCaseSensitive(mp, "labels");
    if (cJSON_IsArray(labels)) {
        cJSON *v;
        cJSON_ArrayForEach(v, labels) {
            const char *label = json_str(cJSON_GetObjectItemCaseSensitive(v, "label"));
            const char *vis = json_str(cJSON_GetObjectItemCaseSensitive(v, "visibility"));
            wf_status st = apply_global_label(p, label, vis);
            if (st != WF_OK) return st;
        }
    }
    cJSON *hidden = cJSON_GetObjectItemCaseSensitive(mp, "hiddenPosts");
    if (cJSON_IsArray(hidden)) {
        cJSON *pp;
        cJSON_ArrayForEach(pp, hidden) {
            wf_status st = json_strarr_push(&p->hidden_posts, &p->hidden_post_count,
                                            &p->hidden_post_cap, json_str(pp));
            if (st != WF_OK) return st;
        }
    }
    cJSON *words = cJSON_GetObjectItemCaseSensitive(mp, "mutedWords");
    if (cJSON_IsArray(words)) {
        cJSON *w;
        cJSON_ArrayForEach(w, words) {
            wf_status st = apply_muted_word(p, w);
            if (st != WF_OK) return st;
        }
    }
    return WF_OK;
}

wf_status wf_mod_prefs_from_json(wf_mod_prefs *out, const char *json) {
    if (!out || !json) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        memset(out, 0, sizeof(*out));
        return WF_ERR_INVALID_ARG;
    }

    wf_status st = WF_OK;

    /* Resolve to the preferences collection. */
    cJSON *items = root;
    cJSON *prefs_field = cJSON_GetObjectItemCaseSensitive(root, "preferences");
    if (cJSON_IsArray(prefs_field)) {
        items = prefs_field;
    }

    if (cJSON_IsArray(items)) {
        cJSON *item;
        cJSON_ArrayForEach(item, items) {
            st = parse_pref_item(out, item);
            if (st != WF_OK) goto done;
        }
    } else if (cJSON_IsObject(items)) {
        /* Could be a flat moderationPrefs object. */
        st = parse_moderation_prefs(out, items);
    }

done:
    cJSON_Delete(root);
    if (st != WF_OK) wf_mod_prefs_free(out);
    return st;
}

void wf_mod_prefs_free(wf_mod_prefs *prefs) {
    if (!prefs) return;
    for (size_t i = 0; i < prefs->global_label_count; i++) {
        free(prefs->global_label_identifiers[i]);
    }
    free(prefs->global_label_identifiers);
    free(prefs->global_label_prefs);

    for (size_t i = 0; i < prefs->labeler_count; i++) {
        free(prefs->labelers[i].did);
        free(prefs->labelers[i].label_prefs);
        for (size_t j = 0; j < prefs->labelers[i].label_count; j++) {
            free(prefs->labelers[i].label_identifiers[j]);
        }
        free(prefs->labelers[i].label_identifiers);
    }
    free(prefs->labelers);

    for (size_t i = 0; i < prefs->muted_word_count; i++) {
        free(prefs->muted_words[i].value);
        free(prefs->muted_words[i].actor_target);
        free(prefs->muted_words[i].expires_at);
    }
    free(prefs->muted_words);

    for (size_t i = 0; i < prefs->hidden_post_count; i++) {
        free(prefs->hidden_posts[i]);
    }
    free(prefs->hidden_posts);

    memset(prefs, 0, sizeof(*prefs));
}

wf_status wf_mod_label_defs_from_labeler(const char *labeler_did,
                                         const char *json,
                                         wf_mod_label_def **out,
                                         size_t *out_count) {
    if (!out || !out_count || !json) return WF_ERR_INVALID_ARG;
    *out = NULL;
    *out_count = 0;

    cJSON *root = cJSON_Parse(json);
    if (!root) return WF_ERR_INVALID_ARG;

    wf_status st = WF_OK;
    cJSON *defs = cJSON_GetObjectItemCaseSensitive(root, "labelValueDefinitions");
    if (!defs) {
        cJSON *policies = cJSON_GetObjectItemCaseSensitive(root, "policies");
        if (cJSON_IsObject(policies)) {
            defs = cJSON_GetObjectItemCaseSensitive(policies, "labelValueDefinitions");
        }
    }
    if (!cJSON_IsArray(defs)) {
        cJSON_Delete(root);
        return WF_ERR_INVALID_ARG;
    }

    size_t cap = 0;
    wf_mod_label_def *arr = NULL;
    cJSON *d;
    cJSON_ArrayForEach(d, defs) {
        const char *id = json_str(cJSON_GetObjectItemCaseSensitive(d, "identifier"));
        const char *blurs = json_str(cJSON_GetObjectItemCaseSensitive(d, "blurs"));
        const char *severity = json_str(cJSON_GetObjectItemCaseSensitive(d, "severity"));
        const char *default_setting = json_str(
            cJSON_GetObjectItemCaseSensitive(d, "defaultSetting"));
        cJSON *adult = cJSON_GetObjectItemCaseSensitive(d, "adultOnly");
        int adult_only = cJSON_IsTrue(adult) ? 1 : 0;
        if (!id) continue;
        if (*out_count >= cap) {
            size_t nc = (cap == 0) ? 4 : cap * 2;
            wf_mod_label_def *na = realloc(arr, nc * sizeof(wf_mod_label_def));
            if (!na) { st = WF_ERR_ALLOC; goto done; }
            arr = na;
            cap = nc;
        }
        wf_mod_label_def *def = &arr[*out_count];
        memset(def, 0, sizeof(*def));
        st = wf_mod_interpret_label_def(def, id, labeler_did,
                                        blurs, severity, adult_only,
                                        default_setting);
        if (st != WF_OK) goto done;
        (*out_count)++;
    }

done:
    cJSON_Delete(root);
    if (st != WF_OK) {
        if (arr) {
            for (size_t i = 0; i < *out_count; i++) wf_mod_label_def_free(&arr[i]);
            free(arr);
        }
        *out = NULL;
        *out_count = 0;
    } else {
        *out = arr;
    }
    return st;
}

void wf_mod_label_defs_free(wf_mod_label_def *defs, size_t count) {
    if (!defs) return;
    for (size_t i = 0; i < count; i++) {
        wf_mod_label_def_free(&defs[i]);
    }
    free(defs);
}

wf_status wf_mod_labels_from_json(wf_mod_label **out,
                                  size_t *out_count,
                                  const char *json) {
    if (!out || !out_count || !json) return WF_ERR_INVALID_ARG;
    *out = NULL;
    *out_count = 0;

    cJSON *root = cJSON_Parse(json);
    if (!root) return WF_ERR_INVALID_ARG;

    wf_status st = WF_OK;
    cJSON *arr_json = root;
    cJSON *labels_field = cJSON_GetObjectItemCaseSensitive(root, "labels");
    if (cJSON_IsArray(labels_field)) {
        arr_json = labels_field;
    }
    if (!cJSON_IsArray(arr_json)) {
        cJSON_Delete(root);
        return WF_ERR_INVALID_ARG;
    }

    size_t cap = 0;
    wf_mod_label *arr = NULL;
    cJSON *l;
    cJSON_ArrayForEach(l, arr_json) {
        const char *src = json_str(cJSON_GetObjectItemCaseSensitive(l, "src"));
        const char *uri = json_str(cJSON_GetObjectItemCaseSensitive(l, "uri"));
        const char *val = json_str(cJSON_GetObjectItemCaseSensitive(l, "val"));
        const char *cts = json_str(cJSON_GetObjectItemCaseSensitive(l, "cts"));
        if (!val) continue;
        if (*out_count >= cap) {
            size_t nc = (cap == 0) ? 4 : cap * 2;
            wf_mod_label *na = realloc(arr, nc * sizeof(wf_mod_label));
            if (!na) { st = WF_ERR_ALLOC; goto done; }
            arr = na;
            cap = nc;
        }
        wf_mod_label *lab = &arr[*out_count];
        memset(lab, 0, sizeof(*lab));
        lab->src = dup_str(src);
        lab->uri = dup_str(uri);
        lab->val = dup_str(val);
        lab->cts = dup_str(cts);
        if (!lab->val || (src && !lab->src) || (uri && !lab->uri) ||
            (cts && !lab->cts)) {
            st = WF_ERR_ALLOC;
            goto done;
        }
        (*out_count)++;
    }

done:
    cJSON_Delete(root);
    if (st != WF_OK) {
        if (arr) {
            for (size_t i = 0; i < *out_count; i++) {
                free(arr[i].src); free(arr[i].uri);
                free(arr[i].val); free(arr[i].cts);
            }
            free(arr);
        }
        *out = NULL;
        *out_count = 0;
    } else {
        *out = arr;
    }
    return st;
}

void wf_mod_labels_free(wf_mod_label *labels, size_t count) {
    if (!labels) return;
    for (size_t i = 0; i < count; i++) {
        free(labels[i].src);
        free(labels[i].uri);
        free(labels[i].val);
        free(labels[i].cts);
    }
    free(labels);
}
