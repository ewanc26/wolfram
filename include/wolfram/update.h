/*
 * update.h -- the platform-neutral half of client self-update: release
 * manifest parsing, semantic-version comparison and SHA-256 verification.
 * docs/update.md is the specification; test/vectors/update/ holds the shared
 * vectors every client (including Platinum's Node bridge) runs.
 *
 * What is NOT here: fetching the manifest or the file (use the XRPC/HTTP
 * transport), replacing the running binary, relaunching. Those are per
 * platform. Wolfram itself does not self-update.
 *
 * The header and the version/SHA-256 code are C89, so a Classic Mac OS build
 * can use them. Manifest parsing needs cJSON and is only built where cJSON is
 * (not the Mac OS 9 target).
 *
 * Security: a SHA-256 taken from a manifest fetched over TLS from the same
 * release is integrity against corruption, not authenticity against a
 * compromised release. Authenticity is a detached Ed25519 signature over the
 * manifest bytes (`update.json.sig`), checked by wf_update_verify_signature()
 * against a key compiled into the client. The manifest's own `signature` field
 * stays reserved: it is reported through `has_signature` and is NOT verified.
 */

#ifndef WOLFRAM_UPDATE_H
#define WOLFRAM_UPDATE_H

#include "wolfram/xrpc.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_UPDATE_SCHEMA 1
#define WF_UPDATE_APP_MAX 32
#define WF_UPDATE_VERSION_MAX 32
#define WF_UPDATE_NOTES_MAX 1024
#define WF_UPDATE_NAME_MAX 96
#define WF_UPDATE_URL_MAX 512
#define WF_SHA256_DIGEST_LEN 32

/* ---- versions -------------------------------------------------------- */

/*
 * Compare two strict semantic versions, MAJOR.MINOR.PATCH[-prerelease][+build]
 * (semver.org 2.0.0 precedence; build metadata is ignored). No leading "v", no
 * leading zeros in numeric parts, no missing parts. Returns <0, 0 or >0.
 * On a malformed string returns 0 and sets *err to nonzero (a malformed
 * version never silently compares equal: check *err). *err is set to 0 on
 * success. `err` may be NULL only if the caller knows both strings are valid.
 */
int wf_update_compare_versions(const char *a, const char *b, int *err);

/* Nonzero if `v` is a valid strict version. */
int wf_update_version_valid(const char *v);

/* ---- SHA-256 --------------------------------------------------------- */

typedef struct wf_sha256 {
    unsigned long state[8]; /* each holds a 32-bit value */
    unsigned long count_hi; /* total bytes, high and low 32 bits */
    unsigned long count_lo;
    unsigned char buf[64];
    unsigned int buf_len;
} wf_sha256;

void wf_sha256_init(wf_sha256 *ctx);
void wf_sha256_update(wf_sha256 *ctx, const void *data, size_t len);
void wf_sha256_final(wf_sha256 *ctx, unsigned char out[WF_SHA256_DIGEST_LEN]);

/* One-shot SHA-256 over an in-memory buffer. */
void wf_sha256_buffer(const void *data, size_t len,
                      unsigned char out[WF_SHA256_DIGEST_LEN]);

/* Decode 64 hex characters (either case) into 32 bytes. Returns WF_OK, or
 * WF_ERR_PARSE for any other length or a non-hex character. */
wf_status wf_sha256_from_hex(const char *hex, size_t len,
                             unsigned char out[WF_SHA256_DIGEST_LEN]);

/* ---- manifest -------------------------------------------------------- */

typedef struct wf_update_asset {
    char name[WF_UPDATE_NAME_MAX];
    char url[WF_UPDATE_URL_MAX];
    unsigned long size;
    unsigned char sha256[WF_SHA256_DIGEST_LEN];
} wf_update_asset;

typedef struct wf_update_manifest {
    char app[WF_UPDATE_APP_MAX];
    char version[WF_UPDATE_VERSION_MAX];
    char notes[WF_UPDATE_NOTES_MAX]; /* optional; empty if absent */
    wf_update_asset asset;
    int has_signature; /* a non-null "signature" was present; NOT verified */
} wf_update_manifest;

/* Constraints a caller can impose on a manifest. All optional (zero/NULL). */
typedef struct wf_update_policy {
    unsigned long max_size; /* reject assets larger than this; 0 = 64 MiB */
    const char *app;        /* require this "app" value */
    const char *url_prefix; /* asset url must start with this (https://...) */
} wf_update_policy;

#ifndef WF_UPDATE_NO_JSON
/*
 * Parse a manifest body (see docs/update.md). Rejects: bad JSON, a schema
 * other than 1, a missing or over-long field (never truncated), a malformed
 * version, a non-https asset URL (or one with userinfo or whitespace), size 0
 * or above the policy limit, a sha256 that is not 64 hex characters, and any
 * policy mismatch. All are WF_ERR_PARSE except a policy mismatch
 * (WF_ERR_VALIDATION). `out` is zeroed on every error.
 */
wf_status wf_update_parse_manifest(const char *body, size_t len,
                                   const wf_update_policy *policy,
                                   wf_update_manifest *out);
#endif

/* ---- verifying the download ----------------------------------------- */

typedef struct wf_update_verify {
    wf_sha256 sha;
    unsigned char expected[WF_SHA256_DIGEST_LEN];
    unsigned long want_size;
    unsigned long got_size;
    int overflow;
} wf_update_verify;

void wf_update_verify_init(wf_update_verify *v, const wf_update_asset *asset);
/* Feed downloaded bytes in any chunking. Returns WF_ERR_VALIDATION as soon as
 * more than the manifest's size has arrived (stop downloading), else WF_OK. */
wf_status wf_update_verify_feed(wf_update_verify *v, const void *data,
                                size_t len);
/* WF_OK only if exactly `size` bytes arrived and the SHA-256 matches
 * (constant-time compare); WF_ERR_VALIDATION otherwise. */
wf_status wf_update_verify_final(wf_update_verify *v);

/* ---- signature ------------------------------------------------------- */

#define WF_UPDATE_PUBLIC_KEY_LEN 32

/*
 * Check the exact bytes of update.json against its detached Ed25519 signature,
 * the `update.json.sig` asset: 128 hex characters (either case), optionally
 * followed by one newline. `pk` is the 32-byte public key compiled into the
 * client. WF_OK only if the signature verifies; WF_ERR_PARSE if the signature
 * text is not exactly that shape; WF_ERR_VALIDATION if it does not verify. Call
 * it on the downloaded bytes BEFORE parsing them, and refuse to update on any
 * result other than WF_OK: an unsigned release is not a valid one.
 *
 * Built where wolfram/ed25519.h is (C99); not part of the Mac OS 9 C89 target.
 */
wf_status wf_update_verify_signature(const void *manifest, size_t manifest_len,
                                     const char *sig_text, size_t sig_len,
                                     const unsigned char pk[WF_UPDATE_PUBLIC_KEY_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_UPDATE_H */
