/*
 * compat/microhttpd.h — the slice of libmicrohttpd that xrpc_server.c uses.
 *
 * libmicrohttpd has no port to the Nintendo consoles: it is autotools-built
 * and probes for a POSIX surface that devkitPPC/newlib does not have. But the
 * API xrpc_server.c actually touches is 16 functions, 23 constants and 4
 * opaque types, all in that one file — so reimplementing that slice over BSD
 * sockets is a far smaller and more predictable job than porting MHD, and it
 * lets the HTTP layer sit directly on mbedTLS, which devkitPro already ships
 * for the Wii U, rather than dragging GnuTLS onto a console.
 *
 * The declarations mirror MHD's own, so xrpc_server.c compiles unmodified
 * against either. Enable with -DWOLFRAM_MHD_SHIM; see src/server/mhd_shim.c.
 */
#ifndef MHD_SHIM_H
#define MHD_SHIM_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int MHD_socket;
#define MHD_INVALID_SOCKET (-1)
#define MHD_SIZE_UNKNOWN ((uint64_t)-1LL)
#define MHD_CONTENT_READER_END_OF_STREAM ((ssize_t)-1)

enum MHD_Result { MHD_NO = 0, MHD_YES = 1 };

enum MHD_ValueKind {
    MHD_RESPONSE_HEADER_KIND = 0,
    MHD_HEADER_KIND = 1,
    MHD_COOKIE_KIND = 2,
    MHD_POSTDATA_KIND = 4,
    MHD_GET_ARGUMENT_KIND = 8,
    MHD_FOOTER_KIND = 16
};

enum MHD_ConnectionNotificationCode {
    MHD_CONNECTION_NOTIFY_STARTED = 0,
    MHD_CONNECTION_NOTIFY_CLOSED = 1
};

enum MHD_ResponseMemoryMode {
    MHD_RESPMEM_PERSISTENT = 0,
    MHD_RESPMEM_MUST_FREE = 1,
    MHD_RESPMEM_MUST_COPY = 2
};

enum MHD_FLAG {
    MHD_USE_INTERNAL_POLLING_THREAD = 8,
    MHD_ALLOW_SUSPEND_RESUME = 128,
    MHD_ALLOW_UPGRADE = 1024
};

enum MHD_OPTION {
    MHD_OPTION_END = 0,
    MHD_OPTION_NOTIFY_COMPLETED = 4,
    MHD_OPTION_EXTERNAL_LOGGER = 6,
    MHD_OPTION_NOTIFY_CONNECTION = 27
};

enum MHD_ConnectionInfoType { MHD_CONNECTION_INFO_CLIENT_ADDRESS = 4 };
enum MHD_DaemonInfoType { MHD_DAEMON_INFO_BIND_PORT = 7 };
enum MHD_UpgradeAction { MHD_UPGRADE_ACTION_CLOSE = 0 };

#define MHD_HTTP_OK 200
#define MHD_HTTP_SWITCHING_PROTOCOLS 101

struct MHD_Daemon;
struct MHD_Connection;
struct MHD_Response;
struct MHD_UpgradeResponseHandle;

union MHD_ConnectionInfo { struct sockaddr *client_addr; };
union MHD_DaemonInfo { uint16_t port; };

enum MHD_RequestTerminationCode {
    MHD_REQUEST_TERMINATED_COMPLETED_OK = 0,
    MHD_REQUEST_TERMINATED_WITH_ERROR = 1
};

typedef enum MHD_Result (*MHD_AccessHandlerCallback)(
    void *cls, struct MHD_Connection *connection, const char *url,
    const char *method, const char *version, const char *upload_data,
    size_t *upload_data_size, void **con_cls);
typedef enum MHD_Result (*MHD_KeyValueIterator)(void *cls,
    enum MHD_ValueKind kind, const char *key, const char *value);
typedef ssize_t (*MHD_ContentReaderCallback)(void *cls, uint64_t pos,
                                             char *buf, size_t max);
typedef void (*MHD_ContentReaderFreeCallback)(void *cls);
typedef void (*MHD_RequestCompletedCallback)(void *cls,
    struct MHD_Connection *connection, void **con_cls,
    enum MHD_RequestTerminationCode toe);
typedef void (*MHD_NotifyConnectionCallback)(void *cls,
    struct MHD_Connection *connection, void **socket_context,
    enum MHD_ConnectionNotificationCode toe);
typedef void (*MHD_UpgradeHandler)(void *cls,
    struct MHD_Connection *connection, void *con_cls, const char *extra_in,
    size_t extra_in_size, MHD_socket sock,
    struct MHD_UpgradeResponseHandle *urh);

struct MHD_Daemon *MHD_start_daemon(unsigned int flags, uint16_t port,
                                    void *apc, void *apc_cls,
                                    MHD_AccessHandlerCallback dh, void *dh_cls,
                                    ...);
void MHD_stop_daemon(struct MHD_Daemon *daemon);
struct MHD_Response *MHD_create_response_from_buffer(
    size_t size, void *buffer, enum MHD_ResponseMemoryMode mode);
struct MHD_Response *MHD_create_response_from_callback(
    uint64_t size, size_t block_size, MHD_ContentReaderCallback crc,
    void *crc_cls, MHD_ContentReaderFreeCallback crfc);
struct MHD_Response *MHD_create_response_for_upgrade(MHD_UpgradeHandler uh,
                                                     void *uh_cls);
enum MHD_Result MHD_add_response_header(struct MHD_Response *response,
                                        const char *header,
                                        const char *content);
enum MHD_Result MHD_queue_response(struct MHD_Connection *connection,
                                   unsigned int status_code,
                                   struct MHD_Response *response);
void MHD_destroy_response(struct MHD_Response *response);
const char *MHD_lookup_connection_value(struct MHD_Connection *connection,
                                        enum MHD_ValueKind kind,
                                        const char *key);
int MHD_get_connection_values(struct MHD_Connection *connection,
                              enum MHD_ValueKind kind,
                              MHD_KeyValueIterator iterator, void *iterator_cls);
const union MHD_ConnectionInfo *MHD_get_connection_info(
    struct MHD_Connection *connection, enum MHD_ConnectionInfoType info_type,
    ...);
const union MHD_DaemonInfo *MHD_get_daemon_info(struct MHD_Daemon *daemon,
                                                enum MHD_DaemonInfoType type,
                                                ...);
void MHD_suspend_connection(struct MHD_Connection *connection);
void MHD_resume_connection(struct MHD_Connection *connection);
enum MHD_Result MHD_upgrade_action(struct MHD_UpgradeResponseHandle *urh,
                                   enum MHD_UpgradeAction action, ...);

#ifdef __cplusplus
}
#endif
#endif
