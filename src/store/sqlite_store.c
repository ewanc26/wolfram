/**
 * sqlite_store.c — SQLite3-backed persistence for wolfram.
 *
 * Optional module. Compiles to nothing unless WOLFRAM_BUILD_STORE is
 * defined (set by the build when CMake's WOLFRAM_BUILD_STORE option is
 * ON and the system SQLite3 library is available). See wolfram/store.h.
 *
 * When additionally built with WOLFRAM_BUILD_STORE_CRYPTO, the persisted
 * session credentials are encrypted at rest with libsodium
 * (crypto_secretbox_easy — XSalsa20-Poly1305) using a key derived from a
 * caller-supplied passphrase via libsodium's crypto_pwhash (Argon2id). All
 * cryptography is delegated to libsodium; no hand-rolled crypto is present.
 */

#ifdef WOLFRAM_BUILD_STORE

#include "wolfram/store.h"

#include <sqlite3.h>

#include <stdlib.h>
#include <string.h>

#ifdef WOLFRAM_BUILD_STORE_CRYPTO
#include <sodium.h>
#endif

struct wf_store {
    sqlite3 *db;
#ifdef WOLFRAM_BUILD_STORE_CRYPTO
    unsigned char key[crypto_secretbox_KEYBYTES];
    int has_key;
#endif
};

/* Placeholder URL for the XRPC client owned by a loaded session. The
 * client is never used for network I/O by the store; it only exists so
 * that wf_session_free can release a valid handle. */
static const char *k_session_client_url = "https://bsky.social";

static wf_status store_init_schema(sqlite3 *db) {
    const char *ddl;
#ifdef WOLFRAM_BUILD_STORE_CRYPTO
    /* Encrypted on-disk format: a per-store pwhash salt lives in store_meta;
     * the session row holds a fresh nonce and the secretbox ciphertext. */
    ddl =
        "CREATE TABLE IF NOT EXISTS session ("
        "  id INTEGER PRIMARY KEY CHECK (id = 1),"
        "  nonce BLOB NOT NULL,"
        "  blob BLOB NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS store_meta ("
        "  k TEXT PRIMARY KEY,"
        "  v BLOB NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS mirror_head ("
        "  did TEXT PRIMARY KEY,"
        "  cid TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS mirror_block ("
        "  did TEXT NOT NULL,"
        "  cid BLOB NOT NULL,"
        "  block BLOB NOT NULL,"
        "  PRIMARY KEY (did, cid)"
        ");";
#else
    ddl =
        "CREATE TABLE IF NOT EXISTS session ("
        "  id INTEGER PRIMARY KEY CHECK (id = 1),"
        "  access_jwt TEXT NOT NULL,"
        "  refresh_jwt TEXT NOT NULL,"
        "  handle TEXT NOT NULL,"
        "  did TEXT NOT NULL,"
        "  email TEXT,"
        "  email_confirmed INTEGER NOT NULL DEFAULT -1,"
        "  email_auth_factor INTEGER NOT NULL DEFAULT -1,"
        "  active INTEGER NOT NULL DEFAULT -1,"
        "  status TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS mirror_head ("
        "  did TEXT PRIMARY KEY,"
        "  cid TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS mirror_block ("
        "  did TEXT NOT NULL,"
        "  cid BLOB NOT NULL,"
        "  block BLOB NOT NULL,"
        "  PRIMARY KEY (did, cid)"
        ");";
#endif

    char *err = NULL;
    if (sqlite3_exec(db, ddl, NULL, NULL, &err) != SQLITE_OK) {
        if (err) sqlite3_free(err);
        return WF_ERR_INVALID_ARG;
    }
    return WF_OK;
}

wf_status wf_store_open(wf_store **out, const char *path) {
    if (!out || !path) return WF_ERR_INVALID_ARG;

#ifdef WOLFRAM_BUILD_STORE_CRYPTO
    /* Initialize libsodium (idempotent; safe to call repeatedly). */
    if (sodium_init() < 0) return WF_ERR_INVALID_ARG;
#endif

    wf_store *s = calloc(1, sizeof(*s));
    if (!s) return WF_ERR_ALLOC;

    if (sqlite3_open(path, &s->db) != SQLITE_OK) {
        sqlite3_close(s->db);
        free(s);
        return WF_ERR_INVALID_ARG;
    }

    wf_status st = store_init_schema(s->db);
    if (st != WF_OK) {
        sqlite3_close(s->db);
        free(s);
        return st;
    }

    *out = s;
    return WF_OK;
}

