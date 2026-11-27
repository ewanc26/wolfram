/*
 * proxy_id.h — shared by the XRPC transports and the identifier builder.
 *
 * An atproto-proxy value is a service reference "<did>#<service id>", for
 * example "did:web:api.bsky.chat#bsky_chat". It goes on the wire as a header
 * line, so a value carrying CR, LF or any other control character could
 * smuggle a second header into the request: refuse it rather than escape it.
 * Space is refused too, since neither a DID nor a service id may hold one.
 */
#ifndef WOLFRAM_TRANSPORT_PROXY_ID_H
#define WOLFRAM_TRANSPORT_PROXY_ID_H

/* C89: the Mac OS 9 transport includes this too. Returns 1 when `value` is
 * "did:<rest>#<service id>" with exactly one '#', a non-empty DID body and
 * service id, and no control characters or spaces. */
static int wf_proxy_id_valid(const char *value) {
    const unsigned char *p;
    const unsigned char *hash;

    if (value == NULL || value[0] != 'd' || value[1] != 'i' ||
        value[2] != 'd' || value[3] != ':')
        return 0;
    hash = NULL;
    for (p = (const unsigned char *)value + 4; *p != '\0'; p++) {
        if (*p <= 0x20 || *p == 0x7f) return 0;
        if (*p == '#') {
            if (hash != NULL) return 0;
            hash = p;
        }
    }
    if (hash == NULL) return 0;
    /* "did:#x" has no DID body and "did:x#" no service id. */
    if (hash == (const unsigned char *)value + 4 || hash[1] == '\0') return 0;
    return 1;
}

#endif
