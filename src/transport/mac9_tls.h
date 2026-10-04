#ifndef WOLFRAM_MAC9_TLS_H
#define WOLFRAM_MAC9_TLS_H

#include <stddef.h>
#include <stdint.h>
#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mac9_tls_conn mac9_tls_conn;

/*
 * Yield hook used while a synchronous Wolfram request is being driven by the
 * cooperative Mac OS 9 transport. The callback must return quickly and may
 * call WaitNextEvent() or otherwise service the application's event loop.
 */
typedef void (*wf_macos9_yield_fn)(void *userdata);

void wf_macos9_set_yield_callback(wf_macos9_yield_fn fn, void *userdata);

mac9_tls_conn *mac9_tls_connect(const char *host, uint16_t port);
long mac9_tls_send(mac9_tls_conn *conn, const void *buf, size_t len);
long mac9_tls_recv(mac9_tls_conn *conn, void *buf, size_t cap);
void mac9_tls_close(mac9_tls_conn *conn);

#ifdef __cplusplus
}
#endif

#endif
