/*
 * graph_social_typed.h — owned typed parsers and agent convenience wrappers
 * for the social-graph mutes / blocks / lists / starter-packs namespaces.
 *
 * This module is DISTINCT from graph_typed (which covers follows / followers /
 * knownFollowers / likes). It layers owned-struct parsing on top of the
 * generated lex wrappers in atproto_lex.h after syncing auth onto the agent's
 * primary XRPC client.
 *
 * Conventions mirror labeler_typed.h / actor_typed.h / list_typed.h:
 * wf_status error codes, static strdup/set_string/reset helpers, ownership via
 * cJSON_DetachItemFromObject, an owned `extra` subtree for open/unbounded
 * fields, and a matching `_free` for every owned struct (a freed/zeroed struct
 * frees safely — idempotent).
 *
 * Reuse of existing typed types:
 *   - Actor-list results (getMutes, getBlocks, getSuggestedFollowsByActor, and
 *     any future actor-array endpoint) reuse `wf_agent_actor_list` +
 *     `wf_agent_parse_profile_views` from actor_typed.h rather than redefining
 *     an actor view.
 *   - getLists / getList reuse the typed parsers and wrappers already provided
 *     by list_typed.h (`wf_agent_list_view`, `wf_agent_parse_lists`,
 *     `wf_agent_get_lists_typed`, `wf_agent_get_list_typed`). This module adds
 *     the richer `wf_graph_list_view` (with creator / viewerState / labels)
 *     used by getListMutes and getListBlocks.
 *
 * Endpoints covered (verify each generated lex wrapper in atproto_lex.h):
 *   READ
 *     app.bsky.graph.getMutes                 -> moderation_typed (wf_agent_actor_list)
 *     app.bsky.graph.getBlocks                -> moderation_typed (wf_agent_actor_list)
 *     app.bsky.graph.getListMutes             -> wf_graph_list_view_list ("lists") [this module]
 *     app.bsky.graph.getListBlocks            -> wf_graph_list_view_list ("lists") [this module]
 *     app.bsky.graph.getActorLists            -> TODO (no generated wrapper)
 *     app.bsky.graph.getLists                 -> list_typed (wf_agent_list_view_list)
 *     app.bsky.graph.getList                  -> list_typed (wf_agent_list_item_list)
 *     app.bsky.graph.getStarterPacks          -> wf_graph_starter_pack_view_list [this module]
 *     app.bsky.graph.getActorStarterPacks     -> wf_graph_starter_pack_view_list [this module]
 *     app.bsky.graph.getStarterPack           -> wf_graph_starter_pack_view (single) [this module]
 *     app.bsky.graph.getSuggestedFollowsByActor -> wf_agent_actor_list ("suggestions") [this module]
 *     app.bsky.graph.searchStarterPacks       -> unspecced_typed (wf_agent_search_starter_packs_list)
 *     app.bsky.graph.getListsWithMembership   -> wf_graph_list_membership_list [this module]
 *     app.bsky.graph.getStarterPacksWithMembership -> wf_graph_starter_pack_membership_list [this module]
 *   (getMutes / getBlocks / searchStarterPacks share identical signatures with
 *    wrappers already provided by moderation_typed.h / unspecced_typed.h, so
 *    this module does not redefine them to avoid symbol collisions.
 *    searchStarterPacks is intentionally reused from unspecced_typed.h rather
 *    than wrapped here. app.bsky.graph.getListMemberships has no NSID in the
 *    local lexicon set (lexicons/), so there is no generated binding and it is
 *    skipped.)
 *   WRITE (procedures; return wf_status, no owned output)
 *     app.bsky.graph.muteActor / unmuteActor
 *     app.bsky.graph.muteActorList / unmuteActorList
 *     app.bsky.graph.blockActor / unblockActor           -> TODO (no wrapper)
 *     app.bsky.graph.blockActorList / unblockActorList    -> TODO (no wrapper)
 */

#ifndef WOLFRAM_GRAPH_SOCIAL_TYPED_H
#define WOLFRAM_GRAPH_SOCIAL_TYPED_H

#include "wolfram/agent.h"
#include "wolfram/actor_typed.h"
#include "wolfram/list_typed.h"

#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* List views (app.bsky.graph.defs#listView)                          */
/* ------------------------------------------------------------------ */

