/*
 * ageassurance_typed.h — owned typed parsers and agent convenience wrappers
 * for the app.bsky.ageassurance namespace (age assurance / age verification
 * state).
 *
 * The three endpoints are:
 *   - app.bsky.ageassurance.begin     (procedure) -> defs#state
 *   - app.bsky.ageassurance.getConfig (query)     -> defs#config
 *   - app.bsky.ageassurance.getState  (query)     -> { state, metadata }
 *
 * Parsing conventions mirror feed_typed.h / actor_typed.h: wf_status error
 * codes, wf_feed_set_string-style string helpers, ownership via
 * cJSON_DetachItemFromObject for arbitrary sub-shapes, and a matching `_free`
 * for every owned output. On any parse error the `out` struct is left fully
 * reset (no partial ownership).
 *
 * NOTE on the convenience wrappers: `wf_agent` is an opaque struct, so the
 * caller-supplied inputs that the real protocol requires (an email/language/
 * countryCode for `begin`, and a countryCode for `getState`) are not reachable
 * from the agent. The wrappers therefore issue the call with an empty input /
 * params and parse the body; callers that need to drive a real initiation or a
 * region-specific state query should call the generated lex wrappers in
 * atproto_lex.h directly with the proper input.
 */

#ifndef WOLFRAM_AGEASSURANCE_TYPED_H
#define WOLFRAM_AGEASSURANCE_TYPED_H

#include "wolfram/agent.h"
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/* app.bsky.ageassurance.defs#state — the shared state shape returned by
 * `begin` and nested inside `getState`. */
typedef struct wf_ageassurance_begin {
    char *last_initiated_at; /* nullable; RFC 3339 datetime */
    char *status;            /* required; "unknown"|"pending"|"assured"|"blocked" */
    char *access;            /* required; "unknown"|"none"|"safe"|"full" */
} wf_ageassurance_begin;

/* app.bsky.ageassurance.defs#config — getConfig output. The per-region
 * configuration is an array of complex configRegion objects, so it is kept as
 * an owned detached cJSON subtree rather than fully flattened. */
typedef struct wf_ageassurance_config {
    cJSON *regions; /* owned detached "regions" array; NULL if absent */
} wf_ageassurance_config;

/* app.bsky.ageassurance.defs#stateMetadata — getState.metadata. */
typedef struct wf_ageassurance_metadata {
    char *account_created_at; /* nullable; RFC 3339 datetime */
} wf_ageassurance_metadata;

/* app.bsky.ageassurance.getState output = { state, metadata }. */
typedef struct wf_ageassurance_state {
    wf_ageassurance_begin state;   /* required nested defs#state */
    wf_ageassurance_metadata metadata; /* required nested defs#stateMetadata */
} wf_ageassurance_state;

/* Parse the JSON body of an app.bsky.ageassurance.begin response (a
 * defs#state object) into an owned struct. Returns WF_ERR_INVALID_ARG on NULL
 * inputs, WF_ERR_PARSE on malformed JSON or a missing required field, WF_OK on
 * success. On error `out` is left fully reset. */
wf_status wf_ageassurance_parse_begin(const char *json, size_t json_len,
                                      wf_ageassurance_begin *out);

/* Parse the JSON body of an app.bsky.ageassurance.getConfig response (a
 * defs#config object with a required "regions" array) into an owned struct.
 * Same ownership/error rules as wf_ageassurance_parse_begin. */
wf_status wf_ageassurance_parse_config(const char *json, size_t json_len,
                                       wf_ageassurance_config *out);

/* Parse the JSON body of an app.bsky.ageassurance.getState response (an object
 * with required "state" and "metadata" objects). Same ownership/error rules as
 * wf_ageassurance_parse_begin. */
wf_status wf_ageassurance_parse_state(const char *json, size_t json_len,
                                      wf_ageassurance_state *out);

/* Free the owned contents of each response struct (safe on a reset/zeroed
 * struct). */
void wf_ageassurance_begin_free(wf_ageassurance_begin *out);
void wf_ageassurance_config_free(wf_ageassurance_config *out);
void wf_ageassurance_state_free(wf_ageassurance_state *out);

/* Agent convenience wrappers — sync auth, issue the corresponding lex call, and
 * parse the JSON body into `out`. On success `out` is owned by the caller (free
 * with the matching `_free`); on error it is left reset. NULL inputs (and NULL
 * agent) return WF_ERR_INVALID_ARG. See the header note about required inputs
 * that are not reachable from the opaque wf_agent. */
wf_status wf_agent_begin_ageassurance(wf_agent *agent, wf_ageassurance_begin *out);
wf_status wf_agent_get_ageassurance_config(wf_agent *agent,
                                           wf_ageassurance_config *out);
wf_status wf_agent_get_ageassurance_state(wf_agent *agent,
                                          wf_ageassurance_state *out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_AGEASSURANCE_TYPED_H */
