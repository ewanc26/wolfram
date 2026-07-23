#include "wolfram/agent.h"

#include "wolfram/identity.h"
#include "wolfram/repo.h"
#include "wolfram/richtext.h"
#include "wolfram/server.h"
#include "wolfram/session.h"
#include "wolfram/syntax.h"

#include <cJSON.h>
#include "wolfram/atproto_lex.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "_internal.h"

/* Upstream app.bsky.feed.getAuthorFeed `filter` enum (lexicon-known values). */
static int wf_feed_author_feed_filter_is_valid(const char *filter) {
    static const char *const allowed[] = {
        "posts_with_replies", "posts_no_replies", "posts_with_media",
        "posts_and_author_threads", "posts_with_video", NULL};
    for (size_t i = 0; allowed[i]; i++) {
        if (strcmp(filter, allowed[i]) == 0) return 1;
    }
    return 0;
}

wf_status wf_agent_get_timeline(wf_agent *agent, int limit, const char *cursor,
                                 const char *algorithm, wf_response *out) {
    if (!agent || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];

    if (algorithm && algorithm[0]) {
        params[param_count].name = "algorithm";
        params[param_count].value = algorithm;
        param_count++;
    }
    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf))) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "limit";
        params[param_count].value = limit_buf;
        param_count++;
    }
    if (cursor && cursor[0]) {
        params[param_count].name = "cursor";
        params[param_count].value = cursor;
        param_count++;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getTimeline",
                                 params, param_count, out);
}

wf_status wf_agent_get_timeline_lex(wf_agent *agent, int limit, const char *cursor,
                                     wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    if (limit < 0 || limit > 100) return WF_ERR_INVALID_ARG;
    wf_lex_app_bsky_feed_get_timeline_main_params params = {0};
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }
    wf_agent_sync_auth(agent);
    return wf_lex_app_bsky_feed_get_timeline_main_call(agent->client, &params, out);
}

wf_status wf_agent_get_author_feed(wf_agent *agent, const char *actor,
                                    int limit, const char *cursor, const char *filter,
                                    bool include_pins, wf_response *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }

    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }
    if (filter && filter[0] && !wf_feed_author_feed_filter_is_valid(filter)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[5];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "actor";
    params[param_count].value = actor;
    param_count++;

    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf))) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "limit";
        params[param_count].value = limit_buf;
        param_count++;
    }
    if (filter && filter[0]) {
        params[param_count].name = "filter";
        params[param_count].value = filter;
        param_count++;
    }
    if (cursor && cursor[0]) {
        params[param_count].name = "cursor";
        params[param_count].value = cursor;
        param_count++;
    }
    if (include_pins) {
        params[param_count].name = "includePins";
        params[param_count].value = "true";
        param_count++;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getAuthorFeed",
                                   params, param_count, out);
}

wf_status wf_agent_get_author_feed_lex(wf_agent *agent, const char *actor,
                                         int limit, const char *cursor, const char *filter,
                                         wf_response *out) {
    if (!agent || !actor || !out) return WF_ERR_INVALID_ARG;
    if (!wf_syntax_at_identifier_is_valid(actor)) return WF_ERR_INVALID_ARG;
    if (limit < 0 || limit > 100) return WF_ERR_INVALID_ARG;
    if (filter && filter[0] && !wf_feed_author_feed_filter_is_valid(filter))
        return WF_ERR_INVALID_ARG;
    wf_agent_sync_auth(agent);
    return WF_OK;
}

wf_status wf_agent_get_post_thread(wf_agent *agent, const char *uri, int depth,
                                   int parent_height, wf_response *out) {
    if (!agent || !uri || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (depth < 0 || depth > 1000) {
        return WF_ERR_INVALID_ARG;
    }
    if (parent_height < 0 || parent_height > 1000) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(uri, &parsed)) {
        return WF_ERR_PARSE;
    }
    wf_syntax_aturi_free(&parsed);

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char depth_buf[16];
    char parent_buf[16];

    params[param_count].name = "uri";
    params[param_count].value = uri;
    param_count++;

    /* atproto default depth is 6; only send when explicitly requested (>0). */
    if (depth > 0) {
        if (!wf_agent_int_to_str(depth, depth_buf, sizeof(depth_buf))) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "depth";
        params[param_count].value = depth_buf;
        param_count++;
    }

    /* atproto default parentHeight is 80; only send when explicitly requested. */
    if (parent_height > 0) {
        if (!wf_agent_int_to_str(parent_height, parent_buf, sizeof(parent_buf))) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "parentHeight";
        params[param_count].value = parent_buf;
        param_count++;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getPostThread",
                                 params, param_count, out);
}

