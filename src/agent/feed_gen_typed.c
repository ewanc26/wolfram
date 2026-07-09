/*
 * feed_gen_typed.c — owned typed parsers + agent convenience wrappers for feed
 * generators + feed discovery. See include/wolfram/feed_gen_typed.h for the
 * public API, the authoritative wire format, and ownership rules.
 *
 * Mirrors labeler_typed.c / actor_typed.c: static strdup/set_string/reset
 * helpers, owned strings, detached `extra` cJSON subtrees where shapes are
 * open/unbounded, and full cleanup on the first error. The agent wrappers call
 * the generated lex wrappers directly after syncing auth via wf_agent_sync_auth.
 */

#include "wolfram/feed_gen_typed.h"

#include "wolfram/actor_typed.h"
#include "agent/_internal.h"
#include "wolfram/atproto_lex.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_fg_strdup(const char *s) {
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

static wf_status wf_fg_set_string(char **dst, const char *src) {
    char *copy = wf_fg_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

/* ---- generator view ---- */

static void wf_feedgen_generator_view_reset(wf_feedgen_generator_view *g) {
    if (!g) {
        return;
    }
    free(g->uri);
    free(g->cid);
    wf_agent_profile_free(&g->creator);
    free(g->display_name);
    free(g->description);
    free(g->avatar);
    free(g->content_mode);
    free(g->indexed_at);
    if (g->extra) {
        cJSON_Delete(g->extra);
    }
    memset(g, 0, sizeof(*g));
}

/* Parse a profileView creator object into a wf_agent_profile (reuses
 * wf_agent_profile_free for cleanup). Maps the wire profileView fields we keep;
 * the full profile has additional fields unused here. */
static wf_status wf_feedgen_parse_creator(cJSON *obj, wf_agent_profile *creator) {
    wf_status status = WF_OK;
    if (!cJSON_IsObject(obj)) {
        return WF_OK;
    }
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(obj, "handle");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
    cJSON *desc = cJSON_GetObjectItemCaseSensitive(obj, "description");
    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");
    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_fg_set_string(&creator->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
        status = wf_fg_set_string(&creator->handle, handle->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_fg_set_string(&creator->display_name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(desc) && desc->valuestring) {
        status = wf_fg_set_string(&creator->description, desc->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        /* wf_agent_profile stores the avatar as `avatar_cid`; a generatorView
         * creator's avatar is a URL here, copied as-is. */
        status = wf_fg_set_string(&creator->avatar_cid, avatar->valuestring);
    }
    if (status != WF_OK) {
        wf_agent_profile_free(creator);
    }
    return status;
}

static wf_status wf_feedgen_parse_int(cJSON *num, int64_t *dst, bool *has) {
    if (cJSON_IsNumber(num)) {
        *dst = (int64_t)num->valuedouble;
        *has = true;
    }
    return WF_OK;
}

/* Parse a generatorView object (already detached from any parent) into `g`.
 * On success `g->extra` takes ownership of `obj` (the remaining unknown fields).
 * On error `g` is reset and `obj` is deleted by the caller. */
static wf_status wf_feedgen_parse_generator_view(cJSON *obj,
                                                 wf_feedgen_generator_view *g) {
    wf_status status = WF_OK;
    if (!cJSON_IsObject(obj)) {
        return WF_ERR_PARSE;
    }

    cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(obj, "cid");
    cJSON *creator = cJSON_GetObjectItemCaseSensitive(obj, "creator");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
    cJSON *desc = cJSON_GetObjectItemCaseSensitive(obj, "description");
    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");
    cJSON *like = cJSON_GetObjectItemCaseSensitive(obj, "likeCount");
    cJSON *accept = cJSON_GetObjectItemCaseSensitive(obj, "acceptsInteractions");
    cJSON *mode = cJSON_GetObjectItemCaseSensitive(obj, "contentMode");
    cJSON *indexed = cJSON_GetObjectItemCaseSensitive(obj, "indexedAt");

    if (cJSON_IsString(uri) && uri->valuestring) {
        status = wf_fg_set_string(&g->uri, uri->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring) {
        status = wf_fg_set_string(&g->cid, cid->valuestring);
    }
    if (status == WF_OK && cJSON_IsObject(creator)) {
        status = wf_feedgen_parse_creator(creator, &g->creator);
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_fg_set_string(&g->display_name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(desc) && desc->valuestring) {
        status = wf_fg_set_string(&g->description, desc->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        status = wf_fg_set_string(&g->avatar, avatar->valuestring);
    }
    if (status == WF_OK && cJSON_IsNumber(like)) {
        status = wf_feedgen_parse_int(like, &g->like_count, &g->has_like_count);
    }
    if (status == WF_OK && cJSON_IsBool(accept)) {
        g->has_accepts_interactions = true;
        g->accepts_interactions = cJSON_IsTrue(accept);
    }
    if (status == WF_OK && cJSON_IsString(mode) && mode->valuestring) {
        status = wf_fg_set_string(&g->content_mode, mode->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(indexed) && indexed->valuestring) {
        status = wf_fg_set_string(&g->indexed_at, indexed->valuestring);
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "uri");
        cJSON_DetachItemFromObject(obj, "cid");
        cJSON_DetachItemFromObject(obj, "creator");
        cJSON_DetachItemFromObject(obj, "displayName");
        cJSON_DetachItemFromObject(obj, "description");
        cJSON_DetachItemFromObject(obj, "avatar");
        cJSON_DetachItemFromObject(obj, "likeCount");
        cJSON_DetachItemFromObject(obj, "acceptsInteractions");
        cJSON_DetachItemFromObject(obj, "contentMode");
        cJSON_DetachItemFromObject(obj, "indexedAt");
        g->extra = obj;
    } else {
        wf_feedgen_generator_view_reset(g);
    }
    return status;
}

/* ---- search post view ---- */

static void wf_feedgen_profile_view_reset(wf_agent_profile_view *p) {
    if (!p) {
        return;
    }
    free(p->did);
    free(p->handle);
    free(p->display_name);
    free(p->avatar);
    memset(p, 0, sizeof(*p));
}

static void wf_feedgen_search_post_reset(wf_feedgen_search_post *p) {
    if (!p) {
        return;
    }
    free(p->uri);
    free(p->cid);
    wf_feedgen_profile_view_reset(&p->author);
    if (p->record) {
        cJSON_Delete(p->record);
    }
    if (p->embed) {
        cJSON_Delete(p->embed);
    }
    free(p->indexed_at);
    if (p->extra) {
        cJSON_Delete(p->extra);
    }
    memset(p, 0, sizeof(*p));
}

/* Parse a profileView author object into a wf_agent_profile_view. */
static wf_status wf_feedgen_parse_author(cJSON *obj, wf_agent_profile_view *a) {
    wf_status status = WF_OK;
    if (!cJSON_IsObject(obj)) {
        return WF_OK;
    }
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(obj, "handle");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");
    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_fg_set_string(&a->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
        status = wf_fg_set_string(&a->handle, handle->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_fg_set_string(&a->display_name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        status = wf_fg_set_string(&a->avatar, avatar->valuestring);
    }
    if (status != WF_OK) {
        wf_feedgen_profile_view_reset(a);
    }
    return status;
}

/* Parse a postView object (already detached) into `p`. On success `p->extra`
 * owns `obj`; on error `p` is reset. */
static wf_status wf_feedgen_parse_search_post(cJSON *obj,
                                              wf_feedgen_search_post *p) {
    wf_status status = WF_OK;
    if (!cJSON_IsObject(obj)) {
        return WF_ERR_PARSE;
    }

    cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(obj, "cid");
    cJSON *author = cJSON_GetObjectItemCaseSensitive(obj, "author");
    cJSON *record = cJSON_GetObjectItemCaseSensitive(obj, "record");
    cJSON *embed = cJSON_GetObjectItemCaseSensitive(obj, "embed");
    cJSON *reply = cJSON_GetObjectItemCaseSensitive(obj, "replyCount");
    cJSON *repost = cJSON_GetObjectItemCaseSensitive(obj, "repostCount");
    cJSON *like = cJSON_GetObjectItemCaseSensitive(obj, "likeCount");
    cJSON *quote = cJSON_GetObjectItemCaseSensitive(obj, "quoteCount");
    cJSON *book = cJSON_GetObjectItemCaseSensitive(obj, "bookmarkCount");
    cJSON *indexed = cJSON_GetObjectItemCaseSensitive(obj, "indexedAt");

    if (cJSON_IsString(uri) && uri->valuestring) {
        status = wf_fg_set_string(&p->uri, uri->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring) {
        status = wf_fg_set_string(&p->cid, cid->valuestring);
    }
    if (status == WF_OK && cJSON_IsObject(author)) {
        status = wf_feedgen_parse_author(author, &p->author);
    }
    if (status == WF_OK && cJSON_IsObject(record)) {
        p->record = cJSON_DetachItemFromObject(obj, "record");
    }
    if (status == WF_OK && cJSON_IsObject(embed)) {
        p->embed = cJSON_DetachItemFromObject(obj, "embed");
    }
    if (status == WF_OK && cJSON_IsNumber(reply)) {
        status = wf_feedgen_parse_int(reply, &p->reply_count, &p->has_reply_count);
    }
    if (status == WF_OK && cJSON_IsNumber(repost)) {
        status = wf_feedgen_parse_int(repost, &p->repost_count,
                                      &p->has_repost_count);
    }
    if (status == WF_OK && cJSON_IsNumber(like)) {
        status = wf_feedgen_parse_int(like, &p->like_count, &p->has_like_count);
    }
    if (status == WF_OK && cJSON_IsNumber(quote)) {
        status = wf_feedgen_parse_int(quote, &p->quote_count, &p->has_quote_count);
    }
    if (status == WF_OK && cJSON_IsNumber(book)) {
        status = wf_feedgen_parse_int(book, &p->bookmark_count,
                                      &p->has_bookmark_count);
    }
    if (status == WF_OK && cJSON_IsString(indexed) && indexed->valuestring) {
        status = wf_fg_set_string(&p->indexed_at, indexed->valuestring);
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "uri");
        cJSON_DetachItemFromObject(obj, "cid");
        cJSON_DetachItemFromObject(obj, "author");
        cJSON_DetachItemFromObject(obj, "replyCount");
        cJSON_DetachItemFromObject(obj, "repostCount");
        cJSON_DetachItemFromObject(obj, "likeCount");
        cJSON_DetachItemFromObject(obj, "quoteCount");
        cJSON_DetachItemFromObject(obj, "bookmarkCount");
        cJSON_DetachItemFromObject(obj, "indexedAt");
        p->extra = obj;
    } else {
        wf_feedgen_search_post_reset(p);
    }
    return status;
}

/* ---- top-level parse functions ---- */

wf_status wf_feedgen_parse_generators(const char *json, size_t json_len,
                                      wf_feedgen_generator_list *out) {
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
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "feeds");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_feedgen_generator_view *items = NULL;
    cJSON **ptrs = NULL;
    if (count > 0) {
        items = (wf_feedgen_generator_view *)calloc(count, sizeof(*items));
        ptrs = (cJSON **)calloc(count, sizeof(*ptrs));
        if (!items || !ptrs) {
            free(items);
            free(ptrs);
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
        for (size_t i = 0; i < count; ++i) {
            ptrs[i] = cJSON_GetArrayItem(arr, (int)i);
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *obj = ptrs[i];
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_feedgen_parse_generator_view(obj, &items[i]);
        if (status == WF_OK) {
            items[i].extra = cJSON_DetachItemViaPointer(arr, obj);
        }
        if (status != WF_OK) {
            wf_feedgen_generator_view_reset(&items[i]);
        }
    }
    free(ptrs);

    if (status == WF_OK) {
        out->generators = items;
        out->generator_count = count;
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_fg_set_string(&out->cursor, cursor->valuestring);
        }
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_feedgen_generator_view_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

wf_status wf_feedgen_parse_feed_generator(const char *json, size_t json_len,
                                          wf_feedgen_generator_detail *out) {
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
    cJSON *is_online = cJSON_GetObjectItemCaseSensitive(root, "isOnline");
    cJSON *is_valid = cJSON_GetObjectItemCaseSensitive(root, "isValid");
    cJSON *view = cJSON_GetObjectItemCaseSensitive(root, "view");

    if (cJSON_IsBool(is_online)) {
        out->is_online = cJSON_IsTrue(is_online);
    }
    if (cJSON_IsBool(is_valid)) {
        out->is_valid = cJSON_IsTrue(is_valid);
    }
    if (!cJSON_IsObject(view)) {
        status = WF_ERR_PARSE;
    } else {
        cJSON *view_obj = cJSON_DetachItemFromObject(root, "view");
        status = wf_feedgen_parse_generator_view(view_obj, &out->view);
        if (status != WF_OK) {
            cJSON_DetachItemFromObject(root, "view");
            cJSON_Delete(view_obj);
        }
    }

    if (status != WF_OK) {
        wf_feedgen_generator_detail_free(out);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

wf_status wf_feedgen_parse_search_posts(const char *json, size_t json_len,
                                        wf_feedgen_search_result_list *out) {
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
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "posts");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    cJSON *hits = cJSON_GetObjectItemCaseSensitive(root, "hitsTotal");
    if (cJSON_IsNumber(hits)) {
        out->has_hits_total = true;
        out->hits_total = (int64_t)hits->valuedouble;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_feedgen_search_post *items = NULL;
    cJSON **ptrs = NULL;
    if (count > 0) {
        items = (wf_feedgen_search_post *)calloc(count, sizeof(*items));
        ptrs = (cJSON **)calloc(count, sizeof(*ptrs));
        if (!items || !ptrs) {
            free(items);
            free(ptrs);
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
        for (size_t i = 0; i < count; ++i) {
            ptrs[i] = cJSON_GetArrayItem(arr, (int)i);
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *obj = ptrs[i];
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_feedgen_parse_search_post(obj, &items[i]);
        if (status == WF_OK) {
            items[i].extra = cJSON_DetachItemViaPointer(arr, obj);
        }
        if (status != WF_OK) {
            wf_feedgen_search_post_reset(&items[i]);
        }
    }
    free(ptrs);

    if (status == WF_OK) {
        out->posts = items;
        out->post_count = count;
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_fg_set_string(&out->cursor, cursor->valuestring);
        }
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_feedgen_search_post_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

/* ---- free functions ---- */

void wf_feedgen_generator_view_free(wf_feedgen_generator_view *g) {
    if (!g) {
        return;
    }
    wf_feedgen_generator_view_reset(g);
}

void wf_feedgen_generator_list_free(wf_feedgen_generator_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->generator_count; ++i) {
        wf_feedgen_generator_view_reset(&list->generators[i]);
    }
    free(list->generators);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

void wf_feedgen_generator_detail_free(wf_feedgen_generator_detail *d) {
    if (!d) {
        return;
    }
    wf_feedgen_generator_view_reset(&d->view);
    memset(d, 0, sizeof(*d));
}

void wf_feedgen_search_result_list_free(wf_feedgen_search_result_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->post_count; ++i) {
        wf_feedgen_search_post_reset(&list->posts[i]);
    }
    free(list->posts);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

/* ---- Agent convenience wrappers ---- */

wf_status wf_feedgen_get_feed_generator_typed(wf_agent *agent,
                                             const char *feed_uri,
                                             wf_feedgen_generator_detail *out) {
    if (!agent || !agent->client || !feed_uri || !feed_uri[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_feedgen_generator_detail detail = {0};
    wf_lex_app_bsky_feed_get_feed_generator_main_params params = {0};
    params.feed = feed_uri;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_feed_get_feed_generator_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_feedgen_parse_feed_generator(res.body, res.body_len, &detail);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = detail;
    }
    return status;
}

wf_status wf_feedgen_get_feed_generators_typed(wf_agent *agent,
                                               const char *const *feeds,
                                               size_t feed_count,
                                               wf_feedgen_generator_list *out) {
    if (!agent || !agent->client || !feeds || feed_count == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < feed_count; ++i) {
        if (!feeds[i] || !feeds[i][0]) {
            return WF_ERR_INVALID_ARG;
        }
    }

    wf_feedgen_generator_list list = {0};
    wf_lex_app_bsky_feed_get_feed_generators_main_params params = {0};
    params.feeds.items = feeds;
    params.feeds.count = feed_count;

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_feed_get_feed_generators_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_feedgen_parse_generators(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_feedgen_get_actor_feeds_typed(wf_agent *agent, const char *actor,
                                           int limit, const char *cursor,
                                           wf_feedgen_generator_list *out) {
    if (!agent || !agent->client || !actor || !actor[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_feedgen_generator_list list = {0};
    wf_lex_app_bsky_feed_get_actor_feeds_main_params params = {0};
    params.actor = actor;
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_feed_get_actor_feeds_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_feedgen_parse_generators(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_feedgen_get_suggested_feeds_typed(wf_agent *agent, int limit,
                                               const char *cursor,
                                               wf_feedgen_generator_list *out) {
    if (!agent || !agent->client || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_feedgen_generator_list list = {0};
    wf_lex_app_bsky_feed_get_suggested_feeds_main_params params = {0};
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_feed_get_suggested_feeds_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_feedgen_parse_generators(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_feedgen_get_likes_typed(wf_agent *agent, const char *uri,
                                     const char *cid, int limit,
                                     const char *cursor,
                                     wf_agent_like_list *out) {
    if (!agent || !agent->client || !uri || !uri[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_like_list list = {0};
    wf_lex_app_bsky_feed_get_likes_main_params params = {0};
    params.uri = uri;
    if (cid && cid[0]) {
        params.has_cid = true;
        params.cid = cid;
    }
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_feed_get_likes_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_agent_parse_likes(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_feedgen_get_reposted_by_typed(wf_agent *agent, const char *uri,
                                           const char *cid, int limit,
                                           const char *cursor,
                                           wf_agent_actor_list *out) {
    if (!agent || !agent->client || !uri || !uri[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_actor_list list = {0};
    wf_lex_app_bsky_feed_get_reposted_by_main_params params = {0};
    params.uri = uri;
    if (cid && cid[0]) {
        params.has_cid = true;
        params.cid = cid;
    }
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_feed_get_reposted_by_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_agent_parse_reposted_by(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_feedgen_get_quotes_typed(wf_agent *agent, const char *uri,
                                      int limit, const char *cursor,
                                      wf_agent_feed_list *out) {
    if (!agent || !agent->client || !uri || !uri[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_feed_list list = {0};
    wf_lex_app_bsky_feed_get_quotes_main_params params = {0};
    params.uri = uri;
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_feed_get_quotes_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_agent_parse_feed_key(res.body, res.body_len, "posts", &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_feedgen_get_actor_likes_typed(wf_agent *agent, const char *actor,
                                           int limit, const char *cursor,
                                           wf_agent_feed_list *out) {
    if (!agent || !agent->client || !actor || !actor[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_feed_list list = {0};
    wf_lex_app_bsky_feed_get_actor_likes_main_params params = {0};
    params.actor = actor;
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_feed_get_actor_likes_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_agent_parse_feed_key(res.body, res.body_len, "feed", &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_feedgen_get_list_feed_typed(wf_agent *agent, const char *list_uri,
                                         int limit, const char *cursor,
                                         wf_agent_feed_list *out) {
    if (!agent || !agent->client || !list_uri || !list_uri[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_agent_feed_list list = {0};
    wf_lex_app_bsky_feed_get_list_feed_main_params params = {0};
    params.list = list_uri;
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_feed_get_list_feed_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_agent_parse_feed_key(res.body, res.body_len, "feed", &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_feedgen_search_posts_typed(wf_agent *agent, const char *query,
                                        int limit, const char *cursor,
                                        wf_feedgen_search_result_list *out) {
    if (!agent || !agent->client || !query || !query[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_feedgen_search_result_list list = {0};
    wf_lex_app_bsky_feed_search_posts_main_params params = {0};
    params.q = query;
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_feed_search_posts_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_feedgen_parse_search_posts(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_feedgen_search_posts_v2_typed(wf_agent *agent, const char *query,
                                           int limit, const char *cursor,
                                           wf_feedgen_search_result_list *out) {
    if (!agent || !agent->client || !query || !query[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_feedgen_search_result_list list = {0};
    wf_lex_app_bsky_feed_search_posts_v2_main_params params = {0};
    params.has_query = true;
    params.query = query;
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_feed_search_posts_v2_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_feedgen_parse_search_posts(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

/* TODO: app.bsky.feed.getPopularFeedGenerators is absent from the generated
 * lex wrappers. The NSID only exists as app.bsky.unspecced.getPopularFeedGenerators
 * (wf_lex_app_bsky_unspecced_get_popular_feed_generators_main_call). Add an
 * unspecced_typed wrapper if a typed popular-feed generator list is required. */
