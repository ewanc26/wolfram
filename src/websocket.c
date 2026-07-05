#include "wolfram/websocket.h"
#include "wolfram/version.h"

#include <curl/curl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WF_WEBSOCKET_MAX_MESSAGE (16u * 1024u * 1024u)

struct wf_websocket {
#if LIBCURL_VERSION_NUM >= 0x075600
    CURL *curl;
    unsigned char *pending;
    size_t pending_len;
    size_t pending_cap;
    wf_websocket_message_type pending_type;
    unsigned char *outgoing;
    size_t outgoing_len;
    size_t outgoing_offset;
#else
    int unused;
#endif
};

wf_status wf_websocket_send_text(wf_websocket *socket,
                                 const char *text, size_t text_len) {
    if (!socket || (!text && text_len) || text_len > WF_WEBSOCKET_MAX_MESSAGE)
        return WF_ERR_INVALID_ARG;
#if LIBCURL_VERSION_NUM >= 0x075600
    if (socket->outgoing) {
        if (socket->outgoing_len != text_len ||
            (text_len && memcmp(socket->outgoing, text, text_len) != 0))
            return WF_ERR_INVALID_ARG;
    } else {
        socket->outgoing = malloc(text_len ? text_len : 1);
        if (!socket->outgoing) return WF_ERR_ALLOC;
        if (text_len) memcpy(socket->outgoing, text, text_len);
        socket->outgoing_len = text_len;
    }
    do {
        size_t sent = 0;
        CURLcode result = curl_ws_send(
            socket->curl, socket->outgoing + socket->outgoing_offset,
            socket->outgoing_len - socket->outgoing_offset, &sent, 0,
            CURLWS_TEXT);
        socket->outgoing_offset += sent;
        if (result == CURLE_AGAIN) return WF_ERR_WOULD_BLOCK;
        if (result != CURLE_OK) {
            free(socket->outgoing); socket->outgoing = NULL;
            socket->outgoing_len = socket->outgoing_offset = 0;
            return WF_ERR_NETWORK;
        }
        if (!sent && socket->outgoing_offset < socket->outgoing_len)
            return WF_ERR_WOULD_BLOCK;
    } while (socket->outgoing_offset < socket->outgoing_len);
    free(socket->outgoing); socket->outgoing = NULL;
    socket->outgoing_len = socket->outgoing_offset = 0;
    return WF_OK;
#else
    (void)text; (void)text_len;
    return WF_ERR_INVALID_ARG;
#endif
}

static int wf_websocket_protocol_supported(const char *wanted) {
#if LIBCURL_VERSION_NUM < 0x075600
    (void)wanted;
#endif
#if LIBCURL_VERSION_NUM >= 0x075600
    const curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
    const char * const *protocol;
    if (!info || !info->protocols) return 0;
    for (protocol = info->protocols; *protocol; ++protocol) {
        if (strcmp(*protocol, wanted) == 0) return 1;
    }
#endif
    return 0;
}

int wf_websocket_supported(void) {
    return wf_websocket_protocol_supported("ws") ||
           wf_websocket_protocol_supported("wss");
}

