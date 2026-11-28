/**
 * plc_recovery.h — pre-registered did:plc rotation keys for account
 * recovery.
 *
 * The atproto account-recovery guide recommends registering a rotation key
 * you hold yourself, ahead of the PDS's key, so the account survives losing
 * the PDS. This header covers the client half of that, in three parts:
 *
 *   1. Keys: generate one, print it as the multibase secret `goat key
 *      generate` prints, and read it back, checked against the public key
 *      the caller expects.
 *   2. Registration: whether a public key is in a DID's current
 *      `rotationKeys`, and at which priority (index 0 is the highest).
 *   3. Update: an operation that adds or replaces one rotation key and
 *      copies every other field forward, plus a validator for that
 *      operation that runs before anything is submitted.
 *
 * What it does not do: no live PLC directory has been exercised by the
 * test suite (recorded public operations stand in), and the key string is
 * the private key itself, with no passphrase and no checksum. How a client
 * stores it is the client's decision.
 *
 * KEM: upstream has not standardised a KEM-based rekeying scheme. Keys go
 * through wf_plc_recovery_key, tagged with a wf_plc_recovery_scheme, so a
 * KEM scheme can be added as a new tag without changing these signatures.
 * Today the only scheme is a did:plc rotation key. Nothing here writes an
 * `atproto_kem` verification method; an update copies an existing one
 * forward untouched.
 *
 * Every heap output is owned by the caller and has its release function
 * named next to it.
 */

#ifndef WOLFRAM_PLC_RECOVERY_H
#define WOLFRAM_PLC_RECOVERY_H

#include <stddef.h>

#include "wolfram/crypto.h"
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The did:plc spec's limit on `rotationKeys` entries. */
#define WF_PLC_MAX_ROTATION_KEYS 5

/** How a recovery key recovers the account. */
typedef enum wf_plc_recovery_scheme {
    WF_PLC_RECOVERY_SCHEME_UNKNOWN = 0,
    /** A did:plc rotation key: an ECDSA key (secp256k1 or P-256) in
     * `rotationKeys` that signs PLC operations directly. */
    WF_PLC_RECOVERY_SCHEME_ROTATION_KEY = 1,
    /* Reserved: a KEM scheme, once upstream standardises one. */
} wf_plc_recovery_scheme;

/** A recovery key in memory. The key material is secret. */
typedef struct wf_plc_recovery_key {
    wf_plc_recovery_scheme scheme;
    /** The signing key, for WF_PLC_RECOVERY_SCHEME_ROTATION_KEY. */
    wf_signing_key signing;
} wf_plc_recovery_key;

/**
 * Generate a recovery key. Only WF_PLC_RECOVERY_SCHEME_ROTATION_KEY is
 * supported; any other scheme is WF_ERR_UNSUPPORTED. secp256k1 needs
 * libsecp256k1 (see wf_signing_key_generate); P-256 builds everywhere.
 */
wf_status wf_plc_recovery_key_generate(wf_plc_recovery_scheme scheme,
                                       wf_key_type type,
                                       wf_plc_recovery_key *out);

/**
 * Print a recovery key as its secret string: for a rotation key, the
 * multibase form of wf_signing_key_to_multibase, readable by `goat` and
 * indigo. Never log it. Release with wf_signing_key_secret_free.
 */
wf_status wf_plc_recovery_key_encode(const wf_plc_recovery_key *key,
                                     char **out_secret);

/**
 * Read a secret string back into a key.
 *
 * Structural damage (see wf_signing_key_from_multibase) is WF_ERR_PARSE.
 * base58btc has no checksum, so a mistyped character usually still parses,
 * as a different key. Pass `expected_didkey` (the did:key the account has
 * registered, from wf_plc_rotation_key_check or the recovery record) and
 * the derived public key must match it: a mismatch is WF_ERR_VALIDATION.
 * This is how a typo is caught. NULL skips that check.
 *
 * Deriving a secp256k1 public key needs libsecp256k1; without it a
 * secp256k1 secret with an `expected_didkey` fails with the status
 * wf_signing_key_public_didkey returns. On any error `out` is zeroed.
 */
wf_status wf_plc_recovery_key_decode(const char *secret,
                                     const char *expected_didkey,
                                     wf_plc_recovery_key *out);

/** The key's public did:key, to register. free() the result. */
wf_status wf_plc_recovery_key_public(const wf_plc_recovery_key *key,
                                     char **out_didkey);

/** Wipe a key held in memory. NULL-safe. */
void wf_plc_recovery_key_clear(wf_plc_recovery_key *key);

/**
 * Where `didkey` sits in the `rotationKeys` of a PLC operation: *out_index
 * is its index (0 = highest authority), or -1 when it is not registered.
 * Keys are compared by curve and public point, not by string.
 *
 * `op_json` must be a `plc_operation`. A `plc_tombstone` or a legacy
 * `create` operation is WF_ERR_UNSUPPORTED; a malformed one WF_ERR_PARSE;
 * a `didkey` that does not decode WF_ERR_INVALID_ARG.
 */
