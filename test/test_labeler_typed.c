/*
 * test_labeler_typed.c — offline tests for the labeler typed parsers. Hardcodes
 * representative response bodies and asserts the owned structs are populated
 * correctly, then freed. Agent wrappers require live auth and are exercised
 * only for NULL-argument validation.
 */

#include "wolfram/labeler_typed.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* com.atproto.label.queryLabels response (labels[] + cursor). */
static const char *kQueryLabelsJson =
    "{"
    "  \"cursor\": \"cursor-xyz\","
    "  \"labels\": ["
    "    {"
    "      \"src\": \"did:plc:labeler000000000000000000\","
    "      \"uri\": \"at://did:plc:alice/app.bsky.feed.post/abc\","
    "      \"val\": \"!warn\","
    "      \"cts\": \"2026-07-01T10:00:00.000Z\""
    "    },"
    "    {"
    "      \"ver\": 1,"
    "      \"src\": \"did:plc:labeler000000000000000000\","
    "      \"uri\": \"at://did:plc:bob/app.bsky.actor.profile/self\","
    "      \"cid\": \"bafyreigh2ak3aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
    "      \"val\": \"porn\","
    "      \"neg\": true,"
    "      \"cts\": \"2026-07-02T11:00:00.000Z\","
    "      \"exp\": \"2026-08-02T11:00:00.000Z\","
    "      \"sig\": \"a2V5c2ln\""
    "    }"
    "  ]"
    "}";

/* app.bsky.labeler.getServices response (views[]; one detailed with policies
 * and label value definitions, one basic). */
static const char *kGetServicesJson =
    "{"
    "  \"views\": ["
    "    {"
    "      \"uri\": \"at://did:plc:labeler000000000000000000/app.bsky.labeler.service/self\","
    "      \"cid\": \"bafyreilabeler1111111111111111111111111111111111111111111\","
    "      \"creator\": {"
    "        \"did\": \"did:plc:labeler000000000000000000\","
    "        \"handle\": \"mod.bsky.social\","
    "        \"displayName\": \"Mod Service\","
    "        \"avatar\": \"https://cdn.bsky.app/img/mod.jpg\""
    "      },"
    "      \"policies\": {"
    "        \"labelValues\": [\"!warn\", \"porn\", \"sexual\"],"
    "        \"labelValueDefinitions\": ["
    "          {"
    "            \"identifier\": \"custom-spam\","
    "            \"severity\": \"alert\","
    "            \"blurs\": \"content\","
    "            \"defaultSetting\": \"hide\","
    "            \"adultOnly\": true,"
    "            \"locales\": ["
    "              {"
    "                \"lang\": \"en\","
    "                \"name\": \"Spam\","
    "                \"description\": \"This is spam.\""
    "              }"
    "            ]"
    "          }"
    "        ]"
    "      },"
    "      \"likeCount\": 42,"
    "      \"indexedAt\": \"2026-07-01T09:00:00.000Z\","
    "      \"labels\": ["
    "        {"
    "          \"src\": \"did:plc:labeler000000000000000000\","
    "          \"uri\": \"at://did:plc:alice/app.bsky.feed.post/abc\","
    "          \"val\": \"!warn\","
    "          \"cts\": \"2026-07-01T10:00:00.000Z\""
    "        }"
    "      ],"
    "      \"reasonTypes\": [\"com.atproto.moderation.defs#reasonSpam\"],"
    "      \"subjectTypes\": [\"account\", \"record\"],"
    "      \"subjectCollections\": [\"app.bsky.feed.post\"]"
    "    },"
    "    {"
    "      \"uri\": \"at://did:plc:labeler200000000000000000/app.bsky.labeler.service/self\","
    "      \"cid\": \"bafyreilabeler2222222222222222222222222222222222222222222\","
    "      \"creator\": {"
    "        \"did\": \"did:plc:labeler200000000000000000\","
    "        \"handle\": \"other.bsky.social\","
    "        \"displayName\": \"Other\""
    "      },"
    "      \"likeCount\": 3,"
    "      \"indexedAt\": \"2026-07-03T09:00:00.000Z\""
    "    }"
    "  ]"
    "}";

