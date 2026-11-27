/**
 * spaces/httpsig.h — the HTTP Message Signatures (RFC 9421) of Spaces
 * (experimental; build with WOLFRAM_BUILD_SPACES). See docs/spaces.md.
 *
 * Two kinds, each found by its `tag` under whatever label it carries:
 *
 *   - Credential binding. Every request presenting a space credential is signed
 *     with the P-256 key the credential names in `cnf.kid`, over exactly
 *     ("authorization" "atproto-space-audience").
 *   - Response MACs. A repo host answers a MAC request with an HMAC-SHA256 over
 *     the `Atproto-Space-Commit` or `Atproto-Space-Record` header, keyed with
 *     the secret of spaces/kem.h.
 *
 * Header arguments are the field values, without the field name. A repeated
 * field must be joined with ", " first. Every function that checks input from a
 * peer returns WF_ERR_VALIDATION for anything wrong with it, malformed headers
 * included.
 */

#ifndef WOLFRAM_SPACES_HTTPSIG_H
#define WOLFRAM_SPACES_HTTPSIG_H

#include <stddef.h>

#include "wolfram/crypto.h"
#include "wolfram/spaces/kem.h"
#include "wolfram/spaces/lthash.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- credential binding ---- */

/** The `Signature-Input` and `Signature` values of a signed request. */
typedef struct wf_space_signature {
    char *signature_input;
    char *signature;
} wf_space_signature;

void wf_space_signature_free(wf_space_signature *signature);

/**
 * Sign a request. `authorization` is the exact `Authorization` value that will
 * be sent ("Atproto-Space-Credential <jwt>"), `audience` the
 * `Atproto-Space-Audience` value: the repo DID for repo methods, the authority
 * DID for host methods. `key` must be the P-256 credential key.
 */
wf_status wf_space_credential_sign(const wf_signing_key *key,
                                   const char *authorization,
                                   const char *audience,
                                   wf_space_signature *out);

/**
 * Repo or space host side: check a request's signature against `cnf_kid`, the
 * P-256 did:key of the credential. High-S signatures are accepted, as the
 * proposal requires. This checks the signature only: the caller must also
 * verify the credential (spaces/token.h) and that `audience` is the one the
 * request implies.
 */
wf_status wf_space_credential_verify(const char *authorization,
                                     const char *audience,
                                     const char *signature_input,
                                     const char *signature,
                                     const char *cnf_kid);

/* ---- commits ---- */

#define WF_SPACE_COMMIT_VERSION 1

/** `com.atproto.space.defs#commit`. Strings are borrowed unless parsed. */
typedef struct wf_space_commit {
    long long ver;
    const char *repo;
    const char *space;
    const char *rev;
    unsigned char data[WF_SPACE_DIGEST_BYTES];
    void *owned; /* storage of a parsed commit; NULL when built by hand */
} wf_space_commit;

/**
 * Parse the JSON form of a commit, where `data` is `{"$bytes": "..."}`. Checks
 * the version, the DID, space reference and TID syntax and the digest length.
 * Release with wf_space_commit_free. Authenticates nothing.
 */
wf_status wf_space_commit_parse_json(const char *json, size_t len,
                                     wf_space_commit *out);

void wf_space_commit_free(wf_space_commit *commit);

/* ---- response MACs ---- */

typedef enum wf_space_mac_profile {
    WF_SPACE_MAC_COMMIT = 0, /* getLatestCommit, getRepo, listRepoOps */
    WF_SPACE_MAC_RECORD = 1  /* getRecord */
} wf_space_mac_profile;

/** The `Accept-Signature` value asking for a MAC under `key_share`. */
wf_status wf_space_mac_request_format(wf_space_mac_profile profile,
                                      const char *key_share, char **out);

/** A MAC request as the host reads it. */
typedef struct wf_space_mac_request {
    wf_space_mac_profile profile;
    char *label;
    char *key_share;
    char *params; /* serialised signature parameters, echoed in the response */
} wf_space_mac_request;

/**
 * Host side: find the request for `profile` in an `Accept-Signature` value.
 * Requests with other tags are ignored. WF_ERR_VALIDATION (answer
 * `InvalidSpaceMac`) when the header is missing or malformed, holds no request
 * or more than one for the profile, covers anything else, carries a parameter
 * other than keyid, tag and alg, or names no key share.
 */
wf_status wf_space_mac_request_parse(const char *accept_signature,
                                     wf_space_mac_profile profile,
                                     wf_space_mac_request *out);

void wf_space_mac_request_free(wf_space_mac_request *request);

/** The three response headers of a MAC. */
typedef struct wf_space_mac_headers {
    char *covered; /* Atproto-Space-Commit or Atproto-Space-Record */
    char *signature_input;
    char *signature;
} wf_space_mac_headers;

void wf_space_mac_headers_free(wf_space_mac_headers *headers);

/** Host side: MAC a commit. `request` must be for the commit profile. Also send
 *  `Vary: Accept-Signature`. */
wf_status wf_space_commit_mac(const wf_space_commit *commit,
                              const wf_space_mac_request *request,
                              const unsigned char secret[WF_SPACE_SECRET_BYTES],
                              wf_space_mac_headers *out);

/** Host side: MAC a record. `request` must be for the record profile. */
wf_status wf_space_record_mac(const char *uri, const char *cid,
                              const wf_space_mac_request *request,
                              const unsigned char secret[WF_SPACE_SECRET_BYTES],
                              wf_space_mac_headers *out);

/**
 * Syncer side: accept `commit` (from the body, or the STAR metadata) only if it
 * is version 1, is for the requested `repo` and `space`, and the response MAC
 * verifies and describes exactly this commit. `key_share` is the one sent.
 *
 * On failure refresh the author's DID document and retry with a new
 * encapsulation. WF_OK authenticates the commit; only comparing its digest with
 * an LtHash of the records held shows the copy is complete.
 */
wf_status
wf_space_commit_verify(const wf_space_commit *commit, const char *repo,
                       const char *space, const char *key_share,
                       const unsigned char secret[WF_SPACE_SECRET_BYTES],
                       const char *commit_header, const char *signature_input,
                       const char *signature);

/**
 * Syncer side: check a `getRecord` MAC. `uri` is the URI requested and `cid`
 * the CID computed from the record returned, not the one the response states.
 */
wf_status
wf_space_record_verify(const char *uri, const char *cid, const char *key_share,
                       const unsigned char secret[WF_SPACE_SECRET_BYTES],
                       const char *record_header, const char *signature_input,
                       const char *signature);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SPACES_HTTPSIG_H */