void wf_store_close(wf_store *s) {
    if (!s) return;
#ifdef WOLFRAM_BUILD_STORE_CRYPTO
    if (s->has_key) {
        sodium_memzero(s->key, sizeof(s->key));
        s->has_key = 0;
    }
#endif
    sqlite3_close(s->db);
    free(s);
}

/* Bind a (possibly NULL) text value to a 1-based parameter. */
static int bind_text_opt(sqlite3_stmt *stmt, int idx, const char *text) {
    if (text) return sqlite3_bind_text(stmt, idx, text, -1, SQLITE_TRANSIENT);
    return sqlite3_bind_null(stmt, idx);
}

#ifdef WOLFRAM_BUILD_STORE_CRYPTO

/* ---- Encrypted (libsodium) session persistence -------------------------- */

static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Little-endian, length-prefixed serialization of the credential fields.
 * Layout: for each of the 6 string fields a 1-byte presence flag, then
 * (when present) a 4-byte length and the raw bytes; then the 3 int fields
 * as 4-byte little-endian values. The caller owns the returned buffer. */
static uint8_t *session_serialize(const wf_session *sess, size_t *out_len) {
    const wf_session_data *d = &sess->data;
    const char *strs[6] = { d->access_jwt, d->refresh_jwt, d->handle,
                            d->did, d->email, d->status };

    size_t total = 0;
    for (int i = 0; i < 6; i++) {
        total += 1;
        if (strs[i]) total += 4 + strlen(strs[i]);
    }
    total += 3 * 4;

    uint8_t *buf = malloc(total ? total : 1);
    if (!buf) return NULL;

    uint8_t *p = buf;
    for (int i = 0; i < 6; i++) {
        if (strs[i]) {
            size_t len = strlen(strs[i]);
            *p++ = 1;
            put_u32(p, (uint32_t)len);
            p += 4;
            memcpy(p, strs[i], len);
            p += len;
        } else {
            *p++ = 0;
        }
    }
    put_u32(p, (uint32_t)d->email_confirmed); p += 4;
    put_u32(p, (uint32_t)d->email_auth_factor); p += 4;
    put_u32(p, (uint32_t)d->active); p += 4;

    *out_len = total;
    return buf;
}

/* Inverse of session_serialize. Returns a caller-owned session, or NULL on
 * any malformed-input / allocation error (partial allocations are freed). */
static wf_session *session_deserialize(const uint8_t *buf, size_t len) {
    wf_session *s = wf_session_new(k_session_client_url);
    if (!s) return NULL;
    wf_session_data *d = &s->data;

    const uint8_t *p = buf;
    size_t rem = len;

    for (int i = 0; i < 6; i++) {
        if (rem < 1) goto fail;
        uint8_t present = *p++;
        rem -= 1;
        if (!present) continue;
        if (rem < 4) goto fail;
        uint32_t slen = get_u32(p);
        p += 4; rem -= 4;
        if (rem < slen) goto fail;
        char *copy = malloc(slen + 1);
        if (!copy) goto fail;
        memcpy(copy, p, slen);
        copy[slen] = '\0';
        p += slen; rem -= slen;
        switch (i) {
            case 0: d->access_jwt = copy; break;
            case 1: d->refresh_jwt = copy; break;
            case 2: d->handle = copy; break;
            case 3: d->did = copy; break;
            case 4: d->email = copy; break;
            case 5: d->status = copy; break;
        }
    }

    if (rem < 3 * 4) goto fail;
    d->email_confirmed = (int)get_u32(p); p += 4;
    d->email_auth_factor = (int)get_u32(p); p += 4;
    d->active = (int)get_u32(p); p += 4;
    s->has_session = 1;
    return s;

fail:
    wf_session_free(s);
    return NULL;
}

