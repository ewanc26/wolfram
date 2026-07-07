/**
 * store.h — optional SQLite3-backed persistence for wolfram.
 *
 * Provides local persistence for two things an AT Protocol client
 * wants to survive a restart without network I/O:
 *
 *   1. The PDS session credentials (so a client can resume without
 *      re-logging-in), and
 *   2. The offline repo mirror produced by the agent_sync pipeline
 *      (its head CID and content-addressed blocks).
 *
 * This whole module is OPTIONAL. It is only compiled when wolfram is
 * built with `-DWOLFRAM_BUILD_STORE=ON`, which in turn requires the
 * system SQLite3 library + headers to be present. When the option is
 * OFF the header declares nothing and `sqlite_store.c` compiles to an
 * empty translation unit, so including this header unconditionally is
 * always safe.
 *
 * Optional at-rest encryption
 * ----------------------------
 * When wolfram is additionally built with `-DWOLFRAM_BUILD_STORE_CRYPTO=ON`
 * (which requires `WOLFRAM_BUILD_STORE=ON` and a system libsodium), the
 * persisted session credentials are encrypted at rest with libsodium's
 * `crypto_secretbox_easy` (XSalsa20-Poly1305, an authenticated cipher).
 * The 32-byte secret key is derived from a caller-supplied passphrase via
 * libsodium's `crypto_pwhash` (Argon2id); a per-store random salt is stored
 * in the database and a fresh random nonce is stored with every saved row.
 * wolfram performs NO hand-rolled cryptography — every cryptographic
 * operation is a direct libsodium call. When crypto is OFF the on-disk
 * format and behavior are unchanged (backward compatible).
 *
 * All functions return a `wf_status`. SQL failures are never swallowed:
 * they are mapped to a `WF_ERR_*` code (see wolfram/xrpc.h). Every
 * heap-allocated output has a matching free function, documented next
 * to the function that returns it.
 */

#ifndef WOLFRAM_STORE_H
#define WOLFRAM_STORE_H

#include "wolfram/session.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef WOLFRAM_BUILD_STORE

/** An open SQLite-backed store. Opaque to callers. */
typedef struct wf_store wf_store;

/**
 * Open (or create) a store at `path`. ":memory:" opens a private,
 * in-memory database useful for tests. On first open the required
 * tables are created if absent.
 *
 * Ownership: the returned store is caller-owned; free it with
 * wf_store_close.
 */
wf_status wf_store_open(wf_store **out, const char *path);

/** Close a store and release all resources. Safe to call with NULL. */
void wf_store_close(wf_store *s);

/**
 * Persist a session's credential data.
 *
 * The store keeps a single current session row (replaced on each call),
 * so only the most recently saved session is retrievable via
 * wf_store_load_session. Uses the caller's `sess->data` only; the
 * session's XRPC client is not touched.
 */
wf_status wf_store_save_session(wf_store *s, const wf_session *sess);

/**
 * Load the persisted session.
 *
 * Ownership: the returned `wf_session` is caller-owned and must be freed
 * with wf_session_free. On WF_OK `*out` is non-NULL; when no session has
 * been saved it returns WF_ERR_NOT_FOUND and leaves `*out` untouched.
 * The loaded session has a valid (but otherwise idle) XRPC client and
 * `has_session == 1`.
 */
wf_status wf_store_load_session(wf_store *s, wf_session **out);

#ifdef WOLFRAM_BUILD_STORE_CRYPTO

/**
 * Optional at-rest encryption — only declared when wolfram is built with
 * `-DWOLFRAM_BUILD_STORE_CRYPTO=ON`.
 *
 * Derive the secret key for the store from `passphrase` and cache it for the
 * lifetime of the store. The key is derived with libsodium `crypto_pwhash`
 * (Argon2id) from `passphrase` and a per-store random salt that is persisted
 * in the database on first call. The derived key is held in memory only and
 * is zeroed on wf_store_close.
 *
 * Must be called exactly once after wf_store_open and before the first
 * wf_store_save_session / wf_store_load_session when encryption is enabled;
 * saving or loading without a passphrase set fails with WF_ERR_INVALID_ARG.
 * Supplying the wrong passphrase at load time causes wf_store_load_session to
 * fail authentication (WF_ERR_INVALID_ARG) rather than return decrypted
 * garbage — the authenticated cipher detects tampering / wrong key.
 *
 * Ownership: `passphrase` remains caller-owned; the store keeps no reference
 * to it after the call returns. Safe to call with NULL (returns
 * WF_ERR_INVALID_ARG).
 */
wf_status wf_store_set_passphrase(wf_store *s, const char *passphrase);

#endif /* WOLFRAM_BUILD_STORE_CRYPTO */

/**
 * Persist the head CID of a repo mirror for `did`.
 * Replaces any previously stored head for that DID.
 */
wf_status wf_store_save_mirror_head(wf_store *s, const char *did,
                                    const char *cid);

/**
 * Load the persisted head CID for `did`.
 *
 * Ownership: `*out_cid` is caller-owned and must be freed with free().
 * Returns WF_ERR_NOT_FOUND when no head is stored for `did`.
 */
wf_status wf_store_load_mirror_head(wf_store *s, const char *did,
                                    char **out_cid);

/**
 * Persist a content-addressed mirror block (`block`/`block_len`) keyed by
 * the repo `did` and the block's CID (`cid`/`cid_len`). A block that is
 * already stored is treated as a no-op (INSERT OR IGNORE).
 */
wf_status wf_store_save_mirror_block(wf_store *s, const char *did,
                                     const uint8_t *cid, size_t cid_len,
                                     const uint8_t *block, size_t block_len);

/**
 * Load a previously stored mirror block by its `did` and CID.
 *
 * Ownership: `*out_block` is caller-owned and must be freed with free().
 * Returns WF_ERR_NOT_FOUND when no block matches.
 */
wf_status wf_store_load_mirror_block(wf_store *s, const char *did,
                                     const uint8_t *cid, size_t cid_len,
                                     uint8_t **out_block, size_t *out_block_len);

#endif /* WOLFRAM_BUILD_STORE */

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_STORE_H */