/* com.atproto.temp.fetchLabels response (labels[]; no cursor). */
static const char *kTempLabelsJson =
    "{"
    "  \"labels\": ["
    "    {"
    "      \"src\": \"did:plc:labeler000000000000000000\","
    "      \"uri\": \"at://did:plc:carol/app.bsky.feed.post/xyz\","
    "      \"val\": \"graphic-media\","
    "      \"cts\": \"2026-07-04T08:00:00.000Z\""
    "    }"
    "  ]"
    "}";

/* app.bsky.labeler.service raw record. */
static const char *kServiceRecordJson =
    "{"
    "  \"policies\": {"
    "    \"labelValues\": [\"!hide\", \"nudity\"],"
    "    \"labelValueDefinitions\": ["
    "      {"
    "        \"identifier\": \"custom-nsfw\","
    "        \"severity\": \"alert\","
    "        \"blurs\": \"media\","
    "        \"locales\": ["
    "          {"
    "            \"lang\": \"en\","
    "            \"name\": \"NSFW\","
    "            \"description\": \"Not safe for work.\""
    "          }"
    "        ]"
    "      }"
    "    ]"
    "  },"
    "  \"labels\": {"
    "    \"values\": ["
    "      { \"val\": \"custom-nsfw\" },"
    "      { \"val\": \"!hide\" }"
    "    ]"
    "  },"
    "  \"createdAt\": \"2026-07-01T00:00:00.000Z\","
    "  \"reasonTypes\": [\"com.atproto.moderation.defs#reasonSpam\"],"
    "  \"subjectTypes\": [\"record\"]"
    "}";

