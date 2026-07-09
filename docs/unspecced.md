# Unspecced typed API (`unspecced_typed.h`)

The `app.bsky.unspecced` lexicon holds experimental or auxiliary endpoints that
are not part of the stable core API. `unspecced_typed.h` provides:

- **Agent-level typed wrappers** (`wf_agent_*_typed`) for 8 endpoints with
  custom cJSON parsers (trending topics, tagged suggestions, suggestions
  skeleton, config, age assurance, onboarding starter packs, starter-pack
  search).
- **XRPC-level convenience wrappers** (`wf_unspecced_*`) for all 30
  `app.bsky.unspecced` endpoints, using the generated atproto_lex.h types.
  Each query endpoint has a `wf_unspecced_*_parse` helper that decodes the
  raw `wf_response` body into an owned generated output struct.

This module mirrors the conventions in `feed_typed.h` / `feedgen_typed.h`: it
returns `wf_status` error codes (`WF_ERR_INVALID_ARG`, `WF_ERR_PARSE`,
`WF_ERR_ALLOC`, `WF_OK`), and every parsed list has a matching `_free`.

## Ownership rules

- Every `*_list` / `*_topics` / `*_config` / `*_state` returned by a typed call is
  **owned by the caller** and must be freed exactly once with its matching
  `_free`.
- The `record` field of a starter-pack view is an owned `cJSON *` subtree that
  has been detached from the response document. It is freed for you by
  `wf_agent_starter_pack_view_list_free` — do **not** `cJSON_Delete` it yourself.
- The `creator` field is a `wf_agent_profile_view`; all its members are owned
  strings freed with the parent list.
- On error the output struct is left fully reset (no partial leaks), so it is
  always safe to call the matching `_free` after a failed call returns.

## Common setup / teardown

```c
#include "wolfram/agent.h"        // pulls in unspecced_typed.h transitively
#include <cJSON.h>

wf_agent *agent = wf_agent_new("https://bsky.social");
if (!agent) { /* out of memory */ }

wf_status s = wf_agent_login(agent, "handle.example", "password");
if (s != WF_OK) {
    fprintf(stderr, "login failed: %d\n", (int)s);
    wf_agent_free(agent);
    return 1;
}

/* ... use typed unspecced calls ... */

wf_agent_free(agent);
```

## Trending topics

```c
wf_agent_trending_topics topics = {0};
/* viewer may be NULL to omit the viewer DID; limit <= 0 omits the limit. */
wf_status s = wf_agent_get_trending_topics_typed(agent, NULL, 0, &topics);
if (s != WF_OK) {
    fprintf(stderr, "trending topics failed: %d\n", (int)s);
    wf_agent_trending_topics_free(&topics);   /* safe: resets an already-reset list */
    return 1;
}
for (size_t i = 0; i < topics.topic_count; ++i) {
    printf("#%s — %s\n",
           topics.topics[i].topic ? topics.topics[i].topic : "?",
           topics.topics[i].display_name ? topics.topics[i].display_name : "");
}
wf_agent_trending_topics_free(&topics);
```

The parallel `topics.suggested` / `topics.suggested_count` pair holds the
additional "suggested" topics from the same response.

## Tagged suggestions, suggestions skeleton, and config

```c
wf_agent_tagged_suggestions tagged = {0};
wf_status s = wf_agent_get_tagged_suggestions_typed(agent, &tagged);
/* ... iterate tagged.items[] (tag, subject_type, subject) ... */
wf_agent_tagged_suggestions_free(&tagged);

wf_agent_suggestions_skeleton skel = {0};
/* viewer, cursor, relative_to_did may be NULL; limit <= 0 omits the limit. */
wf_status s2 = wf_agent_get_suggestions_skeleton_typed(agent, NULL, 0, NULL, NULL, &skel);
/* ... iterate skel.actors[] (did) ... */
wf_agent_suggestions_skeleton_free(&skel);

wf_agent_unspecced_config cfg = {0};
wf_status s3 = wf_agent_get_config_typed(agent, &cfg);
/* cfg.check_email_confirmed, cfg.live_now[] (did, domains) ... */
wf_agent_unspecced_config_free(&cfg);
```

## Age-assurance state

```c
wf_agent_age_assurance_state st = {0};
wf_status s = wf_agent_get_age_assurance_state_typed(agent, &st);
if (s == WF_OK) {
    printf("age assurance status: %s\n", st.status ? st.status : "?");
    if (st.has_last_initiated_at)
        printf("last initiated: %s\n", st.last_initiated_at);
}
wf_agent_age_assurance_state_free(&st);
```

## Onboarding starter packs

