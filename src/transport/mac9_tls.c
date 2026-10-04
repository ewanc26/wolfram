#include "mac9_tls.h"
#include "ostls_async.h"

#include <stdlib.h>
#include <string.h>

struct mac9_tls_conn {
    OSTLSConnection *tls;
    int started;
};

static wf_macos9_yield_fn g_yield;
static void *g_yield_userdata;

void wf_macos9_set_yield_callback(wf_macos9_yield_fn fn, void *userdata)
{
    g_yield = fn;
    g_yield_userdata = userdata;
}

static void mac9_yield(void)
{
    if (g_yield != NULL)
        g_yield(g_yield_userdata);
}

mac9_tls_conn *mac9_tls_connect(const char *host, uint16_t port)
{
    OSTLSConfig config;
    OSTLSConnection *tls;
    mac9_tls_conn *conn;
    OSErr err;

    if (host == NULL || host[0] == '\0')
        return NULL;

    memset(&config, 0, sizeof(config));
    config.host = host;
    config.port = (UInt16)port;
    config.server_name = host;

    tls = NULL;
    err = OSTLS_New(&tls, &config);
    if (err != kOSTLSAsync_OK || tls == NULL)
        return NULL;

    err = OSTLS_Start(tls);
    if (err != kOSTLSAsync_OK) {
        OSTLS_Dispose(tls);
        return NULL;
    }

    conn = (mac9_tls_conn *)calloc(1, sizeof(*conn));
    if (conn == NULL) {
        OSTLS_Close(tls);
        OSTLS_Dispose(tls);
        return NULL;
    }
    conn->tls = tls;
    conn->started = 1;

    for (;;) {
        OSTLSEvent event;
        OSTLSState state;

        event = kOSTLSEventNone;
        err = OSTLS_Pump(tls, 8, &event);
        if (err != kOSTLSAsync_OK) {
            OSTLS_Dispose(tls);
            free(conn);
            return NULL;
        }

        state = OSTLS_GetState(tls);
        if (state == kOSTLSStateOpen)
            return conn;
        if (state == kOSTLSStateFailed ||
            state == kOSTLSStateClosed) {
            OSTLS_Dispose(tls);
            free(conn);
            return NULL;
        }

        mac9_yield();
    }
}

long mac9_tls_send(mac9_tls_conn *conn, const void *buf, size_t len)
{
    const unsigned char *p;
    size_t sent;

    if (conn == NULL || conn->tls == NULL || buf == NULL)
        return -(long)WF_ERR_INVALID_ARG;

    p = (const unsigned char *)buf;
    sent = 0;

    while (sent < len) {
        UInt32 written;
        OSErr err;
        OSTLSEvent event;
        OSTLSState state;

        written = 0;
        err = OSTLS_Write(conn->tls, p + sent, (UInt32)(len - sent), &written);
        if (err != kOSTLSAsync_OK &&
            err != kOSTLSAsync_WrongState)
            return -(long)WF_ERR_NETWORK;

        sent += (size_t)written;
        if (sent == len)
            return (long)sent;

        event = kOSTLSEventNone;
        err = OSTLS_Pump(conn->tls, 8, &event);
        if (err != kOSTLSAsync_OK)
            return -(long)WF_ERR_NETWORK;

        state = OSTLS_GetState(conn->tls);
        if (state == kOSTLSStateFailed || state == kOSTLSStateClosed)
            return -(long)WF_ERR_NETWORK;

        mac9_yield();
    }

    return (long)sent;
}

long mac9_tls_recv(mac9_tls_conn *conn, void *buf, size_t cap)
{
    if (conn == NULL || conn->tls == NULL || buf == NULL || cap == 0)
        return -(long)WF_ERR_INVALID_ARG;

    for (;;) {
        UInt32 read_count;
        OSErr err;
        OSTLSEvent event;
        OSTLSState state;

        read_count = 0;
        err = OSTLS_Read(conn->tls, buf, (UInt32)cap, &read_count);
        if (err != kOSTLSAsync_OK &&
            err != kOSTLSAsync_WrongState)
            return -(long)WF_ERR_NETWORK;
        if (read_count != 0)
            return (long)read_count;

        state = OSTLS_GetState(conn->tls);
        if (state == kOSTLSStateClosed)
            return 0;
        if (state == kOSTLSStateFailed)
            return -(long)WF_ERR_NETWORK;

        event = kOSTLSEventNone;
        err = OSTLS_Pump(conn->tls, 8, &event);
        if (err != kOSTLSAsync_OK)
            return -(long)WF_ERR_NETWORK;

        state = OSTLS_GetState(conn->tls);
        if (state == kOSTLSStateClosed) {
            read_count = 0;
            err = OSTLS_Read(conn->tls, buf, (UInt32)cap, &read_count);
            if (err != kOSTLSAsync_OK)
                return -(long)WF_ERR_NETWORK;
            if (read_count != 0)
                return (long)read_count;
            return 0;
        }
        if (state == kOSTLSStateFailed)
            return -(long)WF_ERR_NETWORK;

        mac9_yield();
    }
}

void mac9_tls_close(mac9_tls_conn *conn)
{
    if (conn == NULL)
        return;
    if (conn->tls != NULL) {
        OSTLS_Close(conn->tls);
        OSTLS_Dispose(conn->tls);
    }
    free(conn);
}
