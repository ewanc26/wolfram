/**
 * websocket_wii.c — Wii stub implementation of the WebSocket transport.
 *
 * All functions return WF_ERR_NOT_IMPLEMENTED. Replace with a real
 * lwIP + mbedTLS WebSocket implementation when building the Wii
 * integration (needed for firehose/Jetstream subscriptions).
 */

#include "wolfram/websocket.h"

#include <stdlib.h>

struct wf_websocket { int dummy; };

int wf_websocket_supported(void) {
    return 0;
}

wf_status wf_websocket_connect(const char *url, wf_websocket **out) {
    (void)url; (void)out;
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_websocket_receive(wf_websocket *socket,
                               wf_websocket_message *out) {
    (void)socket; (void)out;
    return WF_ERR_NOT_IMPLEMENTED;
}

wf_status wf_websocket_send_text(wf_websocket *socket,
                                 const char *text, size_t text_len) {
    (void)socket; (void)text; (void)text_len;
    return WF_ERR_NOT_IMPLEMENTED;
}

void wf_websocket_message_free(wf_websocket_message *message) {
    if (!message) return;
    free(message->data);
    message->data = NULL;
    message->len = 0;
}

void wf_websocket_free(wf_websocket *socket) {
    free(socket);
}
