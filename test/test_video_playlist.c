#include "test.h"
#include "wolfram/embed_typed.h"
#include <string.h>

/* A finished VOD playlist: three segments, 10 + 9.5 + 4.25 = 23.75 s. */
static const char kVod[] = "#EXTM3U\n"
                           "#EXT-X-VERSION:3\n"
                           "#EXT-X-TARGETDURATION:10\n"
                           "#EXT-X-PLAYLIST-TYPE:VOD\n"
                           "#EXTINF:10.000,\n"
                           "seg0.ts\n"
                           "#EXTINF:9.500,\n"
                           "seg1.ts\n"
                           "#EXTINF:4.25,\n"
                           "seg2.ts\n"
                           "#EXT-X-ENDLIST\n";

/* The same playlist with CRLF line endings and a segment title. */
static const char kVodCrlf[] = "#EXTM3U\r\n"
                               "#EXT-X-TARGETDURATION:10\r\n"
                               "#EXTINF:10,first\r\n"
                               "seg0.ts\r\n"
                               "#EXTINF: 9.5 ,\r\n"
                               "seg1.ts\r\n"
                               "#EXTINF:4.25,\r\n"
                               "seg2.ts\r\n"
                               "#EXT-X-ENDLIST\r\n";

/* A live playlist: segments so far, no #EXT-X-ENDLIST, so no final length. */
static const char kLive[] = "#EXTM3U\n"
                            "#EXT-X-TARGETDURATION:10\n"
                            "#EXT-X-MEDIA-SEQUENCE:4\n"
                            "#EXTINF:10.000,\n"
                            "seg4.ts\n"
                            "#EXTINF:10.000,\n"
                            "seg5.ts\n";

/* A live playlist with no segments yet. */
static const char kLiveEmpty[] = "#EXTM3U\n"
                                 "#EXT-X-TARGETDURATION:10\n"
                                 "#EXT-X-MEDIA-SEQUENCE:0\n";

/* A finished playlist that lists no segments. */
static const char kEndedEmpty[] = "#EXTM3U\n#EXT-X-ENDLIST\n";

/* A master playlist: it lists variant playlists, not segments, so it has no
 * duration of its own. The caller must fetch a variant and call again. */
static const char kMaster[] =
    "#EXTM3U\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360\n"
    "360p/index.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=2000000,RESOLUTION=1280x720\n"
    "720p/index.m3u8\n";

/* Not an m3u8 at all: an HTML error page. */
static const char kNotM3u8[] = "<html><body>Not Found</body></html>\n";

/* A duration that is not a number. */
static const char kBadNumber[] = "#EXTM3U\n"
                                 "#EXTINF:ten,\n"
                                 "seg0.ts\n"
                                 "#EXT-X-ENDLIST\n";

/* An #EXTINF line with no comma after the duration. */
static const char kNoComma[] = "#EXTM3U\n"
                               "#EXTINF:10\n"
                               "seg0.ts\n"
                               "#EXT-X-ENDLIST\n";

/* A negative duration. */
static const char kNegative[] = "#EXTM3U\n"
                                "#EXTINF:-3.0,\n"
                                "seg0.ts\n"
                                "#EXT-X-ENDLIST\n";

static int near(double a, double b) {
    double diff = a - b;
    return diff < 1e-9 && diff > -1e-9;
}

static void check_duration(const char *body, size_t len, wf_status want_status,
                           double want_seconds) {
    double seconds = -1.0;
    wf_status s = wf_embed_video_playlist_duration(body, len, &seconds);
    WF_CHECK(s == want_status);
    if (want_status == WF_OK) {
        WF_CHECK(near(seconds, want_seconds));
    } else {
        WF_CHECK(seconds == 0.0);
    }
}

int main(void) {
    double seconds = -1.0;

#define CHECK_BODY(lit, status, secs)                                          \
    check_duration((lit), sizeof(lit) - 1, (status), (secs))

    /* VOD: the EXTINF durations sum to the length. */
    CHECK_BODY(kVod, WF_OK, 23.75);
    CHECK_BODY(kVodCrlf, WF_OK, 23.75);

    /* Live: no final duration, so no answer. */
    CHECK_BODY(kLive, WF_ERR_NOT_FOUND, 0.0);
    CHECK_BODY(kLiveEmpty, WF_ERR_NOT_FOUND, 0.0);
    CHECK_BODY(kEndedEmpty, WF_ERR_NOT_FOUND, 0.0);
    CHECK_BODY(kMaster, WF_ERR_NOT_FOUND, 0.0);

    /* Malformed input. */
    CHECK_BODY(kNotM3u8, WF_ERR_PARSE, 0.0);
    CHECK_BODY(kBadNumber, WF_ERR_PARSE, 0.0);
    CHECK_BODY(kNoComma, WF_ERR_PARSE, 0.0);
    CHECK_BODY(kNegative, WF_ERR_PARSE, 0.0);
    check_duration("", 0, WF_ERR_PARSE, 0.0);

    /* Bad arguments. */
    WF_CHECK(wf_embed_video_playlist_duration(NULL, 0, &seconds) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_embed_video_playlist_duration(kVod, sizeof(kVod) - 1, NULL) ==
             WF_ERR_INVALID_ARG);

#undef CHECK_BODY

    WF_TEST_SUMMARY();
}