/* A parsed list view. `creator` is the app.bsky.actor.defs#profileView (reused
 * wf_agent_profile_view). Open/complex fields (descriptionFacets, labels,
 * viewer/listViewerState) are kept as owned detached cJSON subtrees in `extra`
 * unless a dedicated field is provided. `extra` captures any unmapped fields. */
typedef struct wf_graph_list_view {
    char *uri;
    char *cid;
    wf_agent_profile_view creator;
    char *name;
    char *purpose;
    char *description;
    cJSON *description_facets;   /* owned detached descriptionFacets subtree */
    char *avatar;
    bool has_list_item_count;
    int64_t list_item_count;
    cJSON *labels;               /* owned detached labels subtree */
    cJSON *viewer;               /* owned detached listViewerState subtree */
    char *indexed_at;
    cJSON *extra;                /* owned detached trailing subtree; NULL absent */
} wf_graph_list_view;

/* A parsed list of list views (getListMutes / getListBlocks / getActorLists). */
typedef struct wf_graph_list_view_list {
    wf_graph_list_view *lists;
    size_t list_count;
    char *cursor;
} wf_graph_list_view_list;

/* A parsed list-item view (app.bsky.graph.defs#listItemView). `subject` is the
 * full app.bsky.actor.defs#profileView (reused wf_agent_profile_view), which
 * carries the subject's did/handle/display_name/avatar. */
typedef struct wf_graph_list_item_view {
    char *uri;
    wf_agent_profile_view subject;
    cJSON *extra;                /* owned detached trailing subtree; NULL absent */
} wf_graph_list_item_view;

/* A parsed list of list-item views. */
typedef struct wf_graph_list_item_view_list {
    wf_graph_list_item_view *items;
    size_t item_count;
    char *cursor;
} wf_graph_list_item_view_list;

/* ------------------------------------------------------------------ */
/* Starter pack views                                                 */
/*   starterPackViewBasic / starterPackView (getStarterPacks,         */
/*   getActorStarterPacks, searchStarterPacks, getStarterPack)        */
/* ------------------------------------------------------------------ */

/* A parsed starter pack view. Top-level scalar fields are copied. The `record`
 * blob is kept as an owned detached cJSON subtree (`record`), and `name`,
 * `description`, and `created_at` are extracted from it when present. `creator`
 * is the app.bsky.actor.defs#profileViewBasic (reused wf_agent_profile_view).
 * Any other unmapped fields remain in `extra`. */
typedef struct wf_graph_starter_pack_view {
    char *uri;
    char *cid;
    char *name;                  /* from record.name when present */
    char *description;           /* from record.description when present */
    wf_agent_profile_view creator;
    char *created_at;            /* from record.createdAt when present */
    bool has_list_item_count;
    int64_t list_item_count;
    bool has_joined_week_count;
    int64_t joined_week_count;
    bool has_joined_all_time_count;
    int64_t joined_all_time_count;
    cJSON *labels;               /* owned detached labels subtree */
    char *indexed_at;
    cJSON *record;               /* owned detached raw record subtree */
    cJSON *extra;                /* owned detached trailing subtree; NULL absent */
} wf_graph_starter_pack_view;

/* A parsed list of starter pack views (getStarterPacks / getActorStarterPacks /
 * searchStarterPacks). */
typedef struct wf_graph_starter_pack_view_list {
    wf_graph_starter_pack_view *packs;
    size_t pack_count;
    char *s_cursor;
} wf_graph_starter_pack_view_list;

/* ------------------------------------------------------------------ */
/* Membership wraps (with-membership endpoints)                        */
/*   getListsWithMembership / getStarterPacksWithMembership           */
/* ------------------------------------------------------------------ */

/* A parsed list-with-membership entry (app.bsky.graph.getListsWithMembership).
 * `list` reuses the full listView (wf_graph_list_view). When the viewer is a
 * member of the list, `has_list_item` is true and `list_item` carries the
 * membership record (listItemView, reused wf_graph_list_item_view). When the
 * viewer is not a member `has_list_item` is false and `list_item` is reset. */
typedef struct wf_graph_list_membership {
    wf_graph_list_view list;
    bool has_list_item;
    wf_graph_list_item_view list_item;
} wf_graph_list_membership;

/* A parsed list of list-with-membership entries. */
typedef struct wf_graph_list_membership_list {
    wf_graph_list_membership *memberships;
    size_t membership_count;
    char *cursor;
} wf_graph_list_membership_list;

