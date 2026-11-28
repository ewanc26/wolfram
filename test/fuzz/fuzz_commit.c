/*
 * fuzz_commit.c — a signed repo commit block (wf_commit_parse).
 */

#include <string.h>

#include "fuzz.h"
#include "wolfram/repo/commit.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    wf_commit commit;
    memset(&commit, 0, sizeof(commit));
    if (wf_commit_parse(data, size, &commit) != WF_OK) return 0;
    /* Fixed-size fields must stay terminated and in bounds. */
    wf_fuzz_check(memchr(commit.did, '\0', sizeof(commit.did)) != NULL);
    wf_fuzz_check(memchr(commit.rev, '\0', sizeof(commit.rev)) != NULL);
    wf_fuzz_check(commit.sig_len <= sizeof(commit.sig));
    wf_fuzz_check(commit.data.len <= sizeof(commit.data.bytes));
    return 0;
}
