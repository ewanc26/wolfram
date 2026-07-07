#include "wolfram/syntax.h"
#include "test.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYNTAX_FIXTURE_DIR "/Volumes/Storage/Developer/Git/wolfram/test/fixtures/syntax"

typedef int (*wf_fixture_validator)(const char *);

typedef struct {
    const char *path;
    wf_fixture_validator validator;
    int expected;
} wf_fixture_case;

static char *wf_read_entire_file(const char *path, size_t *len_out) {
    FILE *fp;
    long size_long;
    size_t size;
    char *buffer;
    if (len_out) *len_out = 0;
    fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size_long = ftell(fp);
    if (size_long < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    size = (size_t)size_long;
    buffer = (char *)malloc(size + 1);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }
    if (size > 0 && fread(buffer, 1, size, fp) != size) {
        free(buffer);
        fclose(fp);
        return NULL;
    }
    buffer[size] = '\0';
    fclose(fp);
    if (len_out) *len_out = size;
    return buffer;
}

static void wf_run_fixture_file(const char *path,
                                wf_fixture_validator validator,
                                int expected) {
    size_t size = 0;
    char *buffer = wf_read_entire_file(path, &size);
    char *line;
    char *end;
    char *limit;
    WF_CHECK(buffer != NULL);
    if (!buffer) return;
    line = buffer;
    limit = buffer + size;
    while (line < limit) {
        size_t len;
        if ((end = memchr(line, '\n', (size_t)(limit - line))) != NULL) {
            len = (size_t)(end - line);
        } else {
            len = (size_t)(limit - line);
        }
        if (len > 0 && line[len - 1] == '\r') len--;
        if (len > 0) {
            char saved = line[len];
            line[len] = '\0';
            if (line[0] != '#') {
                WF_CHECK(validator(line) == expected);
            }
            line[len] = saved;
        }
        if (!end) break;
        line = end + 1;
    }
    free(buffer);
}

static void wf_run_aturi_fixture_file(const char *path, int expected) {
    size_t size = 0;
    char *buffer = wf_read_entire_file(path, &size);
    char *line;
    char *end;
    char *limit;
    WF_CHECK(buffer != NULL);
    if (!buffer) return;
    line = buffer;
    limit = buffer + size;
    while (line < limit) {
        size_t len;
        if ((end = memchr(line, '\n', (size_t)(limit - line))) != NULL) {
            len = (size_t)(end - line);
        } else {
            len = (size_t)(limit - line);
        }
        if (len > 0 && line[len - 1] == '\r') len--;
        if (len > 0) {
            char saved = line[len];
            wf_syntax_aturi parsed = {0};
            line[len] = '\0';
            if (line[0] != '#') {
                int ok = wf_syntax_aturi_parse(line, &parsed);
                WF_CHECK(ok == expected);
                if (expected) {
                    WF_CHECK(parsed.alloc != NULL);
                    WF_CHECK(parsed.authority != NULL);
                    WF_CHECK(wf_syntax_at_identifier_is_valid(parsed.authority));
                    if (parsed.collection) {
                        WF_CHECK(wf_syntax_nsid_is_valid(parsed.collection));
                    }
                    if (parsed.record_key) {
                        WF_CHECK(wf_syntax_record_key_is_valid(parsed.record_key));
                    }
                } else {
                    WF_CHECK(parsed.alloc == NULL);
                }
            }
            wf_syntax_aturi_free(&parsed);
            line[len] = saved;
        }
        if (!end) break;
        line = end + 1;
    }
    free(buffer);
}

static void wf_run_fixture_cases(const wf_fixture_case *cases, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        wf_run_fixture_file(cases[i].path, cases[i].validator, cases[i].expected);
    }
}

static void wf_run_aturi_cases(const wf_fixture_case *cases, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        wf_run_aturi_fixture_file(cases[i].path, cases[i].expected);
    }
}

static void test_syntax_reference_valid(void) {
    const wf_fixture_case cases[] = {
        {SYNTAX_FIXTURE_DIR "/did_syntax_valid.txt", wf_syntax_did_is_valid, 1},
        {SYNTAX_FIXTURE_DIR "/handle_syntax_valid.txt", wf_syntax_handle_is_valid, 1},
        {SYNTAX_FIXTURE_DIR "/atidentifier_syntax_valid.txt", wf_syntax_at_identifier_is_valid, 1},
        {SYNTAX_FIXTURE_DIR "/nsid_syntax_valid.txt", wf_syntax_nsid_is_valid, 1},
        {SYNTAX_FIXTURE_DIR "/recordkey_syntax_valid.txt", wf_syntax_record_key_is_valid, 1},
        {SYNTAX_FIXTURE_DIR "/tid_syntax_valid.txt", wf_syntax_tid_is_valid, 1},
        {SYNTAX_FIXTURE_DIR "/datetime_syntax_valid.txt", wf_syntax_datetime_is_valid, 1},
        {SYNTAX_FIXTURE_DIR "/language_syntax_valid.txt", wf_syntax_language_is_valid, 1},
        {SYNTAX_FIXTURE_DIR "/datetime_parse_invalid.txt", wf_syntax_datetime_is_valid, 0},
        {SYNTAX_FIXTURE_DIR "/language_parse_invalid.txt", wf_syntax_language_is_valid, 0},
        {SYNTAX_FIXTURE_DIR "/aturi_syntax_valid.txt", NULL, 1},
    };
    wf_run_fixture_cases(cases, sizeof(cases) / sizeof(cases[0]) - 1);
    wf_run_aturi_cases(cases + (sizeof(cases) / sizeof(cases[0]) - 1), 1);
}

static void test_syntax_reference_invalid(void) {
    const wf_fixture_case cases[] = {
        {SYNTAX_FIXTURE_DIR "/did_syntax_invalid.txt", wf_syntax_did_is_valid, 0},
        {SYNTAX_FIXTURE_DIR "/handle_syntax_invalid.txt", wf_syntax_handle_is_valid, 0},
        {SYNTAX_FIXTURE_DIR "/atidentifier_syntax_invalid.txt", wf_syntax_at_identifier_is_valid, 0},
        {SYNTAX_FIXTURE_DIR "/nsid_syntax_invalid.txt", wf_syntax_nsid_is_valid, 0},
        {SYNTAX_FIXTURE_DIR "/recordkey_syntax_invalid.txt", wf_syntax_record_key_is_valid, 0},
        {SYNTAX_FIXTURE_DIR "/tid_syntax_invalid.txt", wf_syntax_tid_is_valid, 0},
        {SYNTAX_FIXTURE_DIR "/datetime_syntax_invalid.txt", wf_syntax_datetime_is_valid, 0},
        {SYNTAX_FIXTURE_DIR "/language_syntax_invalid.txt", wf_syntax_language_is_valid, 0},
        {SYNTAX_FIXTURE_DIR "/datetime_parse_invalid.txt", wf_syntax_datetime_is_valid, 0},
        {SYNTAX_FIXTURE_DIR "/language_parse_invalid.txt", wf_syntax_language_is_valid, 0},
        {SYNTAX_FIXTURE_DIR "/aturi_syntax_invalid.txt", NULL, 0},
    };
    wf_run_fixture_cases(cases, sizeof(cases) / sizeof(cases[0]) - 1);
    wf_run_aturi_cases(cases + (sizeof(cases) / sizeof(cases[0]) - 1), 0);
}

int main(void) {
    test_syntax_reference_valid();
    test_syntax_reference_invalid();
    WF_TEST_SUMMARY();
}
