# Bluesky CDN image URLs

The AppView returns image URLs like `https://cdn.bsky.app/img/avatar/plain/did:plc:.../bafkrei...`. Two things about them cost clients on small machines:

- The path names a **preset**: `avatar` is up to 1000 px square, `avatar_thumbnail` is 128 px, `banner`, `feed_thumbnail` (up to 1000 px) and `feed_fullsize` (up to 2000 px) are the others. A 128 px avatar is a couple of kilobytes; the `avatar` one can be over a hundred.
- With no `@format` suffix the CDN answers in **WebP**, which [`wolfram/image.h`](../include/wolfram/image.h) (vendored stb_image) cannot decode. `@jpeg` and `@png` work.

[`wolfram/cdn.h`](../include/wolfram/cdn.h) rewrites one into the other, once, so Indigo, Cobalt and the rest do not each carry their own string surgery:

```c
char small[160];
if (wf_bsky_cdn_url(avatar_url, WF_CDN_AVATAR_THUMBNAIL, WF_CDN_FORMAT_JPEG,
                    small, sizeof small) != WF_OK)
    /* not a CDN URL, or the buffer is too small: use the original */;
```

It refuses (`WF_ERR_VALIDATION`) anything that is not exactly `https://cdn.bsky.app/img/<known preset>/plain/<did>/<cid>[@jpeg|@png]`: another host, a query string, an unknown preset or format, a second suffix. `WF_ERR_INVALID_ARG` is a NULL argument or a buffer too small; `out` is then an empty string. The result is longer than the input when the preset name is longer (`avatar_thumbnail` adds ten bytes, `@jpeg` five), so size the buffer for 160 bytes, not for the input.

It is strict C89 with no allocation. Vectors are in [`test/vectors/cdn.json`](../test/vectors/cdn.json).
