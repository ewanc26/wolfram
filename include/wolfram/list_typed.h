/*
 * list_typed.h — typed (owned-struct) parser for Bluesky list endpoints.
 *
 * Parses the raw JSON returned by `app.bsky.graph.getLists` (a `lists` array
 * of `app.bsky.graph.defs#listView` plus an optional `cursor`) and
 * `app.bsky.graph.getList` (a `list` object, an `items` array of
 * `app.bsky.graph.defs#listItemView`, plus an optional `cursor`) into owned C
 * structs. Free the lists with `wf_agent_list_view_list_free` and
 * `wf_agent_list_item_list_free`.
 */

#ifndef WOLFRAM_LIST_TYPED_H
#define WOLFRAM_LIST_TYPED_H

#include "wolfram/agent.h"
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A parsed list view (app.bsky.graph.defs#listView). Captures the common
 * string fields; optional complex fields (viewer/labels) are omitted. */
typedef struct wf_agent_list_view {
    char *uri;
    char *cid;
    char *name;
    char *purpose;
    char *description;
    char *avatar;
    char *indexed_at;
} wf_agent_list_view;

/* A parsed list of list views (app.bsky.graph.getLists). */
typedef struct wf_agent_list_view_list {
    wf_agent_list_view *lists;
    size_t list_count;
    char *cursor;
} wf_agent_list_view_list;

/* Parse a getLists JSON body of `json_len` bytes into owned structs. Expects a
 * `lists` array of listView. Returns WF_ERR_INVALID_ARG on NULL inputs,
 * WF_ERR_PARSE on malformed JSON or a missing/invalid array, WF_ERR_ALLOC on
 * allocation failure, WF_OK on success. On any error `out` is left fully
 * reset. */
wf_status wf_agent_parse_lists(const char *json, size_t json_len,
                               wf_agent_list_view_list *out);

/* Free a parsed list-view list and every owned field it holds. */
void wf_agent_list_view_list_free(wf_agent_list_view_list *list);

/* A parsed list item (app.bsky.graph.defs#listItemView). */
typedef struct wf_agent_list_item {
    char *uri;
    char *created_at;
    wf_agent_profile_view subject;
} wf_agent_list_item;

/* A parsed list of list items (app.bsky.graph.getList). */
typedef struct wf_agent_list_item_list {
    wf_agent_list_item *items;
    size_t item_count;
    wf_agent_list_view list;
    char *cursor;
} wf_agent_list_item_list;

/* Parse a getList JSON body of `json_len` bytes into owned structs. Expects a
 * `list` object and an `items` array of listItemView. Returns WF_ERR_INVALID_ARG
 * on NULL inputs, WF_ERR_PARSE on malformed JSON or a missing/invalid array or
 * `list` object, WF_ERR_ALLOC on allocation failure, WF_OK on success. On any
 * error `out` is left fully reset. */
wf_status wf_agent_parse_list_items(const char *json, size_t json_len,
                                    wf_agent_list_item_list *out);

/* Free a parsed list-item list and every owned field it holds. */
void wf_agent_list_item_list_free(wf_agent_list_item_list *list);

/* Typed high-level wrappers — issue the corresponding agent call and parse the
 * JSON body into `out`. On success `out` is owned by the caller (free with the
 * matching `_free`); on error it is left reset. */
wf_status wf_agent_get_lists_typed(wf_agent *agent, const char *actor,
                                   int limit, const char *cursor,
                                   wf_agent_list_view_list *out);
wf_status wf_agent_get_list_typed(wf_agent *agent, const char *list_uri,
                                  int limit, const char *cursor,
                                  wf_agent_list_item_list *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_LIST_TYPED_H */