/* Read the persisted pwhash salt, generating and storing one on first use. */
static wf_status store_read_or_create_salt(sqlite3 *db,
                                           unsigned char salt[static crypto_pwhash_SALTBYTES]) {
    static const char *get_sql =
        "SELECT v FROM store_meta WHERE k = 'pwhash_salt'";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, get_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return WF_ERR_INVALID_ARG;
    }
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const void *blob = sqlite3_column_blob(stmt, 0);
        int n = sqlite3_column_bytes(stmt, 0);
        if (n != (int)crypto_pwhash_SALTBYTES) {
            sqlite3_finalize(stmt);
            return WF_ERR_INVALID_ARG;
        }
        memcpy(salt, blob, crypto_pwhash_SALTBYTES);
        sqlite3_finalize(stmt);
        return WF_OK;
    }
    sqlite3_finalize(stmt);

    randombytes_buf(salt, crypto_pwhash_SALTBYTES);
    static const char *put_sql =
        "INSERT OR REPLACE INTO store_meta (k, v) VALUES ('pwhash_salt', ?1)";
    if (sqlite3_prepare_v2(db, put_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return WF_ERR_INVALID_ARG;
    }
    if (sqlite3_bind_blob(stmt, 1, salt, (int)crypto_pwhash_SALTBYTES,
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return WF_ERR_INVALID_ARG;
    }
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return WF_ERR_INVALID_ARG;
    return WF_OK;
}

wf_status wf_store_set_passphrase(wf_store *s, const char *passphrase) {
    if (!s || !passphrase) return WF_ERR_INVALID_ARG;

    unsigned char salt[crypto_pwhash_SALTBYTES];
    wf_status st = store_read_or_create_salt(s->db, salt);
    if (st != WF_OK) return st;

    if (crypto_pwhash(s->key, sizeof(s->key), passphrase, strlen(passphrase),
                      salt, crypto_pwhash_OPSLIMIT_INTERACTIVE,
                      crypto_pwhash_MEMLIMIT_INTERACTIVE,
                      crypto_pwhash_ALG_ARGON2ID13) != 0) {
        /* Out of memory inside the KDF; leave the store keyless. */
        s->has_key = 0;
        return WF_ERR_ALLOC;
    }

    s->has_key = 1;
    return WF_OK;
}

wf_status wf_store_save_session(wf_store *s, const wf_session *sess) {
    if (!s || !sess || !sess->has_session || !sess->data.access_jwt ||
        !sess->data.refresh_jwt || !sess->data.handle || !sess->data.did) {
        return WF_ERR_INVALID_ARG;
    }
    if (!s->has_key) return WF_ERR_INVALID_ARG;

    size_t plain_len = 0;
    uint8_t *plain = session_serialize(sess, &plain_len);
    if (!plain) return WF_ERR_ALLOC;

    unsigned char nonce[crypto_secretbox_NONCEBYTES];
    randombytes_buf(nonce, sizeof(nonce));

    size_t ct_len = plain_len + crypto_secretbox_MACBYTES;
    uint8_t *ct = malloc(ct_len ? ct_len : 1);
    if (!ct) {
        free(plain);
        return WF_ERR_ALLOC;
    }

    int rc = crypto_secretbox_easy(ct, plain, plain_len, nonce, s->key);
    sodium_memzero(plain, plain_len);
    free(plain);
    if (rc != 0) {
        free(ct);
        return WF_ERR_INVALID_ARG;
    }

    static const char *sql =
        "INSERT OR REPLACE INTO session (id, nonce, blob) VALUES (1, ?1, ?2)";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        free(ct);
        return WF_ERR_INVALID_ARG;
    }
    int brc = sqlite3_bind_blob(stmt, 1, nonce, (int)sizeof(nonce), SQLITE_TRANSIENT);
    if (brc == SQLITE_OK)
        brc = sqlite3_bind_blob(stmt, 2, ct, (int)ct_len, SQLITE_TRANSIENT);
    free(ct);
    if (brc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return WF_ERR_INVALID_ARG;
    }
    int step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (step != SQLITE_DONE) return WF_ERR_INVALID_ARG;
    return WF_OK;
}

