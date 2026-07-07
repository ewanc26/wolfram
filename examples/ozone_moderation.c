/*
 * ozone_moderation.c — ozone moderation / labeler helper demo.
 *
 * Demonstrates:
 *   1. Building an app.bsky.labeler.service record offline (no network) and
 *      printing the JSON it produces.
 *   2. (Network-gated) Reporting a subject, querying moderation statuses, and
 *      fetching label value definitions through the ozone helper wrappers.
 *
 * Usage (offline-safe without arguments — prints usage and exits 0):
 *   ozone_moderation
 *   ozone_moderation <service-url> <handle-or-email> <password> [subject-did]
 *
 * With three or more arguments the program logs in and performs live ozone
 * calls. `subject-did` (a repo DID to report / inspect) is optional; when
 * omitted the calls demonstrate the helper with no specific subject.
 */

#include "wolfram/ozone.h"
#include "wolfram/session.h"
#include "wolfram/xrpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
    printf("usage: %s\n", prog);
    printf("  (no arguments: build a labeler service record offline, "
           "print it, exit 0)\n");
    printf("usage: %s <service-url> <handle-or-email> <password> "
           "[subject-did]\n", prog);
    printf("  logs in and demonstrates ozone report/query/label-defs calls\n");
}

static void print_json(const char *label, const wf_response *res) {
    printf("%s: status=%ld\n", label, res ? res->status : -1);
    if (res && res->body && res->body_len) {
        printf("%.*s\n", (int)res->body_len, res->body);
    }
}

int main(int argc, char **argv) {
    /* ---- Offline portion: always build and print a service record. ---- */
    const char *labeler_did = "did:plc:examplelabelerexampleexample1234";
    char *service_record =
        wf_labeler_build_service_record(labeler_did, NULL, NULL);
    if (!service_record) {
        fprintf(stderr, "failed to build labeler service record\n");
        return 1;
    }

    printf("Built app.bsky.labeler.service record:\n%s\n\n", service_record);
    free(service_record);

    if (argc < 4) {
        print_usage(argv[0]);
        return 0;
    }

    /* ---- Network-gated portion. ---- */
    const char *service_url = argv[1];
    const char *identifier  = argv[2];
    const char *password    = argv[3];
    const char *subject_did = (argc >= 5) ? argv[4] : NULL;

    wf_session *session = wf_session_new(service_url);
    if (!session) {
        fprintf(stderr, "failed to create session\n");
        return 1;
    }

    wf_status status = wf_session_login(session, identifier, password);
    if (status != WF_OK) {
        fprintf(stderr, "login failed: %d\n", (int)status);
        wf_session_free(session);
        return 1;
    }

    wf_xrpc_client *client = session->client;

    /* Report the subject (or, when no subject is given, the labeler itself). */
    const char *report_did = subject_did ? subject_did : labeler_did;
    wf_response report = {0};
    status = wf_ozone_report_subject(client, report_did, NULL, NULL,
                                     "com.atproto.moderation.defs#reasonOther",
                                     "Reported via wolfram ozone helper demo.",
                                     &report);
    if (status == WF_OK) {
        print_json("createReport", &report);
    } else {
        printf("createReport skipped/failed: %d\n", (int)status);
    }
    wf_response_free(&report);

    /* Query moderation statuses for the subject, if any. */
    wf_response statuses = {0};
    const char **subjects = NULL;
    size_t n = 0;
    const char *subject_list[1];
    if (subject_did) {
        subject_list[0] = subject_did;
        subjects = subject_list;
        n = 1;
    }
    status = wf_ozone_query_statuses(client, subjects, n, &statuses);
    if (status == WF_OK) {
        print_json("queryStatuses", &statuses);
    } else {
        printf("queryStatuses skipped/failed: %d\n", (int)status);
    }
    wf_response_free(&statuses);

    /* Fetch label value definitions for the labeler's service record. */
    wf_response defs = {0};
    const char *def_uris[1];
    def_uris[0] = "at://did:plc:examplelabelerexampleexample1234/"
                  "app.bsky.labeler.service/self";
    status = wf_ozone_get_label_defs(client, def_uris, 1, &defs);
    if (status == WF_OK) {
        print_json("getLabelDefs", &defs);
    } else {
        printf("getLabelDefs skipped/failed: %d\n", (int)status);
    }
    wf_response_free(&defs);

    wf_session_free(session);
    return 0;
}
