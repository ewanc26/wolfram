/*
 * test_contact_typed.c — offline tests for the app.bsky.contact typed parsers.
 * Hardcodes representative response bodies and asserts the owned structs are
 * populated correctly, then freed. Agent wrappers require live auth and are
 * exercised only for NULL-argument validation.
 */

#include "wolfram/contact_typed.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* app.bsky.contact.getMatches response (matches: profileView[] + cursor). */
static const char *kGetMatchesJson =
    "{"
    "  \"cursor\": \"next-cursor-123\","
    "  \"matches\": ["
    "    {"
    "      \"did\": \"did:plc:alice00000000000000000000\","
    "      \"handle\": \"alice.bsky.social\","
    "      \"displayName\": \"Alice\","
    "      \"avatar\": \"https://cdn.bsky.app/img/alice.jpg\""
    "    },"
    "    {"
    "      \"did\": \"did:plc:bob0000000000000000000000\","
    "      \"handle\": \"bob.bsky.social\","
    "      \"displayName\": \"Bob\""
    "    }"
    "  ]"
    "}";

/* app.bsky.contact.importContacts response (matchesAndContactIndexes). */
static const char *kImportJson =
    "{"
    "  \"matchesAndContactIndexes\": ["
    "    {"
    "      \"match\": {"
    "        \"did\": \"did:plc:carol0000000000000000000\","
    "        \"displayName\": \"Carol\","
    "        \"avatar\": \"https://cdn.bsky.app/img/carol.jpg\""
    "      },"
    "      \"contactIndex\": 0"
    "    },"
    "    {"
    "      \"match\": {"
    "        \"did\": \"did:plc:dave000000000000000000000\","
    "        \"displayName\": \"Dave\""
    "      },"
    "      \"contactIndex\": 3"
    "    }"
    "  ]"
    "}";

/* app.bsky.contact.getSyncStatus response (syncStatus present). */
static const char *kSyncStatusJson =
    "{"
    "  \"syncStatus\": {"
    "    \"syncedAt\": \"2026-07-01T12:00:00.000Z\","
    "    \"matchesCount\": 7"
    "  }"
    "}";

/* app.bsky.contact.getSyncStatus response (never imported -> no syncStatus). */
static const char *kSyncStatusEmptyJson = "{ }";

