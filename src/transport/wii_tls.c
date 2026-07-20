/**
 * wii_tls.c — TLS wrapper for the Nintendo Wii transport.
 *
 * Implements wii_tls.h on top of lwIP (libogc sockets + DNS) and mbedTLS.
 * The Wii has no application-facing cryptographic RNG, so this file requires
 * an installation-unique seed supplied by wf_wii_set_entropy_seed(). It must
 * come from a cryptographically secure generator on another machine. Timing,
 * addresses, the console MAC, and rand() are deliberately not accepted as
 * entropy.
 *   - clock: mbedtls_platform_time() backed by gettimeofday() so X.509
 *     NotBefore/NotAfter checks are meaningful.
 */

#include "wii_tls.h"
#include "wii_ca_bundle.h"
#include "wolfram/wii.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

#include <network.h>

#include <mbedtls/platform.h>
#include <mbedtls/platform_time.h>
#include <mbedtls/error.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>

/* ── Global TLS state ───────────────────────────────────────────────── */

static int               g_init       = 0;
static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context  g_ctr;
static mbedtls_x509_crt    g_ca;       /* trusted root store */
static mbedtls_ssl_config  g_conf;     /* shared client config */

/* ── Provisioned entropy source ─────────────────────────────────────── */

#define WII_ENTROPY_SEED_SIZE 64
static unsigned char g_seed[WII_ENTROPY_SEED_SIZE];
static int g_seed_ready;
static int g_rotation_pending;

wf_status wf_wii_set_entropy_seed(const unsigned char *seed, size_t seed_len) {
    if (!seed || seed_len != WII_ENTROPY_SEED_SIZE || g_init)
        return WF_ERR_INVALID_ARG;
    memcpy(g_seed, seed, sizeof(g_seed));
    g_seed_ready = 1;
    return WF_OK;
}

wf_status wf_wii_rotate_entropy_seed(unsigned char *out, size_t out_len) {
    if (!out || out_len != WII_ENTROPY_SEED_SIZE || g_rotation_pending)
        return WF_ERR_INVALID_ARG;
    wf_status status = wii_tls_global_init();
    if (status != WF_OK) return status;
    if (mbedtls_ctr_drbg_random(&g_ctr, out, out_len) != 0)
        return WF_ERR_CRYPTO;
    g_rotation_pending = 1;
    return WF_OK;
}

wf_status wf_wii_commit_entropy_rotation(void) {
    if (!g_init || !g_rotation_pending) return WF_ERR_INVALID_ARG;
    g_rotation_pending = 0;
    return WF_OK;
}

