/**
 * test_idn.c — offline unit test for libidn2 IDNA2008 / punycode handling.
 *
 * Builds only when WOLFRAM_BUILD_IDN=ON (see CMakeLists.txt). It exercises the
 * same encode/decode calls wolfram uses inside handle resolution:
 *   - idn2_to_ascii_8z  : Unicode handle -> ASCII punycode (wire form)
 *   - idn2_to_unicode_8z: ASCII punycode -> Unicode handle (display form)
 * and asserts the round trip for known AT-Proto-style internationalized
 * handles. No network is required, so it runs fully offline.
 */

#include "test.h"

#include <idn2.h>
#include <stdio.h>
#include <string.h>

/* Known IDN handles and their expected IDNA2008 ASCII (ACE) forms.
 * "münchen.example" uses U+00FC (ü); "faß.de" exercises the sharp-s (ß). */
static const struct {
    const char *unicode;   /* UTF-8 input handle */
    const char *ascii;     /* expected xn--… punycode */
} kCases[] = {
    {"m\xc3\xbcnchen.example", "xn--mnchen-3ya.example"},
    {"fa\xc3\x9f.de",          "xn--fa-hia.de"},
    {"example.com",            "example.com"},
};

static int test_encode(void) {
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        char *ascii = NULL;
        int rc = idn2_to_ascii_8z(kCases[i].unicode, &ascii, IDN2_NONTRANSITIONAL);
        if (rc != IDN2_OK) {
            fprintf(stderr, "encode failed for %s: %s\n",
                    kCases[i].unicode, idn2_strerror(rc));
            WF_CHECK(0);
            continue;
        }
        WF_CHECK(strcmp(ascii, kCases[i].ascii) == 0);
        if (strcmp(ascii, kCases[i].ascii) != 0) {
            fprintf(stderr, "encode mismatch: got %s expected %s\n",
                    ascii, kCases[i].ascii);
        }
        idn2_free(ascii);
    }
    return 0;
}

static int test_roundtrip(void) {
    for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
        char *ascii = NULL;
        int rc = idn2_to_ascii_8z(kCases[i].unicode, &ascii, IDN2_NONTRANSITIONAL);
        WF_CHECK(rc == IDN2_OK && ascii);
        if (rc != IDN2_OK || !ascii) continue;

        char *uni = NULL;
        rc = idn2_to_unicode_8z8z(ascii, &uni, IDN2_NONTRANSITIONAL);
        WF_CHECK(rc == IDN2_OK && uni);
        if (rc == IDN2_OK && uni) {
            WF_CHECK(strcmp(uni, kCases[i].unicode) == 0);
            if (strcmp(uni, kCases[i].unicode) != 0) {
                fprintf(stderr, "roundtrip mismatch: got %s expected %s\n",
                        uni, kCases[i].unicode);
            }
            idn2_free(uni);
        }
        idn2_free(ascii);
    }
    return 0;
}

int main(void) {
    test_encode();
    test_roundtrip();
    WF_TEST_SUMMARY();
}
