/**
 * test_lexcall.c — exercise the generic lexicon output decoder registry.
 */

#ifndef WF_TEST_FIXTURE_DIR
#define WF_TEST_FIXTURE_DIR "test/fixtures"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/atproto_lex.h"
#include "wolfram/lexcall.h"
#include "test.h"

/* Minimal fixture loader modeled on test/test_crypto_interop.c. */
static char *load_fixture(const char *name, size_t *out_len)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", WF_TEST_FIXTURE_DIR, name);

    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0) {
        fclose(fp);
        return NULL;
    }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[got] = '\0';
    if (out_len)
        *out_len = got;
    return buf;
}

static void test_null_args(void)
{
    void *out = NULL;
    WF_CHECK(wf_lex_decode_output(NULL, "{}", 2, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_lex_decode_output("x", NULL, 0, &out) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_lex_decode_output("x", "{}", 2, NULL) == WF_ERR_INVALID_ARG);
}

static void test_unknown_nsid(void)
{
    void *out = NULL;
    WF_CHECK(wf_lex_decode_output("com.example.unknown", "{}", 2, &out) ==
             WF_ERR_NOT_FOUND);
    WF_CHECK(out == NULL);
}

static void test_unread_count(void)
{
    size_t len = 0;
    char *json = load_fixture("lexcall_unread.json", &len);
    WF_CHECK(json != NULL);
    if (!json)
        return;

    void *out = NULL;
    wf_status st = wf_lex_decode_output(
        "app.bsky.notification.getUnreadCount", json, len, &out);
    WF_CHECK(st == WF_OK);
    WF_CHECK(out != NULL);
    if (out) {
        wf_lex_app_bsky_notification_get_unread_count_main_output *u =
            (wf_lex_app_bsky_notification_get_unread_count_main_output *)out;
        WF_CHECK(u->count == 7);
        wf_lex_output_free("app.bsky.notification.getUnreadCount", out);
    }
    free(json);
}

static void test_get_head(void)
{
    const char *json = "{\"root\":\"bafyreiaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}";
    void *out = NULL;
    wf_status st = wf_lex_decode_output("com.atproto.sync.getHead", json,
                                        strlen(json), &out);
    WF_CHECK(st == WF_OK);
    WF_CHECK(out != NULL);
    if (out) {
        wf_lex_com_atproto_sync_get_head_main_output *h =
            (wf_lex_com_atproto_sync_get_head_main_output *)out;
        WF_CHECK(h->root != NULL);
        WF_CHECK(strcmp(h->root,
                        "bafyreiaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") ==
                 0);
        wf_lex_output_free("com.atproto.sync.getHead", out);
    }
}

static void test_free_unknown_nsid_is_noop(void)
{
    wf_lex_output_free("com.example.unknown", NULL);
    wf_lex_output_free("com.example.unknown", (void *)0x1);
}

int main(void)
{
    test_null_args();
    test_unknown_nsid();
    test_unread_count();
    test_get_head();
    test_free_unknown_nsid_is_noop();
    WF_TEST_SUMMARY();
}