wf_status wf_store_load_session(wf_store *s, wf_session **out) {
    if (!s || !out) return WF_ERR_INVALID_ARG;
    if (!s->has_key) return WF_ERR_INVALID_ARG;

    static const char *sql =
        "SELECT nonce, blob FROM session WHERE id = 1";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return WF_ERR_INVALID_ARG;
    }

    wf_status st = WF_ERR_NOT_FOUND;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void *nonce_blob = sqlite3_column_blob(stmt, 0);
        int nonce_len = sqlite3_column_bytes(stmt, 0);
        const void *ct_blob = sqlite3_column_blob(stmt, 1);
        int ct_len = sqlite3_column_bytes(stmt, 1);

        if (nonce_len != (int)crypto_secretbox_NONCEBYTES ||
            ct_len < (int)crypto_secretbox_MACBYTES) {
            sqlite3_finalize(stmt);
            return WF_ERR_INVALID_ARG;
        }

        size_t plain_len = (size_t)ct_len - crypto_secretbox_MACBYTES;
        uint8_t *plain = malloc(plain_len ? plain_len : 1);
        if (!plain) {
            sqlite3_finalize(stmt);
            return WF_ERR_ALLOC;
        }

        int ok = crypto_secretbox_open_easy(plain, ct_blob, (size_t)ct_len,
                                            nonce_blob, s->key);
        if (ok != 0) {
            /* Authentication failed: wrong passphrase or tampered data.
             * Never return garbage. */
            sodium_memzero(plain, plain_len);
            free(plain);
            sqlite3_finalize(stmt);
            return WF_ERR_INVALID_ARG;
        }

        wf_session *loaded = session_deserialize(plain, plain_len);
        sodium_memzero(plain, plain_len);
        free(plain);
        if (!loaded) {
            st = WF_ERR_ALLOC;
        } else {
            *out = loaded;
            st = WF_OK;
        }
    }

    sqlite3_finalize(stmt);
    return st;
}

#else /* !WOLFRAM_BUILD_STORE_CRYPTO */

/* Deep-copy the credential fields of `src` into an otherwise-idle
 * session created here. Returns NULL on allocation failure. */
static wf_session *session_copy_for_load(const wf_session *src) {
    wf_session *s = wf_session_new(k_session_client_url);
    if (!s) return NULL;

    wf_session_data *d = &s->data;
#define DUP(field)                                            \
    do {                                                      \
        if (src->data.field) {                               \
            d->field = strdup(src->data.field);              \
            if (!d->field) { wf_session_free(s); return NULL; } \
        }                                                     \
    } while (0)

    DUP(access_jwt);
    DUP(refresh_jwt);
    DUP(handle);
    DUP(did);
    DUP(email);
    DUP(status);
#undef DUP

    d->email_confirmed = src->data.email_confirmed;
    d->email_auth_factor = src->data.email_auth_factor;
    d->active = src->data.active;
    s->has_session = 1;
    return s;
}

wf_status wf_store_save_session(wf_store *s, const wf_session *sess) {
    if (!s || !sess || !sess->has_session || !sess->data.access_jwt ||
        !sess->data.refresh_jwt || !sess->data.handle || !sess->data.did) {
        return WF_ERR_INVALID_ARG;
    }

    static const char *sql =
        "INSERT OR REPLACE INTO session"
        " (id, access_jwt, refresh_jwt, handle, did, email,"
        "  email_confirmed, email_auth_factor, active, status)"
        " VALUES (1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return WF_ERR_INVALID_ARG;
    }

    int rc = SQLITE_OK;
    rc = sqlite3_bind_text(stmt, 2, sess->data.access_jwt, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 3, sess->data.refresh_jwt, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 4, sess->data.handle, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 5, sess->data.did, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = bind_text_opt(stmt, 6, sess->data.email);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 7, sess->data.email_confirmed);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 8, sess->data.email_auth_factor);
    if (rc == SQLITE_OK) rc = sqlite3_bind_int(stmt, 9, sess->data.active);
    if (rc == SQLITE_OK) rc = bind_text_opt(stmt, 10, sess->data.status);

    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return WF_ERR_INVALID_ARG;
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return WF_ERR_INVALID_ARG;
    return WF_OK;
}

wf_status wf_store_load_session(wf_store *s, wf_session **out) {
    if (!s || !out) return WF_ERR_INVALID_ARG;

    static const char *sql =
        "SELECT access_jwt, refresh_jwt, handle, did, email,"
        "       email_confirmed, email_auth_factor, active, status"
        " FROM session WHERE id = 1";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return WF_ERR_INVALID_ARG;
    }

    wf_status st = WF_ERR_NOT_FOUND;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        wf_session probe;
        memset(&probe, 0, sizeof(probe));
        probe.data.access_jwt = (char *)sqlite3_column_text(stmt, 0);
        probe.data.refresh_jwt = (char *)sqlite3_column_text(stmt, 1);
        probe.data.handle = (char *)sqlite3_column_text(stmt, 2);
        probe.data.did = (char *)sqlite3_column_text(stmt, 3);
        probe.data.email = (char *)sqlite3_column_text(stmt, 4);
        probe.data.email_confirmed = sqlite3_column_int(stmt, 5);
        probe.data.email_auth_factor = sqlite3_column_int(stmt, 6);
        probe.data.active = sqlite3_column_int(stmt, 7);
        probe.data.status = (char *)sqlite3_column_text(stmt, 8);
        probe.has_session = 1;

        wf_session *loaded = session_copy_for_load(&probe);
        if (!loaded) {
            st = WF_ERR_ALLOC;
        } else {
            *out = loaded;
            st = WF_OK;
        }
    }

    sqlite3_finalize(stmt);
    return st;
}

