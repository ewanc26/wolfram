/*
 * blob_store.c — in-memory / file-backed blob store keyed by CID string.
 *
 * A small, self-contained store letting wolfram persist and serve blobs as a
 * PDS would. See blob_store.h for ownership and mode semantics.
 */

#include "wolfram/blob_store.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <dirent.h>

typedef struct wf_blob_node {
    char                *cid;    /* owned CID string (key) */
    char                *mime;   /* owned MIME type */
    unsigned char       *data;   /* owned blob bytes */
    size_t               len;
    struct wf_blob_node *next;
} wf_blob_node;

struct wf_blob_store {
    bool          file_backed;
    char         *dir;        /* owned base directory (file-backed only) */
    wf_blob_node *head;      /* in-memory index; source of truth */
};

/* Join dir + name into a heap buffer (caller frees). Tolerates a trailing
 * slash on dir. Returns NULL on allocation failure. */
static char *wf_blob_path(const char *dir, const char *name) {
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    int   sep = (dlen > 0 && dir[dlen - 1] == '/') ? 0 : 1;
    char *out = (char *)malloc(dlen + nlen + 1 + sep);
    if (!out) return NULL;
    memcpy(out, dir, dlen);
    if (sep) out[dlen] = '/';
    memcpy(out + dlen + sep, name, nlen);
    out[dlen + sep + nlen] = '\0';
    return out;
}

/* Append a node to the store's in-memory index (takes ownership of args). */
static wf_status wf_blob_node_push(wf_blob_store *store, char *cid,
                                    char *mime, unsigned char *data,
                                    size_t len) {
    wf_blob_node *node = (wf_blob_node *)calloc(1, sizeof(*node));
    if (!node) {
        free(cid); free(mime); free(data);
        return WF_ERR_ALLOC;
    }
    node->cid = cid;
    node->mime = mime;
    node->data = data;
    node->len = len;
    node->next = store->head;
    store->head = node;
    return WF_OK;
}

static void wf_blob_node_free(wf_blob_node *node) {
    if (!node) return;
    free(node->cid);
    free(node->mime);
    free(node->data);
    free(node);
}

/* Read an entire file into a heap buffer (caller frees). Returns WF_OK and sets
 * *out/*out_len; WF_ERR_NOT_FOUND if unopenable; WF_ERR_ALLOC on OOM. */
static wf_status wf_blob_read_file(const char *path, unsigned char **out,
                                    size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return WF_ERR_NOT_FOUND;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return WF_ERR_INTERNAL; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return WF_ERR_INTERNAL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return WF_ERR_INTERNAL; }
    unsigned char *buf = (unsigned char *)malloc((size_t)size ? (size_t)size : 1);
    if (!buf) { fclose(f); return WF_ERR_ALLOC; }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) { free(buf); return WF_ERR_INTERNAL; }
    *out = buf;
    *out_len = (size_t)size;
    return WF_OK;
}

static bool wf_blob_ends_with(const char *s, const char *suffix) {
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && strcmp(s + ls - lx, suffix) == 0;
}

wf_blob_store *wf_blob_store_new(const char *path) {
    wf_blob_store *store = (wf_blob_store *)calloc(1, sizeof(*store));
    if (!store) return NULL;

    if (path && path[0] != '\0') {
        store->file_backed = true;
        store->dir = strdup(path);
        if (!store->dir) {
            free(store);
            return NULL;
        }

        /* Load any pre-existing blobs from disk into the in-memory index. */
        DIR *d = opendir(store->dir);
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                const char *name = ent->d_name;
                if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
                if (wf_blob_ends_with(name, ".mime")) continue;

                char *datap = wf_blob_path(store->dir, name);
                char *mimep = wf_blob_path(store->dir, name);
                if (!datap || !mimep) {
                    free(datap); free(mimep);
                    continue;
                }
                /* Append ".mime" to the mime sidecar path. */
                size_t plen = strlen(mimep);
                char *mp = (char *)realloc(mimep, plen + 6);
                if (!mp) { free(datap); free(mimep); continue; }
                mimep = mp;
                memcpy(mimep + plen, ".mime", 6);

                unsigned char *data = NULL;
                size_t dlen = 0;
                char *mime = NULL;
                size_t mlen = 0;
                unsigned char *mraw = NULL;
                size_t mraw_len = 0;

                if (wf_blob_read_file(datap, &data, &dlen) != WF_OK) {
                    free(datap); free(mimep); continue;
                }
                if (wf_blob_read_file(mimep, &mraw, &mraw_len) != WF_OK) {
                    free(datap); free(mimep); free(data); continue;
                }
                mime = (char *)malloc(mraw_len + 1);
                if (!mime) {
                    free(datap); free(mimep); free(data); free(mraw); continue;
                }
                memcpy(mime, mraw, mraw_len);
                mime[mraw_len] = '\0';
                mlen = mraw_len;
                (void)mlen;

                char *cid = strdup(name);
                if (!cid) {
                    free(datap); free(mimep); free(data); free(mraw);
                    free(mime); continue;
                }
                /* Best-effort index load; ignore failures. */
                (void)wf_blob_node_push(store, cid, mime, data, dlen);

                free(datap); free(mimep); free(mraw);
            }
            closedir(d);
        }
    }

    return store;
}