static int wii_entropy_poll(void *p, unsigned char *out, size_t len,
                            size_t *olen) {
    (void)p;
    if (!g_seed_ready || len < WII_ENTROPY_SEED_SIZE) {
        *olen = 0;
        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    memcpy(out, g_seed, WII_ENTROPY_SEED_SIZE);
    memset(g_seed, 0, sizeof(g_seed));
    g_seed_ready = 0;
    *olen = WII_ENTROPY_SEED_SIZE;
    return 0;
}

/* mbedTLS clock source (MBEDTLS_PLATFORM_TIME_ALT): seconds since epoch.
 * The Wii RTC is typically set; gettimeofday() reflects it. */
mbedtls_time_t mbedtls_platform_time(mbedtls_time_t *t) {
    struct timeval tv;
    mbedtls_time_t s = 0;
    if (gettimeofday(&tv, NULL) == 0) s = (mbedtls_time_t)tv.tv_sec;
    if (t) *t = s;
    return s;
}

/* ── lwIP BIO callbacks for mbedTLS ───────────────────────────────────── */

struct wii_tls_conn {
    int fd;
    mbedtls_ssl_context ssl;
};

static int wii_tls_net_send(void *ctx, const unsigned char *buf, size_t len) {
    wii_tls_conn *c = (wii_tls_conn *)ctx;
    int ret = (int)net_send(c->fd, (const void *)buf, (int)len, 0);
    if (ret >= 0) return ret;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}

static int wii_tls_net_recv(void *ctx, unsigned char *buf, size_t len) {
    wii_tls_conn *c = (wii_tls_conn *)ctx;
    int ret = (int)net_recv(c->fd, (void *)buf, (int)len, 0);
    if (ret >= 0) return ret;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

/* ── BIO helpers ─────────────────────────────────────────────────────── */

wf_status wii_tls_global_init(void) {
    if (g_init) return WF_OK;
    if (!g_seed_ready) return WF_ERR_CRYPTO;

    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_ctr);
    mbedtls_x509_crt_init(&g_ca);
    mbedtls_ssl_config_init(&g_conf);

    /* Seed the DRBG from our Wii entropy source. */
    int rc = mbedtls_entropy_add_source(&g_entropy, wii_entropy_poll, NULL,
                                        64, MBEDTLS_ENTROPY_SOURCE_STRONG);
    if (rc != 0) return WF_ERR_CRYPTO;

    static const unsigned char pers[] = "wolfram-wii-xrpc";
    rc = mbedtls_ctr_drbg_seed(&g_ctr, mbedtls_entropy_func, &g_entropy,
                               pers, sizeof(pers) - 1);
    if (rc != 0) return WF_ERR_CRYPTO;

    /* Parse the bundled root CAs into the trust store. */
    rc = mbedtls_x509_crt_parse(&g_ca,
                                (const unsigned char *)WII_TLS_CA_BUNDLE,
                                sizeof(WII_TLS_CA_BUNDLE));
    /* A positive return is the number of certificates mbedTLS could not
     * parse; successfully parsed roots remain in the chain and are usable.
     * Only a negative return means the trust store could not be constructed.
     * Passing the terminating NUL is required for PEM parsing. */
    if (rc < 0 || g_ca.raw.p == NULL) return WF_ERR_CRYPTO;

    /* Default verifying TLS 1.2 client profile. */
    rc = mbedtls_ssl_config_defaults(&g_conf, MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT);
    if (rc != 0) return WF_ERR_CRYPTO;

    mbedtls_ssl_conf_rng(&g_conf, mbedtls_ctr_drbg_random, &g_ctr);
    mbedtls_ssl_conf_ca_chain(&g_conf, &g_ca, NULL);
    mbedtls_ssl_conf_authmode(&g_conf, MBEDTLS_SSL_VERIFY_REQUIRED);

    g_init = 1;
    return WF_OK;
}

wf_status wii_tls_add_ca_pem(const char *pem) {
    if (!g_init) {
        wf_status rc = wii_tls_global_init();
        if (rc != WF_OK) return rc;
    }
    if (!pem) return WF_ERR_INVALID_ARG;
    int rc = mbedtls_x509_crt_parse(&g_ca, (const unsigned char *)pem,
                                    strlen(pem) + 1);
    if (rc != 0) return WF_ERR_PARSE;
    return WF_OK;
}

wii_tls_conn *wii_tls_connect(const char *host, uint16_t port) {
    if (!host || g_rotation_pending) return NULL;
    if (!g_init) {
        if (wii_tls_global_init() != WF_OK) return NULL;
    }

    wii_tls_conn *c = (wii_tls_conn *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->fd = -1;

    /* DNS resolution via lwIP. */
    struct hostent *he = net_gethostbyname(host);
    if (!he || he->h_addrtype != AF_INET || he->h_addr_list[0] == NULL) {
        free(c);
        return NULL;
    }

    c->fd = net_socket(AF_INET, SOCK_STREAM, 0);
    if (c->fd < 0) { free(c); return NULL; }

    /* IOS networking can otherwise block forever on a weak or lost WiFi
     * connection. These timeouts bound each socket operation; the TLS loops
     * below also cap consecutive WANT_READ/WANT_WRITE retries. */
    struct timeval timeout = {10, 0};
    (void)net_setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO,
                         &timeout, sizeof(timeout));
    (void)net_setsockopt(c->fd, SOL_SOCKET, SO_SNDTIMEO,
                         &timeout, sizeof(timeout));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);

    if (net_connect(c->fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        net_close(c->fd);
        free(c);
        return NULL;
    }

    mbedtls_ssl_init(&c->ssl);
    if (mbedtls_ssl_setup(&c->ssl, &g_conf) != 0) {
        net_close(c->fd);
        mbedtls_ssl_free(&c->ssl);
        free(c);
        return NULL;
    }

    /* SNI + hostname for certificate verification. */
    if (mbedtls_ssl_set_hostname(&c->ssl, host) != 0) {
        net_close(c->fd);
        mbedtls_ssl_free(&c->ssl);
        free(c);
        return NULL;
    }

    mbedtls_ssl_set_bio(&c->ssl, c, wii_tls_net_send, wii_tls_net_recv, NULL);

    int retries = 0;
    int rc = mbedtls_ssl_handshake(&c->ssl);
    while ((rc == MBEDTLS_ERR_SSL_WANT_READ ||
            rc == MBEDTLS_ERR_SSL_WANT_WRITE) && retries++ < 2) {
        rc = mbedtls_ssl_handshake(&c->ssl);
    }
    if (rc != 0) {
        net_close(c->fd);
        mbedtls_ssl_free(&c->ssl);
        free(c);
        return NULL;
    }

    /* Enforce certificate chain trust. */
    if (mbedtls_ssl_get_verify_result(&c->ssl) != 0) {
        net_close(c->fd);
        mbedtls_ssl_free(&c->ssl);
        free(c);
        return NULL;
    }

    return c;
}

long wii_tls_send(wii_tls_conn *c, const void *buf, size_t len) {
    if (!c || c->fd < 0) return -WF_ERR_INVALID_ARG;
    size_t sent = 0;
    int retries = 0;
    const unsigned char *p = (const unsigned char *)buf;
    while (sent < len) {
        int n = mbedtls_ssl_write(&c->ssl, p + sent, len - sent);
        if ((n == MBEDTLS_ERR_SSL_WANT_READ ||
             n == MBEDTLS_ERR_SSL_WANT_WRITE) && retries++ < 2)
            continue;
        if (n < 0) return -WF_ERR_NETWORK;
        retries = 0;
        sent += (size_t)n;
    }
    return (long)sent;
}

long wii_tls_recv(wii_tls_conn *c, void *buf, size_t cap) {
    if (!c || c->fd < 0) return -WF_ERR_INVALID_ARG;
    int retries = 0;
    int n;
    do {
        n = mbedtls_ssl_read(&c->ssl, (unsigned char *)buf, cap);
    } while ((n == MBEDTLS_ERR_SSL_WANT_READ ||
              n == MBEDTLS_ERR_SSL_WANT_WRITE) && retries++ < 2);
    if (n > 0) return n;
    if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0; /* clean EOF */
    return -WF_ERR_NETWORK;
}

void wii_tls_close(wii_tls_conn *c) {
    if (!c) return;
    if (c->fd >= 0) {
        mbedtls_ssl_close_notify(&c->ssl);
        net_close(c->fd);
    }
    mbedtls_ssl_free(&c->ssl);
    free(c);
}

/* mbedTLS f_rng callback backed by the global DRBG (used by crypto signing). */
int wii_tls_random(void *p, unsigned char *out, size_t len) {
    (void)p;
    if (g_rotation_pending) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    if (!g_init) {
        if (wii_tls_global_init() != WF_OK) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    }
    return mbedtls_ctr_drbg_random(&g_ctr, out, len);
}
