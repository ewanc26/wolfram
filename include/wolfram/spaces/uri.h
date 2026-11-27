/**
 * spaces/uri.h — `at:space/` URIs for AT Protocol Spaces (experimental).
 *
 * Spaces are in beta upstream and this API may change in any release. See
 * docs/spaces.md for the spec revision it follows.
 *
 * Two forms exist:
 *
 *   space reference  at:space/<authority>/<type>/<skey>
 *   space record
 * at:space/<authority>/<type>/<skey>/<author>/<collection>/<rkey>
 *
 * In a public `at://` URI the first DID is the author. In a space URI the first
 * DID is the space authority and the author comes later, so code that wants
 * "the DID" must know which kind it holds. This module is pure C and builds on
 * every target, console clients included.
 */

#ifndef WOLFRAM_SPACES_URI_H
#define WOLFRAM_SPACES_URI_H

#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest space key, in bytes. */
#define WF_SPACE_SKEY_MAX 512

/** A parsed space reference or space record URI. All strings are owned. */
typedef struct wf_space_uri {
    char *authority;  /* space authority DID */
    char *type;       /* space type NSID */
    char *skey;       /* space key */
    char *author;     /* record author DID; NULL in a space reference */
    char *collection; /* record collection NSID; NULL in a space reference */
    char *rkey;       /* record key; NULL in a space reference */
} wf_space_uri;

/**
 * True when `uri` starts with `at:space/`. This is only the prefix test used to
 * tell the two URI families apart; it says nothing about validity.
 */
int wf_space_uri_has_prefix(const char *uri);

/** True for 1-512 bytes of `A-Za-z0-9._:~-` other than "." and "..". */
int wf_space_skey_is_valid(const char *skey);

/**
 * Parse a space reference or a space record URI. `out` is zeroed first; on
 * WF_OK release it with wf_space_uri_free. Anything else, including a public
 * `at://` URI, a query or a fragment, is WF_ERR_PARSE.
 */
wf_status wf_space_uri_parse(const char *uri, wf_space_uri *out);

/** Release a parsed URI. Safe on a zeroed struct and on NULL. */
void wf_space_uri_free(wf_space_uri *uri);

/** Build a space reference. Caller frees *out. */
wf_status wf_space_ref_format(const char *authority, const char *type,
                              const char *skey, char **out);

/**
 * Build a space record URI inside the space named by `space_ref`, which must be
 * a space reference (not a record URI). Caller frees *out.
 */
wf_status wf_space_record_uri_format(const char *space_ref, const char *author,
                                     const char *collection, const char *rkey,
                                     char **out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SPACES_URI_H */
