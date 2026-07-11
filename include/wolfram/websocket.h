/**
 * websocket.h — libcurl-backed WebSocket transport.
 *
 * libcurl performs the HTTP upgrade, framing, masking, control-frame handling,
 * and TLS.  Received payloads are reassembled across frame chunks/fragments.
 */
#ifndef WOLFRAM_WEBSOCKET_H
#define WOLFRAM_WEBSOCKET_H

#include "wolfram/xrpc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wf_websocket wf_websocket;

typedef enum wf_websocket_message_type {
    WF_WEBSOCKET_TEXT = 1,
    WF_WEBSOCKET_BINARY = 2,
} wf_websocket_message_type;

/** A complete message. `data` is heap-owned; free with wf_websocket_message_free. */
typedef struct wf_websocket_message {
    unsigned char *data;
    size_t len;
    wf_websocket_message_type type;
} wf_websocket_message;

/** True when the linked libcurl advertises ws and wss protocol support. */
int wf_websocket_supported(void);

/**
 * Connect and complete a WebSocket upgrade for an absolute ws:// or wss:// URL.
 * Returns WF_ERR_INVALID_ARG when built or linked against libcurl without its
 * WebSocket API. The returned connection is owned by the caller.
 */
wf_status wf_websocket_connect(const char *url, wf_websocket **out);

/**
 * Receive one complete message. This call is non-blocking after connection:
 * WF_ERR_WOULD_BLOCK means the socket is not readable yet and may be retried.
 * Partial message state is retained by the connection.
 */
wf_status wf_websocket_receive(wf_websocket *socket,
                               wf_websocket_message *out);

/** Send one text message; retry the same payload after WF_ERR_WOULD_BLOCK. */
wf_status wf_websocket_send_text(wf_websocket *socket,
                                  const char *text, size_t text_len);

/**
 * Send a zero-length WebSocket PING control frame for connection keepalive.
 * libcurl performs the opcode/framing and the peer's PONG is auto-handled by
 * libcurl; this call only emits the ping. Returns WF_ERR_INVALID_ARG when
 * built against libcurl without its WebSocket API, or WF_ERR_WOULD_BLOCK when
 * the socket is not writable yet (retry later).
 */
wf_status wf_websocket_send_ping(wf_websocket *socket);

/** Release message payload ownership and zero the structure. */
void wf_websocket_message_free(wf_websocket_message *message);

/** Send a normal close frame when possible, then release the connection. */
void wf_websocket_free(wf_websocket *socket);

#ifdef __cplusplus
}
#endif
#endif