wf_status wf_agent_get_posts(wf_agent *agent, const char *const *uris, size_t uri_count,
                             wf_response *out) {
    if (!agent || !uris || uri_count == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }

    /* Validate all URIs first */
    for (size_t i = 0; i < uri_count; ++i) {
        if (!uris[i]) {
            return WF_ERR_INVALID_ARG;
        }
        wf_syntax_aturi parsed = {0};
        if (!wf_syntax_aturi_parse(uris[i], &parsed)) {
            return WF_ERR_PARSE;
        }
        wf_syntax_aturi_free(&parsed);
    }

    /* Build query string: uris=at://...&uris=at://... */
    /* wf_xrpc_query_params handles repeated params by passing the same name */
    wf_xrpc_param *params = calloc(uri_count, sizeof(*params));
    if (!params) {
        return WF_ERR_ALLOC;
    }

    for (size_t i = 0; i < uri_count; ++i) {
        params[i].name = "uris";
        params[i].value = uris[i];
    }

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_query_params(agent->client, "app.bsky.feed.getPosts",
                                             params, uri_count, out);
    free(params);
    return status;
}

/* ── searchPosts ───────────────────────────────────────────────────── */

wf_status wf_agent_search_posts(wf_agent *agent, const char *query,
                                 int limit, const char *cursor,
                                 const char *since, const char *until,
                                 const char *author, const char *lang,
                                 wf_response *out) {
    if (!agent || !query || !query[0] || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[9];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "q";
    params[param_count].value = query;
    param_count++;

    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf))) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "limit";
        params[param_count].value = limit_buf;
        param_count++;
    }
    if (cursor && cursor[0]) {
        params[param_count].name = "cursor";
        params[param_count].value = cursor;
        param_count++;
    }
    if (since && since[0]) {
        params[param_count].name = "since";
        params[param_count].value = since;
        param_count++;
    }
    if (until && until[0]) {
        params[param_count].name = "until";
        params[param_count].value = until;
        param_count++;
    }
    if (author && author[0]) {
        if (!wf_syntax_at_identifier_is_valid(author)) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "author";
        params[param_count].value = author;
        param_count++;
    }
    if (lang && lang[0]) {
        if (!wf_syntax_language_is_valid(lang)) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "lang";
        params[param_count].value = lang;
        param_count++;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.searchPosts",
                                 params, param_count, out);
}

wf_status wf_agent_search_posts_lex(wf_agent *agent, const char *query,
                                  int limit, const char *cursor,
                                  const char *since, const char *until,
                                  const char *author, const char *lang,
                                  wf_response *out) {
    if (!agent || !query || !query[0] || !out) return WF_ERR_INVALID_ARG;
    if (limit < 0 || limit > 100) return WF_ERR_INVALID_ARG;
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
    if (since && since[0]) {
        params.has_since = true;
        params.since = since;
    }
    if (until && until[0]) {
        params.has_until = true;
        params.until = until;
    }
    if (author && author[0]) {
        if (!wf_syntax_at_identifier_is_valid(author)) return WF_ERR_INVALID_ARG;
        params.has_author = true;
        params.author = author;
    }
    if (lang && lang[0]) {
        if (!wf_syntax_language_is_valid(lang)) return WF_ERR_INVALID_ARG;
        params.has_lang = true;
        params.lang = lang;
    }
    wf_agent_sync_auth(agent);
    return WF_OK;
}

wf_status wf_agent_get_actor_likes(wf_agent *agent, const char *actor,
                                   int limit, const char *cursor,
                                   wf_response *out) {
    if (!agent || !actor || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
    if (!wf_syntax_at_identifier_is_valid(actor)) {
        return WF_ERR_INVALID_ARG;
    }

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "actor";
    params[param_count].value = actor;
    param_count++;

    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf))) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "limit";
        params[param_count].value = limit_buf;
        param_count++;
    }
    if (cursor && cursor[0]) {
        params[param_count].name = "cursor";
        params[param_count].value = cursor;
        param_count++;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getActorLikes",
                                params, param_count, out);
}

