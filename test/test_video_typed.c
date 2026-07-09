/*
 * test_video_typed.c — offline tests for the app.bsky.video typed parsers,
 * builders, and agent wrappers. Hardcodes representative response bodies and
 * asserts the owned structs are populated correctly, then freed. Agent wrappers
 * require live auth and are exercised only for NULL-argument validation.
 */

#include "wolfram/video_typed.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* app.bsky.video.getJobStatus output (envelope around a defs#jobStatus). */
static const char *kGetJobStatusJson =
    "{"
    "  \"jobStatus\": {"
    "    \"jobId\": \"bafytestjob000000000000000000000000000000000\","
    "    \"did\": \"did:plc:alice0000000000000000000000\","
    "    \"state\": \"JOB_STATE_COMPLETED\","
    "    \"progress\": 100,"
    "    \"blob\": {"
    "      \"$type\": \"blob\","
    "      \"ref\": { \"$link\": \"bafyreivideocid000000000000000000000\" },"
    "      \"mimeType\": \"video/mp4\","
    "      \"size\": 1234567"
    "    },"
    "    \"message\": \"done\""
    "  }"
    "}";

/* app.bsky.video.getUploadLimits output (with optional fields). */
static const char *kGetUploadLimitsJson =
    "{"
    "  \"canUpload\": true,"
    "  \"remainingDailyVideos\": 9,"
    "  \"remainingDailyBytes\": 1073741824,"
    "  \"message\": \"you may upload\""
    "}";

/* app.bsky.video.uploadVideo output (same envelope shape as getJobStatus). */
static const char *kUploadVideoJson =
    "{"
    "  \"jobStatus\": {"
    "    \"jobId\": \"bafytestjob111111111111111111111111111111111\","
    "    \"did\": \"did:plc:bob000000000000000000000000\","
    "    \"state\": \"JOB_STATE_IN_PROGRESS\","
    "    \"progress\": 42"
    "  }"
    "}";

static void test_parse_job_status(void) {
    wf_video_job_status s = {0};
    wf_status st = wf_video_job_status_parse(kGetJobStatusJson,
                                             strlen(kGetJobStatusJson), &s);
    WF_CHECK(st == WF_OK);
    WF_CHECK(s.job_status.job_id &&
             strcmp(s.job_status.job_id,
                    "bafytestjob000000000000000000000000000000000") == 0);
    WF_CHECK(s.job_status.did &&
             strcmp(s.job_status.did, "did:plc:alice0000000000000000000000") ==
                 0);
    WF_CHECK(s.job_status.state &&
             strcmp(s.job_status.state, "JOB_STATE_COMPLETED") == 0);
    WF_CHECK(s.job_status.has_progress && s.job_status.progress == 100);
    WF_CHECK(s.job_status.has_blob);
    WF_CHECK(s.job_status.blob.cid &&
             strcmp(s.job_status.blob.cid,
                    "bafyreivideocid000000000000000000000") == 0);
    WF_CHECK(s.job_status.blob.mime_type &&
             strcmp(s.job_status.blob.mime_type, "video/mp4") == 0);
    WF_CHECK(s.job_status.blob.has_size && s.job_status.blob.size == 1234567);
    WF_CHECK(s.job_status.message &&
             strcmp(s.job_status.message, "done") == 0);
    wf_video_job_status_free(&s);
}

static void test_parse_upload_limits(void) {
    wf_video_upload_limits l = {0};
    wf_status st = wf_video_upload_limits_parse(
        kGetUploadLimitsJson, strlen(kGetUploadLimitsJson), &l);
    WF_CHECK(st == WF_OK);
    WF_CHECK(l.can_upload == true);
    WF_CHECK(l.has_remaining_daily_videos && l.remaining_daily_videos == 9);
    WF_CHECK(l.has_remaining_daily_bytes && l.remaining_daily_bytes == 1073741824);
    WF_CHECK(l.message && strcmp(l.message, "you may upload") == 0);
    wf_video_upload_limits_free(&l);
}

static void test_parse_upload_limits_cannot(void) {
    static const char *json = "{\"canUpload\":false,\"error\":\"rate-limited\"}";
    wf_video_upload_limits l = {0};
    wf_status st = wf_video_upload_limits_parse(json, strlen(json), &l);
    WF_CHECK(st == WF_OK);
    WF_CHECK(l.can_upload == false);
    WF_CHECK(!l.has_remaining_daily_videos);
    WF_CHECK(!l.has_remaining_daily_bytes);
    WF_CHECK(l.error && strcmp(l.error, "rate-limited") == 0);
    wf_video_upload_limits_free(&l);
}

