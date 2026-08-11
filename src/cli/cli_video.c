/*
 * cli_video.c — the `video` subcommands (upload, status), split out of main.c
 * as their own self-contained concern.
 */

#include "cli_video.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/video_typed.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_video(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }

    const char *sub = argv[1];
    if (strcmp(sub, "upload") == 0) {
        if (argc < 6) {
            usage_stream(stderr);
            return 0;
        }
        const char *service = argv[2];
        const char *handle = argv[3];
        const char *password = argv[4];
        const char *path = argv[5];

        FILE *f = fopen(path, "rb");
        if (!f) {
            fprintf(stderr, "error: cannot open '%s'\n", path);
            return 1;
        }
        if (fseek(f, 0, SEEK_END) != 0) {
            fclose(f);
            fprintf(stderr, "error: cannot seek '%s'\n", path);
            return 1;
        }
        long size = ftell(f);
        if (size < 0) {
            fclose(f);
            fprintf(stderr, "error: cannot stat '%s'\n", path);
            return 1;
        }
        rewind(f);
        void *buf = malloc((size_t)size > 0 ? (size_t)size : 1);
        if (!buf) {
            fclose(f);
            fprintf(stderr, "error: out of memory\n");
            return 1;
        }
        if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
            fclose(f);
            free(buf);
            fprintf(stderr, "error: failed to read '%s'\n", path);
            return 1;
        }
        fclose(f);

        wf_agent *agent = agent_login_or_err(service, handle, password);
        if (!agent) {
            free(buf);
            return 1;
        }

        wf_video_job_status job = {0};
        wf_status s =
            wf_agent_video_upload(agent, buf, (size_t)size, "video/mp4", &job);
        free(buf);
        if (s != WF_OK) {
            fprintf(stderr, "error: video upload failed (status %d)\n", (int)s);
            wf_agent_free(agent);
            return 1;
        }

        printf("uploaded: jobId=%s did=%s state=%s progress=%d\n",
               job.job_status.job_id ? job.job_status.job_id : "?",
               job.job_status.did ? job.job_status.did : "?",
               job.job_status.state ? job.job_status.state : "?",
               job.job_status.has_progress ? job.job_status.progress : -1);
        if (job.job_status.has_blob) {
            printf("blob: cid=%s mime=%s size=%lld\n",
                   job.job_status.blob.cid ? job.job_status.blob.cid : "?",
                   job.job_status.blob.mime_type ? job.job_status.blob.mime_type
                                                 : "?",
                   (long long)job.job_status.blob.size);
        }
        if (job.job_status.error)
            printf("error: %s\n", job.job_status.error);
        if (job.job_status.failure_code)
            printf("failureCode: %s\n", job.job_status.failure_code);
        if (job.job_status.message)
            printf("message: %s\n", job.job_status.message);

        wf_video_job_status_free(&job);
        wf_agent_free(agent);
        return 0;
    }

    if (strcmp(sub, "status") == 0) {
        if (argc < 6) {
            usage_stream(stderr);
            return 0;
        }
        const char *service = argv[2];
        const char *handle = argv[3];
        const char *password = argv[4];
        const char *job_id = argv[5];

        wf_agent *agent = agent_login_or_err(service, handle, password);
        if (!agent) {
            return 1;
        }

        wf_response res = {0};
        wf_status s = wf_agent_get_video_job_status(agent, job_id, &res);
        return finish_agent_response(agent, s, &res);
    }

    fprintf(stderr,
            "error: unknown video subcommand '%s' (try 'upload' or 'status')\n",
            sub);
    return 1;
}