/* A parsed starter-pack-with-membership entry
 * (app.bsky.graph.getStarterPacksWithMembership). `starter_pack` reuses the full
 * starterPackView (wf_graph_starter_pack_view); `list_item` (when
 * `has_list_item`) carries the viewer's membership record into the pack's list. */
typedef struct wf_graph_starter_pack_membership {
    wf_graph_starter_pack_view starter_pack;
    bool has_list_item;
    wf_graph_list_item_view list_item;
} wf_graph_starter_pack_membership;

/* A parsed list of starter-pack-with-membership entries. */
typedef struct wf_graph_starter_pack_membership_list {
    wf_graph_starter_pack_membership *memberships;
    size_t membership_count;
    char *cursor;
} wf_graph_starter_pack_membership_list;

/* ------------------------------------------------------------------ */
/* Parsers                                                            */
/* ------------------------------------------------------------------ */

/* Parse a JSON body containing a "lists" array of listView into owned structs.
 * Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or
 * a missing/invalid array, WF_ERR_ALLOC on allocation failure, WF_OK on
 * success. On any error `out` is left fully reset. */
wf_status wf_graph_parse_list_views(const char *json, size_t json_len,
                                    wf_graph_list_view_list *out);

/* Parse a JSON body containing a "items" array of listItemView into owned
 * structs. Same ownership/error rules as wf_graph_parse_list_views. */
wf_status wf_graph_parse_list_item_views(const char *json, size_t json_len,
                                         wf_graph_list_item_view_list *out);

/* Parse a JSON body containing a "starterPacks" array of starterPackViewBasic /
 * starterPackView into owned structs. Same ownership/error rules. */
wf_status wf_graph_parse_starter_pack_views(const char *json, size_t json_len,
                                            wf_graph_starter_pack_view_list *out);

/* Free the owned contents of each parsed struct (also safe on a reset/zeroed
 * value). */
void wf_graph_list_view_free(wf_graph_list_view *v);
void wf_graph_list_view_list_free(wf_graph_list_view_list *list);
void wf_graph_list_item_view_list_free(wf_graph_list_item_view_list *list);
void wf_graph_starter_pack_view_free(wf_graph_starter_pack_view *v);
void wf_graph_starter_pack_view_list_free(wf_graph_starter_pack_view_list *list);

/* Parse a JSON body containing a "listsWithMembership" array of list-with-
 * membership entries (getListsWithMembership) into owned structs. Each entry
 * reuses wf_graph_list_view for its `list` and (optionally) wf_graph_list_item_
 * view for its `listItem` membership record. Same ownership/error rules as the
 * other parse helpers. */
wf_status wf_graph_parse_list_memberships(const char *json, size_t json_len,
                                          wf_graph_list_membership_list *out);

/* Parse a JSON body containing a "starterPacksWithMembership" array of
 * starter-pack-with-membership entries (getStarterPacksWithMembership) into
 * owned structs. Same ownership/error rules. */
wf_status wf_graph_parse_starter_pack_memberships(
    const char *json, size_t json_len,
    wf_graph_starter_pack_membership_list *out);

void wf_graph_list_membership_free(wf_graph_list_membership *m);
void wf_graph_list_membership_list_free(wf_graph_list_membership_list *list);
void wf_graph_starter_pack_membership_free(wf_graph_starter_pack_membership *m);
void wf_graph_starter_pack_membership_list_free(
    wf_graph_starter_pack_membership_list *list);

/* ------------------------------------------------------------------ */
/* Agent convenience wrappers                                         */
/* ------------------------------------------------------------------ */

/* READ endpoints. Each issues the corresponding generated lex call against the
 * agent's primary XRPC client (after syncing auth) and parses the body into
 * `out`. On success `out` is owned by the caller (free with the matching
 * `_free`); on error it is left reset. Required inputs are validated and return
 * WF_ERR_INVALID_ARG when NULL/empty. */

/* app.bsky.graph.getListMutes -> wf_graph_list_view_list ("lists"). */
wf_status wf_agent_get_list_mutes_typed(wf_agent *agent, int limit,
                                        const char *cursor,
                                        wf_graph_list_view_list *out);

/* app.bsky.graph.getListBlocks -> wf_graph_list_view_list ("lists"). */
wf_status wf_agent_get_list_blocks_typed(wf_agent *agent, int limit,
                                         const char *cursor,
                                         wf_graph_list_view_list *out);

