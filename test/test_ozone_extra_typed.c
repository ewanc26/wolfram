/*
 * test_ozone_extra_typed.c — offline tests for the tools.ozone.* typed
 * wrappers covering endpoints whose parse/agent paths are not yet exercised
 * by test_ozone_typed.c, test_ozone_admin_typed.c, or
 * test_ozone_moderation_ops_typed.c.
 *
 * Hardcodes representative response bodies with cJSON, asserts the
 * generated-decode path populates the owned output structs (here verified
 * with empty result arrays, which reliably decode to count 0), frees them,
 * and exercises the agent wrappers for NULL-argument validation. No network.
 */

#include "wolfram/ozone_typed.h"
#include "wolfram/atproto_lex.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Decode an array-valued output body of the form {"<field>":[]} and confirm
 * the generated decoder accepts it and yields count 0. */
#define WF_TEST_PARSE_EMPTY(ns, op, genop, arrfield)                          \
    do {                                                                      \
        const char *json = "{\"" #arrfield "\":[]}";                         \
        wf_lex_tools_ozone_##genop##_main_output *out = NULL;                 \
        wf_status st =                                                        \
            wf_ozone_parse_##ns##_##op(json, strlen(json), &out);             \
        WF_CHECK(st == WF_OK);                                                \
        WF_CHECK(out != NULL);                                                \
        if (out) {                                                            \
            WF_CHECK(out->arrfield.count == 0);                              \
            WF_CHECK(out->arrfield.items == NULL);                           \
            wf_lex_tools_ozone_##genop##_main_output_free(out);               \
        }                                                                     \
    } while (0)

