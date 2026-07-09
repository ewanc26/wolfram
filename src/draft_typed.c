/*
 * draft_typed.c — typed parser for app.bsky.draft.getDrafts plus owned
 * convenience agent wrappers for the post-draft namespace.
 *
 * Mirrors feed_typed.c / actor_typed.c: static strdup/set_string/reset
 * helpers, ownership via cJSON_DetachItemFromObject, and full cleanup on the
 * first error. The agent wrappers build the minimal lex input structs from a
 * raw draft JSON and dispatch through the generated
 * `wf_lex_app_bsky_draft_*` calls after `wf_agent_sync_auth`.
 */

#include "wolfram/draft_typed.h"

#include "wolfram/atproto_lex.h"
#include "agent/_internal.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_draft_strdup(const char *s) {
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

static wf_status wf_draft_set_string(char **dst, const char *src) {
    char *copy = wf_draft_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

static void wf_draft_reset(wf_draft *d) {
    if (!d) {
        return;
    }
    free(d->uri);
    free(d->created_at);
    free(d->text);
    if (d->langs) {
        for (size_t i = 0; i < d->lang_count; ++i) {
            free(d->langs[i]);
        }
        free(d->langs);
    }
    if (d->record) {
        cJSON_Delete(d->record);
    }
    memset(d, 0, sizeof(*d));
}

static void wf_draft_list_reset(wf_draft_list *list) {
    if (!list) {
        return;
    }
    for (size_t i = 0; i < list->count; ++i) {
        wf_draft_reset(&list->items[i]);
    }
    free(list->items);
    free(list->cursor);
    memset(list, 0, sizeof(*list));
}

/* Resolve a field by trying two key names (documented vs. wire variants). */
static cJSON *wf_draft_get(cJSON *obj, const char *a, const char *b) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, a);
    if (!v) {
        v = cJSON_GetObjectItemCaseSensitive(obj, b);
    }
    return v;
}

/* Extract `text` and `langs` from a detached record subtree, then take
 * ownership of the subtree itself. */
static wf_status wf_draft_take_record(wf_draft *d, cJSON *rec) {
    wf_status status = WF_OK;

    cJSON *text = cJSON_GetObjectItemCaseSensitive(rec, "text");
    if (cJSON_IsString(text) && text->valuestring) {
        status = wf_draft_set_string(&d->text, text->valuestring);
    } else {
        /* defs#draft nests text under posts[0].text */
        cJSON *posts = cJSON_GetObjectItemCaseSensitive(rec, "posts");
        if (cJSON_IsArray(posts) && cJSON_GetArraySize(posts) > 0) {
            cJSON *p0 = cJSON_GetArrayItem(posts, 0);
            cJSON *pt = cJSON_GetObjectItemCaseSensitive(p0, "text");
            if (cJSON_IsString(pt) && pt->valuestring) {
                status = wf_draft_set_string(&d->text, pt->valuestring);
            }
        }
    }

    cJSON *langs = cJSON_GetObjectItemCaseSensitive(rec, "langs");
    if (status == WF_OK && cJSON_IsArray(langs)) {
        int n = cJSON_GetArraySize(langs);
        if (n > 0) {
            char **arr = (char **)calloc((size_t)n, sizeof(*arr));
            if (!arr) {
                status = WF_ERR_ALLOC;
            } else {
                size_t got = 0;
                for (int i = 0; i < n && status == WF_OK; ++i) {
                    cJSON *l = cJSON_GetArrayItem(langs, i);
                    if (cJSON_IsString(l) && l->valuestring) {
                        char *cp = wf_draft_strdup(l->valuestring);
                        if (!cp) {
                            status = WF_ERR_ALLOC;
                        } else {
                            arr[got++] = cp;
                        }
                    }
                }
                if (status == WF_OK) {
                    d->langs = arr;
                    d->lang_count = got;
                } else {
                    for (size_t i = 0; i < got; ++i) {
                        free(arr[i]);
                    }
                    free(arr);
                }
            }
        }
    }

    if (status == WF_OK) {
        d->record = rec;
    } else {
        cJSON_Delete(rec);
    }
    return status;
}

