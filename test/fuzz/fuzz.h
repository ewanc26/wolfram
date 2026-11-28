/*
 * fuzz.h — shared declarations for the libFuzzer harnesses in test/fuzz/.
 *
 * Each harness is one LLVMFuzzerTestOneInput over a public decoder. A harness
 * reports a bug only through a sanitizer or wf_fuzz_check(): a decoder that
 * rejects its input is the expected outcome, not a finding.
 */

#ifndef WOLFRAM_TEST_FUZZ_H
#define WOLFRAM_TEST_FUZZ_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/* An invariant the decoder promises. abort() so libFuzzer and the replay
 * driver both record the input as a crash. */
#define wf_fuzz_check(cond)                                                    \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "fuzz invariant failed %s:%d: %s\n", __FILE__,     \
                    __LINE__, #cond);                                          \
            abort();                                                           \
        }                                                                      \
    } while (0)

#endif /* WOLFRAM_TEST_FUZZ_H */