#endif /* WOLFRAM_BUILD_STORE_CRYPTO */

wf_status wf_store_save_mirror_head(wf_store *s, const char *did,
                                    const char *cid) {
    if (!s || !did || !cid) return WF_ERR_INVALID_ARG;

    static const char *sql =
        "INSERT OR REPLACE INTO mirror_head (did, cid) VALUES (?1, ?2)";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return WF_ERR_INVALID_ARG;
    }

    int rc = sqlite3_bind_text(stmt, 1, did, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_text(stmt, 2, cid, -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return WF_ERR_INVALID_ARG;
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return WF_ERR_INVALID_ARG;
    return WF_OK;
}

wf_status wf_store_load_mirror_head(wf_store *s, const char *did,
                                    char **out_cid) {
    if (!s || !did || !out_cid) return WF_ERR_INVALID_ARG;

    static const char *sql = "SELECT cid FROM mirror_head WHERE did = ?1";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return WF_ERR_INVALID_ARG;
    }

    wf_status st = WF_ERR_INVALID_ARG;
    if (sqlite3_bind_text(stmt, 1, did, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return st;
    }

    st = WF_ERR_NOT_FOUND;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *cid = (const char *)sqlite3_column_text(stmt, 0);
        char *dup = cid ? strdup(cid) : NULL;
        if (!dup) {
            st = WF_ERR_ALLOC;
        } else {
            *out_cid = dup;
            st = WF_OK;
        }
    }

    sqlite3_finalize(stmt);
    return st;
}

wf_status wf_store_save_mirror_block(wf_store *s, const char *did,
                                     const uint8_t *cid, size_t cid_len,
                                     const uint8_t *block, size_t block_len) {
    if (!s || !did || !cid || cid_len == 0 || !block || block_len == 0) {
        return WF_ERR_INVALID_ARG;
    }

    static const char *sql =
        "INSERT OR IGNORE INTO mirror_block (did, cid, block) VALUES (?1, ?2, ?3)";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return WF_ERR_INVALID_ARG;
    }

    int rc = sqlite3_bind_text(stmt, 1, did, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_blob(stmt, 2, cid, (int)cid_len, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_blob(stmt, 3, block, (int)block_len, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return WF_ERR_INVALID_ARG;
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return WF_ERR_INVALID_ARG;
    return WF_OK;
}

wf_status wf_store_load_mirror_block(wf_store *s, const char *did,
                                     const uint8_t *cid, size_t cid_len,
                                     uint8_t **out_block, size_t *out_block_len) {
    if (!s || !did || !cid || cid_len == 0 || !out_block || !out_block_len) {
        return WF_ERR_INVALID_ARG;
    }

    static const char *sql =
        "SELECT block FROM mirror_block WHERE did = ?1 AND cid = ?2";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(s->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return WF_ERR_INVALID_ARG;
    }

    int rc = sqlite3_bind_text(stmt, 1, did, -1, SQLITE_TRANSIENT);
    if (rc == SQLITE_OK) rc = sqlite3_bind_blob(stmt, 2, cid, (int)cid_len, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return WF_ERR_INVALID_ARG;
    }

    wf_status st = WF_ERR_NOT_FOUND;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void *blob = sqlite3_column_blob(stmt, 0);
        int len = sqlite3_column_bytes(stmt, 0);
        if (len < 0) {
            st = WF_ERR_INVALID_ARG;
        } else {
            uint8_t *buf = malloc((size_t)len > 0 ? (size_t)len : 1);
            if (!buf) {
                st = WF_ERR_ALLOC;
            } else {
                if (len > 0) memcpy(buf, blob, (size_t)len);
                *out_block = buf;
                *out_block_len = (size_t)len;
                st = WF_OK;
            }
        }
    }

    sqlite3_finalize(stmt);
    return st;
}

#endif /* WOLFRAM_BUILD_STORE */