/* Parse a single draft item (tolerant of uri/id and value/draft variants). */
static wf_status wf_draft_parse_one(cJSON *item, wf_draft *out) {
    wf_status status = WF_OK;

    cJSON *uri = wf_draft_get(item, "uri", "id");
    if (cJSON_IsString(uri) && uri->valuestring) {
        status = wf_draft_set_string(&out->uri, uri->valuestring);
    }

    cJSON *created = cJSON_GetObjectItemCaseSensitive(item, "createdAt");
    if (status == WF_OK && cJSON_IsString(created) && created->valuestring) {
        status = wf_draft_set_string(&out->created_at, created->valuestring);
    }

    cJSON *rec = wf_draft_get(item, "value", "draft");
    if (status == WF_OK && cJSON_IsObject(rec)) {
        cJSON *detached = cJSON_DetachItemFromObject(item, "value");
        if (!detached) {
            detached = cJSON_DetachItemFromObject(item, "draft");
        }
        if (detached) {
            status = wf_draft_take_record(out, detached);
        }
    }

    return status;
}

wf_status wf_draft_parse_list(const char *json, size_t len, wf_draft_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "drafts");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_draft *items = NULL;
    if (count > 0) {
        items = (wf_draft *)calloc(count, sizeof(*items));
        if (!items) {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *item = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(item)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_draft_parse_one(item, &items[i]);
        if (status != WF_OK) {
            wf_draft_reset(&items[i]);
        }
    }

    if (status == WF_OK) {
        out->items = items;
        out->count = count;
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_draft_set_string(&out->cursor, cursor->valuestring);
        }
    }

    if (status != WF_OK) {
        for (size_t i = 0; i < count; ++i) {
            wf_draft_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }

    cJSON_Delete(root);
    return status;
}

void wf_draft_list_free(wf_draft_list *list) {
    wf_draft_list_reset(list);
}

/* ---- Owned output parsers for the write procedures ---- */

wf_status wf_draft_createDraft_parse(const char *json, size_t len,
                                     wf_draft_createDraft_result *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        /* An empty/zero-length body yields no id (left NULL). */
        if (len == 0) {
            return WF_OK;
        }
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (cJSON_IsString(id) && id->valuestring) {
        status = wf_draft_set_string(&out->id, id->valuestring);
    }
    cJSON_Delete(root);
    return status;
}

void wf_draft_createDraft_result_free(wf_draft_createDraft_result *r) {
    if (!r) {
        return;
    }
    free(r->id);
    memset(r, 0, sizeof(*r));
}

/* updateDraft / deleteDraft return no body on the wire; treat successful
 * decode (including an empty body) as ok=true. */
static wf_status wf_draft_parse_ok(const char *json, size_t len, bool *ok) {
    if (!json || !ok) {
        return WF_ERR_INVALID_ARG;
    }
    *ok = false;
    if (len == 0) {
        *ok = true;
        return WF_OK;
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return WF_ERR_PARSE;
    }
    *ok = true;
    cJSON_Delete(root);
    return WF_OK;
}

wf_status wf_draft_updateDraft_parse(const char *json, size_t len,
                                     wf_draft_updateDraft_result *out) {
    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    return wf_draft_parse_ok(json, len, &out->ok);
}

void wf_draft_updateDraft_result_free(wf_draft_updateDraft_result *r) {
    if (!r) {
        return;
    }
    memset(r, 0, sizeof(*r));
}

