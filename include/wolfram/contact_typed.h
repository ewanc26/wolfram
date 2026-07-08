/*
 * contact_typed.h — owned typed parsers + agent convenience wrappers for the
 * app.bsky.contact namespace (contact import / phone verification / match
 * discovery).
 *
 * The wire format below follows the authoritative lexicons in
 * atproto/lexicons/app/bsky/contact (read 2026-07). Notable differences from
 * the originally sketched shape:
 *   - importContacts output is `{ matchesAndContactIndexes: [...] }`, an array
 *     of { match: profileView, contactIndex }, NOT { imported, duplicates,
 *     invalid }.
 *   - importContacts input is `{ token, contacts: [E.164 phones] }`, NOT a list
 *     of at-uris.
 *   - getSyncStatus output is `{ syncStatus?: { syncedAt, matchesCount } }`, NOT
 *     { uptodate, syncing, lastSyncedAt }.
 *
 * All output structs are caller-owned and released with the matching `_free`.
 * Parse functions take the raw JSON body (as returned by the agent wrappers)
 * and follow the conventions of feed_typed.c / actor_typed.c: static
 * strdup/set_string/reset helpers, full cleanup on the first error.
 */

#ifndef WOLFRAM_CONTACT_TYPED_H
#define WOLFRAM_CONTACT_TYPED_H

#include "wolfram/agent.h"
#include <cJSON.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** A single contact match (a profileView, trimmed to the common fields). */
typedef struct wf_contact_match {
    char *did;
    char *display_name;
    char *avatar;
} wf_contact_match;

/** An owned list of contact matches plus an optional pagination cursor. */
typedef struct wf_contact_match_list {
    wf_contact_match *items;
    size_t count;
    char *cursor;
} wf_contact_match_list;

/** A single import result entry: a matched profile and its input index. */
typedef struct wf_contact_import_match {
    wf_contact_match match;
    int64_t contact_index;
} wf_contact_import_match;

/** Owned result of app.bsky.contact.importContacts. */
typedef struct wf_contact_import_result {
    wf_contact_import_match *items;
    size_t count;
} wf_contact_import_result;

/**
 * Sync status returned by app.bsky.contact.getSyncStatus. `has_status` is 0
 * when the user has never imported contacts (or removed their data); non-zero
 * when `last_synced_at` / `matches_count` are populated.
 */
typedef struct wf_contact_sync_status {
    int has_status;
    char *last_synced_at;
    int64_t matches_count;
} wf_contact_sync_status;

/* ---- Parsers (raw JSON body -> owned structs) ---- */

wf_status wf_contact_parse_matches(const char *json, size_t json_len,
                                   wf_contact_match_list *out);
void wf_contact_match_list_free(wf_contact_match_list *list);

wf_status wf_contact_parse_import(const char *json, size_t json_len,
                                  wf_contact_import_result *out);
void wf_contact_import_result_free(wf_contact_import_result *out);

wf_status wf_contact_parse_sync_status(const char *json, size_t json_len,
                                       wf_contact_sync_status *out);
void wf_contact_sync_status_free(wf_contact_sync_status *out);

/* ---- Agent convenience wrappers ---- */

/** app.bsky.contact.getMatches — list contacts that mutually imported you. */
wf_status wf_agent_get_contact_matches_typed(wf_agent *agent, int limit,
                                             const char *cursor,
                                             wf_contact_match_list *out);

/**
 * app.bsky.contact.importContacts — import E.164 `contacts` using a verification
 * `token` obtained from app.bsky.contact.verifyPhone.
 */
wf_status wf_agent_import_contacts(wf_agent *agent, const char *token,
                                   const char *const *contacts,
                                   size_t contact_count,
                                   wf_contact_import_result *out);

/** app.bsky.contact.getSyncStatus — current contact import status. */
wf_status wf_agent_get_contact_sync_status(wf_agent *agent,
                                           wf_contact_sync_status *out);

/** app.bsky.contact.sendNotification — notify `did` that you imported them. */
wf_status wf_agent_contact_send_notification(wf_agent *agent, const char *did);

/** app.bsky.contact.startPhoneVerification — begin SMS code flow for `phone`. */
wf_status wf_agent_contact_start_phone_verification(wf_agent *agent,
                                                    const char *phone_number);

/** app.bsky.contact.verifyPhone — verify `code` for `phone`, mint an import token. */
wf_status wf_agent_contact_verify_phone(wf_agent *agent,
                                        const char *phone_number,
                                        const char *code);

/** app.bsky.contact.dismissMatch — permanently dismiss the match for `did`. */
wf_status wf_agent_contact_dismiss_match(wf_agent *agent, const char *did);

/** app.bsky.contact.removeData — delete all stored contact data. */
wf_status wf_agent_contact_remove_data(wf_agent *agent);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_CONTACT_TYPED_H */
