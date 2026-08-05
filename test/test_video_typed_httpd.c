/*
 * test_video_typed_httpd.c — offline end-to-end integration test for the
 * app.bsky.video agent wrappers (wf_agent_get_video_job_status /
 * wf_agent_get_video_upload_limits, in src/agent/agent.c).
 *
 * Prior to issue #17's fix these two reached the PDS via a hand-built
 * wf_xrpc_query_params call plus a WF_LEX_*_NSID constant, bypassing the
 * generated wf_lex_app_bsky_video_get_job_status_main_call /
 * wf_lex_app_bsky_video_get_upload_limits_main_call. test_video_typed.c
 * already covers the parsers offline; this test drives a real local
 * libmicrohttpd mock PDS so the request actually leaves the agent and is
 * served by an HTTP handler, then asserts both that the correct NSID was
 * hit and that the canned response round-trips through the typed parser —
 * proof the swap to generated marshalling didn't change the wire behavior.
 *
 * Built only when WOLFRAM_BUILD_TEST_HTTPD=ON.
 */

#include "wolfram/video_typed.h"
#include "wolfram/agent.h"

#include "mock_pds.h"
#include "test.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    wf_mock_pds *pds = NULL;
    int port = 0;
    WF_CHECK(wf_mock_pds_start(&pds, &port) == WF_OK);
    WF_CHECK(pds != NULL);
    WF_CHECK(port > 0);

    /* wf_agent_resume forces a token-refresh round-trip (matching the
     * reference client's semantics), so refreshSession needs a canned
     * response too — see wf_session_resume -> wf_session_refresh. */
    const char *session_json =
        "{\"did\":\"did:plc:abc123\",\"handle\":\"alice.test\","
        "\"accessJwt\":\"eyJ.fake.access\",\"refreshJwt\":\"eyJ.fake.refresh\","
        "\"active\":true}";
    const char *job_status_json =
        "{\"jobStatus\":{\"jobId\":\"httpd-job-1\",\"did\":\"did:plc:abc123\","
        "\"state\":\"JOB_STATE_COMPLETED\",\"progress\":100}}";
    const char *upload_limits_json =
        "{\"canUpload\":true,\"remainingDailyVideos\":5}";

    WF_CHECK(wf_mock_pds_register(pds, "com.atproto.server.refreshSession",
                                  session_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "app.bsky.video.getJobStatus",
                                  job_status_json) == WF_OK);
    WF_CHECK(wf_mock_pds_register(pds, "app.bsky.video.getUploadLimits",
                                  upload_limits_json) == WF_OK);

    char base_url[64];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%d", port);
    wf_agent *agent = wf_agent_new(base_url);
    WF_CHECK(agent != NULL);

    wf_session_data data;
    memset(&data, 0, sizeof(data));
    data.did = "did:plc:abc123";
    data.handle = "alice.test";
    data.access_jwt = "eyJ.fake.access";
    data.refresh_jwt = "eyJ.fake.refresh";
    data.active = 1;
    WF_CHECK(wf_agent_resume(agent, &data) == WF_OK);

    const char *last_nsid = NULL;
    const char *last_method = NULL;
    const char *last_body = NULL;

    /* ---- getJobStatus: must route through the generated _main_call ---- */
    {
        wf_video_job_status out = {0};
        wf_status st =
            wf_agent_video_get_job_status(agent, "httpd-job-1", &out);
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid &&
                 strcmp(last_nsid, "app.bsky.video.getJobStatus") == 0);
        WF_CHECK(last_method && strcmp(last_method, "GET") == 0);
        WF_CHECK(out.job_status.job_id &&
                 strcmp(out.job_status.job_id, "httpd-job-1") == 0);
        WF_CHECK(out.job_status.has_progress && out.job_status.progress == 100);
        wf_video_job_status_free(&out);
    }

    /* ---- getUploadLimits: must route through the generated _main_call --- */
    {
        wf_video_upload_limits out = {0};
        wf_status st = wf_agent_video_get_upload_limits(agent, &out);
        WF_CHECK(st == WF_OK);
        wf_mock_pds_get_last_request(pds, &last_nsid, &last_method, &last_body);
        WF_CHECK(last_nsid &&
                 strcmp(last_nsid, "app.bsky.video.getUploadLimits") == 0);
        WF_CHECK(last_method && strcmp(last_method, "GET") == 0);
        WF_CHECK(out.can_upload == true);
        WF_CHECK(out.has_remaining_daily_videos &&
                 out.remaining_daily_videos == 5);
        wf_video_upload_limits_free(&out);
    }

    wf_agent_free(agent);
    wf_mock_pds_free(pds);

    WF_TEST_SUMMARY();
}
