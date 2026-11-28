/*
 * replay_main.c — a corpus replay driver for compilers without libFuzzer.
 *
 * Built in place of libFuzzer when WOLFRAM_BUILD_FUZZ is ON and the compiler
 * is not Clang. Each argument is a file or a directory of files; every input
 * is passed to LLVMFuzzerTestOneInput once, under the same sanitizers the
 * fuzzing build uses. It does not mutate or explore: it replays.
 */

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "fuzz.h"

static int replay_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "replay: cannot open %s\n", path);
        return 1;
    }
    size_t cap = 4096, len = 0;
    unsigned char *buf = malloc(cap);
    if (!buf) {
        fclose(f);
        return 1;
    }
    size_t n;
    while ((n = fread(buf + len, 1, cap - len, f)) > 0) {
        len += n;
        if (len == cap) {
            unsigned char *grown = realloc(buf, cap * 2);
            if (!grown) {
                free(buf);
                fclose(f);
                return 1;
            }
            buf = grown;
            cap *= 2;
        }
    }
    fclose(f);
    /* An exact-size copy, so a read one past the input is visible to ASan
     * here as it is under libFuzzer. */
    unsigned char *exact = malloc(len ? len : 1);
    if (!exact) {
        free(buf);
        return 1;
    }
    memcpy(exact, buf, len);
    free(buf);
    LLVMFuzzerTestOneInput(exact, len);
    free(exact);
    return 0;
}

static int replay_path(const char *path, unsigned *count) {
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "replay: cannot stat %s\n", path);
        return 1;
    }
    if (!S_ISDIR(st.st_mode)) {
        (*count)++;
        return replay_file(path);
    }
    DIR *dir = opendir(path);
    if (!dir) return 1;
    int rc = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        size_t need = strlen(path) + strlen(ent->d_name) + 2;
        char *child = malloc(need);
        if (!child) {
            rc = 1;
            break;
        }
        snprintf(child, need, "%s/%s", path, ent->d_name);
        rc |= replay_path(child, count);
        free(child);
    }
    closedir(dir);
    return rc;
}

int main(int argc, char **argv) {
    unsigned count = 0;
    int rc = 0;
    for (int i = 1; i < argc; i++) rc |= replay_path(argv[i], &count);
    printf("replayed %u inputs\n", count);
    return rc;
}