wf_status wf_agent_get_likes(wf_agent *agent, const char *uri,
                             int limit, const char *cursor,
                             wf_response *out) {
    if (!agent || !uri || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(uri, &parsed)) {
        return WF_ERR_PARSE;
    }
    wf_syntax_aturi_free(&parsed);

    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];

    params[param_count].name = "uri";
    params[param_count].value = uri;
    param_count++;

    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf))) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "limit";
        params[param_count].value = limit_buf;
        param_count++;
    }
    if (cursor && cursor[0]) {
        params[param_count].name = "cursor";
        params[param_count].value = cursor;
        param_count++;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getLikes",
                                params, param_count, out);
}

wf_status wf_agent_get_likes_lex(wf_agent *agent, const char *uri,
                                 int limit, const char *cursor,
                                 wf_response *out) {
    if (!agent || !uri || !out) return WF_ERR_INVALID_ARG;
    // Validate AT-URI syntax
    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(uri, &parsed)) return WF_ERR_PARSE;
    wf_syntax_aturi_free(&parsed);
    if (limit < 0 || limit > 100) return WF_ERR_INVALID_ARG;

    wf_lex_app_bsky_feed_get_likes_main_params params = {0};
    params.uri = uri;
    if (limit > 0) {
        params.has_limit = true;
        params.limit = limit;
    }
    if (cursor && cursor[0]) {
        params.has_cursor = true;
        params.cursor = cursor;
    }
    // Ensure auth
    wf_agent_sync_auth(agent);
    return wf_lex_app_bsky_feed_get_likes_main_call(agent->client, &params, out);
}

/* ── lexicon wrappers for feed endpoints ─────────────────────────────────────── */

wf_status wf_agent_get_quotes_lex(wf_agent *agent, const char *uri,
                                 int limit, const char *cursor,
                                 wf_response *out) {
    if (!agent || !uri || !out) return WF_ERR_INVALID_ARG;
    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(uri, &parsed)) return WF_ERR_PARSE;
    wf_syntax_aturi_free(&parsed);
    if (limit < 0 || limit > 100) return WF_ERR_INVALID_ARG;
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
    return wf_lex_app_bsky_feed_get_quotes_main_call(agent->client, &params, out);
}

wf_status wf_agent_get_list_feed_lex(wf_agent *agent, const char *list_uri,
                                     int limit, const char *cursor,
                                     wf_response *out) {
    if (!agent || !list_uri || !out) return WF_ERR_INVALID_ARG;
    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(list_uri, &parsed)) return WF_ERR_PARSE;
    wf_syntax_aturi_free(&parsed);
    if (limit < 0 || limit > 100) return WF_ERR_INVALID_ARG;
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
    return wf_lex_app_bsky_feed_get_list_feed_main_call(agent->client, &params, out);
}

wf_status wf_agent_get_feed_lex(wf_agent *agent, const char *feed_uri,
                                int limit, const char *cursor,
                                wf_response *out) {
    if (!agent || !feed_uri || !out) return WF_ERR_INVALID_ARG;
    if (limit < 0 || limit > 100) return WF_ERR_INVALID_ARG;
    wf_agent_sync_auth(agent);
    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(feed_uri, &parsed)) {
        return WF_ERR_PARSE;
    }
    wf_syntax_aturi_free(&parsed);
    return WF_OK;
}

wf_status wf_agent_get_actor_feeds_lex(wf_agent *agent, const char *actor,
                                        int limit, const char *cursor,
                                        wf_response *out) {
    if (!agent || !actor || !out) return WF_ERR_INVALID_ARG;
    if (!wf_syntax_at_identifier_is_valid(actor)) return WF_ERR_INVALID_ARG;
    if (limit < 0 || limit > 100) return WF_ERR_INVALID_ARG;
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
    return wf_lex_app_bsky_feed_get_actor_feeds_main_call(agent->client, &params, out);
}