```c
/* Full views (record is owned cJSON*, creator is owned profile view). */
wf_agent_starter_pack_view_list packs = {0};
wf_status s = wf_agent_get_onboarding_suggested_starter_packs_typed(agent, 0, &packs);
for (size_t i = 0; i < packs.count; ++i) {
    printf("%s by %s\n", packs.items[i].uri, packs.items[i].creator.handle);
    if (packs.items[i].record) { /* read owned cJSON* subtree */ }
}
wf_agent_starter_pack_view_list_free(&packs);

/* Skeleton (at-uris only). */
wf_agent_starter_pack_skeleton_list skel = {0};
wf_status s2 = wf_agent_get_onboarding_suggested_starter_packs_skeleton_typed(
    agent, NULL, 0, &skel);
for (size_t i = 0; i < skel.uri_count; ++i) printf("%s\n", skel.uris[i]);
wf_agent_starter_pack_skeleton_list_free(&skel);
```

## Search starter packs

```c
wf_agent_search_starter_packs_list results = {0};
wf_status s = wf_agent_search_starter_packs_typed(agent, "art", NULL, 0, NULL, &results);
for (size_t i = 0; i < results.count; ++i)
    printf("%s\n", results.items[i].uri);
if (results.has_hits_total) printf("total hits: %d\n", results.hits_total);
wf_agent_search_starter_packs_free(&results);
```

## Offline parsing

Each typed call has a `wf_agent_parse_*` sibling that parses a raw JSON body you
already have (from `wf_agent_*` raw wrappers or a fixture) without a network
round-trip:

```c
wf_agent_trending_topics topics = {0};
wf_status s = wf_agent_parse_trending_topics(res.body, res.body_len, &topics);
/* ... iterate as above ... */
wf_agent_trending_topics_free(&topics);
```

Available parse functions: `wf_agent_parse_trending_topics`,
`wf_agent_parse_tagged_suggestions`, `wf_agent_parse_suggestions_skeleton`,
`wf_agent_parse_config`, `wf_agent_parse_age_assurance_state`,
`wf_agent_parse_onboarding_starter_packs`,
`wf_agent_parse_onboarding_starter_packs_skeleton`, and
`wf_agent_parse_search_starter_packs`.

## Free-function summary

| Result struct | Free function |
| --- | --- |
| `wf_agent_trending_topics` | `wf_agent_trending_topics_free` |
| `wf_agent_tagged_suggestions` | `wf_agent_tagged_suggestions_free` |
| `wf_agent_suggestions_skeleton` | `wf_agent_suggestions_skeleton_free` |
| `wf_agent_unspecced_config` | `wf_agent_unspecced_config_free` |
| `wf_agent_age_assurance_state` | `wf_agent_age_assurance_state_free` |
| `wf_agent_starter_pack_view_list` | `wf_agent_starter_pack_view_list_free` |
| `wf_agent_starter_pack_skeleton_list` | `wf_agent_starter_pack_skeleton_list_free` |
| `wf_agent_search_starter_packs_list` | `wf_agent_search_starter_packs_free` |

All free functions are safe to call with `NULL`.

## XRPC-level wrappers (`wf_unspecced_*`)

The 22 remaining `app.bsky.unspecced` endpoints are exposed as thin
XRPC-level wrappers in `unspecced_typed.h`. They follow the `ozone.h` pattern:
the call function validates inputs and delegates to the generated
`wf_lex_app_bsky_unspecced_*_main_call`; the `*_parse` helper wraps the
generated `_output_decode_json`.

Usage pattern:

```c
#include "wolfram/unspecced_typed.h"  /* also reachable via agent.h */

wf_xrpc_client *client = /* … */;
/* Query params */
wf_lex_app_bsky_unspecced_search_actors_skeleton_main_params params = {0};
params.q = "alice";
params.has_limit = true;
params.limit = 25;

wf_response res = {0};
wf_status s = wf_unspecced_search_actors_skeleton(client, &params, &res);
if (s != WF_OK) { /* handle error */ }

/* Decode the response body into an owned typed struct */
wf_lex_app_bsky_unspecced_search_actors_skeleton_main_output *out = NULL;
s = wf_unspecced_search_actors_skeleton_parse(&res, &out);
if (s == WF_OK && out) {
    for (size_t i = 0; i < out->actors.count; ++i) {
        printf("actor: %s\n", out->actors.items[i]->did);
    }
    wf_lex_app_bsky_unspecced_search_actors_skeleton_main_output_free(out);
}
wf_response_free(&res);
```

Each NSID is also available as a `WF_UNSPECCED_*_NSID` define for use with
the lower-level XRPC API directly.