int main(void) {
    /* ---- report.* generated-decode paths ---- */
    WF_TEST_PARSE_EMPTY(report, queryReports, report_query_reports, reports);
    WF_TEST_PARSE_EMPTY(report, getAssignments, report_get_assignments,
                        assignments);
    WF_TEST_PARSE_EMPTY(report, getHistoricalStats,
                        report_get_historical_stats, stats);
    WF_TEST_PARSE_EMPTY(report, queryActivities, report_query_activities,
                        activities);
    WF_TEST_PARSE_EMPTY(report, listActivities, report_list_activities,
                        activities);

    /* ---- queue.* generated-decode paths ---- */
    WF_TEST_PARSE_EMPTY(queue, listQueues, queue_list_queues, queues);
    WF_TEST_PARSE_EMPTY(queue, getAssignments, queue_get_assignments,
                        assignments);

    /* ---- signature.* generated-decode paths ---- */
    WF_TEST_PARSE_EMPTY(signature, searchAccounts, signature_search_accounts,
                        accounts);
    WF_TEST_PARSE_EMPTY(signature, findCorrelation,
                        signature_find_correlation, details);
    WF_TEST_PARSE_EMPTY(signature, findRelatedAccounts,
                        signature_find_related_accounts, accounts);

    /* ---- set / setting / communication / verification / safelink ---- */
    WF_TEST_PARSE_EMPTY(set, querySets, set_query_sets, sets);
    WF_TEST_PARSE_EMPTY(setting, listOptions, setting_list_options, options);
    /* communication.listTemplates: wire key is camelCase "communicationTemplates"
     * but the decoded struct field is snake_case "communication_templates". */
    {
        const char *json = "{\"communicationTemplates\":[]}";
        wf_lex_tools_ozone_communication_list_templates_main_output *out = NULL;
        wf_status st =
            wf_ozone_parse_communication_listTemplates(json, strlen(json), &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out != NULL);
        if (out) {
            WF_CHECK(out->communication_templates.count == 0);
            wf_lex_tools_ozone_communication_list_templates_main_output_free(out);
        }
    }
    WF_TEST_PARSE_EMPTY(verification, listVerifications,
                        verification_list_verifications, verifications);
    WF_TEST_PARSE_EMPTY(safelink, queryRules, safelink_query_rules, rules);
    WF_TEST_PARSE_EMPTY(safelink, queryEvents, safelink_query_events, events);

    /* ---- hosting / moderation extras generated-decode paths ---- */
    WF_TEST_PARSE_EMPTY(hosting, getAccountHistory,
                        hosting_get_account_history, events);
    WF_TEST_PARSE_EMPTY(moderation, getReporterStats,
                        moderation_get_reporter_stats, stats);
    WF_TEST_PARSE_EMPTY(moderation, getSubjects, moderation_get_subjects,
                        subjects);
    WF_TEST_PARSE_EMPTY(moderation, queryEvents, moderation_query_events,
                        events);

    /* ---- server.getConfig: object output, all fields optional ---- */
    {
        const char *json = "{}";
        wf_lex_tools_ozone_server_get_config_main_output *out = NULL;
        wf_status st =
            wf_ozone_parse_server_getConfig(json, strlen(json), &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out != NULL);
        if (out) {
            WF_CHECK(out->has_appview == false);
            WF_CHECK(out->has_verifier_did == false);
            wf_lex_tools_ozone_server_get_config_main_output_free(out);
        }
    }

    /* ---- moderation.queryStatuses: object output, all fields optional ---- */
    {
        const char *json = "{\"subjectStatuses\":[]}";
        wf_lex_tools_ozone_moderation_query_statuses_main_output *out = NULL;
        wf_status st =
            wf_ozone_parse_moderation_queryStatuses(json, strlen(json), &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out != NULL);
        if (out) {
            WF_CHECK(out->subject_statuses.count == 0);
            wf_lex_tools_ozone_moderation_query_statuses_main_output_free(out);
        }
    }

    /* ---- parse NULL-argument validation ---- */
    {
        wf_lex_tools_ozone_report_query_reports_main_output *r = NULL;
        WF_CHECK(wf_ozone_parse_report_queryReports(NULL, 0, &r) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ozone_parse_report_queryReports("{}", 2, NULL) ==
                 WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_queue_list_queues_main_output *q = NULL;
        WF_CHECK(wf_ozone_parse_queue_listQueues(NULL, 0, &q) ==
                 WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_signature_search_accounts_main_output *s = NULL;
        WF_CHECK(wf_ozone_parse_signature_searchAccounts(NULL, 0, &s) ==
                 WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_verification_list_verifications_main_output *v = NULL;
        WF_CHECK(wf_ozone_parse_verification_listVerifications(NULL, 0, &v) ==
                 WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_safelink_query_rules_main_output *sl = NULL;
        WF_CHECK(wf_ozone_parse_safelink_queryRules(NULL, 0, &sl) ==
                 WF_ERR_INVALID_ARG);
    }

    /* ---- agent wrappers: NULL-argument validation (offline) ---- */
    {
        wf_agent *agent = wf_agent_new("https://mod.example.com");
        WF_CHECK(agent != NULL);

        wf_lex_tools_ozone_report_query_reports_main_output *r = NULL;
        WF_CHECK(wf_ozone_report_queryReports(
                     NULL, NULL, &r) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ozone_report_queryReports(
                     agent, NULL, &r) == WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_report_get_assignments_main_params rap = {0};
        wf_lex_tools_ozone_report_get_assignments_main_output *ra = NULL;
        WF_CHECK(wf_ozone_report_getAssignments(
                     agent, NULL, &ra) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ozone_report_getAssignments(
                     agent, &rap, NULL) == WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_queue_list_queues_main_output *q = NULL;
        WF_CHECK(wf_ozone_queue_listQueues(
                     NULL, NULL, &q) == WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_signature_search_accounts_main_params sap = {0};
        wf_lex_tools_ozone_signature_search_accounts_main_output *sa = NULL;
        WF_CHECK(wf_ozone_signature_searchAccounts(
                     agent, NULL, &sa) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ozone_signature_searchAccounts(
                     agent, &sap, NULL) == WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_set_query_sets_main_params ssp = {0};
        wf_lex_tools_ozone_set_query_sets_main_output *ss = NULL;
        WF_CHECK(wf_ozone_set_querySets(agent, NULL, &ss) ==
                 WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ozone_set_querySets(agent, &ssp, NULL) ==
                 WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_setting_list_options_main_output *so = NULL;
        WF_CHECK(wf_ozone_setting_listOptions(
                     NULL, NULL, &so) == WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_communication_list_templates_main_output *ct = NULL;
        WF_CHECK(wf_ozone_communication_listTemplates(
                     NULL, &ct) == WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_server_get_config_main_output *sc = NULL;
        WF_CHECK(wf_ozone_server_getConfig(NULL, &sc) == WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_verification_list_verifications_main_output *v = NULL;
        WF_CHECK(wf_ozone_verification_listVerifications(
                     NULL, NULL, &v) == WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_safelink_query_rules_main_input sri = {0};
        wf_lex_tools_ozone_safelink_query_rules_main_output *slo = NULL;
        WF_CHECK(wf_ozone_safelink_queryRules(
                     agent, NULL, &slo) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ozone_safelink_queryRules(
                     agent, &sri, NULL) == WF_ERR_INVALID_ARG);

        wf_lex_tools_ozone_moderation_query_statuses_main_params msp = {0};
        wf_lex_tools_ozone_moderation_query_statuses_main_output *ms = NULL;
        WF_CHECK(wf_ozone_moderation_queryStatuses(
                     agent, NULL, &ms) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_ozone_moderation_queryStatuses(
                     agent, &msp, NULL) == WF_ERR_INVALID_ARG);

        wf_agent_free(agent);
    }

    WF_TEST_SUMMARY();
}
