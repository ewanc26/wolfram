/**
 * wii_mbedtls_config.h — Minimal mbedTLS 2.28 configuration for the Nintendo
 * Wii (devkitPPC / libogc, newlib).
 *
 * Selected for an XRPC/HTTPS client against AT Protocol PDS endpoints:
 *   - TLS 1.2 client with ECDHE-ECDSA / ECDHE-RSA key exchange
 *   - X.509 certificate chain verification (in-memory CA store)
 *   - AES-GCM / AES-CBC bulk ciphers, SHA-256/SHA-1 (cert sigs)
 *   - P-256/P-384/P-521 (and secp256k1) curve support for future use
 *
 * Deliberately excluded: PSA crypto, networking (we use lwIP directly),
 * timing (DTLS/server only), filesystem IO, and threading. Entropy and the
 * system clock are provided by wolfram's Wii platform layer
 * (src/transport/wii_tls.c) rather than by mbedTLS defaults.
 */

#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* Platform abstraction: mbedtls_calloc/free via newlib stdlib. */
#define MBEDTLS_PLATFORM_C

/* Entropy + deterministic RNG. wolfram registers an installation-unique,
 * externally provisioned seed in wii_tls.c; no timing, MAC-address, or
 * platform/file source is accepted. */
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

/* Hashing. */
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_MD_C

/* Bignum + public key. */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_PK_WRITE_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_SECP521R1_ENABLED
#define MBEDTLS_ECP_DP_SECP256K1_ENABLED

/* X.509 certificate parsing and verification. */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_PEM_WRITE_C
#define MBEDTLS_BASE64_C

/* Symmetric ciphers used by TLS 1.2 bulk encryption. */
#define MBEDTLS_CIPHER_C
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CCM_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_CIPHER_PADDING_PKCS7
#define MBEDTLS_CIPHER_PADDING_ONE_AND_ZEROS

/* TLS 1.2 client. We keep the peer certificate so verification works. */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_PKCS1_V15

/* Current time is supplied by wolfram's Wii layer (gettimeofday-backed) so
 * X.509 NotBefore/NotAfter checks are meaningful. Date-string population is
 * not needed on a verifying client. */
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_PLATFORM_TIME_ALT

/* Diagnostics. */
#define MBEDTLS_ERROR_C
#define MBEDTLS_VERSION_C

/* ── Explicitly disabled to keep the surface small and buildable ────── */
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_FS_IO
#undef MBEDTLS_PSA_CRYPTO_C
#undef MBEDTLS_PSA_CRYPTO_CONFIG
#undef MBEDTLS_USE_PSA_CRYPTO
#undef MBEDTLS_DEPRECATED_WARNING
#undef MBEDTLS_MEMORY_BACKTRACE
/* We register a Wii-specific entropy source in wii_tls.c, so the built-in
 * platform/hardware pollers (which assume Unix/Windows) must be disabled. */
#define MBEDTLS_NO_PLATFORM_ENTROPY
#undef MBEDTLS_ENTROPY_HARDWARE_ALT
#undef MBEDTLS_HAVE_TIME_DATE
#undef MBEDTLS_THREADING_C
#undef MBEDTLS_PSA_INJECT_ENTROPY
#undef MBEDTLS_TEST_HOOKS

#endif /* MBEDTLS_CONFIG_H */