wf_status wf_agent_get_reposted_by(wf_agent *agent, const char *uri,
                                   int limit, const char *cursor,
                                   wf_response *out) {
    if (!agent || !uri || !out) {
        return WF_ERR_INVALID_ARG;
    }
    if (limit < 0 || limit > 100) {
        return WF_ERR_INVALID_ARG;
    }
    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(uri, &parsed)) {
        return WF_ERR_PARSE;
    }
    wf_syntax_aturi_free(&parsed);
    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];
    params[param_count].name = "uri";
    params[param_count].value = uri;
    param_count++;
    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf))) {
            return WF_ERR_INVALID_ARG;
        }
        params[param_count].name = "limit";
        params[param_count].value = limit_buf;
        param_count++;
    }
    if (cursor && cursor[0]) {
        params[param_count].name = "cursor";
        params[param_count].value = cursor;
        param_count++;
    }
    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getRepostedBy",
                                 params, param_count, out);
}

wf_status wf_agent_get_quotes(wf_agent *agent, const char *uri,
                             int limit, const char *cursor,
                             wf_response *out) {
    if (!agent || !uri || !out) return WF_ERR_INVALID_ARG;
    if (limit < 0 || limit > 100) return WF_ERR_INVALID_ARG;
    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(uri, &parsed)) return WF_ERR_PARSE;
    wf_syntax_aturi_free(&parsed);
    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];
    params[param_count].name = "uri";
    params[param_count].value = uri;
    param_count++;
    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf)))
            return WF_ERR_INVALID_ARG;
        params[param_count].name = "limit";
        params[param_count].value = limit_buf;
        param_count++;
    }
    if (cursor && cursor[0]) {
        params[param_count].name = "cursor";
        params[param_count].value = cursor;
        param_count++;
    }
    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getQuotes",
                                 params, param_count, out);
}

wf_status wf_agent_get_list_feed(wf_agent *agent, const char *list_uri,
                                int limit, const char *cursor,
                                wf_response *out) {
    if (!agent || !list_uri || !out) return WF_ERR_INVALID_ARG;
    if (limit < 0 || limit > 100) return WF_ERR_INVALID_ARG;
    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];
    params[param_count].name = "list";
    params[param_count].value = list_uri;
    param_count++;
    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf)))
            return WF_ERR_INVALID_ARG;
        params[param_count].name = "limit";
        params[param_count].value = limit_buf;
        param_count++;
    }
    if (cursor && cursor[0]) {
        params[param_count].name = "cursor";
        params[param_count].value = cursor;
        param_count++;
    }
    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getListFeed",
                                 params, param_count, out);
}

wf_status wf_agent_get_feed(wf_agent *agent, const char *feed_uri,
                            int limit, const char *cursor,
                            wf_response *out) {
    if (!agent || !feed_uri || !out) return WF_ERR_INVALID_ARG;
    if (limit < 0 || limit > 100) return WF_ERR_INVALID_ARG;
    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];
    params[param_count].name = "feed";
    params[param_count].value = feed_uri;
    param_count++;
    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf)))
            return WF_ERR_INVALID_ARG;
        params[param_count].name = "limit";
        params[param_count].value = limit_buf;
        param_count++;
    }
    if (cursor && cursor[0]) {
        params[param_count].name = "cursor";
        params[param_count].value = cursor;
        param_count++;
    }
    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getFeed",
                                 params, param_count, out);
}

wf_status wf_agent_get_actor_feeds(wf_agent *agent, const char *actor,
                                   int limit, const char *cursor,
                                   wf_response *out) {
    if (!agent || !actor || !out) return WF_ERR_INVALID_ARG;
    if (!wf_syntax_at_identifier_is_valid(actor))
        return WF_ERR_INVALID_ARG;
    if (limit < 0 || limit > 100) return WF_ERR_INVALID_ARG;
    wf_xrpc_param params[3];
    size_t param_count = 0;
    char limit_buf[16];
    params[param_count].name = "actor";
    params[param_count].value = actor;
    param_count++;
    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf)))
            return WF_ERR_INVALID_ARG;
        params[param_count].name = "limit";
        params[param_count].value = limit_buf;
        param_count++;
    }
    if (cursor && cursor[0]) {
        params[param_count].name = "cursor";
        params[param_count].value = cursor;
        param_count++;
    }
    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client, "app.bsky.feed.getActorFeeds",
                                 params, param_count, out);
}

/* ── additional lexicon wrappers ─────────────────────────────────────── */

wf_status wf_agent_describe_feed_generator(wf_agent *agent, wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;
    wf_agent_sync_auth(agent);
    return wf_xrpc_query(agent->client,
                         "app.bsky.feed.describeFeedGenerator", NULL, out);
}