void wf_blob_store_free(wf_blob_store *store) {
    if (!store) return;
    wf_blob_node *n = store->head;
    while (n) {
        wf_blob_node *next = n->next;
        wf_blob_node_free(n);
        n = next;
    }
    free(store->dir);
    free(store);
}

wf_status wf_blob_store_put(wf_blob_store *store, const char *cid,
                            const char *mime_type,
                            const unsigned char *data, size_t len) {
    if (!store || !cid || !*cid || !mime_type || !data) {
        return WF_ERR_INVALID_ARG;
    }

    unsigned char *data_copy = (unsigned char *)malloc(len ? len : 1);
    if (!data_copy) return WF_ERR_ALLOC;
    memcpy(data_copy, data, len);
    char *cid_copy = strdup(cid);
    char *mime_copy = strdup(mime_type);
    if (!cid_copy || !mime_copy) {
        free(data_copy); free(cid_copy); free(mime_copy);
        return WF_ERR_ALLOC;
    }

    /* Replace an existing entry with the same CID. */
    for (wf_blob_node *n = store->head; n; n = n->next) {
        if (strcmp(n->cid, cid) == 0) {
            free(n->mime);
            free(n->data);
            n->mime = mime_copy;
            n->data = data_copy;
            n->len = len;
            free(cid_copy);
            goto persist;
        }
    }

    if (wf_blob_node_push(store, cid_copy, mime_copy, data_copy, len) != WF_OK) {
        return WF_ERR_ALLOC;
    }

persist:
    if (store->file_backed) {
        char *datap = wf_blob_path(store->dir, cid);
        char *mimep = wf_blob_path(store->dir, cid);
        wf_status st = WF_OK;
        if (!datap || !mimep) {
            free(datap); free(mimep);
            return WF_ERR_INTERNAL;
        }
        size_t plen = strlen(mimep);
        char *mp = (char *)realloc(mimep, plen + 6);
        if (!mp) { free(datap); free(mimep); return WF_ERR_INTERNAL; }
        mimep = mp;
        memcpy(mimep + plen, ".mime", 6);

        FILE *f = fopen(datap, "wb");
        if (!f) { st = WF_ERR_INTERNAL; }
        else {
            if (fwrite(data, 1, len, f) != len) st = WF_ERR_INTERNAL;
            fclose(f);
        }
        if (st == WF_OK) {
            FILE *mf = fopen(mimep, "wb");
            if (!mf) { st = WF_ERR_INTERNAL; }
            else {
                if (fwrite(mime_type, 1, strlen(mime_type), mf) != strlen(mime_type))
                    st = WF_ERR_INTERNAL;
                fclose(mf);
            }
        }
        free(datap); free(mimep);
        if (st != WF_OK) return st;
    }

    return WF_OK;
}

wf_status wf_blob_store_get(wf_blob_store *store, const char *cid,
                            unsigned char **out_data, size_t *out_len,
                            char **out_mime) {
    if (!store || !cid || !out_data || !out_len || !out_mime) {
        return WF_ERR_INVALID_ARG;
    }
    *out_data = NULL;
    *out_len = 0;
    *out_mime = NULL;

    for (wf_blob_node *n = store->head; n; n = n->next) {
        if (strcmp(n->cid, cid) == 0) {
            unsigned char *data = (unsigned char *)malloc(n->len ? n->len : 1);
            char *mime = strdup(n->mime);
            if (!data || !mime) {
                free(data); free(mime);
                return WF_ERR_ALLOC;
            }
            memcpy(data, n->data, n->len);
            *out_data = data;
            *out_len = n->len;
            *out_mime = mime;
            return WF_OK;
        }
    }

    return WF_ERR_NOT_FOUND;
}

wf_status wf_blob_store_exists(wf_blob_store *store, const char *cid) {
    if (!store || !cid) return WF_ERR_INVALID_ARG;
    for (wf_blob_node *n = store->head; n; n = n->next) {
        if (strcmp(n->cid, cid) == 0) return WF_OK;
    }
    return WF_ERR_NOT_FOUND;
}

wf_status wf_blob_store_list(wf_blob_store *store, char ***out_cids,
                             size_t *out_count) {
    if (!store || !out_cids || !out_count) return WF_ERR_INVALID_ARG;
    *out_cids = NULL;
    *out_count = 0;

    size_t count = 0;
    for (wf_blob_node *n = store->head; n; n = n->next) count++;
    if (count == 0) return WF_OK;

    char **cids = (char **)calloc(count, sizeof(*cids));
    if (!cids) return WF_ERR_ALLOC;
    size_t i = 0;
    wf_status status = WF_OK;
    for (wf_blob_node *n = store->head; n && status == WF_OK; n = n->next) {
        cids[i] = strdup(n->cid);
        if (!cids[i]) status = WF_ERR_ALLOC;
        else i++;
    }
    if (status != WF_OK) {
        for (size_t j = 0; j < i; j++) free(cids[j]);
        free(cids);
        return status;
    }
    *out_cids = cids;
    *out_count = count;
    return WF_OK;
}

void wf_blob_store_list_free(char **cids, size_t count) {
    if (!cids) return;
    for (size_t i = 0; i < count; i++) free(cids[i]);
    free(cids);
}