wf_status wf_plc_rotation_key_index(const char *op_json, const char *didkey,
                                    int *out_index);

/**
 * Fetch the DID's current operation from the directory
 * (wf_plc_get_last_op) and report wf_plc_rotation_key_index for it. This
 * says whether a recovery with that key would be accepted today, before
 * the emergency. `client` is caller-owned.
 */
wf_status wf_plc_rotation_key_check(wf_xrpc_client *client,
                                    const char *plc_directory_url,
                                    const char *did, const char *didkey,
                                    int *out_index);

/** One rotation-key change. */
typedef struct wf_plc_rotation_key_change {
    /** The did:key to add. Must be a secp256k1 or P-256 did:key. */
    const char *add_didkey;
    /**
     * NULL: insert `add_didkey` at `position`.
     * Otherwise: put `add_didkey` where this key is, keeping its priority.
     */
    const char *replace_didkey;
    /**
     * Insert index for an insert, 0 = highest authority (goat's `--first`).
     * Must not exceed the current key count. Ignored for a replace.
     */
    size_t position;
} wf_plc_rotation_key_change;

/** A list of did:key strings. Release with wf_plc_rotation_keys_free. */
typedef struct wf_plc_rotation_keys {
    char **keys;
    size_t count;
} wf_plc_rotation_keys;

void wf_plc_rotation_keys_free(wf_plc_rotation_keys *keys);

/**
 * The `rotationKeys` that result from applying `change` to `prev_op_json`.
 * Use it as the rotation_keys of a com.atproto.identity.signPlcOperation
 * call (wf_identity_sign_plc_operation) when the PDS signs.
 *
 * Errors: WF_ERR_INVALID_ARG for a missing or undecodable key or an insert
 * position past the end; WF_ERR_DUPLICATE when `add_didkey` is already
 * registered; WF_ERR_NOT_FOUND when `replace_didkey` is not; WF_ERR_VALIDATION
 * when the result would exceed WF_PLC_MAX_ROTATION_KEYS; the statuses of
 * wf_plc_rotation_key_index for an unusable previous operation.
 */
wf_status wf_plc_rotation_keys_apply(const char *prev_op_json,
                                     const wf_plc_rotation_key_change *change,
                                     wf_plc_rotation_keys *out);

/**
 * Build the unsigned update: `prev_op_json` with `rotationKeys` changed by
 * `change`, `prev` set to `prev_cid`, and every other field (alsoKnownAs,
 * verificationMethods, services, and anything else present) copied
 * unchanged. Sign it with wf_plc_operation_sign using a key from the
 * previous operation's rotationKeys. Release with wf_plc_operation_free.
 */
wf_status
wf_plc_rotation_key_update_build(const char *prev_op_json, const char *prev_cid,
                                 const wf_plc_rotation_key_change *change,
                                 char **out_unsigned_op_json);

/**
 * Check a signed update before it is submitted, whoever signed it (this
 * process, or the PDS through signPlcOperation). In order:
 *
 *   - both operations are `plc_operation`s (WF_ERR_UNSUPPORTED otherwise
 *     for the previous one, WF_ERR_VALIDATION for the update);
 *   - the update's `prev` is `prev_cid` (WF_ERR_VALIDATION);
 *   - its rotationKeys are 1 to 5 secp256k1/P-256 did:keys with no
 *     duplicates (WF_ERR_VALIDATION);
 *   - no field other than rotationKeys, prev and sig differs from the
 *     previous operation (WF_ERR_VALIDATION);
 *   - when `expected` is given, rotationKeys equal what
 *     wf_plc_rotation_keys_apply gives for it (WF_ERR_VALIDATION);
 *   - the signature verifies under a key of the previous operation's
 *     rotationKeys, not the update's own (WF_ERR_CRYPTO).
 *
 * This checks what the directory will check about authority and shape. It
 * cannot see a `prev` that has since been superseded, or the directory's
 * rate limits. On WF_OK, if `out_signer_didkey` is non-NULL it receives the
 * did:key that signed; free() it.
 */
wf_status wf_plc_rotation_key_update_validate(
    const char *prev_op_json, const char *prev_cid, const char *signed_op_json,
    const wf_plc_rotation_key_change *expected, char **out_signer_didkey);

/**
 * Fetch the DID's current operation, build the update for `change`, sign
 * it with `signer` and validate it as above, without submitting it. A
 * `signer` that is not one of the current rotation keys fails here with
 * WF_ERR_CRYPTO instead of at the directory. Submit the result with
 * wf_plc_submit_operation_raw. Release with wf_plc_operation_free.
 */
wf_status wf_plc_build_rotation_key_update(
    wf_xrpc_client *client, const char *plc_directory_url, const char *did,
    const wf_plc_rotation_key_change *change, const wf_signing_key *signer,
    char **out_signed_op_json);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_PLC_RECOVERY_H */