int main(void) {
    /* ---- Invalid args -> WF_ERR_INVALID_ARG ---- */
    wf_labeler_label_list q = {0};
    WF_CHECK(wf_labeler_parse_query_labels(NULL, 0, &q) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_labeler_parse_query_labels(kQueryLabelsJson,
                                           strlen(kQueryLabelsJson), NULL) ==
             WF_ERR_INVALID_ARG);

    wf_labeler_service_list s = {0};
    WF_CHECK(wf_labeler_parse_services(NULL, 0, &s) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_labeler_parse_services(kGetServicesJson,
                                       strlen(kGetServicesJson), NULL) ==
             WF_ERR_INVALID_ARG);

    wf_labeler_temp_label_list t = {0};
    WF_CHECK(wf_labeler_parse_temp_labels(NULL, 0, &t) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_labeler_parse_temp_labels(kTempLabelsJson,
                                          strlen(kTempLabelsJson), NULL) ==
             WF_ERR_INVALID_ARG);

    wf_labeler_service_record r = {0};
    WF_CHECK(wf_labeler_parse_service_record(NULL, 0, &r) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_labeler_parse_service_record(kServiceRecordJson,
                                             strlen(kServiceRecordJson),
                                             NULL) == WF_ERR_INVALID_ARG);

    /* ---- queryLabels ---- */
    WF_CHECK(wf_labeler_parse_query_labels(kQueryLabelsJson,
                                           strlen(kQueryLabelsJson), &q) ==
             WF_OK);
    WF_CHECK(q.label_count == 2);
    WF_CHECK(q.cursor && strcmp(q.cursor, "cursor-xyz") == 0);
    WF_CHECK(q.labels[0].src &&
             strcmp(q.labels[0].src,
                    "did:plc:labeler000000000000000000") == 0);
    WF_CHECK(q.labels[0].uri &&
             strcmp(q.labels[0].uri,
                    "at://did:plc:alice/app.bsky.feed.post/abc") == 0);
    WF_CHECK(q.labels[0].val && strcmp(q.labels[0].val, "!warn") == 0);
    WF_CHECK(q.labels[0].cts &&
             strcmp(q.labels[0].cts, "2026-07-01T10:00:00.000Z") == 0);
    WF_CHECK(q.labels[0].has_cid == false && q.labels[0].has_sig == false);
    WF_CHECK(q.labels[1].has_ver == true && q.labels[1].ver == 1);
    WF_CHECK(q.labels[1].has_cid == true && q.labels[1].cid &&
             strncmp(q.labels[1].cid, "bafyrei", 6) == 0);
    WF_CHECK(q.labels[1].has_neg == true && q.labels[1].neg == true);
    WF_CHECK(q.labels[1].has_exp == true && q.labels[1].exp &&
             strcmp(q.labels[1].exp, "2026-08-02T11:00:00.000Z") == 0);
    WF_CHECK(q.labels[1].has_sig == true && q.labels[1].sig &&
             strcmp(q.labels[1].sig, "a2V5c2ln") == 0);
    wf_labeler_label_list_free(&q);
    WF_CHECK(q.label_count == 0 && q.labels == NULL && q.cursor == NULL);

    /* ---- getServices (detailed + basic) ---- */
    WF_CHECK(wf_labeler_parse_services(kGetServicesJson,
                                       strlen(kGetServicesJson), &s) == WF_OK);
    WF_CHECK(s.service_count == 2);
    wf_labeler_service_view *d = &s.services[0];
    WF_CHECK(d->is_detailed == true && d->has_policies == true);
    WF_CHECK(d->uri &&
             strstr(d->uri, "did:plc:labeler000000000000000000") != NULL);
    WF_CHECK(d->cid && strncmp(d->cid, "bafy", 4) == 0);
    WF_CHECK(d->creator.did &&
             strcmp(d->creator.did, "did:plc:labeler000000000000000000") == 0);
    WF_CHECK(d->creator.handle &&
             strcmp(d->creator.handle, "mod.bsky.social") == 0);
    WF_CHECK(d->creator.display_name &&
             strcmp(d->creator.display_name, "Mod Service") == 0);
    WF_CHECK(d->creator.avatar &&
             strcmp(d->creator.avatar, "https://cdn.bsky.app/img/mod.jpg") == 0);
    WF_CHECK(d->creator.extra == NULL);
    WF_CHECK(d->has_like_count == true && d->like_count == 42);
    WF_CHECK(d->indexed_at &&
             strcmp(d->indexed_at, "2026-07-01T09:00:00.000Z") == 0);
    WF_CHECK(d->label_count == 1);
    WF_CHECK(d->labels[0].val && strcmp(d->labels[0].val, "!warn") == 0);
    WF_CHECK(d->policies.label_value_count == 3);
    WF_CHECK(d->policies.label_values[0] &&
             strcmp(d->policies.label_values[0], "!warn") == 0);
    WF_CHECK(d->policies.label_value_definition_count == 1);
    wf_labeler_label_value_def *def = &d->policies.label_value_definitions[0];
    WF_CHECK(def->identifier &&
             strcmp(def->identifier, "custom-spam") == 0);
    WF_CHECK(def->severity && strcmp(def->severity, "alert") == 0);
    WF_CHECK(def->blurs && strcmp(def->blurs, "content") == 0);
    WF_CHECK(def->has_default_setting == true && def->default_setting &&
             strcmp(def->default_setting, "hide") == 0);
    WF_CHECK(def->has_adult_only == true && def->adult_only == true);
    WF_CHECK(def->locale_count == 1);
    WF_CHECK(def->locales[0].lang &&
             strcmp(def->locales[0].lang, "en") == 0);
    WF_CHECK(def->locales[0].name &&
             strcmp(def->locales[0].name, "Spam") == 0);
    WF_CHECK(def->locales[0].description &&
             strcmp(def->locales[0].description, "This is spam.") == 0);
    WF_CHECK(d->reason_type_count == 1 &&
             strcmp(d->reason_types[0],
                    "com.atproto.moderation.defs#reasonSpam") == 0);
    WF_CHECK(d->subject_type_count == 2 &&
             strcmp(d->subject_types[0], "account") == 0 &&
             strcmp(d->subject_types[1], "record") == 0);
    WF_CHECK(d->subject_collection_count == 1 &&
             strcmp(d->subject_collections[0], "app.bsky.feed.post") == 0);
    WF_CHECK(d->extra == NULL);

    wf_labeler_service_view *b = &s.services[1];
    WF_CHECK(b->is_detailed == false && b->has_policies == false);
    WF_CHECK(b->creator.handle &&
             strcmp(b->creator.handle, "other.bsky.social") == 0);
    WF_CHECK(b->has_like_count == true && b->like_count == 3);
    WF_CHECK(b->label_count == 0);
    WF_CHECK(b->policies.label_value_count == 0);
    wf_labeler_service_list_free(&s);
    WF_CHECK(s.service_count == 0 && s.services == NULL);

    /* ---- temp.fetchLabels ---- */
    WF_CHECK(wf_labeler_parse_temp_labels(kTempLabelsJson,
                                          strlen(kTempLabelsJson), &t) == WF_OK);
    WF_CHECK(t.label_count == 1);
    WF_CHECK(t.labels[0].val &&
             strcmp(t.labels[0].val, "graphic-media") == 0);
    WF_CHECK(t.labels[0].src &&
             strcmp(t.labels[0].src,
                    "did:plc:labeler000000000000000000") == 0);
    wf_labeler_temp_label_list_free(&t);
    WF_CHECK(t.label_count == 0 && t.labels == NULL);

    /* ---- service record (raw) ---- */
    WF_CHECK(wf_labeler_parse_service_record(kServiceRecordJson,
                                             strlen(kServiceRecordJson),
                                             &r) == WF_OK);
    WF_CHECK(r.policies.label_value_count == 2);
    WF_CHECK(r.policies.label_values[0] &&
             strcmp(r.policies.label_values[0], "!hide") == 0);
    WF_CHECK(r.policies.label_value_definition_count == 1);
    WF_CHECK(r.policies.label_value_definitions[0].identifier &&
             strcmp(r.policies.label_value_definitions[0].identifier,
                    "custom-nsfw") == 0);
    WF_CHECK(r.policies.label_value_definitions[0].blurs &&
             strcmp(r.policies.label_value_definitions[0].blurs, "media") == 0);
    WF_CHECK(r.has_labels == true && r.self_label_count == 2);
    WF_CHECK(r.self_label_values[0] &&
             strcmp(r.self_label_values[0], "custom-nsfw") == 0);
    WF_CHECK(r.self_label_values[1] &&
             strcmp(r.self_label_values[1], "!hide") == 0);
    WF_CHECK(r.created_at &&
             strcmp(r.created_at, "2026-07-01T00:00:00.000Z") == 0);
    WF_CHECK(r.reason_type_count == 1 &&
             strcmp(r.reason_types[0],
                    "com.atproto.moderation.defs#reasonSpam") == 0);
    WF_CHECK(r.subject_type_count == 1 &&
             strcmp(r.subject_types[0], "record") == 0);
    WF_CHECK(r.subject_collection_count == 0);
    WF_CHECK(r.extra == NULL);
    wf_labeler_service_record_free(&r);
    WF_CHECK(r.policies.label_value_count == 0 &&
             r.self_label_count == 0 && r.created_at == NULL);

    /* ---- Agent wrapper NULL validation (no live session; NULL agent passed so
       the wrappers never dereference a fake pointer). ---- */
    const char *uris[] = {"at://did:plc:alice/app.bsky.feed.post/abc"};
    const char *dids[] = {"did:plc:labeler000000000000000000"};
    WF_CHECK(wf_agent_query_labels(NULL, uris, 1, NULL, 0, &q) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_query_labels(NULL, NULL, 0, NULL, 0, &q) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_query_labels(NULL, uris, 1, NULL, 0, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_labeler_services(NULL, dids, 1, true, &s) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_labeler_services(NULL, NULL, 0, true, &s) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_labeler_services(NULL, dids, 1, true, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_fetch_labels_typed(NULL, "did:plc:labeler000000000000000000",
                                   &t) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_fetch_labels_typed(NULL, NULL, &t) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_fetch_labels_typed(NULL, "did:plc:x", NULL) ==
             WF_ERR_INVALID_ARG);

    printf("labeler_typed: all checks passed\n");
    return 0;
}