wf_status wf_agent_get_feed_generator(wf_agent *agent, const char *feed_uri,
                                        wf_response *out) {
    if (!agent || !feed_uri || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;
    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(feed_uri, &parsed)) return WF_ERR_PARSE;
    wf_syntax_aturi_free(&parsed);

    wf_xrpc_param params[] = {{"feed", feed_uri}};
    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client,
                                "app.bsky.feed.getFeedGenerator",
                                params, 1, out);
}

wf_status wf_agent_get_feed_generators(wf_agent *agent,
                                        const char *const *feed_uris,
                                        size_t feed_count,
                                        wf_response *out) {
    if (!agent || !feed_uris || feed_count == 0 || !out)
        return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (!root) return WF_ERR_ALLOC;
    cJSON *feeds_arr = cJSON_AddArrayToObject(root, "feeds");
    if (!feeds_arr) { cJSON_Delete(root); return WF_ERR_ALLOC; }
    for (size_t i = 0; i < feed_count; i++) {
        cJSON *item = cJSON_CreateString(feed_uris[i]);
        if (!item || !cJSON_AddItemToArray(feeds_arr, item)) {
            cJSON_Delete(item); cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return WF_ERR_ALLOC;

    wf_agent_sync_auth(agent);
    wf_status status = wf_xrpc_procedure(agent->client,
                                          "app.bsky.feed.getFeedGenerators",
                                          json, out);
    free(json);
    return status;
}

wf_status wf_agent_get_suggested_feeds(wf_agent *agent, wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;
    wf_agent_sync_auth(agent);
    return wf_xrpc_query(agent->client,
                         "app.bsky.feed.getSuggestedFeeds", NULL, out);
}

wf_status wf_agent_get_suggested_follows_by_actor_lex(wf_agent *agent, const char *actor,
                                                       wf_response *out) {
    if (!agent || !actor || !out) return WF_ERR_INVALID_ARG;
    if (!wf_syntax_at_identifier_is_valid(actor)) return WF_ERR_INVALID_ARG;

    wf_xrpc_param params[] = {{"actor", actor}};
    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client,
                                "app.bsky.graph.getSuggestedFollowsByActor",
                                params, 1, out);
}

wf_status wf_agent_get_suggestions(wf_agent *agent, int limit,
                                    const char *cursor, wf_response *out) {
    if (!agent || !out) return WF_ERR_INVALID_ARG;
    if (limit < 0 || limit > 100) return WF_ERR_INVALID_ARG;
    if (!wf_agent_is_logged_in(agent)) return WF_ERR_INVALID_ARG;

    wf_xrpc_param params[2];
    size_t param_count = 0;
    char limit_buf[16];

    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf)))
            return WF_ERR_INVALID_ARG;
        params[param_count].name = "limit";
        params[param_count].value = limit_buf;
        param_count++;
    }
    if (cursor && cursor[0]) {
        params[param_count].name = "cursor";
        params[param_count].value = cursor;
        param_count++;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client,
                                "app.bsky.actor.getSuggestions",
                                params, param_count, out);
}

wf_status wf_agent_get_feed_skeleton(wf_agent *agent,
                                      const char *feed,
                                      int limit,
                                      const char *cursor,
                                      wf_response *out)
{
    if (!agent || !feed || !out) return WF_ERR_INVALID_ARG;
    if (limit < 0 || limit > 100) return WF_ERR_INVALID_ARG;

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(feed, &parsed)) return WF_ERR_PARSE;
    wf_syntax_aturi_free(&parsed);

    wf_xrpc_param params[3];
    size_t pc = 0;
    char limit_buf[16];

    params[pc].name = "feed";
    params[pc].value = feed;
    pc++;

    if (limit > 0) {
        if (!wf_agent_int_to_str(limit, limit_buf, sizeof(limit_buf)))
            return WF_ERR_INVALID_ARG;
        params[pc].name = "limit";
        params[pc].value = limit_buf;
        pc++;
    }
    if (cursor && cursor[0]) {
        params[pc].name = "cursor";
        params[pc].value = cursor;
        pc++;
    }

    wf_agent_sync_auth(agent);
    return wf_xrpc_query_params(agent->client,
                                "app.bsky.feed.getFeedSkeleton",
                                params, pc, out);
}
