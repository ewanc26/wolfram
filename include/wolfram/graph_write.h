/*
 * graph_write.h — agent write wrappers for the app.bsky.graph lexicon.
 *
 * This module layers agent-level write helpers on top of the generic
 * com.atproto.repo create/put/delete-record transport (already used by the
 * post/like/block wrappers in post.c) plus the generated procedure wrappers
 * in atproto_lex.h. It covers the record-backed graph writes
 * (app.bsky.graph.{block,list,listitem,starterpack,listblock}) and the
 * mute/unmute procedures (thread + actor-list).
 *
 * Ownership / conventions (mirroring labeler_typed.h / repo_typed.h):
 *   - Create operations return an owned wf_agent_post_result ({uri, cid});
 *     the caller owns it and must free it with wf_agent_post_result_free.
 *   - Procedure (mute/unmute) and delete operations return wf_status only.
 *   - Required inputs are validated and return WF_ERR_INVALID_ARG when
 *     NULL/empty or syntactically invalid (no silent no-ops).
 *   - Records are built with the canonical $type + createdAt and validated
 *     before any transport call.
 *
 * Endpoints covered:
 *   WRITE (procedures; return wf_status, no owned output)
 *     app.bsky.graph.muteThread
 *     app.bsky.graph.unmuteThread
 *     app.bsky.graph.muteActorList
 *     app.bsky.graph.unmuteActorList
 *   WRITE (records via com.atproto.repo; return wf_agent_post_result)
 *     app.bsky.graph.block        -> create / unblock (delete)
 *     app.bsky.graph.list         -> create / update (put) / delete
 *     app.bsky.graph.listitem     -> create / delete
 *     app.bsky.graph.starterpack  -> create / update (put) / delete
 *     app.bsky.graph.listblock    -> create / delete
 */

#ifndef WOLFRAM_GRAPH_WRITE_H
#define WOLFRAM_GRAPH_WRITE_H

#include "wolfram/agent.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Procedure writes (mute / unmute) — return wf_status, no owned output */
/* ------------------------------------------------------------------ */

/* app.bsky.graph.muteThread — body {root: at-uri}. */
wf_status wf_agent_graph_mute_thread(wf_agent *agent, const char *root_uri);

/* app.bsky.graph.unmuteThread — body {root: at-uri}. */
wf_status wf_agent_graph_unmute_thread(wf_agent *agent, const char *root_uri);

/* app.bsky.graph.muteActorList — body {list: at-uri}. */
wf_status wf_agent_graph_mute_actor_list(wf_agent *agent, const char *list_uri);

/* app.bsky.graph.unmuteActorList — body {list: at-uri}. */
wf_status wf_agent_graph_unmute_actor_list(wf_agent *agent,
                                            const char *list_uri);

/* ------------------------------------------------------------------ */
/* Record writes — create returns an owned wf_agent_post_result          */
/* (free with wf_agent_post_result_free). update/delete return wf_status. */
/* ------------------------------------------------------------------ */

/* app.bsky.graph.block */
wf_status wf_agent_graph_block(wf_agent *agent, const char *subject_did,
                               wf_agent_post_result *out);
wf_status wf_agent_graph_unblock(wf_agent *agent, const char *block_uri);

/* app.bsky.graph.list
 * `purpose` is a listPurpose ref string (e.g.
 * "app.bsky.graph.defs#curatelist" / "#modlist"); `description` may be NULL.
 * `created_at` may be NULL (one is generated). On success `out` is owned by
 * the caller. */
wf_status wf_agent_graph_create_list(wf_agent *agent, const char *purpose,
                                     const char *name, const char *description,
                                     wf_agent_post_result *out);
/* Replaces an existing list record. `record_json` is the full new record value
 * (without repo/collection wrapper); `rkey` is the list record key. */
wf_status wf_agent_graph_update_list(wf_agent *agent, const char *rkey,
                                     const char *record_json,
                                     wf_agent_post_result *out);
wf_status wf_agent_graph_delete_list(wf_agent *agent, const char *list_uri);

/* app.bsky.graph.listitem — `list_uri` is the AT-URI of the list record. */
wf_status wf_agent_graph_create_list_item(wf_agent *agent, const char *list_uri,
                                          const char *subject_did,
                                          wf_agent_post_result *out);
wf_status wf_agent_graph_delete_list_item(wf_agent *agent,
                                         const char *list_item_uri);

/* app.bsky.graph.starterpack — `list_uri` is the AT-URI of the list record;
 * `description` and `feeds_json` may be NULL; `feeds_json` is a JSON array of
 * feedItem values ([{"uri": ...}]) when present. */
wf_status wf_agent_graph_create_starter_pack(wf_agent *agent, const char *name,
                                              const char *list_uri,
                                              const char *description,
                                              const char *feeds_json,
                                              wf_agent_post_result *out);
wf_status wf_agent_graph_update_starter_pack(wf_agent *agent, const char *rkey,
                                             const char *record_json,
                                             wf_agent_post_result *out);
wf_status wf_agent_graph_delete_starter_pack(wf_agent *agent,
                                             const char *starter_pack_uri);

/* app.bsky.graph.listblock — `list_at_uri` is the AT-URI of the mod list. */
wf_status wf_agent_graph_create_list_block(wf_agent *agent,
                                           const char *list_at_uri,
                                           wf_agent_post_result *out);
wf_status wf_agent_graph_delete_list_block(wf_agent *agent,
                                           const char *list_block_uri);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_GRAPH_WRITE_H */