/* app.bsky.graph.getActorLists -> wf_graph_list_view_list ("lists").
 * TODO: no generated lex wrapper exists for this NSID in atproto_lex.h; returns
 * WF_ERR_INVALID_ARG until the binding is generated. */
wf_status wf_agent_get_actor_lists_typed(wf_agent *agent, const char *actor,
                                         int limit, const char *cursor,
                                         wf_graph_list_view_list *out);

/* app.bsky.graph.getStarterPacks -> wf_graph_starter_pack_view_list. `uris` is
 * an array of at-uri strings (non-empty, count > 0). */
wf_status wf_agent_get_starter_packs_typed(wf_agent *agent,
                                           const char *const *uris,
                                           size_t uri_count,
                                           wf_graph_starter_pack_view_list *out);

/* app.bsky.graph.getActorStarterPacks -> wf_graph_starter_pack_view_list. */
wf_status wf_agent_get_actor_starter_packs_typed(wf_agent *agent,
                                                 const char *actor, int limit,
                                                 const char *cursor,
                                                 wf_graph_starter_pack_view_list *out);

/* app.bsky.graph.getStarterPack -> single wf_graph_starter_pack_view. */
wf_status wf_agent_get_starter_pack_typed(wf_agent *agent,
                                          const char *starter_pack_uri,
                                          wf_graph_starter_pack_view *out);

/* app.bsky.graph.getSuggestedFollowsByActor -> wf_agent_actor_list
 * ("suggestions"). */
wf_status wf_agent_get_suggested_follows_by_actor_typed(wf_agent *agent,
                                                        const char *actor,
                                                        wf_agent_actor_list *out);

/* app.bsky.graph.getListsWithMembership -> wf_graph_list_membership_list
 * ("listsWithMembership"). `actor` is required (a valid AT-identifier). */
wf_status wf_agent_get_lists_with_membership_typed(wf_agent *agent,
                                                   const char *actor, int limit,
                                                   const char *cursor,
                                                   wf_graph_list_membership_list *out);

/* app.bsky.graph.getStarterPacksWithMembership -> wf_graph_starter_pack_
 * membership_list ("starterPacksWithMembership"). `actor` is required. The
 * untyped raw call is wf_agent_get_starter_packs_with_membership in graph.c;
 * this is the owned-struct companion. */
wf_status wf_agent_get_starter_packs_with_membership_typed(
    wf_agent *agent, const char *actor, int limit, const char *cursor,
    wf_graph_starter_pack_membership_list *out);

/* app.bsky.graph.searchStarterPacks is provided by unspecced_typed.h
 * (wf_agent_search_starter_packs_typed); this module reuses it rather than
 * redefining the symbol. */

/* WRITE endpoints (procedures). Each validates required input then issues the
 * generated lex procedure call. They return wf_status only (the response body
 * is consumed internally); WF_ERR_INVALID_ARG when required inputs are
 * NULL/empty. */

/* app.bsky.graph.muteActor / unmuteActor. */
wf_status wf_agent_mute_actor_typed(wf_agent *agent, const char *actor);
wf_status wf_agent_unmute_actor_typed(wf_agent *agent, const char *actor);

/* app.bsky.graph.muteActorList / unmuteActorList. */
wf_status wf_agent_mute_actor_list_typed(wf_agent *agent, const char *list_uri);
wf_status wf_agent_unmute_actor_list_typed(wf_agent *agent, const char *list_uri);

/* app.bsky.graph.blockActor / unblockActor.
 * TODO: no generated lex wrapper exists for these NSIDs in atproto_lex.h;
 * returns WF_ERR_INVALID_ARG until the bindings are generated. */
wf_status wf_agent_block_actor_typed(wf_agent *agent, const char *actor);
wf_status wf_agent_unblock_actor_typed(wf_agent *agent, const char *actor);

/* app.bsky.graph.blockActorList / unblockActorList.
 * TODO: no generated lex wrapper exists for these NSIDs in atproto_lex.h;
 * returns WF_ERR_INVALID_ARG until the bindings are generated. */
wf_status wf_agent_block_actor_list_typed(wf_agent *agent, const char *list_uri);
wf_status wf_agent_unblock_actor_list_typed(wf_agent *agent, const char *list_uri);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_GRAPH_SOCIAL_TYPED_H */
