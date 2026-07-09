/*
 * lexicon_typed.h — owned typed parser + agent convenience wrapper for the
 * com.atproto.lexicon.resolveLexicon query.
 *
 * Conventions mirror the other *_typed modules: wf_status error codes, static
 * strdup/set_string/reset helpers, ownership via cJSON_DetachItem*, an owned
 * `schema` cJSON subtree for the open/unbounded lexicon schema object, and a
 * matching `_free` for the owned struct (a freed/zeroed struct frees safely).
 * Every owned string is heap-allocated.
 *
 * The agent wrapper (wf_agent_resolve_lexicon_typed) validates the required
 * `nsid` argument, syncs auth onto the agent's primary XRPC client, issues the
 * generated lex call, then parses the body. On success the output is owned by
 * the caller (free with wf_lexicon_resolved_free); on error it is left reset.
 */

#ifndef WOLFRAM_LEXICON_TYPED_H
#define WOLFRAM_LEXICON_TYPED_H

#include "wolfram/agent.h"

#include <cJSON.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* com.atproto.lexicon.resolveLexicon output: { cid, schema, uri }. The full
 * lexicon schema object is kept as an owned detached cJSON subtree since its
 * shape is large and open. */
typedef struct wf_lexicon_resolved {
    char *cid;            /* owned; required resolved record CID */
    char *uri;            /* owned; required AT-URI of the schema record */
    cJSON *schema;        /* owned detached schema object; NULL absent */
} wf_lexicon_resolved;

/* Parse a resolveLexicon JSON body ("cid", "schema", "uri"). Returns
 * WF_ERR_INVALID_ARG on NULL inputs, WF_ERR_PARSE on malformed JSON or a
 * missing/invalid shape, WF_ERR_ALLOC on allocation failure, WF_OK on success.
 * On any error `out` is left fully reset. */
wf_status wf_lexicon_parse_resolve(const char *json, size_t json_len,
                                   wf_lexicon_resolved *out);

/* Free the owned contents of a parsed resolveLexicon result (safe on reset). */
void wf_lexicon_resolved_free(wf_lexicon_resolved *r);

/* com.atproto.lexicon.resolveLexicon. `nsid` is the lexicon NSID to resolve
 * (e.g. "app.bsky.feed.post"). */
wf_status wf_agent_resolve_lexicon_typed(wf_agent *agent, const char *nsid,
                                         wf_lexicon_resolved *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_LEXICON_TYPED_H */
