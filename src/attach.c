#include "wolfram/attach.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int ascii_lower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int ext_equals(const char *ext, const char *want) {
    while (*ext && *want) {
        if (ascii_lower((unsigned char)*ext) != *want) return 0;
        ext++;
        want++;
    }
    return *ext == '\0' && *want == '\0';
}

const char *wf_attach_mime(const char *path) {
    const char *dot = path ? strrchr(path, '.') : NULL;
    if (!dot) return NULL;
    if (ext_equals(dot, ".jpg") || ext_equals(dot, ".jpeg"))
        return "image/jpeg";
    if (ext_equals(dot, ".png")) return "image/png";
    return NULL;
}

/* Insertion sort over fixed-width rows: qsort cannot be given the row width as
 * context everywhere, and a folder of images is short. */
static void sort_rows(char *names, size_t name_cap, int n) {
    char *tmp = malloc(name_cap);
    if (!tmp) return;
    for (int i = 1; i < n; i++) {
        memcpy(tmp, names + (size_t)i * name_cap, name_cap);
        int j = i - 1;
        while (j >= 0 && strcmp(names + (size_t)j * name_cap, tmp) > 0) {
            memcpy(names + (size_t)(j + 1) * name_cap,
                   names + (size_t)j * name_cap, name_cap);
            j--;
        }
        memcpy(names + (size_t)(j + 1) * name_cap, tmp, name_cap);
    }
    free(tmp);
}

int wf_attach_scan_images(const char *dir, char *names, size_t name_cap,
                          int max, int *too_large) {
    if (too_large) *too_large = 0;
    if (!dir || !names || name_cap == 0 || max <= 0) return 0;
    DIR *d = opendir(dir);
    if (!d) return 0;

    int n = 0;
    struct dirent *e;
    while (n < max && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || !wf_attach_mime(e->d_name) ||
            strlen(e->d_name) >= name_cap) {
            continue;
        }
        char path[1024];
        if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >=
            (int)sizeof path) {
            continue;
        }
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
            continue;
        }
        if (st.st_size > WF_ATTACH_MAX_BYTES) {
            if (too_large) (*too_large)++;
            continue;
        }
        memcpy(names + (size_t)n * name_cap, e->d_name, strlen(e->d_name) + 1);
        n++;
    }
    closedir(d);
    sort_rows(names, name_cap, n);
    return n;
}

/* Files of `dir` that wf_attach_scan_images would list, prefixed with `prefix`
 * ("" for the top folder, "folder/" for a subfolder), appended after `n` rows.
 * Returns the new row count. */
static int scan_into(const char *dir, const char *prefix, char *names,
                     size_t name_cap, int n, int max, int *too_large) {
    DIR *d = opendir(dir);
    if (!d) return n;
    struct dirent *e;
    while (n < max && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || !wf_attach_mime(e->d_name)) continue;
        if (strlen(prefix) + strlen(e->d_name) >= name_cap) continue;
        char path[1024];
        if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >=
            (int)sizeof path)
            continue;
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0)
            continue;
        if (st.st_size > WF_ATTACH_MAX_BYTES) {
            if (too_large) (*too_large)++;
            continue;
        }
        snprintf(names + (size_t)n * name_cap, name_cap, "%s%s", prefix,
                 e->d_name);
        n++;
    }
    closedir(d);
    return n;
}

int wf_attach_scan_images_tree(const char *dir, char *names, size_t name_cap,
                               int max, int *too_large) {
    if (too_large) *too_large = 0;
    if (!dir || !names || name_cap == 0 || max <= 0) return 0;
    DIR *d = opendir(dir);
    if (!d) return 0;

    int n = scan_into(dir, "", names, name_cap, 0, max, too_large);
    /* Collect the subfolder names first: scan_into reads each one. */
    struct dirent *e;
    while (n < max && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char sub[1024];
        if (snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name) >=
            (int)sizeof sub)
            continue;
        struct stat st;
        if (stat(sub, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        char prefix[256];
        if (snprintf(prefix, sizeof prefix, "%s/", e->d_name) >=
            (int)sizeof prefix)
            continue;
        n = scan_into(sub, prefix, names, name_cap, n, max, too_large);
    }
    closedir(d);
    sort_rows(names, name_cap, n);
    return n;
}