static void test_parse_upload_video(void) {
    wf_video_job_status s = {0};
    wf_status st = wf_video_job_status_parse(kUploadVideoJson,
                                             strlen(kUploadVideoJson), &s);
    WF_CHECK(st == WF_OK);
    WF_CHECK(s.job_status.did &&
             strcmp(s.job_status.did, "did:plc:bob000000000000000000000000") ==
                 0);
    WF_CHECK(s.job_status.state &&
             strcmp(s.job_status.state, "JOB_STATE_IN_PROGRESS") == 0);
    WF_CHECK(s.job_status.has_progress && s.job_status.progress == 42);
    WF_CHECK(!s.job_status.has_blob);
    wf_video_job_status_free(&s);
}

static void test_parse_job_status_def(void) {
    static const char *json =
        "{"
        "  \"jobId\": \"j1\","
        "  \"did\": \"did:plc:xyz\","
        "  \"state\": \"JOB_STATE_FAILED\","
        "  \"error\": \"boom\""
        "}";
    wf_video_job_status_def d = {0};
    wf_status st = wf_video_job_status_def_parse(json, strlen(json), &d);
    WF_CHECK(st == WF_OK);
    WF_CHECK(d.job_id && strcmp(d.job_id, "j1") == 0);
    WF_CHECK(d.state && strcmp(d.state, "JOB_STATE_FAILED") == 0);
    WF_CHECK(d.error && strcmp(d.error, "boom") == 0);
    WF_CHECK(!d.has_blob);
    wf_video_job_status_def_free(&d);
}

static void test_build_job_status_def_roundtrip(void) {
    wf_video_job_status_def in = {0};
    in.job_id = "job-abc";
    in.did = "did:plc:owner";
    in.state = "JOB_STATE_COMPLETED";
    in.has_progress = true;
    in.progress = 100;
    in.has_blob = true;
    in.blob.cid = "bafycid";
    in.blob.mime_type = "video/mp4";
    in.blob.has_size = true;
    in.blob.size = 2048;
    in.message = "ok";

    char *json = NULL;
    wf_status st = wf_video_job_status_def_build(&in, &json);
    WF_CHECK(st == WF_OK);
    WF_CHECK(json != NULL);

    wf_video_job_status_def out = {0};
    if (json) {
        st = wf_video_job_status_def_parse(json, strlen(json), &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out.job_id && strcmp(out.job_id, "job-abc") == 0);
        WF_CHECK(out.state && strcmp(out.state, "JOB_STATE_COMPLETED") == 0);
        WF_CHECK(out.has_progress && out.progress == 100);
        WF_CHECK(out.has_blob);
        WF_CHECK(out.blob.cid && strcmp(out.blob.cid, "bafycid") == 0);
        WF_CHECK(out.blob.mime_type &&
                 strcmp(out.blob.mime_type, "video/mp4") == 0);
        WF_CHECK(out.blob.has_size && out.blob.size == 2048);
        WF_CHECK(out.message && strcmp(out.message, "ok") == 0);
        wf_video_job_status_def_free(&out);
        free(json);
    }
}

static void test_build_upload_limits_roundtrip(void) {
    wf_video_upload_limits in = {0};
    in.can_upload = true;
    in.has_remaining_daily_videos = true;
    in.remaining_daily_videos = 3;
    in.has_remaining_daily_bytes = true;
    in.remaining_daily_bytes = 999;

    char *json = NULL;
    wf_status st = wf_video_upload_limits_build(&in, &json);
    WF_CHECK(st == WF_OK);
    WF_CHECK(json != NULL);

    wf_video_upload_limits out = {0};
    if (json) {
        st = wf_video_upload_limits_parse(json, strlen(json), &out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(out.can_upload == true);
        WF_CHECK(out.has_remaining_daily_videos &&
                 out.remaining_daily_videos == 3);
        WF_CHECK(out.has_remaining_daily_bytes &&
                 out.remaining_daily_bytes == 999);
        wf_video_upload_limits_free(&out);
        free(json);
    }
}

static void test_parse_errors(void) {
    wf_video_job_status s = {0};
    WF_CHECK(wf_video_job_status_parse(NULL, 0, &s) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_video_job_status_parse("{", 1, &s) == WF_ERR_PARSE);
    WF_CHECK(wf_video_job_status_parse("{}", 2, &s) == WF_ERR_PARSE);

    wf_video_upload_limits l = {0};
    WF_CHECK(wf_video_upload_limits_parse("{}", 2, &l) == WF_ERR_PARSE);
    WF_CHECK(wf_video_upload_limits_parse("{\"canUpload\":1}", 13, &l) ==
             WF_ERR_PARSE);
}

int main(void) {
    test_parse_job_status();
    test_parse_upload_limits();
    test_parse_upload_limits_cannot();
    test_parse_upload_video();
    test_parse_job_status_def();
    test_build_job_status_def_roundtrip();
    test_build_upload_limits_roundtrip();
    test_parse_errors();
    WF_TEST_SUMMARY();
}
