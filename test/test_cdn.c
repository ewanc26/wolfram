/*
 * test_cdn.c -- wf_bsky_cdn_url against test/vectors/cdn.json, plus buffer-size
 * and argument edges.
 */

#include "wolfram/cdn.h"

#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WF_TEST_VECTOR_DIR
#define WF_TEST_VECTOR_DIR "test/vectors"
#endif

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) == (size_t)n)
        b[n] = '\0';
    else {
        free(b);
        b = NULL;
    }
    fclose(f);
    return b;
}

static const char *str(const cJSON *o, const char *k) {
    return cJSON_GetObjectItemCaseSensitive(o, k)->valuestring;
}

static wf_cdn_preset preset_of(const char *n) {
    if (!strcmp(n, "avatar")) return WF_CDN_AVATAR;
    if (!strcmp(n, "avatar_thumbnail")) return WF_CDN_AVATAR_THUMBNAIL;
    if (!strcmp(n, "banner")) return WF_CDN_BANNER;
    if (!strcmp(n, "feed_thumbnail")) return WF_CDN_FEED_THUMBNAIL;
    return WF_CDN_FEED_FULLSIZE;
}

static wf_cdn_format format_of(const char *n) {
    if (!strcmp(n, "jpeg")) return WF_CDN_FORMAT_JPEG;
    if (!strcmp(n, "png")) return WF_CDN_FORMAT_PNG;
    return WF_CDN_FORMAT_KEEP;
}

int main(void) {
    char path[512], out[256];
    snprintf(path, sizeof path, "%s/cdn.json", WF_TEST_VECTOR_DIR);
    char *text = slurp(path);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    const cJSON *v;
    int ok = 0, refused = 0;
    WF_CHECK(root != NULL);

    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(root, "ok")) {
        wf_status st =
            wf_bsky_cdn_url(str(v, "url"), preset_of(str(v, "preset")),
                            format_of(str(v, "format")), out, sizeof out);
        if (st != WF_OK || strcmp(out, str(v, "expect")) != 0)
            fprintf(stderr, "case failed: %s -> %s\n", str(v, "name"), out);
        WF_CHECK(st == WF_OK);
        WF_CHECK(strcmp(out, str(v, "expect")) == 0);
        ok++;
    }
    cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(root, "refused")) {
        wf_status st = wf_bsky_cdn_url(str(v, "url"), WF_CDN_AVATAR_THUMBNAIL,
                                       WF_CDN_FORMAT_JPEG, out, sizeof out);
        if (st != WF_ERR_VALIDATION)
            fprintf(stderr, "accepted: %s\n", str(v, "name"));
        WF_CHECK(st == WF_ERR_VALIDATION);
        WF_CHECK(out[0] == '\0');
        refused++;
    }
    WF_CHECK(ok >= 6 && refused >= 10);

    {
        const char *u =
            "https://cdn.bsky.app/img/avatar/plain/did:plc:abc/bafk";
        char exact[80], small[8];
        wf_status st = wf_bsky_cdn_url(u, WF_CDN_AVATAR_THUMBNAIL,
                                       WF_CDN_FORMAT_JPEG, exact, sizeof exact);
        size_t n = strlen(exact);
        WF_CHECK(st == WF_OK);
        /* the exact size fits, one byte less does not and leaves "" behind */
        WF_CHECK(wf_bsky_cdn_url(u, WF_CDN_AVATAR_THUMBNAIL, WF_CDN_FORMAT_JPEG,
                                 out, n + 1) == WF_OK);
        WF_CHECK(wf_bsky_cdn_url(u, WF_CDN_AVATAR_THUMBNAIL, WF_CDN_FORMAT_JPEG,
                                 out, n) == WF_ERR_INVALID_ARG);
        WF_CHECK(out[0] == '\0');
        WF_CHECK(wf_bsky_cdn_url(u, WF_CDN_AVATAR, WF_CDN_FORMAT_KEEP, small,
                                 sizeof small) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_bsky_cdn_url(NULL, WF_CDN_AVATAR, WF_CDN_FORMAT_KEEP, out,
                                 sizeof out) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_bsky_cdn_url(u, WF_CDN_AVATAR, WF_CDN_FORMAT_KEEP, NULL,
                                 10) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_bsky_cdn_url(u, WF_CDN_AVATAR, WF_CDN_FORMAT_KEEP, out,
                                 0) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_bsky_cdn_url(u, (wf_cdn_preset)99, WF_CDN_FORMAT_KEEP, out,
                                 sizeof out) == WF_ERR_INVALID_ARG);
        WF_CHECK(wf_bsky_cdn_url(u, WF_CDN_AVATAR, (wf_cdn_format)99, out,
                                 sizeof out) == WF_ERR_INVALID_ARG);
    }

    cJSON_Delete(root);
    free(text);
    WF_TEST_SUMMARY();
}
