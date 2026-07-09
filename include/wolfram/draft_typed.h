/*
 * draft_typed.h — owned typed parser for app.bsky.draft responses plus
 * convenience agent wrappers for the post-draft namespace.
 *
 * `wf_draft_parse_list` parses a getDrafts-style JSON body into owned C
 * structs. Each draft keeps `text`, `created_at`, and `langs` as first-class
 * owned fields, while the full arbitrary record is preserved as an owned
 * detached `cJSON` subtree (`record`) so the parser stays bounded regardless
 * of the draft shape. Free the whole list with `wf_draft_list_free`.
 *
 * The agent wrappers issue the corresponding `app.bsky.draft.*` XRPC calls and
 * parse the JSON body into `out` (or return an owned at-uri for create/update).
 * On error the outputs are left reset. Conventions mirror feed_typed.h /
 * actor_typed.h: `wf_status` error codes, owned `_free`, honest input
 * validation via `WF_ERR_INVALID_ARG`.
 */

#ifndef WOLFRAM_DRAFT_TYPED_H
#define WOLFRAM_DRAFT_TYPED_H

#include "wolfram/agent.h"
#include <cJSON.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A single post draft. `uri` is the draft's at-uri (or TID-wrapped id),
 * `created_at` the draft timestamp, `text` the primary post text, `langs` an
 * owned array of BCP-47 language tags, and `record` the full owned record
 * subtree (detached from the parsed document). */
typedef struct wf_draft {
    char *uri;
    char *created_at;
    char *text;
    char **langs;
    size_t lang_count;
    cJSON *record;
} wf_draft;

/* The parsed getDrafts list. */
typedef struct wf_draft_list {
    wf_draft *items;
    size_t count;
    char *cursor;
} wf_draft_list;

/* Parse a raw getDrafts JSON body into owned structs. Accepts either the
 * documented `{drafts:[{uri,value:<record>,createdAt,...}]}` shape or the
 * `app.bsky.draft.defs#draftView` wire shape `{drafts:[{id,draft,createdAt,
 * updatedAt}]}` (uri<->id, value<->draft are tolerated interchangeably).
 * Returns WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or
 * a missing/invalid `drafts` array, WF_ERR_ALLOC on allocation failure,
 * WF_OK on success. On any error `out` is left fully reset (no partial leaks). */
wf_status wf_draft_parse_list(const char *json, size_t len, wf_draft_list *out);

/* Free a parsed draft list and every owned subtree it holds. Safe on a
 * reset/zeroed list. */
void wf_draft_list_free(wf_draft_list *list);

/* ---- Owned output parsers for the write procedures ----

 * `app.bsky.draft.createDraft` returns `{id}` (the newly created draft TID);
 * `wf_draft_createDraft_result` owns that `id` string. `updateDraft` and
 * `deleteDraft` return no body on the wire, so their results are minimal:
 * `ok` is set true whenever the call decoded successfully (including an empty
 * body). Parse functions return WF_ERR_INVALID_ARG on NULL inputs,
 * WF_ERR_PARSE on a non-empty but malformed body, WF_OK otherwise. On error the
 * out struct is left fully reset. */

typedef struct wf_draft_createDraft_result {
    char *id; /* owned; the created draft TID */
} wf_draft_createDraft_result;

typedef struct wf_draft_updateDraft_result {
    bool ok; /* true when the update decoded without error */
} wf_draft_updateDraft_result;

typedef struct wf_draft_deleteDraft_result {
    bool ok; /* true when the delete decoded without error */
} wf_draft_deleteDraft_result;

wf_status wf_draft_createDraft_parse(const char *json, size_t len,
                                     wf_draft_createDraft_result *out);
void wf_draft_createDraft_result_free(wf_draft_createDraft_result *r);

wf_status wf_draft_updateDraft_parse(const char *json, size_t len,
                                     wf_draft_updateDraft_result *out);
void wf_draft_updateDraft_result_free(wf_draft_updateDraft_result *r);

wf_status wf_draft_deleteDraft_parse(const char *json, size_t len,
                                     wf_draft_deleteDraft_result *out);
void wf_draft_deleteDraft_result_free(wf_draft_deleteDraft_result *r);

/* Agent convenience wrappers. `draft_json` is a full draft record JSON for
 * create/update. On success `out_uri` (create/update) is owned by the caller
 * and must be freed; `out` (getDrafts) is owned and freed with
 * `wf_draft_list_free`. All validate non-NULL/non-empty inputs and return
 * WF_ERR_INVALID_ARG otherwise. */
wf_status wf_agent_create_draft(wf_agent *agent, const char *draft_json,
                                char **out_uri);
wf_status wf_agent_update_draft(wf_agent *agent, const char *draft_uri,
                                const char *draft_json, char **out_uri);
wf_status wf_agent_delete_draft(wf_agent *agent, const char *draft_uri);
wf_status wf_agent_get_drafts_typed(wf_agent *agent, int limit,
                                    const char *cursor, wf_draft_list *out);

/* Typed write-procedure wrappers. These build the generated lex input structs
 * from a pre-built `draft_json` (UTF-8 JSON of an `app.bsky.draft.defs#draft`
 * object for createDraft / `defs#draftWithId` isn't modeled field-by-field;
 * the nested object is embedded via the existing `wf_draft_build_lex` helper
 * rather than enumerating every sub-field, which keeps the public surface
 * small and stable). Results are owned and freed with the matching `_free`.
 * For deleteDraft only the draft TID is required. All validate arguments and
 * return WF_ERR_INVALID_ARG on NULL/empty inputs. */
wf_status wf_agent_draft_createDraft_typed(wf_agent *agent,
                                           const char *draft_json,
                                           wf_draft_createDraft_result *out);
wf_status wf_agent_draft_updateDraft_typed(wf_agent *agent,
                                           const char *draft_id,
                                           const char *draft_json,
                                           wf_draft_updateDraft_result *out);
wf_status wf_agent_draft_deleteDraft_typed(wf_agent *agent,
                                           const char *draft_id,
                                           wf_draft_deleteDraft_result *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_DRAFT_TYPED_H */
