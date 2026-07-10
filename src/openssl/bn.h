/* On Wii (WOLFRAM_WII) redirect to the OpenSSL compat layer. On every other
 * platform a real OpenSSL is available; delegate to it via #include_next so
 * that -Isrc (which places this shim ahead of the system OpenSSL in the
 * search path) does not shadow the real headers. */
#ifdef WOLFRAM_WII
#include "../openssl_compat.h"
#else
#include_next <openssl/bn.h>
#endif
