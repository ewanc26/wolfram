/* Consoles have no system OpenSSL, so Wii, Wii U and 3DS redirect to wolfram's
 * own OpenSSL compat layer (see src/platform/openssl_compat.c, added to the
 * build by the matching branch in CMakeLists.txt). Everywhere else a real
 * OpenSSL is available; delegate to it via #include_next so that -Isrc (which
 * places this shim ahead of the system OpenSSL in the search path) does not
 * shadow the real headers. */
#if defined(WOLFRAM_WII) || defined(WOLFRAM_WIIU) || defined(WOLFRAM_3DS)
#include "../platform/openssl_compat.h"
#else
#include_next <openssl/sha.h>
#endif
