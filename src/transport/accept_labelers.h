/*
 * accept_labelers.h — shared by the XRPC transports.
 *
 * An atproto-accept-labelers value is a comma-separated list of labeler DIDs,
 * each optionally followed by ";redact". It goes on the wire as a header
 * line, so a value carrying CR, LF or any other control character could
 * smuggle a second header into the request: refuse it rather than escape it.
 */
#ifndef WOLFRAM_TRANSPORT_ACCEPT_LABELERS_H
#define WOLFRAM_TRANSPORT_ACCEPT_LABELERS_H

/* C89: the Mac OS 9 transport includes this too. */
static int wf_accept_labelers_valid(const char *value) {
    const unsigned char *p;

    if (value == NULL || value[0] == '\0') return 0;
    for (p = (const unsigned char *)value; *p != '\0'; p++) {
        if (*p < 0x20 || *p == 0x7f) return 0;
    }
    return 1;
}

#endif
