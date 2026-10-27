/*
 * cdn.h -- pick a size and an image format from a Bluesky CDN URL.
 *
 * The URLs the AppView returns for avatars, banners and post images look like
 *
 *   https://cdn.bsky.app/img/avatar/plain/did:plc:.../bafkrei...[@jpeg]
 *
 * and name a preset (`avatar` is up to 1000 px square, `avatar_thumbnail` is
 * 128 px) and, optionally, a format. With no format the CDN answers in WebP,
 * which wolfram/image.h cannot decode, and the full-size presets cost a client
 * with a few megabytes far more than it can draw. A client that wants a small
 * decodable image rewrites the URL: this does it once, the same way for every
 * client. Pure C89, no allocation.
 */

#ifndef WOLFRAM_CDN_H
#define WOLFRAM_CDN_H

#include "wolfram/xrpc.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum wf_cdn_preset {
    WF_CDN_AVATAR = 0,       /* "avatar": up to 1000 px */
    WF_CDN_AVATAR_THUMBNAIL, /* "avatar_thumbnail": 128 px */
    WF_CDN_BANNER,           /* "banner": 3:1, up to 3000 px wide */
    WF_CDN_FEED_THUMBNAIL,   /* "feed_thumbnail": up to 1000 px */
    WF_CDN_FEED_FULLSIZE     /* "feed_fullsize": up to 2000 px */
} wf_cdn_preset;

typedef enum wf_cdn_format {
    WF_CDN_FORMAT_KEEP =
        0,              /* leave any "@format" suffix as it is (or absent) */
    WF_CDN_FORMAT_JPEG, /* "@jpeg": what wolfram/image.h decodes */
    WF_CDN_FORMAT_PNG   /* "@png" */
} wf_cdn_format;

/*
 * Rewrite `url` to use `preset` and `format`, writing a NUL-terminated string
 * to `out` (`cap` bytes, including the NUL). `url` must be an https URL on
 * cdn.bsky.app of the form /img/<known preset>/plain/<did>/<cid>[@<format>];
 * anything else (another host, a query string, an unknown preset, an unknown
 * format suffix) is refused so a caller never rewrites a URL it does not
 * understand. Returns WF_OK; WF_ERR_VALIDATION if `url` is not such a URL;
 * WF_ERR_INVALID_ARG for a NULL argument or a `cap` too small for the result
 * (`out` is then left as an empty string when cap > 0).
 */
wf_status wf_bsky_cdn_url(const char *url, wf_cdn_preset preset,
                          wf_cdn_format format, char *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_CDN_H */
