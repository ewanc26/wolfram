/**
 * sqlite_store.c — SQLite3-backed persistence for wolfram.
 *
 * Optional module. Compiles to nothing unless WOLFRAM_BUILD_STORE is
 * defined (set by the build when CMake's WOLFRAM_BUILD_STORE option is
 * ON and the system SQLite3 library is available). See wolfram/store.h.
 */

#ifdef WOLFRAM_BUILD_STORE

#include "wolfram/store.h"

#include <sqlite3.h>

#include <stdlib.h>
#include <string.h>

struct wf_store {
    sqlite3 *db;
};

/* Placeholder URL for the XRPC client owned by a loaded session. The
 * client is never used for network I/O by the store; it only exists so
 * that wf_session_free can release a valid handle. */
static const char *k_session_client_url = "https://bsky.social";

static wf_status store_init_schema(sqlite3 *db) {
    const char *ddl =
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

    char *err = NULL;
    if (sqlite3_exec(db, ddl, NULL, NULL, &err) != SQLITE_OK) {
        if (err) sqlite3_free(err);
        return WF_ERR_INVALID_ARG;
    }
    return WF_OK;
}

wf_status wf_store_open(wf_store **out, const char *path) {
    if (!out || !path) return WF_ERR_INVALID_ARG;

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
    sqlite3_close(s->db);
    free(s);
}

/* Bind a (possibly NULL) text value to a 1-based parameter. */
static int bind_text_opt(sqlite3_stmt *stmt, int idx, const char *text) {
    if (text) return sqlite3_bind_text(stmt, idx, text, -1, SQLITE_TRANSIENT);
    return sqlite3_bind_null(stmt, idx);
}

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
