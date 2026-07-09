/*
 * test_lexicon_typed.c — offline unit tests for the com.atproto.lexicon
 * resolveLexicon owned parser and wrapper argument validation. No network.
 */

#include "wolfram/lexicon_typed.h"
#include "test.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* resolveLexicon sample: a minimal lexicon schema record. */
static const char *k_resolve_json =
    "{\"cid\":\"bafyreighash\","
    "\"uri\":\"at://did:plc:abc/com.atproto.lexicon.lexicon/app.bsky.feed.post\","
    "\"schema\":{\"lexicon\":1,\"id\":\"app.bsky.feed.post\","
    "\"defs\":{\"main\":{\"type\":\"record\"}}}}";

int main(void) {
    /* ---- parse round-trip ---- */
    {
        wf_lexicon_resolved out = {0};
        wf_status s = wf_lexicon_parse_resolve(k_resolve_json,
                                               strlen(k_resolve_json), &out);
        WF_CHECK(s == WF_OK);
        WF_CHECK(out.cid && strcmp(out.cid, "bafyreighash") == 0);
        WF_CHECK(out.uri &&
                 strcmp(out.uri,
                        "at://did:plc:abc/com.atproto.lexicon.lexicon/"
                        "app.bsky.feed.post") == 0);
        WF_CHECK(out.schema != NULL);
        if (out.schema) {
            cJSON *id =
                cJSON_GetObjectItemCaseSensitive(out.schema, "id");
            WF_CHECK(cJSON_IsString(id) && id->valuestring &&
                     strcmp(id->valuestring, "app.bsky.feed.post") == 0);
        }
        wf_lexicon_resolved_free(&out);
        wf_lexicon_resolved_free(&out); /* idempotent */
    }

    /* ---- invalid / NULL input validation ---- */
    {
        wf_lexicon_resolved out = {0};
        WF_CHECK(wf_lexicon_parse_resolve(NULL, 0, &out) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_lexicon_parse_resolve(NULL, 0, NULL) ==
                 WF_ERR_INVALID_ARG);
        /* malformed JSON */
        WF_CHECK(wf_lexicon_parse_resolve("not json", 8, &out) ==
                 WF_ERR_PARSE);
        wf_lexicon_resolved_free(&out);
    }

    /* ---- wrapper argument validation (NULL agent) ---- */
    {
        wf_lexicon_resolved out = {0};
        WF_CHECK(wf_agent_resolve_lexicon_typed(NULL, "x", &out) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_resolve_lexicon_typed(NULL, NULL, &out) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_agent_resolve_lexicon_typed(NULL, "x", NULL) ==
                 WF_ERR_INVALID_ARG);
        wf_lexicon_resolved_free(&out);
    }

    WF_TEST_SUMMARY();
    return 0; /* unreachable */
}
