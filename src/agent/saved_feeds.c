#include "wolfram/saved_feeds.h"

#include "wolfram/feed_gen_typed.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What app.bsky.feed.getFeedGenerators accepts in one call. */
#define WF_SAVED_FEED_LOOKUP_MAX 25

/* The record key of an AT-URI: what follows the last slash. */
static const char *record_key(const char *uri) {
    const char *slash = strrchr(uri, '/');
    return slash ? slash + 1 : uri;
}

static void set_feed(wf_saved_feed *f, const char *uri) {
    snprintf(f->uri, sizeof f->uri, "%s", uri);
    snprintf(f->name, sizeof f->name, "%s", record_key(uri));
}

wf_status wf_saved_feeds_parse(const char *preferences_json, wf_saved_feed *out,
                               size_t cap, size_t *count) {
    if (count) *count = 0;
    if (!preferences_json || !out || !count) return WF_ERR_INVALID_ARG;

    cJSON *prefs = cJSON_Parse(preferences_json);
    if (!cJSON_IsArray(prefs)) {
        cJSON_Delete(prefs);
        return WF_ERR_PARSE;
    }

    size_t n = 0;
    const cJSON *pref = NULL;
    cJSON_ArrayForEach(pref, prefs) {
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(pref, "$type");
        if (!cJSON_IsString(type) ||
            !strstr(type->valuestring, "savedFeedsPrefV2"))
            continue;
        const cJSON *it = NULL;
        cJSON_ArrayForEach(it,
                           cJSON_GetObjectItemCaseSensitive(pref, "items")) {
            const cJSON *kind = cJSON_GetObjectItemCaseSensitive(it, "type");
            const cJSON *value = cJSON_GetObjectItemCaseSensitive(it, "value");
            if (n < cap && cJSON_IsString(kind) && cJSON_IsString(value) &&
                strcmp(kind->valuestring, "feed") == 0 &&
                value->valuestring[0]) {
                set_feed(&out[n++], value->valuestring);
            }
        }
    }
    if (n == 0) {
        /* Older accounts only have the V1 list. */
        cJSON_ArrayForEach(pref, prefs) {
            const cJSON *type = cJSON_GetObjectItemCaseSensitive(pref, "$type");
            if (!cJSON_IsString(type) ||
                !strstr(type->valuestring, "savedFeedsPref"))
                continue;
            const cJSON *it = NULL;
            cJSON_ArrayForEach(
                it, cJSON_GetObjectItemCaseSensitive(pref, "saved")) {
                if (n < cap && cJSON_IsString(it) && it->valuestring[0])
                    set_feed(&out[n++], it->valuestring);
            }
        }
    }
    cJSON_Delete(prefs);
    *count = n;
    return WF_OK;
}

wf_status wf_agent_get_saved_feeds(wf_agent *agent, wf_saved_feed *out,
                                   size_t cap, size_t *count) {
    if (count) *count = 0;
    if (!agent || !out || !count) return WF_ERR_INVALID_ARG;

    char *json = NULL;
    wf_status st = wf_agent_get_preferences(agent, &json);
    if (st != WF_OK) return st;
    if (!json) return WF_ERR_PARSE;
    st = wf_saved_feeds_parse(json, out, cap, count);
    free(json);
    if (st != WF_OK || *count == 0) return st;

    /* getFeedGenerators takes at most 25 feeds, so ask in pieces. A piece that
     * fails leaves its feeds named by their record keys. */
    for (size_t first = 0; first < *count; first += WF_SAVED_FEED_LOOKUP_MAX) {
        const char *uris[WF_SAVED_FEED_LOOKUP_MAX];
        size_t asked = *count - first;
        if (asked > WF_SAVED_FEED_LOOKUP_MAX) asked = WF_SAVED_FEED_LOOKUP_MAX;
        for (size_t i = 0; i < asked; i++) uris[i] = out[first + i].uri;

        wf_feedgen_generator_list gens;
        memset(&gens, 0, sizeof gens);
        if (wf_feedgen_get_feed_generators_typed(agent, uris, asked, &gens) ==
            WF_OK) {
            for (size_t i = first; i < first + asked; i++) {
                for (size_t g = 0; g < gens.generator_count; g++) {
                    const wf_feedgen_generator_view *v = &gens.generators[g];
                    if (v->uri && strcmp(v->uri, out[i].uri) == 0 &&
                        v->display_name && v->display_name[0]) {
                        snprintf(out[i].name, sizeof out[i].name, "%s",
                                 v->display_name);
                        break;
                    }
                }
            }
        }
        wf_feedgen_generator_list_free(&gens);
    }
    return WF_OK;
}
