/*
 * feed_typed.c — typed parser for app.bsky.feed.getTimeline / getAuthorFeed.
 *
 * Mirrors notification.c: static strdup/set_string/reset helpers, ownership via
 * cJSON_DetachItemFromObject, and full cleanup on the first error.
 */

#include "wolfram/feed_typed.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_feed_strdup(const char *s) {
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

static wf_status wf_feed_set_string(char **dst, const char *src) {
    char *copy = wf_feed_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static void wf_feed_viewer_state_reset(wf_agent_feed_viewer_state *v) {
    if (!v) {
        return;
    }
    free(v->repost);
    free(v->like);
    memset(v, 0, sizeof(*v));
}

static void wf_feed_profile_view_reset(wf_agent_profile_view *p) {
    if (!p) {
        return;
    }
    free(p->did);
    free(p->handle);
    free(p->display_name);
    free(p->avatar);
    memset(p, 0, sizeof(*p));
}

static void wf_feed_post_view_reset(wf_agent_post_view *p) {
    if (!p) {
        return;
    }
    free(p->uri);
    free(p->cid);
    wf_feed_profile_view_reset(&p->author);
    if (p->record) {
        cJSON_Delete(p->record);
    }
    if (p->embed) {
        cJSON_Delete(p->embed);
    }
    free(p->indexed_at);
    wf_feed_viewer_state_reset(&p->viewer);
    memset(p, 0, sizeof(*p));
}

static void wf_feed_item_reset(wf_agent_feed_item *item) {
    if (!item) {
        return;
    }
    wf_feed_post_view_reset(&item->post);
    if (item->reason) {
        cJSON_Delete(item->reason);
    }
    if (item->reply) {
        cJSON_Delete(item->reply);
    }
    free(item->feed_context);
    free(item->req_id);
    memset(item, 0, sizeof(*item));
}

static void wf_feed_list_reset(wf_agent_feed_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->item_count; ++i) {
        wf_feed_item_reset(&list->items[i]);
    }
    free(list->items);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

/* Parse an integer from a cJSON number, tracking whether it was present. */
static wf_status wf_feed_parse_int(cJSON *num, int *dst, int *has) {
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

static wf_status wf_feed_parse_viewer(wf_agent_feed_viewer_state *v,
                                      cJSON *viewer) {
    wf_status status = WF_OK;
    if (!cJSON_IsObject(viewer)) {
        return WF_OK;
    }
    cJSON *repost = cJSON_GetObjectItemCaseSensitive(viewer, "repost");
    cJSON *like = cJSON_GetObjectItemCaseSensitive(viewer, "like");
    if (cJSON_IsString(repost) && repost->valuestring) {
        status = wf_feed_set_string(&v->repost, repost->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(like) && like->valuestring) {
        status = wf_feed_set_string(&v->like, like->valuestring);
    }
    cJSON *bookmarked = cJSON_GetObjectItemCaseSensitive(viewer, "bookmarked");
    if (cJSON_IsBool(bookmarked)) {
        v->has_bookmarked = 1;
        v->bookmarked = cJSON_IsTrue(bookmarked) ? 1 : 0;
    }
    cJSON *thread_muted = cJSON_GetObjectItemCaseSensitive(viewer, "threadMuted");
    if (cJSON_IsBool(thread_muted)) {
        v->has_thread_muted = 1;
        v->thread_muted = cJSON_IsTrue(thread_muted) ? 1 : 0;
    }
    cJSON *reply_disabled = cJSON_GetObjectItemCaseSensitive(viewer, "replyDisabled");
    if (cJSON_IsBool(reply_disabled)) {
        v->has_reply_disabled = 1;
        v->reply_disabled = cJSON_IsTrue(reply_disabled) ? 1 : 0;
    }
    cJSON *embedding_disabled = cJSON_GetObjectItemCaseSensitive(viewer, "embeddingDisabled");
    if (cJSON_IsBool(embedding_disabled)) {
        v->has_embedding_disabled = 1;
        v->embedding_disabled = cJSON_IsTrue(embedding_disabled) ? 1 : 0;
    }
    cJSON *pinned = cJSON_GetObjectItemCaseSensitive(viewer, "pinned");
    if (cJSON_IsBool(pinned)) {
        v->has_pinned = 1;
        v->pinned = cJSON_IsTrue(pinned) ? 1 : 0;
    }
    return WF_OK;
}

static wf_status wf_feed_parse_author(wf_agent_profile_view *author, cJSON *obj) {
    wf_status status = WF_OK;
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(obj, "handle");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");
    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_feed_set_string(&author->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
        status = wf_feed_set_string(&author->handle, handle->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_feed_set_string(&author->display_name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        status = wf_feed_set_string(&author->avatar, avatar->valuestring);
    }
    return status;
}

static wf_status wf_feed_parse_post_view(wf_agent_post_view *p, cJSON *obj) {
    wf_status status = WF_OK;
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(obj, "cid");
    cJSON *author = cJSON_GetObjectItemCaseSensitive(obj, "author");
    cJSON *indexed_at = cJSON_GetObjectItemCaseSensitive(obj, "indexedAt");
    cJSON *viewer = cJSON_GetObjectItemCaseSensitive(obj, "viewer");
    cJSON *reply_count = cJSON_GetObjectItemCaseSensitive(obj, "replyCount");
    cJSON *repost_count = cJSON_GetObjectItemCaseSensitive(obj, "repostCount");
    cJSON *like_count = cJSON_GetObjectItemCaseSensitive(obj, "likeCount");
    cJSON *quote_count = cJSON_GetObjectItemCaseSensitive(obj, "quoteCount");
    cJSON *bookmark_count = cJSON_GetObjectItemCaseSensitive(obj, "bookmarkCount");

    if (cJSON_IsString(uri) && uri->valuestring) {
        status = wf_feed_set_string(&p->uri, uri->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring) {
        status = wf_feed_set_string(&p->cid, cid->valuestring);
    }
    if (status == WF_OK && cJSON_IsObject(author)) {
        status = wf_feed_parse_author(&p->author, author);
    }
    if (status == WF_OK && cJSON_IsString(indexed_at) && indexed_at->valuestring) {
        status = wf_feed_set_string(&p->indexed_at, indexed_at->valuestring);
    }
    if (status == WF_OK) {
        status = wf_feed_parse_int(reply_count, &p->reply_count, &p->has_reply_count);
    }
    if (status == WF_OK) {
        status = wf_feed_parse_int(repost_count, &p->repost_count, &p->has_repost_count);
    }
    if (status == WF_OK) {
        status = wf_feed_parse_int(like_count, &p->like_count, &p->has_like_count);
    }
    if (status == WF_OK) {
        status = wf_feed_parse_int(quote_count, &p->quote_count, &p->has_quote_count);
    }
    if (status == WF_OK) {
        status = wf_feed_parse_int(bookmark_count, &p->bookmark_count, &p->has_bookmark_count);
    }
    if (status == WF_OK) {
        status = wf_feed_parse_viewer(&p->viewer, viewer);
    }

    /* Take ownership of the `record` and `embed` subtrees (type `unknown`/`union`). */
    if (status == WF_OK) {
        cJSON *record = cJSON_DetachItemFromObject(obj, "record");
        if (record) {
            p->record = record;
        }
        cJSON *embed = cJSON_DetachItemFromObject(obj, "embed");
        if (embed) {
            p->embed = embed;
        }
    }

    return status;
}

static wf_status wf_feed_parse_key(const char *json, size_t json_len,
                                   const char *key, wf_agent_feed_list *out) {
    if (!json || !out || !key) {
        return WF_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_agent_feed_item *items = NULL;
    if (count > 0) {
        items = (wf_agent_feed_item *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        wf_agent_feed_item *item = &items[i];
        cJSON *fvp = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(fvp)) {
            status = WF_ERR_PARSE;
            break;
        }

        cJSON *post = cJSON_GetObjectItemCaseSensitive(fvp, "post");
        cJSON *reply = cJSON_GetObjectItemCaseSensitive(fvp, "reply");
        cJSON *reason = cJSON_GetObjectItemCaseSensitive(fvp, "reason");
        cJSON *feed_context = cJSON_GetObjectItemCaseSensitive(fvp, "feedContext");
        cJSON *req_id = cJSON_GetObjectItemCaseSensitive(fvp, "reqId");

        if (cJSON_IsObject(post)) {
            status = wf_feed_parse_post_view(&item->post, post);
        } else {
            status = WF_ERR_PARSE;
        }

        if (status == WF_OK && cJSON_IsObject(reason)) {
            item->reason = cJSON_DetachItemFromObject(fvp, "reason");
        }
        if (status == WF_OK && cJSON_IsObject(reply)) {
            item->reply = cJSON_DetachItemFromObject(fvp, "reply");
        }
        if (status == WF_OK && cJSON_IsString(feed_context) && feed_context->valuestring) {
            status = wf_feed_set_string(&item->feed_context, feed_context->valuestring);
        }
        if (status == WF_OK && cJSON_IsString(req_id) && req_id->valuestring) {
            status = wf_feed_set_string(&item->req_id, req_id->valuestring);
        }

        if (status != WF_OK) {
            wf_feed_item_reset(item);
        }
    }

    if (status == WF_OK) {
        out->items = items;
        out->item_count = count;

        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_feed_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_feed_item_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

wf_status wf_agent_parse_feed(const char *json, size_t json_len,
                              wf_agent_feed_list *out) {
    return wf_feed_parse_key(json, json_len, "feed", out);
}

wf_status wf_agent_parse_feed_key(const char *json, size_t json_len,
                                  const char *key, wf_agent_feed_list *out) {
    return wf_feed_parse_key(json, json_len, key, out);
}

void wf_agent_feed_list_free(wf_agent_feed_list *list) {
    wf_feed_list_reset(list);
}

/* Typed high-level wrappers — call the raw agent endpoint, then parse the body. */

wf_status wf_agent_get_timeline_typed(wf_agent *agent, int limit,
                                      const char *cursor,
                                      wf_agent_feed_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_timeline(agent, limit, cursor, NULL, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_feed(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_author_feed_typed(wf_agent *agent, const char *actor,
                                         int limit, const char *cursor,
                                         const char *filter,
                                         wf_agent_feed_list *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_author_feed(agent, actor, limit, cursor,
                                                filter, false, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_feed(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_quotes_typed(wf_agent *agent, const char *uri,
                                    int limit, const char *cursor,
                                    wf_agent_feed_list *out) {
    if (!agent || !uri || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_quotes(agent, uri, limit, cursor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    /* getQuotes returns a `posts` array of feedViewPost (same shape as feed). */
    status = wf_feed_parse_key(res.body, res.body_len, "posts", out);
    wf_response_free(&res);
    return status;
}

/* ── getPosts / getFeedSkeleton / describeFeedGenerator ──────────────────── */

static void wf_feed_post_list_reset(wf_agent_post_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->post_count; ++i) {
        wf_feed_post_view_reset(&list->posts[i]);
    }
    free(list->posts);
    memset(list, 0, sizeof(*list));
}

wf_status wf_agent_parse_posts(const char *json, size_t json_len,
                              wf_agent_post_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "posts");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_agent_post_view *items = NULL;
    if (count > 0) {
        items = (wf_agent_post_view *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_feed_parse_post_view(&items[i], obj);
        if (status != WF_OK) {
            wf_feed_post_view_reset(&items[i]);
        }
    }

    if (status == WF_OK) {
        out->posts = items;
        out->post_count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_feed_post_view_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_post_list_free(wf_agent_post_list *list) {
    wf_feed_post_list_reset(list);
}

static void wf_feed_skeleton_reset(wf_agent_skeleton_post *p) {
    if (!p) {
        return;
    }
    free(p->post);
    if (p->reason) {
        cJSON_Delete(p->reason);
    }
    memset(p, 0, sizeof(*p));
}

static void wf_feed_skeleton_list_reset(wf_agent_skeleton_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->post_count; ++i) {
        wf_feed_skeleton_reset(&list->posts[i]);
    }
    free(list->posts);
    free(list->cursor);
    free(list->req_id);
    memset(list, 0, sizeof(*list));
}

wf_status wf_agent_parse_feed_skeleton(const char *json, size_t json_len,
                                       wf_agent_skeleton_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "feed");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_agent_skeleton_post *items = NULL;
    if (count > 0) {
        items = (wf_agent_skeleton_post *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        wf_agent_skeleton_post *item = &items[i];
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        cJSON *post = cJSON_GetObjectItemCaseSensitive(obj, "post");
        if (cJSON_IsString(post) && post->valuestring) {
            status = wf_feed_set_string(&item->post, post->valuestring);
        } else {
            status = WF_ERR_PARSE;
        }
        if (status == WF_OK) {
            cJSON *reason = cJSON_GetObjectItemCaseSensitive(obj, "reason");
            if (cJSON_IsObject(reason)) {
                item->reason = cJSON_DetachItemFromObject(obj, "reason");
            }
        }
        if (status != WF_OK) {
            wf_feed_skeleton_reset(item);
        }
    }

    if (status == WF_OK) {
        out->posts = items;
        out->post_count = count;
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_feed_set_string(&out->cursor, cursor->valuestring);
        }
        cJSON *req_id = cJSON_GetObjectItemCaseSensitive(root, "reqId");
        if (status == WF_OK && cJSON_IsString(req_id) && req_id->valuestring) {
            status = wf_feed_set_string(&out->req_id, req_id->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_feed_skeleton_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_skeleton_list_free(wf_agent_skeleton_list *list) {
    wf_feed_skeleton_list_reset(list);
}

static void wf_feed_gen_describe_reset(wf_agent_feed_gen_describe *d) {
    if (!d) {
        return;
    }
    free(d->did);
    for (size_t i = 0; i < d->feed_count; ++i) {
        free(d->feeds[i].uri);
    }
    free(d->feeds);
    if (d->links) {
        cJSON_Delete(d->links);
    }
    memset(d, 0, sizeof(*d));
}

wf_status wf_agent_parse_describe_feed_generator(const char *json,
                                                size_t json_len,
                                                wf_agent_feed_gen_describe *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *did = cJSON_GetObjectItemCaseSensitive(root, "did");
    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_feed_set_string(&out->did, did->valuestring);
    }

    cJSON *feeds = cJSON_GetObjectItemCaseSensitive(root, "feeds");
    if (status == WF_OK && cJSON_IsArray(feeds)) {
        size_t count = (size_t)cJSON_GetArraySize(feeds);
        wf_agent_feed_gen_feed *items = NULL;
        if (count > 0) {
            items = (wf_agent_feed_gen_feed *)calloc(count, sizeof(*items));
            if (!items) {
                cJSON_Delete(root);
                return WF_ERR_ALLOC;
            }
        }
        for (size_t i = 0; i < count && status == WF_OK; ++i) {
            cJSON *obj = cJSON_GetArrayItem(feeds, (int)i);
            cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
            if (cJSON_IsString(uri) && uri->valuestring) {
                status = wf_feed_set_string(&items[i].uri, uri->valuestring);
            }
            if (status != WF_OK) {
                free(items[i].uri);
            }
        }
        if (status == WF_OK) {
            out->feeds = items;
            out->feed_count = count;
        } else {
            for (size_t i = 0; i < count; ++i) {
                free(items[i].uri);
            }
            free(items);
        }
    }

    if (status == WF_OK) {
        cJSON *links = cJSON_GetObjectItemCaseSensitive(root, "links");
        if (cJSON_IsObject(links)) {
            out->links = cJSON_DetachItemFromObject(root, "links");
        }
    }

    if (status != WF_OK) {
        wf_feed_gen_describe_reset(out);
    }

    cJSON_Delete(root);
    return status;
}

void wf_agent_feed_gen_describe_free(wf_agent_feed_gen_describe *out) {
    wf_feed_gen_describe_reset(out);
}

/* Typed convenience wrappers for the remaining core feed read endpoints. */

wf_status wf_agent_get_posts_typed(wf_agent *agent, const char *const *uris,
                                   size_t uri_count, wf_agent_post_list *out) {
    if (!agent || !uris || uri_count == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_posts(agent, uris, uri_count, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_posts(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_feed_skeleton_typed(wf_agent *agent,
                                           const char *feed_uri, int limit,
                                           const char *cursor,
                                           wf_agent_skeleton_list *out) {
    if (!agent || !feed_uri || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_get_feed_skeleton(agent, feed_uri, limit,
                                                  cursor, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_feed_skeleton(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_describe_feed_generator_typed(wf_agent *agent,
                                                 wf_agent_feed_gen_describe *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }

    wf_response res = {0};
    wf_status status = wf_agent_describe_feed_generator(agent, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_agent_parse_describe_feed_generator(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}
