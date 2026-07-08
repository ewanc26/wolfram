/*
 * bookmark_typed.c — typed parser + agent wrappers for app.bsky.bookmark.
 *
 * Mirrors feed_typed.c: static strdup/set_string/reset helpers, ownership via
 * cJSON_DetachItemFromObject, and full cleanup on the first error.
 */

#include "wolfram/bookmark_typed.h"

#include "wolfram/atproto_lex.h"
#include "wolfram/syntax.h"

#include "agent/_internal.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_bookmark_strdup(const char *s) {
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

static wf_status wf_bookmark_set_string(char **dst, const char *src) {
    char *copy = wf_bookmark_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static void wf_bookmark_reset(wf_bookmark *b) {
    if (!b) {
        return;
    }
    free(b->uri);
    free(b->created_at);
    memset(b, 0, sizeof(*b));
}

static void wf_bookmark_list_reset(wf_bookmark_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; ++i) {
        wf_bookmark_reset(&list->items[i]);
    }
    free(list->items);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

wf_status wf_bookmark_parse_list(const char *json, size_t len,
                                 wf_bookmark_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "bookmarks");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_bookmark *items = NULL;
    if (count > 0) {
        items = (wf_bookmark *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        wf_bookmark *b = &items[i];
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }

        /* The wire format is app.bsky.bookmark.defs#bookmarkView: the record
         * at-uri lives under `subject.uri` and the timestamp under
         * `createdAt`. */
        cJSON *subject = cJSON_GetObjectItemCaseSensitive(obj, "subject");
        cJSON *uri = NULL;
        if (cJSON_IsObject(subject)) {
            uri = cJSON_GetObjectItemCaseSensitive(subject, "uri");
        }
        if (cJSON_IsString(uri) && uri->valuestring) {
            status = wf_bookmark_set_string(&b->uri, uri->valuestring);
        }
        if (status == WF_OK) {
            cJSON *created_at =
                cJSON_GetObjectItemCaseSensitive(obj, "createdAt");
            if (cJSON_IsString(created_at) && created_at->valuestring) {
                status = wf_bookmark_set_string(&b->created_at,
                                                created_at->valuestring);
            }
        }

        if (status != WF_OK) {
            wf_bookmark_reset(b);
        }
    }

    if (status == WF_OK) {
        out->items = items;
        out->count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_bookmark_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_bookmark_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_bookmark_list_free(wf_bookmark_list *list) {
    wf_bookmark_list_reset(list);
}

/* Resolve the CID of `post_uri` by fetching its record. createBookmark requires
 * both `uri` and `cid` on the wire; callers only pass the at-uri, so we look
 * the cid up via com.atproto.repo.getRecord. Returns an owned cid string (or
 * NULL on failure) via `out_cid`. */
static wf_status wf_bookmark_resolve_cid(wf_agent *agent, const char *post_uri,
                                         char **out_cid) {
    *out_cid = NULL;

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(post_uri, &parsed)) {
        return WF_ERR_INVALID_ARG;
    }
    const char *collection = parsed.collection;
    const char *rkey = parsed.record_key;
    wf_syntax_aturi_free(&parsed);

    wf_response res = {0};
    wf_status status =
        wf_agent_get_record(agent, collection, rkey, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
    if (!root) {
        wf_response_free(&res);
        return WF_ERR_PARSE;
    }
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(root, "cid");
    if (cJSON_IsString(cid) && cid->valuestring) {
        *out_cid = wf_bookmark_strdup(cid->valuestring);
    }
    cJSON_Delete(root);
    wf_response_free(&res);

    if (!*out_cid) {
        return WF_ERR_PARSE;
    }
    return WF_OK;
}

wf_status wf_agent_create_bookmark(wf_agent *agent, const char *post_uri,
                                   char **out_uri) {
    if (out_uri) {
        *out_uri = NULL;
    }
    if (!agent || !post_uri) {
        return WF_ERR_INVALID_ARG;
    }
    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(post_uri, &parsed)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_syntax_aturi_free(&parsed);

    char *cid = NULL;
    wf_status status = wf_bookmark_resolve_cid(agent, post_uri, &cid);
    if (status != WF_OK) {
        return status;
    }

    wf_lex_app_bsky_bookmark_create_bookmark_main_input input = {0};
    input.uri = post_uri;
    input.cid = cid;

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    status = wf_lex_app_bsky_bookmark_create_bookmark_main_call(agent->client,
                                                               &input, &res);
    free(cid);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    wf_response_free(&res);

    if (out_uri) {
        *out_uri = wf_bookmark_strdup(post_uri);
        if (!*out_uri) {
            return WF_ERR_ALLOC;
        }
    }
    return WF_OK;
}

wf_status wf_agent_delete_bookmark(wf_agent *agent, const char *post_uri) {
    if (!agent || !post_uri) {
        return WF_ERR_INVALID_ARG;
    }
    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(post_uri, &parsed)) {
        return WF_ERR_INVALID_ARG;
    }
    wf_syntax_aturi_free(&parsed);

    wf_lex_app_bsky_bookmark_delete_bookmark_main_input input = {0};
    input.uri = post_uri;

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_app_bsky_bookmark_delete_bookmark_main_call(
        agent->client, &input, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    wf_response_free(&res);
    return WF_OK;
}

wf_status wf_agent_get_bookmarks_typed(wf_agent *agent, int limit,
                                       const char *cursor,
                                       wf_bookmark_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_app_bsky_bookmark_get_bookmarks_main_params params = {0};
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
    wf_status status = wf_lex_app_bsky_bookmark_get_bookmarks_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_bookmark_parse_list(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}
