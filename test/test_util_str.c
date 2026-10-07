/*
 * test_util_str.c -- wf_str_dup and wf_str_set, the one copy of the helpers
 * every typed parser used to carry.
 */

#include "wolfram/util.h"

#include "test.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
    char *a = wf_str_dup("hello");
    WF_CHECK(a && strcmp(a, "hello") == 0);

    /* A copy, not an alias. */
    const char src[] = "same";
    char *b = wf_str_dup(src);
    WF_CHECK(b && b != src && strcmp(b, src) == 0);

    /* NULL in, NULL out; the empty string is a real (empty) copy. */
    WF_CHECK(wf_str_dup(NULL) == NULL);
    char *e = wf_str_dup("");
    WF_CHECK(e && e[0] == '\0');

    /* wf_str_set replaces what was there. */
    char *dst = NULL;
    WF_CHECK(wf_str_set(&dst, "one") == WF_OK);
    WF_CHECK(dst && strcmp(dst, "one") == 0);
    char *first = dst;
    WF_CHECK(wf_str_set(&dst, "two") == WF_OK);
    WF_CHECK(dst && strcmp(dst, "two") == 0 && dst != src);
    (void)first;

    /* A NULL source clears it. */
    WF_CHECK(wf_str_set(&dst, NULL) == WF_OK);
    WF_CHECK(dst == NULL);

    free(a);
    free(b);
    free(e);
    WF_TEST_SUMMARY();
}