wf_status wf_draft_deleteDraft_parse(const char *json, size_t len,
                                     wf_draft_deleteDraft_result *out) {
    if (!out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    return wf_draft_parse_ok(json, len, &out->ok);
}

void wf_draft_deleteDraft_result_free(wf_draft_deleteDraft_result *r) {
    if (!r) {
        return;
    }
    memset(r, 0, sizeof(*r));
}

/* ---- Agent convenience wrappers ---- */

/* Build a lex `defs#draft` from a draft record JSON. Strings are borrowed from
 * `root` (kept alive by the caller through the XRPC call); only the array
 * containers are heap-allocated and returned via `posts_out` / `langs_out` for
 * the caller to free afterwards. */
static wf_status wf_draft_build_lex(cJSON *root,
                                    wf_lex_app_bsky_draft_defs_draft *draft,
                                    wf_lex_app_bsky_draft_defs_draft_post ***posts_out,
                                    char ***langs_out) {
    *posts_out = NULL;
    *langs_out = NULL;
    wf_status status = WF_OK;

    cJSON *device_id = cJSON_GetObjectItemCaseSensitive(root, "deviceId");
    if (cJSON_IsString(device_id) && device_id->valuestring) {
        draft->has_device_id = true;
        draft->device_id = device_id->valuestring;
    }
    cJSON *device_name = cJSON_GetObjectItemCaseSensitive(root, "deviceName");
    if (cJSON_IsString(device_name) && device_name->valuestring) {
        draft->has_device_name = true;
        draft->device_name = device_name->valuestring;
    }

    cJSON *langs = cJSON_GetObjectItemCaseSensitive(root, "langs");
    if (cJSON_IsArray(langs)) {
        int n = cJSON_GetArraySize(langs);
        if (n > 0) {
            char **arr = (char **)calloc((size_t)n, sizeof(*arr));
            if (!arr) {
                return WF_ERR_ALLOC;
            }
            size_t got = 0;
            for (int i = 0; i < n; ++i) {
                cJSON *l = cJSON_GetArrayItem(langs, i);
                if (cJSON_IsString(l) && l->valuestring) {
                    arr[got++] = l->valuestring;
                }
            }
            draft->has_langs = true;
            draft->langs.items = (const char *const *)arr;
            draft->langs.count = got;
            *langs_out = arr;
        }
    }

    cJSON *posts = cJSON_GetObjectItemCaseSensitive(root, "posts");
    if (cJSON_IsArray(posts)) {
        int n = cJSON_GetArraySize(posts);
        if (n > 0) {
            wf_lex_app_bsky_draft_defs_draft_post **parr =
                (wf_lex_app_bsky_draft_defs_draft_post **)calloc(
                    (size_t)n, sizeof(*parr));
            if (!parr) {
                return WF_ERR_ALLOC;
            }
            size_t got = 0;
            for (int i = 0; i < n && status == WF_OK; ++i) {
                cJSON *p = cJSON_GetArrayItem(posts, i);
                wf_lex_app_bsky_draft_defs_draft_post *pp =
                    (wf_lex_app_bsky_draft_defs_draft_post *)calloc(
                        1, sizeof(*pp));
                if (!pp) {
                    status = WF_ERR_ALLOC;
                    break;
                }
                cJSON *text = cJSON_GetObjectItemCaseSensitive(p, "text");
                if (cJSON_IsString(text) && text->valuestring) {
                    pp->text = text->valuestring;
                }
                parr[got++] = pp;
            }
            if (status == WF_OK) {
                draft->posts.items =
                    (const wf_lex_app_bsky_draft_defs_draft_post *const *)parr;
                draft->posts.count = got;
                *posts_out = parr;
            } else {
                for (size_t i = 0; i < got; ++i) {
                    free(parr[i]);
                }
                free(parr);
            }
        }
    }

    return status;
}

wf_status wf_agent_create_draft(wf_agent *agent, const char *draft_json,
                                char **out_uri) {
    if (!out_uri) {
        return WF_ERR_INVALID_ARG;
    }
    *out_uri = NULL;

    wf_draft_createDraft_result res = {0};
    wf_status status = wf_agent_draft_createDraft_typed(agent, draft_json, &res);
    if (status == WF_OK && res.id) {
        /* Transfer ownership of the id string to the caller. */
        *out_uri = res.id;
        res.id = NULL;
    }
    wf_draft_createDraft_result_free(&res);
    return status;
}

wf_status wf_agent_update_draft(wf_agent *agent, const char *draft_uri,
                                const char *draft_json, char **out_uri) {
    if (!out_uri) {
        return WF_ERR_INVALID_ARG;
    }
    *out_uri = NULL;

    wf_draft_updateDraft_result res = {0};
    wf_status status =
        wf_agent_draft_updateDraft_typed(agent, draft_uri, draft_json, &res);
    if (status == WF_OK && draft_uri) {
        /* updateDraft returns no body; echo the supplied id as the result. */
        *out_uri = wf_draft_strdup(draft_uri);
        if (!*out_uri) {
            status = WF_ERR_ALLOC;
        }
    }
    wf_draft_updateDraft_result_free(&res);
    return status;
}

wf_status wf_agent_delete_draft(wf_agent *agent, const char *draft_uri) {
    wf_draft_deleteDraft_result res = {0};
    wf_status status = wf_agent_draft_deleteDraft_typed(agent, draft_uri, &res);
    wf_draft_deleteDraft_result_free(&res);
    return status;
}

/* ---- Typed write-procedure wrappers ----

 * The nested `app.bsky.draft.defs#draft` object is accepted as a pre-built
 * `draft_json` and embedded via `wf_draft_build_lex` (which borrows the
 * strings from the parsed document and only allocates the array containers).
 * This deliberately avoids modeling every sub-field of the draft up-front;
 * the public surface stays small and stable. */

static wf_status wf_agent_draft_build_and_call_create(
    wf_agent *agent, const char *draft_json,
    wf_lex_app_bsky_draft_create_draft_main_input *input,
    wf_lex_app_bsky_draft_defs_draft_post ***posts_out, char ***langs_out) {
    cJSON *root = cJSON_Parse(draft_json);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_app_bsky_draft_defs_draft draft = {0};
    wf_status status = wf_draft_build_lex(root, &draft, posts_out, langs_out);
    if (status != WF_OK) {
        cJSON_Delete(root);
        return status;
    }

    input->draft = &draft;
    (void)agent;
    return WF_OK;
}

wf_status wf_agent_draft_createDraft_typed(wf_agent *agent,
                                           const char *draft_json,
                                           wf_draft_createDraft_result *out) {
    if (!agent || !draft_json || !*draft_json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    wf_lex_app_bsky_draft_defs_draft_post **posts = NULL;
    char **langs = NULL;
    wf_lex_app_bsky_draft_create_draft_main_input input = {0};
    wf_status status =
        wf_agent_draft_build_and_call_create(agent, draft_json, &input, &posts,
                                             &langs);
    if (status != WF_OK) {
        free(posts);
        free(langs);
        return status;
    }

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    status = wf_lex_app_bsky_draft_create_draft_main_call(agent->client, &input,
                                                          &res);
    if (status == WF_OK) {
        status = wf_draft_createDraft_parse(res.body, res.body_len, out);
    }

    wf_response_free(&res);
    for (size_t i = 0; posts && i < input.draft->posts.count; ++i) {
        free(posts[i]);
    }
    free(posts);
    free(langs);
    return status;
}

wf_status wf_agent_draft_updateDraft_typed(wf_agent *agent,
                                           const char *draft_id,
                                           const char *draft_json,
                                           wf_draft_updateDraft_result *out) {
    if (!agent || !draft_id || !*draft_id || !draft_json || !*draft_json ||
        !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_Parse(draft_json);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_INVALID_ARG;
    }

    wf_lex_app_bsky_draft_defs_draft_post **posts = NULL;
    char **langs = NULL;
    wf_lex_app_bsky_draft_defs_draft draft = {0};
    wf_status status = wf_draft_build_lex(root, &draft, &posts, &langs);
    if (status != WF_OK) {
        cJSON_Delete(root);
        free(posts);
        free(langs);
        return status;
    }

    wf_lex_app_bsky_draft_defs_draft_with_id with_id = {0};
    with_id.id = draft_id;
    with_id.draft = &draft;

    wf_lex_app_bsky_draft_update_draft_main_input input = {0};
    input.draft = &with_id;

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    status = wf_lex_app_bsky_draft_update_draft_main_call(agent->client, &input,
                                                          &res);
    if (status == WF_OK) {
        status = wf_draft_updateDraft_parse(res.body, res.body_len, out);
    }

    wf_response_free(&res);
    for (size_t i = 0; posts && i < draft.posts.count; ++i) {
        free(posts[i]);
    }
    free(posts);
    free(langs);
    cJSON_Delete(root);
    return status;
}

wf_status wf_agent_draft_deleteDraft_typed(wf_agent *agent,
                                           const char *draft_id,
                                           wf_draft_deleteDraft_result *out) {
    if (!agent || !draft_id || !*draft_id || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    wf_lex_app_bsky_draft_delete_draft_main_input input = {0};
    input.id = draft_id;

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_app_bsky_draft_delete_draft_main_call(
        agent->client, &input, &res);
    if (status == WF_OK) {
        status = wf_draft_deleteDraft_parse(res.body, res.body_len, out);
    }
    wf_response_free(&res);
    return status;
}

wf_status wf_agent_get_drafts_typed(wf_agent *agent, int limit,
                                    const char *cursor, wf_draft_list *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    wf_lex_app_bsky_draft_get_drafts_main_params params = {0};
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && *cursor) {
        params.has_cursor = true;
        params.cursor = cursor;
    }

    wf_response res = {0};
    wf_agent_sync_auth(agent);
    wf_status status = wf_lex_app_bsky_draft_get_drafts_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }

    status = wf_draft_parse_list(res.body, res.body_len, out);
    wf_response_free(&res);
    return status;
}
