/**
 * windows_tls.c — Connection backend for the socket transport on Windows.
 *
 * xrpc_wii.c and websocket_wii.c speak HTTP and WebSocket over the small
 * connection interface in wii_tls.h. On the Wii that interface is implemented
 * by wii_tls.c on top of libogc sockets and mbedTLS. This file is the Windows
 * implementation, and for now it implements "no network": Windows builds the
 * console subset of the library (see WOLFRAM_BUILD_WINDOWS in CMakeLists.txt),
 * and that subset does not include a Winsock transport yet.
 *
 * So no connection can be opened here. wii_tls_connect() returns NULL, which
 * the transport reports to its caller as WF_ERR_NETWORK without sending
 * anything; the calls that return a status say WF_ERR_UNSUPPORTED. Everything
 * above the connection still works: URL and header construction, the
 * wf_xrpc_set_handler() seam, response parsing.
 *
 * A Winsock transport replaces the bodies below and nothing else. The
 * interface, and the two transport files written against it, stay as they are.
 *
 * wii_tls_random() is also declared in wii_tls.h but is not part of the
 * transport: it is the RNG crypto_wii.c signs with, and it does work on
 * Windows. It lives in src/crypto/windows_random.c.
 */

#include "wii_tls.h"

wf_status wii_tls_global_init(void) {
    return WF_ERR_UNSUPPORTED;
}

wii_tls_conn *wii_tls_connect(const char *host, uint16_t port) {
    (void)host;
    (void)port;
    return NULL;
}

/* No connection can exist, so any handle passed to the calls below is invalid.
 * They reject it the way wii_tls.c rejects an invalid handle. */

long wii_tls_recv(wii_tls_conn *c, void *buf, size_t cap) {
    (void)c;
    (void)buf;
    (void)cap;
    return -WF_ERR_INVALID_ARG;
}

long wii_tls_send(wii_tls_conn *c, const void *buf, size_t len) {
    (void)c;
    (void)buf;
    (void)len;
    return -WF_ERR_INVALID_ARG;
}

void wii_tls_close(wii_tls_conn *c) {
    (void)c;
}

wf_status wii_tls_add_ca_pem(const char *pem) {
    (void)pem;
    return WF_ERR_UNSUPPORTED;
}

int wii_tls_pending(wii_tls_conn *c) {
    (void)c;
    return -1;
}
