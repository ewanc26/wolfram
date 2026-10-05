/*
 * macos9_mactls_stub.c — link-time stubs for macTLS, for offline tests.
 *
 * test_macos9_transport drives the Classic Mac OS 9 transport through
 * wf_xrpc_set_handler(), which short-circuits every request before it reaches
 * a connection, so no test ever needs a real Open Transport endpoint, a real
 * handshake or a real socket. The transport library still references the
 * macTLS entry points though, and a macOS/arm64 CI runner has no way to
 * resolve them, so the test binary would not link without these definitions.
 *
 * Every stub fails or returns an inert value. That is deliberate: if a future
 * test accidentally reaches the network path, it gets an immediate,
 * obvious failure instead of a hang or a connection to something real. The
 * counters let such a test assert that no connection was ever attempted.
 */

#include "macos9_mactls_stub.h"

#include <stddef.h>

/* Call counters, so a test can assert the network path was never entered. */
unsigned long macos9_stub_new_calls;
unsigned long macos9_stub_start_calls;
unsigned long macos9_stub_pump_calls;
unsigned long macos9_stub_write_calls;
unsigned long macos9_stub_read_calls;
unsigned long macos9_stub_close_calls;
unsigned long macos9_stub_dispose_calls;

void macos9_stub_reset(void) {
    macos9_stub_new_calls = 0;
    macos9_stub_start_calls = 0;
    macos9_stub_pump_calls = 0;
    macos9_stub_write_calls = 0;
    macos9_stub_read_calls = 0;
    macos9_stub_close_calls = 0;
    macos9_stub_dispose_calls = 0;
}

unsigned long macos9_stub_total_calls(void) {
    return macos9_stub_new_calls + macos9_stub_start_calls +
           macos9_stub_pump_calls + macos9_stub_write_calls +
           macos9_stub_read_calls + macos9_stub_close_calls +
           macos9_stub_dispose_calls;
}

OSErr OSTLS_New(OSTLSConnection **out_conn, const OSTLSConfig *config) {
    (void)config;
    macos9_stub_new_calls++;
    if (out_conn != NULL) *out_conn = NULL;
    return 0;
}

OSErr OSTLS_Start(OSTLSConnection *conn) {
    (void)conn;
    macos9_stub_start_calls++;
    return 0;
}

void OSTLS_Close(OSTLSConnection *conn) {
    (void)conn;
    macos9_stub_close_calls++;
}

void OSTLS_Dispose(OSTLSConnection *conn) {
    (void)conn;
    macos9_stub_dispose_calls++;
}

OSErr OSTLS_Pump(OSTLSConnection *conn, UInt32 max_steps,
                 OSTLSEvent *out_event) {
    (void)conn;
    (void)max_steps;
    macos9_stub_pump_calls++;
    if (out_event != NULL) *out_event = kOSTLSEventNone;
    return 0;
}

OSErr OSTLS_Write(OSTLSConnection *conn, const void *buf, UInt32 len,
                  UInt32 *out_written) {
    (void)conn;
    (void)buf;
    (void)len;
    macos9_stub_write_calls++;
    if (out_written != NULL) *out_written = 0;
    return 0;
}

OSErr OSTLS_Read(OSTLSConnection *conn, void *buf, UInt32 cap,
                 UInt32 *out_read) {
    (void)conn;
    (void)buf;
    (void)cap;
    macos9_stub_read_calls++;
    if (out_read != NULL) *out_read = 0;
    return 0;
}

OSTLSState OSTLS_GetState(OSTLSConnection *conn) {
    (void)conn;
    return kOSTLSStateFailed;
}

OSErr OSTLS_GetLastError(OSTLSConnection *conn) {
    (void)conn;
    return 0;
}
