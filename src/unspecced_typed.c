/*
 * unspecced_typed.c — typed parsers for app.bsky.unspecced endpoints.
 *
 * Mirrors feed_typed.c / feedgen_typed.c: static strdup/set_string/reset
 * helpers, ownership via cJSON_DetachItemFromObject, and full cleanup on the
 * first error.
 */

#include "wolfram/unspecced_typed.h"

#include "wolfram/atproto_lex.h"
#include "agent/_internal.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_us_strdup(const char *s) {
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

static wf_status wf_us_set_string(char **dst, const char *src) {
    char *copy = wf_us_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

/* Parse an integer from a cJSON number, tracking whether it was present. */
static wf_status wf_us_parse_int(cJSON *num, int *dst, int *has) {
    if (cJSON_IsNumber(num)) {
        double v = num->valuedouble;
        if (v > 2147483647.0) {
            *dst = 2147483647;
        } else if (v < -2147483648.0) {
            *dst = -2147483648;
        } else {
            *dst = (int)v;
        }
        *has = 1;
    }
    return WF_OK;
}

static void wf_us_profile_view_reset(wf_agent_profile_view *p) {
    if (!p) {
        return;
    }
    free(p->did);
    free(p->handle);
    free(p->display_name);
    free(p->avatar);
    memset(p, 0, sizeof(*p));
}

static wf_status wf_us_parse_profile_view(wf_agent_profile_view *author, cJSON *obj) {
    wf_status status = WF_OK;
    if (!cJSON_IsObject(obj)) {
        return WF_OK;
    }
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(obj, "handle");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");
    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_us_set_string(&author->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
        status = wf_us_set_string(&author->handle, handle->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_us_set_string(&author->display_name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        status = wf_us_set_string(&author->avatar, avatar->valuestring);
    }
    return status;
}

/* Parse `arr` (a cJSON array of strings) into an owned `char **`/`count`. */
static wf_status wf_us_parse_string_array(cJSON *arr, char ***dst, size_t *count) {
    *dst = NULL;
    *count = 0;
    size_t n = (size_t)cJSON_GetArraySize(arr);
    if (n == 0) {
        return WF_OK;
    }
    char **strings = (char **)calloc(n, sizeof(*strings));
    if (!strings) {
        return WF_ERR_ALLOC;
    }
    wf_status status = WF_OK;
    for (size_t i = 0; i < n; ++i) {
        cJSON *item = cJSON_GetArrayItem(arr, (int)i);
        if (cJSON_IsString(item) && item->valuestring) {
            strings[i] = wf_us_strdup(item->valuestring);
            if (!strings[i]) {
                status = WF_ERR_ALLOC;
            }
        } else {
            strings[i] = NULL;
        }
        if (status != WF_OK) {
            break;
        }
    }
    if (status != WF_OK) {
        for (size_t i = 0; i < n; ++i) {
            free(strings[i]);
        }
        free(strings);
        return status;
    }
    *dst = strings;
    *count = n;
    return WF_OK;
}

/* ----------------------------- Trending topics ---------------------------- */

static void wf_us_trending_topic_reset(wf_agent_trending_topic *t) {
    if (!t) {
        return;
    }
    free(t->topic);
    free(t->display_name);
    free(t->description);
    free(t->link);
    memset(t, 0, sizeof(*t));
}

static void wf_us_trending_topics_reset_list(wf_agent_trending_topic *items,
                                             size_t count) {
    for (size_t i = 0; i < count; ++i) {
        wf_us_trending_topic_reset(&items[i]);
    }
    free(items);
}

static wf_status wf_us_parse_trending_topic(wf_agent_trending_topic *t, cJSON *obj) {
    if (!cJSON_IsObject(obj)) {
        return WF_ERR_PARSE;
    }
    wf_status status = WF_OK;
    cJSON *topic = cJSON_GetObjectItemCaseSensitive(obj, "topic");
    cJSON *link = cJSON_GetObjectItemCaseSensitive(obj, "link");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
    cJSON *desc = cJSON_GetObjectItemCaseSensitive(obj, "description");
    if (cJSON_IsString(topic) && topic->valuestring) {
        status = wf_us_set_string(&t->topic, topic->valuestring);
    } else {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK && cJSON_IsString(link) && link->valuestring) {
        status = wf_us_set_string(&t->link, link->valuestring);
    } else if (status == WF_OK) {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_us_set_string(&t->display_name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(desc) && desc->valuestring) {
        status = wf_us_set_string(&t->description, desc->valuestring);
    }
    return status;
}

static wf_status wf_us_parse_trending_array(const char *key, cJSON *root,
                                            wf_agent_trending_topic **out_items,
                                            size_t *out_count) {
    *out_items = NULL;
    *out_count = 0;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsArray(arr)) {
        return WF_ERR_PARSE;
    }
    size_t n = (size_t)cJSON_GetArraySize(arr);
    if (n == 0) {
        return WF_OK;
    }
    wf_agent_trending_topic *items =
        (wf_agent_trending_topic *)calloc(n, sizeof(*items));
    if (!items) {
        return WF_ERR_ALLOC;
    }
    wf_status status = WF_OK;
    for (size_t i = 0; i < n; ++i) {
        cJSON *item = cJSON_GetArrayItem(arr, (int)i);
        status = wf_us_parse_trending_topic(&items[i], item);
        if (status != WF_OK) {
            wf_us_trending_topics_reset_list(items, i);
            return status;
        }
    }
    *out_items = items;
    *out_count = n;
    return WF_OK;
}

wf_status wf_agent_parse_trending_topics(const char *json, size_t json_len,
                                         wf_agent_trending_topics *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = wf_us_parse_trending_array("topics", root,
                                                  &out->topics, &out->topic_count);
    if (status == WF_OK) {
        status = wf_us_parse_trending_array("suggested", root,
                                            &out->suggested, &out->suggested_count);
    }

    if (status != WF_OK) {
        wf_us_trending_topics_reset_list(out->topics, out->topic_count);
        wf_us_trending_topics_reset_list(out->suggested, out->suggested_count);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_trending_topics_free(wf_agent_trending_topics *list) {
    if (!list) {
        return;
    }
    wf_us_trending_topics_reset_list(list->topics, list->topic_count);
    wf_us_trending_topics_reset_list(list->suggested, list->suggested_count);
    memset(list, 0, sizeof(*list));
}

/* ---------------------------- Tagged suggestions -------------------------- */

static void wf_us_tagged_suggestion_reset(wf_agent_tagged_suggestion *s) {
    if (!s) {
        return;
    }
    free(s->tag);
    free(s->subject_type);
    free(s->subject);
    memset(s, 0, sizeof(*s));
}

wf_status wf_agent_parse_tagged_suggestions(const char *json, size_t json_len,
                                            wf_agent_tagged_suggestions *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "suggestions");
    if (!cJSON_IsArray(arr)) {
        status = WF_ERR_PARSE;
    } else {
        size_t n = (size_t)cJSON_GetArraySize(arr);
        if (n > 0) {
            wf_agent_tagged_suggestion *items =
                (wf_agent_tagged_suggestion *)calloc(n, sizeof(*items));
            if (!items) {
                status = WF_ERR_ALLOC;
            } else {
                for (size_t i = 0; i < n && status == WF_OK; ++i) {
                    cJSON *item = cJSON_GetArrayItem(arr, (int)i);
                    if (!cJSON_IsObject(item)) {
                        status = WF_ERR_PARSE;
                        break;
                    }
                    cJSON *tag = cJSON_GetObjectItemCaseSensitive(item, "tag");
                    cJSON *stype = cJSON_GetObjectItemCaseSensitive(item, "subjectType");
                    cJSON *subject = cJSON_GetObjectItemCaseSensitive(item, "subject");
                    if (cJSON_IsString(tag) && tag->valuestring) {
                        status = wf_us_set_string(&items[i].tag, tag->valuestring);
                    } else {
                        status = WF_ERR_PARSE;
                    }
                    if (status == WF_OK && cJSON_IsString(stype) && stype->valuestring) {
                        status = wf_us_set_string(&items[i].subject_type, stype->valuestring);
                    }
                    if (status == WF_OK && cJSON_IsString(subject) && subject->valuestring) {
                        status = wf_us_set_string(&items[i].subject, subject->valuestring);
                    }
                    if (status != WF_OK) {
                        for (size_t j = 0; j <= i; ++j) {
                            wf_us_tagged_suggestion_reset(&items[j]);
                        }
                        free(items);
                        items = NULL;
                        n = 0;
                    }
                }
                if (status == WF_OK) {
                    out->items = items;
                    out->count = n;
                }
            }
        }
    }

    if (status != WF_OK) {
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_tagged_suggestions_free(wf_agent_tagged_suggestions *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; ++i) {
        wf_us_tagged_suggestion_reset(&list->items[i]);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

/* ---------------------------- Suggestions skeleton ------------------------ */

static void wf_us_skeleton_actor_reset(wf_agent_skeleton_actor *a) {
    if (!a) {
        return;
    }
    free(a->did);
    memset(a, 0, sizeof(*a));
}

static void wf_us_skeleton_actors_reset(wf_agent_skeleton_actor *items, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        wf_us_skeleton_actor_reset(&items[i]);
    }
    free(items);
}

wf_status wf_agent_parse_suggestions_skeleton(const char *json, size_t json_len,
                                              wf_agent_suggestions_skeleton *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "actors");
    if (!cJSON_IsArray(arr)) {
        status = WF_ERR_PARSE;
    } else {
        size_t n = (size_t)cJSON_GetArraySize(arr);
        if (n > 0) {
            wf_agent_skeleton_actor *items =
                (wf_agent_skeleton_actor *)calloc(n, sizeof(*items));
            if (!items) {
                status = WF_ERR_ALLOC;
            } else {
                for (size_t i = 0; i < n && status == WF_OK; ++i) {
                    cJSON *item = cJSON_GetArrayItem(arr, (int)i);
                    if (!cJSON_IsObject(item)) {
                        status = WF_ERR_PARSE;
                        break;
                    }
                    cJSON *did = cJSON_GetObjectItemCaseSensitive(item, "did");
                    if (cJSON_IsString(did) && did->valuestring) {
                        status = wf_us_set_string(&items[i].did, did->valuestring);
                    } else {
                        status = WF_ERR_PARSE;
                    }
                }
                if (status == WF_OK) {
                    out->actors = items;
                    out->actor_count = n;
                } else {
                    wf_us_skeleton_actors_reset(items, n);
                }
            }
        }
    }

    if (status == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_us_set_string(&out->cursor, cursor->valuestring);
        }
    }
    if (status == WF_OK) {
        cJSON *rel = cJSON_GetObjectItemCaseSensitive(root, "relativeToDid");
        if (cJSON_IsString(rel) && rel->valuestring) {
            status = wf_us_set_string(&out->relative_to_did, rel->valuestring);
        }
    }
    if (status == WF_OK) {
        cJSON *rec = cJSON_GetObjectItemCaseSensitive(root, "recIdStr");
        if (cJSON_IsString(rec) && rec->valuestring) {
            status = wf_us_set_string(&out->rec_id_str, rec->valuestring);
        }
    }

    if (status != WF_OK) {
        wf_us_skeleton_actors_reset(out->actors, out->actor_count);
        free(out->cursor);
        free(out->relative_to_did);
        free(out->rec_id_str);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_suggestions_skeleton_free(wf_agent_suggestions_skeleton *list) {
    if (!list) {
        return;
    }
    wf_us_skeleton_actors_reset(list->actors, list->actor_count);
    free(list->cursor);
    free(list->relative_to_did);
    free(list->rec_id_str);
    memset(list, 0, sizeof(*list));
}

/* --------------------------------- Config --------------------------------- */

static void wf_us_live_now_reset(wf_agent_live_now_config *c) {
    if (!c) {
        return;
    }
    free(c->did);
    for (size_t i = 0; i < c->domain_count; ++i) {
        free(c->domains[i]);
    }
    free(c->domains);
    memset(c, 0, sizeof(*c));
}

static void wf_us_live_now_list_reset(wf_agent_live_now_config *items, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        wf_us_live_now_reset(&items[i]);
    }
    free(items);
}

wf_status wf_agent_parse_config(const char *json, size_t json_len,
                                wf_agent_unspecced_config *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *check = cJSON_GetObjectItemCaseSensitive(root, "checkEmailConfirmed");
    if (cJSON_IsBool(check)) {
        out->has_check_email_confirmed = 1;
        out->check_email_confirmed = cJSON_IsTrue(check) ? 1 : 0;
    }

    cJSON *live = cJSON_GetObjectItemCaseSensitive(root, "liveNow");
    if (cJSON_IsArray(live)) {
        size_t n = (size_t)cJSON_GetArraySize(live);
        if (n > 0) {
            wf_agent_live_now_config *items =
                (wf_agent_live_now_config *)calloc(n, sizeof(*items));
            if (!items) {
                status = WF_ERR_ALLOC;
            } else {
                for (size_t i = 0; i < n && status == WF_OK; ++i) {
                    cJSON *item = cJSON_GetArrayItem(live, (int)i);
                    if (!cJSON_IsObject(item)) {
                        status = WF_ERR_PARSE;
                        break;
                    }
                    cJSON *did = cJSON_GetObjectItemCaseSensitive(item, "did");
                    cJSON *domains = cJSON_GetObjectItemCaseSensitive(item, "domains");
                    if (cJSON_IsString(did) && did->valuestring) {
                        status = wf_us_set_string(&items[i].did, did->valuestring);
                    } else {
                        status = WF_ERR_PARSE;
                    }
                    if (status == WF_OK && cJSON_IsArray(domains)) {
                        status = wf_us_parse_string_array(domains, &items[i].domains,
                                                          &items[i].domain_count);
                    }
                }
                if (status == WF_OK) {
                    out->live_now = items;
                    out->live_now_count = n;
                } else {
                    wf_us_live_now_list_reset(items, n);
                }
            }
        }
    } else if (!cJSON_IsNull(live)) {
        /* liveNow may be omitted; only an explicitly non-array (non-null) value
         * is a parse error. */
        if (live) {
            status = WF_ERR_PARSE;
        }
    }

    if (status != WF_OK) {
        wf_us_live_now_list_reset(out->live_now, out->live_now_count);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_unspecced_config_free(wf_agent_unspecced_config *cfg) {
    if (!cfg) {
        return;
    }
    wf_us_live_now_list_reset(cfg->live_now, cfg->live_now_count);
    memset(cfg, 0, sizeof(*cfg));
}

/* ---------------------------- Age assurance state ------------------------- */

wf_status wf_agent_parse_age_assurance_state(const char *json, size_t json_len,
                                             wf_agent_age_assurance_state *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *status_field = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (cJSON_IsString(status_field) && status_field->valuestring) {
        status = wf_us_set_string(&out->status, status_field->valuestring);
    }
    if (status == WF_OK) {
        cJSON *last = cJSON_GetObjectItemCaseSensitive(root, "lastInitiatedAt");
        if (cJSON_IsString(last) && last->valuestring) {
            status = wf_us_set_string(&out->last_initiated_at, last->valuestring);
            if (status == WF_OK) {
                out->has_last_initiated_at = 1;
            }
        }
    }

    if (status != WF_OK) {
        free(out->status);
        free(out->last_initiated_at);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_age_assurance_state_free(wf_agent_age_assurance_state *st) {
    if (!st) {
        return;
    }
    free(st->status);
    free(st->last_initiated_at);
    memset(st, 0, sizeof(*st));
}

/* --------------------- Onboarding starter packs (full) -------------------- */

static void wf_us_starter_pack_view_reset(wf_agent_starter_pack_view *v) {
    if (!v) {
        return;
    }
    free(v->uri);
    free(v->cid);
    if (v->record) {
        cJSON_Delete(v->record);
    }
    wf_us_profile_view_reset(&v->creator);
    free(v->indexed_at);
    memset(v, 0, sizeof(*v));
}

static void wf_us_starter_pack_view_list_reset(wf_agent_starter_pack_view *items,
                                               size_t n) {
    for (size_t i = 0; i < n; ++i) {
        wf_us_starter_pack_view_reset(&items[i]);
    }
    free(items);
}

wf_status wf_agent_parse_onboarding_starter_packs(const char *json, size_t json_len,
                                                  wf_agent_starter_pack_view_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "starterPacks");
    if (!cJSON_IsArray(arr)) {
        status = WF_ERR_PARSE;
    } else {
        size_t n = (size_t)cJSON_GetArraySize(arr);
        if (n > 0) {
            wf_agent_starter_pack_view *items =
                (wf_agent_starter_pack_view *)calloc(n, sizeof(*items));
            if (!items) {
                status = WF_ERR_ALLOC;
            } else {
                for (size_t i = 0; i < n && status == WF_OK; ++i) {
                    cJSON *item = cJSON_GetArrayItem(arr, (int)i);
                    if (!cJSON_IsObject(item)) {
                        status = WF_ERR_PARSE;
                        break;
                    }
                    cJSON *uri = cJSON_GetObjectItemCaseSensitive(item, "uri");
                    cJSON *cid = cJSON_GetObjectItemCaseSensitive(item, "cid");
                    cJSON *indexed = cJSON_GetObjectItemCaseSensitive(item, "indexedAt");
                    cJSON *creator = cJSON_GetObjectItemCaseSensitive(item, "creator");
                    cJSON *week = cJSON_GetObjectItemCaseSensitive(item, "joinedWeekCount");
                    cJSON *all = cJSON_GetObjectItemCaseSensitive(item, "joinedAllTimeCount");

                    if (cJSON_IsString(uri) && uri->valuestring) {
                        status = wf_us_set_string(&items[i].uri, uri->valuestring);
                    } else {
                        status = WF_ERR_PARSE;
                    }
                    if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring) {
                        status = wf_us_set_string(&items[i].cid, cid->valuestring);
                    }
                    if (status == WF_OK && cJSON_IsString(indexed) && indexed->valuestring) {
                        status = wf_us_set_string(&items[i].indexed_at, indexed->valuestring);
                    }
                    if (status == WF_OK && cJSON_IsObject(creator)) {
                        status = wf_us_parse_profile_view(&items[i].creator, creator);
                    }
                    if (status == WF_OK) {
                        status = wf_us_parse_int(week, &items[i].joined_week_count,
                                                 &items[i].has_joined_week_count);
                    }
                    if (status == WF_OK) {
                        status = wf_us_parse_int(all, &items[i].joined_all_time_count,
                                                 &items[i].has_joined_all_time_count);
                    }
                    if (status == WF_OK) {
                        cJSON *record = cJSON_DetachItemFromObject(item, "record");
                        if (record) {
                            items[i].record = record;
                        }
                    }
                }
                if (status == WF_OK) {
                    out->items = items;
                    out->count = n;
                } else {
                    wf_us_starter_pack_view_list_reset(items, n);
                }
            }
        }
    }

    if (status != WF_OK) {
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_starter_pack_view_list_free(wf_agent_starter_pack_view_list *list) {
    if (!list) {
        return;
    }
    wf_us_starter_pack_view_list_reset(list->items, list->count);
    memset(list, 0, sizeof(*list));
}

/* ------------------- Onboarding starter packs (skeleton) ------------------ */

wf_status wf_agent_parse_onboarding_starter_packs_skeleton(
    const char *json, size_t json_len, wf_agent_starter_pack_skeleton_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "starterPacks");
    if (!cJSON_IsArray(arr)) {
        status = WF_ERR_PARSE;
    } else {
        status = wf_us_parse_string_array(arr, &out->uris, &out->uri_count);
    }

    if (status != WF_OK) {
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_starter_pack_skeleton_list_free(wf_agent_starter_pack_skeleton_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->uri_count; ++i) {
        free(list->uris[i]);
    }
    free(list->uris);
    memset(list, 0, sizeof(*list));
}

/* ----------------------- Search starter packs (skeleton) ------------------ */

static void wf_us_search_starter_pack_reset(wf_agent_search_starter_pack *s) {
    if (!s) {
        return;
    }
    free(s->uri);
    memset(s, 0, sizeof(*s));
}

static void wf_us_search_starter_packs_reset(wf_agent_search_starter_pack *items,
                                             size_t n) {
    for (size_t i = 0; i < n; ++i) {
        wf_us_search_starter_pack_reset(&items[i]);
    }
    free(items);
}

wf_status wf_agent_parse_search_starter_packs(const char *json, size_t json_len,
                                              wf_agent_search_starter_packs_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "starterPacks");
    if (!cJSON_IsArray(arr)) {
        status = WF_ERR_PARSE;
    } else {
        size_t n = (size_t)cJSON_GetArraySize(arr);
        if (n > 0) {
            wf_agent_search_starter_pack *items =
                (wf_agent_search_starter_pack *)calloc(n, sizeof(*items));
            if (!items) {
                status = WF_ERR_ALLOC;
            } else {
                for (size_t i = 0; i < n && status == WF_OK; ++i) {
                    cJSON *item = cJSON_GetArrayItem(arr, (int)i);
                    if (!cJSON_IsObject(item)) {
                        status = WF_ERR_PARSE;
                        break;
                    }
                    cJSON *uri = cJSON_GetObjectItemCaseSensitive(item, "uri");
                    if (cJSON_IsString(uri) && uri->valuestring) {
                        status = wf_us_set_string(&items[i].uri, uri->valuestring);
                    } else {
                        status = WF_ERR_PARSE;
                    }
                }
                if (status == WF_OK) {
                    out->items = items;
                    out->count = n;
                } else {
                    wf_us_search_starter_packs_reset(items, n);
                }
            }
        }
    }

    if (status == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_us_set_string(&out->cursor, cursor->valuestring);
        }
    }
    if (status == WF_OK) {
        cJSON *hits = cJSON_GetObjectItemCaseSensitive(root, "hitsTotal");
        if (cJSON_IsNumber(hits)) {
            wf_us_parse_int(hits, &out->hits_total, &out->has_hits_total);
        }
    }

    if (status != WF_OK) {
        wf_us_search_starter_packs_reset(out->items, out->count);
        free(out->cursor);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_search_starter_packs_free(wf_agent_search_starter_packs_list *list) {
    if (!list) {
        return;
    }
    wf_us_search_starter_packs_reset(list->items, list->count);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

/* --------------------------- Typed agent wrappers ------------------------- */

wf_status wf_agent_get_trending_topics_typed(wf_agent *agent, const char *viewer,
                                             int limit,
                                             wf_agent_trending_topics *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_unspecced_get_trending_topics_main_params params = {0};
    if (viewer && viewer[0]) {
        params.has_viewer = true;
        params.viewer = viewer;
    }
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_app_bsky_unspecced_get_trending_topics_main_call(agent->client,
                                                               &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_agent_parse_trending_topics(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_tagged_suggestions_typed(wf_agent *agent,
                                                wf_agent_tagged_suggestions *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_unspecced_get_tagged_suggestions_main_params params = {0};
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_app_bsky_unspecced_get_tagged_suggestions_main_call(agent->client,
                                                                  &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_agent_parse_tagged_suggestions(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_suggestions_skeleton_typed(wf_agent *agent,
                                                  const char *viewer, int limit,
                                                  const char *cursor,
                                                  const char *relative_to_did,
                                                  wf_agent_suggestions_skeleton *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_unspecced_get_suggestions_skeleton_main_params params = {0};
    if (viewer && viewer[0]) {
        params.has_viewer = true;
        params.viewer = viewer;
    }
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }
    if (relative_to_did && relative_to_did[0]) {
        params.has_relative_to_did = true;
        params.relative_to_did = relative_to_did;
    }
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_app_bsky_unspecced_get_suggestions_skeleton_main_call(agent->client,
                                                                   &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_agent_parse_suggestions_skeleton(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_config_typed(wf_agent *agent,
                                    wf_agent_unspecced_config *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_app_bsky_unspecced_get_config_main_call(agent->client, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_agent_parse_config(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_age_assurance_state_typed(wf_agent *agent,
                                                 wf_agent_age_assurance_state *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_app_bsky_unspecced_get_age_assurance_state_main_call(agent->client,
                                                                   &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_agent_parse_age_assurance_state(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_onboarding_suggested_starter_packs_typed(
    wf_agent *agent, int limit, wf_agent_starter_pack_view_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_unspecced_get_onboarding_suggested_starter_packs_main_params params = {0};
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_app_bsky_unspecced_get_onboarding_suggested_starter_packs_main_call(
            agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_agent_parse_onboarding_starter_packs(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

/* getSuggestedStarterPacks returns the same "starterPacks" array of hydrated
 * graph.defs#starterPackView objects as getOnboardingSuggestedStarterPacks, so
 * the owned wf_agent_starter_pack_view_list parser is shared verbatim. */
wf_status wf_agent_get_suggested_starter_packs_typed(
    wf_agent *agent, int limit, wf_agent_starter_pack_view_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_unspecced_get_suggested_starter_packs_main_params params = {0};
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_app_bsky_unspecced_get_suggested_starter_packs_main_call(
            agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_agent_parse_onboarding_starter_packs(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_onboarding_suggested_starter_packs_skeleton_typed(
    wf_agent *agent, const char *viewer, int limit,
    wf_agent_starter_pack_skeleton_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_unspecced_get_onboarding_suggested_starter_packs_skeleton_main_params params = {0};
    if (viewer && viewer[0]) {
        params.has_viewer = true;
        params.viewer = viewer;
    }
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_app_bsky_unspecced_get_onboarding_suggested_starter_packs_skeleton_main_call(
            agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_agent_parse_onboarding_starter_packs_skeleton(res.body, res.body_len,
                                                              out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_search_starter_packs_typed(
    wf_agent *agent, const char *q, const char *viewer, int limit,
    const char *cursor, wf_agent_search_starter_packs_list *out) {
    if (!agent || !out || !q || !q[0]) {
        return WF_ERR_INVALID_ARG;
    }
    wf_lex_app_bsky_unspecced_search_starter_packs_skeleton_main_params params = {0};
    params.q = q;
    if (viewer && viewer[0]) {
        params.has_viewer = true;
        params.viewer = viewer;
    }
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }
    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status =
        wf_lex_app_bsky_unspecced_search_starter_packs_skeleton_main_call(
            agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_agent_parse_search_starter_packs(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

/* ================================================================== */
/* Unspecced — generated XRPC-level convenience wrappers              */
/* ================================================================== */

wf_status wf_unspecced_get_onboarding_suggested_users_skeleton(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_onboarding_suggested_users_skeleton_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_onboarding_suggested_users_skeleton_main_call(client, params, out);
}

wf_status wf_unspecced_get_onboarding_suggested_users_skeleton_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_onboarding_suggested_users_skeleton_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_onboarding_suggested_users_skeleton_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_popular_feed_generators(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_popular_feed_generators_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_popular_feed_generators_main_call(client, params, out);
}

wf_status wf_unspecced_get_popular_feed_generators_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_popular_feed_generators_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_popular_feed_generators_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_post_thread_other_v2(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_post_thread_other_v2_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_post_thread_other_v2_main_call(client, params, out);
}

wf_status wf_unspecced_get_post_thread_other_v2_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_post_thread_other_v2_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_post_thread_other_v2_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_post_thread_v2(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_post_thread_v2_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_post_thread_v2_main_call(client, params, out);
}

wf_status wf_unspecced_get_post_thread_v2_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_post_thread_v2_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_post_thread_v2_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_suggested_feeds(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_suggested_feeds_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_feeds_main_call(client, params, out);
}

wf_status wf_unspecced_get_suggested_feeds_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_suggested_feeds_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_feeds_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_suggested_feeds_skeleton(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_suggested_feeds_skeleton_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_feeds_skeleton_main_call(client, params, out);
}

wf_status wf_unspecced_get_suggested_feeds_skeleton_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_suggested_feeds_skeleton_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_feeds_skeleton_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_suggested_onboarding_users(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_suggested_onboarding_users_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_onboarding_users_main_call(client, params, out);
}

wf_status wf_unspecced_get_suggested_onboarding_users_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_suggested_onboarding_users_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_onboarding_users_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_suggested_starter_packs(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_suggested_starter_packs_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_starter_packs_main_call(client, params, out);
}

wf_status wf_unspecced_get_suggested_starter_packs_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_suggested_starter_packs_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_starter_packs_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_suggested_starter_packs_skeleton(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_suggested_starter_packs_skeleton_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_starter_packs_skeleton_main_call(client, params, out);
}

wf_status wf_unspecced_get_suggested_starter_packs_skeleton_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_suggested_starter_packs_skeleton_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_starter_packs_skeleton_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_suggested_users(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_suggested_users_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_users_main_call(client, params, out);
}

wf_status wf_unspecced_get_suggested_users_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_suggested_users_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_users_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_suggested_users_for_discover(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_suggested_users_for_discover_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_users_for_discover_main_call(client, params, out);
}

wf_status wf_unspecced_get_suggested_users_for_discover_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_suggested_users_for_discover_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_users_for_discover_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_suggested_users_for_discover_skeleton(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_suggested_users_for_discover_skeleton_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_users_for_discover_skeleton_main_call(client, params, out);
}

wf_status wf_unspecced_get_suggested_users_for_discover_skeleton_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_suggested_users_for_discover_skeleton_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_users_for_discover_skeleton_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_suggested_users_for_explore(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_suggested_users_for_explore_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_users_for_explore_main_call(client, params, out);
}

wf_status wf_unspecced_get_suggested_users_for_explore_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_suggested_users_for_explore_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_users_for_explore_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_suggested_users_for_explore_skeleton(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_suggested_users_for_explore_skeleton_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_users_for_explore_skeleton_main_call(client, params, out);
}

wf_status wf_unspecced_get_suggested_users_for_explore_skeleton_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_suggested_users_for_explore_skeleton_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_users_for_explore_skeleton_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_suggested_users_for_see_more(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_suggested_users_for_see_more_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_users_for_see_more_main_call(client, params, out);
}

wf_status wf_unspecced_get_suggested_users_for_see_more_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_suggested_users_for_see_more_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_users_for_see_more_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_suggested_users_for_see_more_skeleton(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_suggested_users_for_see_more_skeleton_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_users_for_see_more_skeleton_main_call(client, params, out);
}

wf_status wf_unspecced_get_suggested_users_for_see_more_skeleton_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_suggested_users_for_see_more_skeleton_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_users_for_see_more_skeleton_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_suggested_users_skeleton(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_suggested_users_skeleton_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_users_skeleton_main_call(client, params, out);
}

wf_status wf_unspecced_get_suggested_users_skeleton_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_suggested_users_skeleton_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_suggested_users_skeleton_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_trends(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_trends_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_trends_main_call(client, params, out);
}

wf_status wf_unspecced_get_trends_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_trends_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_trends_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_get_trends_skeleton(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_get_trends_skeleton_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_trends_skeleton_main_call(client, params, out);
}

wf_status wf_unspecced_get_trends_skeleton_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_get_trends_skeleton_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_get_trends_skeleton_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_init_age_assurance(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_init_age_assurance_main_input *input,
    wf_response *out) {
    if (!client || !input || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_init_age_assurance_main_call(client, input, out);
}

wf_status wf_unspecced_search_actors_skeleton(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_search_actors_skeleton_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_search_actors_skeleton_main_call(client, params, out);
}

wf_status wf_unspecced_search_actors_skeleton_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_search_actors_skeleton_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_search_actors_skeleton_main_output_decode_json(
        resp->body, resp->body_len, out);
}

wf_status wf_unspecced_search_posts_skeleton(
    wf_xrpc_client *client,
    const wf_lex_app_bsky_unspecced_search_posts_skeleton_main_params *params,
    wf_response *out) {
    if (!client || !params || !out) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_search_posts_skeleton_main_call(client, params, out);
}

wf_status wf_unspecced_search_posts_skeleton_parse(
    const wf_response *resp,
    wf_lex_app_bsky_unspecced_search_posts_skeleton_main_output **out) {
    if (!resp || !out) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (!resp->body) {
        return WF_ERR_INVALID_ARG;
    }
    return wf_lex_app_bsky_unspecced_search_posts_skeleton_main_output_decode_json(
        resp->body, resp->body_len, out);
}
