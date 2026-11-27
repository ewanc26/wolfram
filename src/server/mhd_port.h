/*
 * The port a libmicrohttpd daemon bound. Not part of the public API.
 */
#ifndef WOLFRAM_SERVER_MHD_PORT_H
#define WOLFRAM_SERVER_MHD_PORT_H

#include <stdint.h>

#include <microhttpd.h>

#if !defined(WOLFRAM_WIIU)
#include <netinet/in.h>
#include <sys/socket.h>
#endif

/*
 * 0 when it cannot be told. A libmicrohttpd built without getsockname support
 * (Homebrew's 1.0.11 arm64 bottle is one) answers 0 for a daemon started on an
 * ephemeral port, so ask the listening socket in that case.
 */
static inline uint16_t wf_mhd_bound_port(struct MHD_Daemon *daemon) {
    const union MHD_DaemonInfo *info =
        MHD_get_daemon_info(daemon, MHD_DAEMON_INFO_BIND_PORT);

    if (info && info->port != 0) {
        return info->port;
    }
#if !defined(WOLFRAM_WIIU) /* the shim always knows its port */
    info = MHD_get_daemon_info(daemon, MHD_DAEMON_INFO_LISTEN_FD);
    if (info) {
        struct sockaddr_storage address;
        socklen_t length = sizeof(address);

        if (getsockname(info->listen_fd, (struct sockaddr *)&address,
                        &length) == 0) {
            if (address.ss_family == AF_INET) {
                return ntohs(((struct sockaddr_in *)&address)->sin_port);
            }
            if (address.ss_family == AF_INET6) {
                return ntohs(((struct sockaddr_in6 *)&address)->sin6_port);
            }
        }
    }
#endif
    return 0;
}

#endif /* WOLFRAM_SERVER_MHD_PORT_H */
