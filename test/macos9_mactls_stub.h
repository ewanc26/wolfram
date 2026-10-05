/*
 * macos9_mactls_stub.h — counters for the macTLS link-time stubs.
 *
 * See macos9_mactls_stub.c. Tests use these to prove that driving the
 * Classic Mac OS 9 transport through the handler seam really does stay off
 * the network path, so a passing test cannot be passing by accident.
 */

#ifndef WOLFRAM_TEST_MACOS9_MACTLS_STUB_H
#define WOLFRAM_TEST_MACOS9_MACTLS_STUB_H

/* Declares the OSTLS types and constants the stubs implement. */
#include "ostls_async.h"

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned long macos9_stub_new_calls;
extern unsigned long macos9_stub_start_calls;
extern unsigned long macos9_stub_pump_calls;
extern unsigned long macos9_stub_write_calls;
extern unsigned long macos9_stub_read_calls;
extern unsigned long macos9_stub_close_calls;
extern unsigned long macos9_stub_dispose_calls;

/** Reset every counter to zero. */
void macos9_stub_reset(void);

/** Sum of every counter: the total number of macTLS entry points called. */
unsigned long macos9_stub_total_calls(void);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_TEST_MACOS9_MACTLS_STUB_H */