wf_status wf_websocket_connect(const char *url, wf_websocket **out) {
    if (!url || !out ||
        (strncmp(url, "ws://", 5) != 0 && strncmp(url, "wss://", 6) != 0)) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
#if LIBCURL_VERSION_NUM >= 0x075600
    const char *protocol = strncmp(url, "wss://", 6) == 0 ? "wss" : "ws";
    if (!wf_websocket_protocol_supported(protocol)) return WF_ERR_INVALID_ARG;

    wf_websocket *socket = calloc(1, sizeof(*socket));
    if (!socket) return WF_ERR_ALLOC;
    socket->curl = curl_easy_init();
    if (!socket->curl) {
        free(socket);
        return WF_ERR_ALLOC;
    }
    curl_easy_setopt(socket->curl, CURLOPT_URL, url);
    curl_easy_setopt(socket->curl, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(socket->curl, CURLOPT_USERAGENT,
                     "wolfram/" WOLFRAM_VERSION_STRING);
    if (curl_easy_perform(socket->curl) != CURLE_OK) {
        curl_easy_cleanup(socket->curl);
        free(socket);
        return WF_ERR_NETWORK;
    }
    *out = socket;
    return WF_OK;
#else
    (void)url;
    return WF_ERR_INVALID_ARG;
#endif
}

#if LIBCURL_VERSION_NUM >= 0x075600
static wf_status wf_websocket_append(wf_websocket *socket,
                                     const unsigned char *data, size_t len) {
    if (len > WF_WEBSOCKET_MAX_MESSAGE - socket->pending_len) return WF_ERR_PARSE;
    size_t needed = socket->pending_len + len + 1;
    if (needed > socket->pending_cap) {
        size_t cap = socket->pending_cap ? socket->pending_cap : 4096;
        while (cap < needed) cap *= 2;
        unsigned char *grown = realloc(socket->pending, cap);
        if (!grown) return WF_ERR_ALLOC;
        socket->pending = grown;
        socket->pending_cap = cap;
    }
    memcpy(socket->pending + socket->pending_len, data, len);
    socket->pending_len += len;
    socket->pending[socket->pending_len] = '\0';
    return WF_OK;
}

static void wf_websocket_discard_pending(wf_websocket *socket) {
    free(socket->pending);
    socket->pending = NULL;
    socket->pending_len = 0;
    socket->pending_cap = 0;
    socket->pending_type = 0;
}
#endif

wf_status wf_websocket_receive(wf_websocket *socket,
                               wf_websocket_message *out) {
    if (!socket || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
#if LIBCURL_VERSION_NUM >= 0x075600
    unsigned char chunk[8192];
    for (;;) {
        size_t received = 0;
        const struct curl_ws_frame *meta = NULL;
        CURLcode result = curl_ws_recv(socket->curl, chunk, sizeof(chunk),
                                      &received, &meta);
        if (result == CURLE_AGAIN) return WF_ERR_WOULD_BLOCK;
        if (result != CURLE_OK || !meta || (meta->flags & CURLWS_CLOSE)) {
            wf_websocket_discard_pending(socket);
            return WF_ERR_NETWORK;
        }
        if (meta->flags & (CURLWS_PING | CURLWS_PONG)) continue;
        if (!(meta->flags & (CURLWS_TEXT | CURLWS_BINARY))) {
            wf_websocket_discard_pending(socket);
            return WF_ERR_PARSE;
        }
        wf_websocket_message_type type = (meta->flags & CURLWS_TEXT)
                                             ? WF_WEBSOCKET_TEXT
                                             : WF_WEBSOCKET_BINARY;
        if (socket->pending_type && socket->pending_type != type) {
            wf_websocket_discard_pending(socket);
            return WF_ERR_PARSE;
        }
        socket->pending_type = type;
        wf_status status = wf_websocket_append(socket, chunk, received);
        if (status != WF_OK) {
            wf_websocket_discard_pending(socket);
            return status;
        }
        if (meta->bytesleft != 0 || (meta->flags & CURLWS_CONT)) continue;

        out->data = socket->pending;
        out->len = socket->pending_len;
        out->type = socket->pending_type;
        socket->pending = NULL;
        socket->pending_len = 0;
        socket->pending_cap = 0;
        socket->pending_type = 0;
        return WF_OK;
    }
#else
    return WF_ERR_INVALID_ARG;
#endif
}

void wf_websocket_message_free(wf_websocket_message *message) {
    if (!message) return;
    free(message->data);
    memset(message, 0, sizeof(*message));
}

void wf_websocket_free(wf_websocket *socket) {
    if (!socket) return;
#if LIBCURL_VERSION_NUM >= 0x075600
    size_t sent = 0;
    (void)curl_ws_send(socket->curl, "", 0, &sent, 0, CURLWS_CLOSE);
    curl_easy_cleanup(socket->curl);
    wf_websocket_discard_pending(socket);
    free(socket->outgoing);
#endif
    free(socket);
}