int main(void) {
    /* ---- Invalid args -> WF_ERR_INVALID_ARG ---- */
    wf_contact_match_list matches = {0};
    WF_CHECK(wf_contact_parse_matches(NULL, 0, &matches) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_contact_parse_matches(kGetMatchesJson,
                                      strlen(kGetMatchesJson), NULL) ==
             WF_ERR_INVALID_ARG);

    wf_contact_import_result import = {0};
    WF_CHECK(wf_contact_parse_import(NULL, 0, &import) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_contact_parse_import(kImportJson, strlen(kImportJson), NULL) ==
             WF_ERR_INVALID_ARG);

    wf_contact_sync_status sync = {0};
    WF_CHECK(wf_contact_parse_sync_status(NULL, 0, &sync) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_contact_parse_sync_status(kSyncStatusJson,
                                          strlen(kSyncStatusJson), NULL) ==
             WF_ERR_INVALID_ARG);

    /* ---- getMatches ---- */
    WF_CHECK(wf_contact_parse_matches(kGetMatchesJson, strlen(kGetMatchesJson),
                                      &matches) == WF_OK);
    WF_CHECK(matches.count == 2);
    WF_CHECK(matches.cursor && strcmp(matches.cursor, "next-cursor-123") == 0);
    WF_CHECK(matches.items[0].did &&
             strcmp(matches.items[0].did, "did:plc:alice00000000000000000000") ==
                 0);
    WF_CHECK(matches.items[0].display_name &&
             strcmp(matches.items[0].display_name, "Alice") == 0);
    WF_CHECK(matches.items[0].avatar &&
             strcmp(matches.items[0].avatar,
                    "https://cdn.bsky.app/img/alice.jpg") == 0);
    /* Second match has no avatar. */
    WF_CHECK(matches.items[1].did &&
             strcmp(matches.items[1].did, "did:plc:bob0000000000000000000000") ==
                 0);
    WF_CHECK(matches.items[1].display_name &&
             strcmp(matches.items[1].display_name, "Bob") == 0);
    WF_CHECK(matches.items[1].avatar == NULL);
    wf_contact_match_list_free(&matches);
    WF_CHECK(matches.count == 0 && matches.items == NULL && matches.cursor == NULL);

    /* ---- importContacts ---- */
    WF_CHECK(wf_contact_parse_import(kImportJson, strlen(kImportJson),
                                     &import) == WF_OK);
    WF_CHECK(import.count == 2);
    WF_CHECK(import.items[0].contact_index == 0);
    WF_CHECK(import.items[0].match.did &&
             strcmp(import.items[0].match.did,
                    "did:plc:carol0000000000000000000") == 0);
    WF_CHECK(import.items[0].match.display_name &&
             strcmp(import.items[0].match.display_name, "Carol") == 0);
    WF_CHECK(import.items[0].match.avatar &&
             strcmp(import.items[0].match.avatar,
                    "https://cdn.bsky.app/img/carol.jpg") == 0);
    WF_CHECK(import.items[1].contact_index == 3);
    WF_CHECK(import.items[1].match.display_name &&
             strcmp(import.items[1].match.display_name, "Dave") == 0);
    WF_CHECK(import.items[1].match.avatar == NULL);
    wf_contact_import_result_free(&import);
    WF_CHECK(import.count == 0 && import.items == NULL);

    /* ---- getSyncStatus (present) ---- */
    WF_CHECK(wf_contact_parse_sync_status(kSyncStatusJson,
                                          strlen(kSyncStatusJson), &sync) ==
             WF_OK);
    WF_CHECK(sync.has_status == 1);
    WF_CHECK(sync.matches_count == 7);
    WF_CHECK(sync.last_synced_at &&
             strcmp(sync.last_synced_at, "2026-07-01T12:00:00.000Z") == 0);
    wf_contact_sync_status_free(&sync);
    WF_CHECK(sync.has_status == 0 && sync.last_synced_at == NULL);

    /* ---- getSyncStatus (absent) ---- */
    WF_CHECK(wf_contact_parse_sync_status(kSyncStatusEmptyJson,
                                          strlen(kSyncStatusEmptyJson),
                                          &sync) == WF_OK);
    WF_CHECK(sync.has_status == 0);
    WF_CHECK(sync.last_synced_at == NULL);
    wf_contact_sync_status_free(&sync);

    /* ---- Agent wrapper NULL validation (no live session; only NULL agent is
       passed so the wrappers never dereference a fake pointer). The NULL-agent
       guard also catches missing required arguments. ---- */
    const char *contacts[] = {"+12125550123"};
    WF_CHECK(wf_agent_get_contact_matches_typed(NULL, 10, NULL, &matches) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_contact_matches_typed(NULL, 10, NULL, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_import_contacts(NULL, "tok", contacts, 1, &import) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_import_contacts(NULL, NULL, contacts, 1, &import) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_import_contacts(NULL, "tok", NULL, 0, &import) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_contact_sync_status(NULL, &sync) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_contact_send_notification(NULL, "did:plc:x") ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_contact_send_notification(NULL, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_contact_start_phone_verification(NULL, "+12125550123") ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_contact_start_phone_verification(NULL, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_contact_verify_phone(NULL, "+12125550123", "123456") ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_contact_verify_phone(NULL, "+12125550123", NULL) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_contact_dismiss_match(NULL, "did:plc:x") ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_contact_dismiss_match(NULL, NULL) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_contact_remove_data(NULL) == WF_ERR_INVALID_ARG);

    printf("contact_typed: all checks passed\n");
    return 0;
}
