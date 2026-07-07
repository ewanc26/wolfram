#include "wolfram/validate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Offline-safe demo: load a lexicon directory and validate a record supplied
 * on the command line.
 *
 *   validate_record <lexicon-dir> <nsid> <record-json>
 *
 * If invoked with no arguments, prints usage and exits 0. */
static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    long size;
    char *buf;
    size_t got;

    if (!f) {
        perror(path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) { free(buf); return NULL; }
    buf[size] = '\0';
    *out_len = (size_t)size;
    return buf;
}

int main(int argc, char **argv) {
    wf_lexicon_registry *reg;
    wf_status status;
    wf_validate_result res;

    if (argc < 4) {
        printf("usage: %s <lexicon-dir> <nsid> <record-json>\n",
               argv[0]);
        return 0;
    }

    reg = wf_lexicon_registry_new();
    if (!reg) {
        fprintf(stderr, "failed to allocate registry\n");
        return 1;
    }

    status = wf_lexicon_registry_load_dir(reg, argv[1]);
    if (status != WF_OK) {
        fprintf(stderr, "failed to load any lexicon from %s\n", argv[1]);
        wf_lexicon_registry_free(reg);
        return 1;
    }

    res = wf_validate_record(reg, argv[2], argv[3], strlen(argv[3]));
    if (res.success) {
        printf("valid\n");
    } else {
        wf_validate_error *e;
        printf("invalid:\n");
        for (e = res.errors; e; e = e->next) {
            printf("  %s: %s\n", e->path, e->message);
        }
    }
    wf_validate_result_free(&res);
    wf_lexicon_registry_free(reg);
    return res.success ? 0 : 1;
}
