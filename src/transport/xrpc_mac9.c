/**
 * xrpc_mac9.c -- XRPC/HTTP transport for Classic Mac OS 9.
 *
 * Implements Wolfram's low-level XRPC client over Open Transport TCP, with
 * macTLS providing the TLS stream (see wolfram/macos9_tls.h). Wolfram owns
 * everything above the TLS record layer: URL building, percent-encoding,
 * request framing, header parsing, Content-Length and chunked response
 * decoding, DPoP-Nonce capture and bounded buffering. macTLS owns Open
 * Transport, DNS, the TLS handshake, certificate validation and entropy.
 *
 * The transport is synchronous from the caller's point of view but never
 * blocks the UI: every wait becomes a macTLS pump slice followed by a yield
 * through wf_macos9_set_yield_callback(). An application that installs no yield
 * hook still gets a working transport, just one that cannot process events
 * while a request is in flight.
 *
 * Deliberately absent: pthreads, blocking BSD sockets, libcurl, OpenSSL, POSIX
 * and C99 libc. This file is strict C89 so a CodeWarrior-era compiler can
 * build it for PowerPC Mac OS 9. That rules out `//` comments, declarations
 * after statements, `snprintf`, `strdup`, `%zu`, `strcasecmp` and <strings.h>;
 * the small local helpers near the top of the file replace each of them. It
 * also rules out a JSON library, so the XRPC error envelope is decoded by the
 * bounded scanner in the wf_json_* helpers below rather than by cJSON, which is
 * C99 and would not compile under a strict C89 configuration.
 *
 * Platform scope: this transport exists for clients that authenticate through
 * an external bridge (Platinum, for one) and therefore need no local OAuth,
 * DPoP or PDS signing. The full libwolfram still needs a Classic Mac OS 9
 * crypto backend before the complete SDK can run on this platform; see the
 * Classic Mac OS 9 transport section of README.md.
 */

#include "wolfram/xrpc.h"
#include "wolfram/macos9_tls.h"

#include <stdlib.h>
#include <string.h>

/* ── Limits ─────────────────────────────────────────────────────────── */

/*
 * Classic Mac OS 9 hardware has very little RAM by modern standards, so the
 * response budget is small enough that a hostile or broken peer cannot exhaust
 * the heap. Every buffered read funnels through wf_buffer_append_limit(), so
 * this one constant bounds headers, body and chunk-decoded body alike.
 *
 * 2 MiB comfortably holds a timeline page of a few dozen posts, a profile
 * document and an avatar-sized response, and is close to the largest allocation
 * this transport will ever attempt. Raise it deliberately, not accidentally,
 * if a caller genuinely needs a larger document.
 */
#define WF_XRPC_MAC9_MAX_RESPONSE_BYTES ((size_t)2 * 1024 * 1024)

/*
 * Separately bounded request head. The head is built entirely from
 * caller-controlled strings (base URL, NSID, header names and values) plus a
 * Content-Length, so this only trips on a caller that has built a pathological
 * request. It exists so such a caller gets WF_ERR_ALLOC before a connection is
 * even attempted.
 */
#define WF_XRPC_MAC9_MAX_REQUEST_BYTES ((size_t)16 * 1024)

/* Longest URL this transport will build from a base URL, NSID and query. */
#define WF_XRPC_MAC9_MAX_URL 1024

/* Bounded copies of server-supplied header values and error envelope fields. */
#define WF_XRPC_MAC9_MAX_FIELD 256

/* Enough decimal digits for any 32- or 64-bit size_t, plus a sign-free margin.
 */
#define WF_MAC9_MAX_DIGITS 24

/* Pump slice size used by the TLS shim; repeated here only as documentation of
 * how often a Classic Mac OS 9 request yields to the application event loop. */
#define WF_MAC9_SMALL_READ 512

/* ── C89 string and formatting helpers ──────────────────────────────── */

/*
 * <strings.h> and strcasecmp/strncasecmp are POSIX, not C89, and are absent
 * from the CodeWarrior libraries this transport targets. HTTP field names are
 * case-insensitive per RFC 9110, so a local ASCII-only comparison is both
 * required and sufficient; restricting it to ASCII letters also avoids the
 * locale surprises ctype-based case folding would invite on a target whose C
 * library has no UTF-8 locale.
 */
static int mac9_ascii_lower(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

static int mac9_streq_ci(const char *a, const char *b) {
    size_t i;

    for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
        if (mac9_ascii_lower((unsigned char)a[i]) !=
            mac9_ascii_lower((unsigned char)b[i]))
            return 0;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static int mac9_starts_with_ci(const char *haystack, const char *prefix,
                               size_t prefix_len) {
    size_t i;

    for (i = 0; i < prefix_len; i++) {
        if (mac9_ascii_lower((unsigned char)haystack[i]) !=
            mac9_ascii_lower((unsigned char)prefix[i]))
            return 0;
    }
    return 1;
}

/* strdup is POSIX. This is the C89 equivalent, and the only allocator-backed
 * string helper the transport needs. */
static char *mac9_strdup(const char *value) {
    size_t len;
    char *copy;

    if (value == NULL) return NULL;
    len = strlen(value);
    copy = (char *)malloc(len + 1);
    if (copy == NULL) return NULL;
    memcpy(copy, value, len + 1);
    return copy;
}

/*
 * Concatenate three pieces into a fresh string, refusing anything longer than
 * `limit`. This is the C89 replacement for the snprintf("%s%s%s") idiom used
 * throughout the desktop transport, and it is the only way request framing
 * builds a string: snprintf is C99 and its %z length modifier is not
 * understood by printf implementations of this era either.
 */
static char *mac9_concat3(const char *a, const char *b, const char *c,
                          size_t limit) {
    size_t a_len;
    size_t b_len;
    size_t c_len;
    char *out;

    if (a == NULL) a = "";
    if (b == NULL) b = "";
    if (c == NULL) c = "";
    a_len = strlen(a);
    b_len = strlen(b);
    c_len = strlen(c);
    /* Every addition below is guarded before it happens, so the three lengths
     * can never overflow size_t when summed. */
    if (a_len > limit || b_len > limit - a_len || c_len > limit - a_len - b_len)
        return NULL;
    out = (char *)malloc(a_len + b_len + c_len + 1);
    if (out == NULL) return NULL;
    memcpy(out, a, a_len);
    memcpy(out + a_len, b, b_len);
    memcpy(out + a_len + b_len, c, c_len + 1);
    return out;
}

/* Render a size_t as decimal digits into `out`, which must have room for
 * WF_MAC9_MAX_DIGITS + 1 bytes. Returns the number of digits written. */
static size_t mac9_format_size(size_t value, char *out) {
    char scratch[WF_MAC9_MAX_DIGITS];
    size_t digits = 0;
    size_t i;

    do {
        scratch[digits++] = (char)('0' + (int)(value % 10));
        value /= 10;
    } while (value != 0);

    for (i = 0; i < digits; i++) out[i] = scratch[digits - 1 - i];
    out[digits] = '\0';
    return digits;
}

/* ── Growable byte buffer ───────────────────────────────────────────── */

struct wf_buffer {
    char *data;
    size_t len;
    size_t cap;
};

/* Append `n` bytes, refusing to exceed `limit`. A remote peer must not be able
 * to grow a response buffer without bound, and a bad caller must not be able
 * to grow a request buffer without bound either. */
static int wf_buffer_append_limit(struct wf_buffer *b, const void *src,
                                  size_t n, size_t limit) {
    size_t want;
    size_t cap;
    char *grown;

    if (b == NULL) return -1;
    if (n > limit || b->len > limit - n) return -1;

    /* +1 keeps room for the NUL that makes the buffer printable and lets the
     * response scanner treat it as a terminated string. */
    want = b->len + n + 1;
    if (want > b->cap) {
        cap = b->cap != 0 ? b->cap : 512;
        while (cap < want) {
            if (cap > limit / 2) return -1;
            cap *= 2;
        }
        if (cap > limit + 1) cap = limit + 1;
        grown = (char *)realloc(b->data, cap);
        if (grown == NULL) return -1;
        b->data = grown;
        b->cap = cap;
    }
    if (n != 0 && src != NULL) memcpy(b->data + b->len, src, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

/* Append a NUL-terminated string to the shared response budget. */
static int wf_buffer_append(struct wf_buffer *b, const void *src, size_t n) {
    return wf_buffer_append_limit(b, src, n, WF_XRPC_MAC9_MAX_RESPONSE_BYTES);
}

static int wf_buffer_append_str(struct wf_buffer *b, const char *text) {
    return wf_buffer_append(b, text, strlen(text));
}

/* Append an HTTP header line: "name: value\r\n". An empty value is written as a
 * bare field name so "Name:" and "Name: " cannot differ on the wire. */
static int wf_buffer_append_header(struct wf_buffer *b, const char *name,
                                   const char *value) {
    if (name == NULL || name[0] == '\0') return -1;
    if (wf_buffer_append_str(b, name) != 0) return -1;
    if (value != NULL && value[0] != '\0') {
        if (wf_buffer_append_str(b, ": ") != 0) return -1;
        if (wf_buffer_append_str(b, value) != 0) return -1;
    }
    return wf_buffer_append_str(b, "\r\n");
}

/* Append "name: <decimal>\r\n" without going through printf. */
static int wf_buffer_append_size_header(struct wf_buffer *b, const char *name,
                                        size_t value) {
    char digits[WF_MAC9_MAX_DIGITS + 1];

    if (name == NULL || name[0] == '\0') return -1;
    if (wf_buffer_append_str(b, name) != 0) return -1;
    if (wf_buffer_append_str(b, ": ") != 0) return -1;
    (void)mac9_format_size(value, digits);
    if (wf_buffer_append_str(b, digits) != 0) return -1;
    return wf_buffer_append_str(b, "\r\n");
}

/* ── Client ─────────────────────────────────────────────────────────── */

struct wf_xrpc_client {
    char *base_url;
    char *auth_header;
    char *proxy_header;
    int https_only;
    size_t max_response_bytes;
    wf_xrpc_handler_fn handler;
    void *handler_userdata;
    wf_xrpc_refresh_fn refresh_cb;
    void *refresh_userdata;
    int refreshing;
    char *last_error; /* XRPC error message from the last non-2xx response */
};

/* ── URL helpers ────────────────────────────────────────────────────── */

/* Strip trailing slashes so paths can be joined predictably. */
static char *wf_normalise_base(const char *base_url) {
    size_t len;
    char *out;

    len = strlen(base_url);
    while (len > 0 && base_url[len - 1] == '/') len--;
    out = (char *)malloc(len + 1);
    if (out == NULL) return NULL;
    memcpy(out, base_url, len);
    out[len] = '\0';
    return out;
}

static int mac9_is_unreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
           c == '~';
}

/* Percent-encode per RFC 3986 (unreserved characters pass through). Returns a
 * caller-owned string, or NULL on allocation failure. The character classes
 * are explicit rather than isalnum(), which is locale dependent and could
 * classify bytes above 127 unpredictably on a target with no UTF-8 locale. */
static char *wf_url_encode(const char *in) {
    static const char hex[] = "0123456789ABCDEF";
    const char *p;
    char *out;
    size_t j;

    if (in == NULL) return NULL;

    /* Worst case: every byte becomes %XX. */
    out = (char *)malloc(strlen(in) * 3 + 1);
    if (out == NULL) return NULL;

    j = 0;
    for (p = in; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;
        if (mac9_is_unreserved(c)) {
            out[j++] = (char)c;
        } else {
            out[j++] = '%';
            out[j++] = hex[(c >> 4) & 0xF];
            out[j++] = hex[c & 0xF];
        }
    }
    out[j] = '\0';
    return out;
}

/* Is `url` an https:// URL with a non-empty authority? */
static int wf_url_is_https(const char *url) {
    if (url == NULL) return 0;
    if (strncmp(url, "https://", 8) != 0) return 0;
    return url[8] != '\0';
}

/* Parse an absolute URL into host/port/path. `path` includes the query string.
 * Returns WF_OK or WF_ERR_INVALID_ARG. */
static wf_status wf_parse_url(const char *url, char *host, size_t host_cap,
                              unsigned short *port, char *path,
                              size_t path_cap) {
    const char *p;
    const char *scheme_end;
    const char *host_start;
    const char *host_end;
    const char *path_start;
    int is_https;
    size_t hlen;
    size_t plen;
    long parsed;

    if (url == NULL || host == NULL || port == NULL || path == NULL)
        return WF_ERR_INVALID_ARG;

    p = url;
    is_https = 1;
    scheme_end = strstr(url, "://");
    if (scheme_end != NULL) {
        size_t slen = (size_t)(scheme_end - url);
        is_https = (slen == 5 && strncmp(url, "https", 5) == 0);
        p = scheme_end + 3;
    }
    *port = (unsigned short)(is_https ? 443 : 80);

    host_start = p;
    host_end = p;
    while (*host_end != '\0' && *host_end != ':' && *host_end != '/' &&
           *host_end != '?')
        host_end++;
    hlen = (size_t)(host_end - host_start);
    if (hlen == 0 || hlen >= host_cap) return WF_ERR_INVALID_ARG;
    memcpy(host, host_start, hlen);
    host[hlen] = '\0';

    p = host_end;
    if (*p == ':') {
        p++;
        parsed = 0;
        while (*p >= '0' && *p <= '9') parsed = parsed * 10 + (*p++ - '0');
        if (parsed > 0 && parsed <= 65535) *port = (unsigned short)parsed;
    }

    path_start = p;
    if (*path_start != '/') path_start = "/"; /* implicit root */
    plen = strlen(path_start);
    if (plen + 1 > path_cap) return WF_ERR_INVALID_ARG;
    memcpy(path, path_start, plen + 1);
    return WF_OK;
}

/* ── Response header parsing ────────────────────────────────────────── */

/* Copy the value of header `name` out of a NUL-terminated header block.
 * Returns 1 when found, 0 otherwise. */
static int wf_find_header_value(const char *headers, const char *name,
                                char *out, size_t out_cap) {
    size_t nlen;
    const char *p;

    if (headers == NULL || name == NULL || out == NULL || out_cap == 0)
        return 0;

    nlen = strlen(name);
    p = headers;
    while (*p != '\0') {
        /* Field names are case-insensitive. The trailing ':' test is what
         * stops a shorter name from matching the prefix of a longer one. */
        if (mac9_starts_with_ci(p, name, nlen) && p[nlen] == ':') {
            const char *v = p + nlen + 1;
            size_t i = 0;
            while (*v == ' ' || *v == '\t') v++;
            while (v[i] != '\0' && v[i] != '\r' && v[i] != '\n') {
                if (i + 1 < out_cap) out[i] = v[i];
                i++;
            }
            out[i < out_cap ? i : out_cap - 1] = '\0';
            return 1;
        }
        /* Advance to the next line. */
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return 0;
}

static int wf_has_header(const wf_http_header *headers, size_t count,
                         const char *name) {
    size_t i;

    for (i = 0; i < count; i++) {
        if (headers[i].name != NULL && mac9_streq_ci(headers[i].name, name))
            return 1;
    }
    return 0;
}

/* ── Minimal bounded JSON string scanner ────────────────────────────── */

/*
 * A deliberately small JSON reader, replacing the cJSON dependency this
 * transport would otherwise inherit from the desktop build.
 *
 * cJSON is C99: it declares variables mid-block and initialises `for` loop
 * variables in the declaration, neither of which a strict C89 CodeWarrior
 * configuration accepts, so it is not portable enough to link into a Classic
 * Mac OS 9 binary. Bridging that would mean forking a third-party library,
 * which is a worse trade than decoding two string fields here.
 *
 * What is needed is narrow. The XRPC error envelope is a flat JSON object with
 * a string "error" and an optional string "message", and the contract is to
 * copy those out or report that there is no envelope. So this scans the
 * top-level object only and refuses to interpret anything it does not
 * understand, reporting "no envelope" rather than guessing. A malformed
 * response can therefore never be mistaken for a well-formed one.
 */

/* Advance past a JSON string starting at `p` (which must point at the opening
 * quote). Returns a pointer just past the closing quote, or NULL if the string
 * is unterminated or holds a raw control character. */
static const char *wf_json_skip_string(const char *p) {
    if (*p != '"') return NULL;
    p++;
    while (*p != '\0') {
        unsigned char c = (unsigned char)*p;

        /* RFC 8259 requires every control character inside a string to be
         * escaped. Rejecting raw ones here keeps this transport in step with
         * cJSON, which the curl-based transports use, so identical input
         * cannot be decoded two different ways depending on platform. */
        if (c < 0x20u) return NULL;
        if (c == '\\') {
            p++;
            if (*p == '\0') return NULL;
            p++;
            continue;
        }
        if (c == '"') return p + 1;
        p++;
    }
    return NULL;
}

/* Advance past exactly one JSON value. Objects, arrays and scalars are all
 * handled; the value is skipped without being interpreted. Returns a pointer
 * just past the value, or NULL if the input is truncated. */
static const char *wf_json_skip_value(const char *p) {
    int depth = 0;

    if (*p == '{' || *p == '[') {
        while (*p != '\0') {
            if (*p == '"') {
                p = wf_json_skip_string(p);
                if (p == NULL) return NULL;
                continue;
            }
            if (*p == '{' || *p == '[') {
                depth++;
            } else if (*p == '}' || *p == ']') {
                depth--;
                if (depth == 0) return p + 1;
            }
            p++;
        }
        return NULL;
    }

    /* A bare scalar ends at the first structural character. */
    while (*p != '\0' && *p != ',' && *p != '}' && *p != ']') p++;
    return p;
}

/* Read exactly four hex digits at `p` into *cp. Returns 0 on success, -1 if any
 * of the four bytes is not a hex digit. */
static int wf_json_hex4(const char *p, unsigned int *cp) {
    unsigned int v = 0;
    int i;

    for (i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f')
            v |= (unsigned int)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v |= (unsigned int)(c - 'A' + 10);
        else
            return -1;
    }
    *cp = v;
    return 0;
}

/* Append one code point to `out` as UTF-8, truncating rather than overflowing.
 * U+0000 becomes U+FFFD because the output is NUL terminated. */
static void wf_json_utf8(unsigned int cp, char *out, size_t out_cap,
                         size_t *j) {
    unsigned char seq[4];
    size_t n;
    size_t k;

    if (cp == 0) cp = 0xFFFDu;
    if (cp < 0x80u) {
        seq[0] = (unsigned char)cp;
        n = 1;
    } else if (cp < 0x800u) {
        seq[0] = (unsigned char)(0xC0u | (cp >> 6));
        seq[1] = (unsigned char)(0x80u | (cp & 0x3Fu));
        n = 2;
    } else if (cp < 0x10000u) {
        seq[0] = (unsigned char)(0xE0u | (cp >> 12));
        seq[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        seq[2] = (unsigned char)(0x80u | (cp & 0x3Fu));
        n = 3;
    } else {
        seq[0] = (unsigned char)(0xF0u | (cp >> 18));
        seq[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
        seq[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
        seq[3] = (unsigned char)(0x80u | (cp & 0x3Fu));
        n = 4;
    }
    for (k = 0; k < n; k++) {
        if (*j + 1 < out_cap) out[(*j)++] = (char)seq[k];
    }
}

/* Decode the body of a JSON string (just past the opening quote) into `out`,
 * honouring every escape JSON defines, including \uXXXX with surrogate pairs:
 * XRPC error messages routinely contain emoji and accented text, and losing
 * the whole envelope over one non-ASCII character would hide the error code
 * exactly when the server is explaining a failure.
 *
 * Returns 0 on success. Returns -1 for input that is not a complete, valid
 * JSON string, in which case the caller reports "no envelope" rather than
 * returning a half-decoded string. */
static int wf_json_decode_string(const char *p, const char *end, char *out,
                                 size_t out_cap) {
    size_t j = 0;

    while (p < end && *p != '"') {
        unsigned char c = (unsigned char)*p++;

        if (c == '\\') {
            if (p >= end) return -1;
            c = (unsigned char)*p++;
            if (c == 'n')
                c = '\n';
            else if (c == 't')
                c = '\t';
            else if (c == 'r')
                c = '\r';
            else if (c == 'b')
                c = '\b';
            else if (c == 'f')
                c = '\f';
            else if (c == 'u') {
                unsigned int cp;

                if (end - p < 4 || wf_json_hex4(p, &cp) != 0) return -1;
                p += 4;
                if (cp >= 0xD800u && cp <= 0xDBFFu) {
                    unsigned int lo;

                    if (end - p >= 6 && p[0] == '\\' && p[1] == 'u' &&
                        wf_json_hex4(p + 2, &lo) == 0 && lo >= 0xDC00u &&
                        lo <= 0xDFFFu) {
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                        p += 6;
                    } else {
                        cp = 0xFFFDu; /* unpaired high surrogate */
                    }
                } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
                    cp = 0xFFFDu; /* unpaired low surrogate */
                }
                wf_json_utf8(cp, out, out_cap, &j);
                continue;
            } else if (c != '"' && c != '\\' && c != '/') {
                return -1;
            }
        } else if (c < 0x20u) {
            /* Unescaped control character: invalid JSON, and accepted by no
             * other Wolfram transport. */
            return -1;
        }
        if (j + 1 < out_cap) out[j++] = (char)c;
    }
    if (p >= end)
        return -1; /* ran out before the closing quote, so this was no string */
    out[j] = '\0';
    return 0;
}

/* Look for a top-level string member called `name` in a JSON object body.
 * Returns 1 when found and `out` filled, 0 otherwise. */
static int wf_json_find_string(const char *body, size_t len, const char *name,
                               char *out, size_t out_cap) {
    const char *p;
    const char *end;
    size_t name_len;

    if (body == NULL || name == NULL || out == NULL || out_cap == 0) return 0;

    name_len = strlen(name);
    end = body + len;
    p = body;
    if (p >= end || *p != '{') return 0;
    p++;

    for (;;) {
        const char *key;
        const char *after_key;
        size_t key_len;
        const char *skip;

        /* Skip whitespace and member separators. */
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' ||
                           *p == '\n' || *p == ','))
            p++;
        if (p >= end || *p == '}')
            return 0;            /* ran out of members, or the object ended */
        if (*p != '"') return 0; /* not an object we understand */

        key = p + 1;
        after_key = wf_json_skip_string(p);
        if (after_key == NULL) return 0;
        key_len = (size_t)(after_key - 1 - key);

        p = after_key;
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        if (p >= end || *p != ':') return 0;
        p++;
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        if (p >= end) return 0;

        if (key_len == name_len && strncmp(key, name, name_len) == 0) {
            /* Present but not a string: report "no envelope" rather than
             * coercing some other type into an error code. */
            if (*p != '"') return 0;
            if (wf_json_decode_string(p + 1, end, out, out_cap) != 0) return 0;
            return 1;
        }

        /* Skip this member's value wholesale, so a same-named member nested
         * inside an object cannot be mistaken for a top-level one. */
        skip = wf_json_skip_value(p);
        if (skip == NULL || skip <= p) return 0;
        p = skip;
    }
}

/* Read the XRPC error envelope out of a response body. Copies up to `err_cap`
 * bytes of the `error` field into the caller's buffer, NUL terminated. Returns
 * 1 when an envelope was found, 0 otherwise. */
static int wf_error_envelope(const wf_response *resp, char *err_buf,
                             size_t err_cap) {
    if (resp == NULL || resp->body == NULL || resp->body_len == 0) return 0;
    return wf_json_find_string(resp->body, resp->body_len, "error", err_buf,
                               err_cap) == 1;
}

/* ── Response reading ───────────────────────────────────────────────── */

/*
 * Read the complete HTTP response (headers plus body) into `buf`, capturing the
 * status code, the DPoP-Nonce header and any Location header.
 *
 * `limit` is the byte ceiling for everything read here, so a caller that wants
 * a tighter cap than WF_XRPC_MAC9_MAX_RESPONSE_BYTES (a DID document, a
 * well-known file, one timeline page) passes one. Every append is checked
 * against it, so an oversized response is refused rather than buffered and then
 * rejected.
 */
static wf_status wf_http_read_response(wf_macos9_conn *conn,
                                       struct wf_buffer *buf, long *status,
                                       size_t limit, char **dpop_nonce,
                                       char **location) {
    struct wf_buffer decoded;
    char *hdr_win;
    char cl_buf[32];
    char nonce_buf[WF_XRPC_MAC9_MAX_FIELD];
    char location_buf[WF_XRPC_MAC9_MAX_FIELD];
    size_t header_end;
    size_t body_have;
    size_t body_want;
    size_t off;
    long content_length;
    long declared;
    int header_done;
    int chunked;

    memset(&decoded, 0, sizeof(decoded));
    nonce_buf[0] = '\0';
    location_buf[0] = '\0';

    /* Phase 1: read until the complete header block has arrived. */
    header_done = 0;
    header_end = 0;
    for (;;) {
        char tmp[WF_MAC9_SMALL_READ];
        long n;
        size_t i;

        if (buf->len >= 4) {
            for (i = 0; i + 3 < buf->len; i++) {
                if (buf->data[i] == '\r' && buf->data[i + 1] == '\n' &&
                    buf->data[i + 2] == '\r' && buf->data[i + 3] == '\n') {
                    header_end = i + 4;
                    header_done = 1;
                    break;
                }
            }
        }
        if (header_done) break;

        n = wf_macos9_recv(conn, tmp, sizeof(tmp));
        if (n < 0) return (wf_status)(-n);
        if (n == 0) break; /* peer closed mid-headers */
        if (wf_buffer_append_limit(buf, tmp, (size_t)n, limit) != 0)
            return WF_ERR_ALLOC;
    }

    if (!header_done)
        return WF_ERR_PARSE; /* closed, or too large, before a complete head */

    /* Status line: "HTTP/1.1 200 OK". */
    *status = 0;
    if (buf->len >= 8 && strncmp(buf->data, "HTTP/", 5) == 0) {
        const char *code = buf->data + 5;
        while (*code != '\0' && *code != ' ') code++;
        if (*code == ' ') *status = strtol(code + 1, NULL, 10);
    }

    /* Isolate the header window so header lookup cannot run into the body. */
    hdr_win = buf->data;
    hdr_win[header_end - 2] = '\0'; /* terminate before the final CRLF */

    cl_buf[0] = '\0';
    content_length = -1;
    chunked = 0;
    if (wf_find_header_value(hdr_win, "Content-Length", cl_buf,
                             sizeof(cl_buf))) {
        declared = strtol(cl_buf, NULL, 10);
        content_length = declared > 0 ? declared : -1;
        /* A declared length above the budget is refused before a single body
         * byte is accepted, so an oversized document cannot be streamed in and
         * only then rejected. */
        if (content_length > 0 && (size_t)content_length > limit)
            return WF_ERR_ALLOC;
    }
    if (wf_find_header_value(hdr_win, "Transfer-Encoding", cl_buf,
                             sizeof(cl_buf)) &&
        mac9_starts_with_ci(cl_buf, "chunked", 7))
        chunked = 1;
    if (wf_find_header_value(hdr_win, "DPoP-Nonce", nonce_buf,
                             sizeof(nonce_buf))) {
        *dpop_nonce = mac9_strdup(nonce_buf);
        if (*dpop_nonce == NULL) return WF_ERR_ALLOC;
    }
    if (wf_find_header_value(hdr_win, "Location", location_buf,
                             sizeof(location_buf))) {
        *location = mac9_strdup(location_buf);
        if (*location == NULL) return WF_ERR_ALLOC;
    }

    /* Phase 2: read the body. */
    body_have = buf->len - header_end;
    body_want = (size_t)(content_length > 0 ? content_length : 0);

    if (chunked) {
        /*
         * Every request carries "Connection: close", so the framed body is
         * collected whole and decoded afterwards. Decoding in place would
         * overwrite a later chunk while compacting an earlier one, so source
         * and destination stay separate buffers.
         */
        for (;;) {
            char tmp[WF_MAC9_SMALL_READ];
            long n = wf_macos9_recv(conn, tmp, sizeof(tmp));
            if (n < 0) return (wf_status)(-n);
            if (n == 0) break;
            if (wf_buffer_append_limit(buf, tmp, (size_t)n, limit) != 0)
                return WF_ERR_ALLOC;
        }

        off = header_end;
        for (;;) {
            size_t line_end;
            size_t size;
            size_t data_start;
            size_t i;
            int digits;

            /* Chunk size line, terminated by CRLF. */
            line_end = off;
            while (line_end + 1 < buf->len &&
                   !(buf->data[line_end] == '\r' &&
                     buf->data[line_end + 1] == '\n'))
                line_end++;
            if (line_end + 1 >= buf->len) {
                free(decoded.data);
                return WF_ERR_PARSE;
            }

            /* Size is hex, optionally followed by chunk extensions. */
            size = 0;
            digits = 0;
            i = off;
            while (i < line_end) {
                char c = buf->data[i];
                int d;
                if (c == ';') break;
                if (c >= '0' && c <= '9')
                    d = c - '0';
                else if (c >= 'a' && c <= 'f')
                    d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    d = c - 'A' + 10;
                else {
                    free(decoded.data);
                    return WF_ERR_PARSE;
                }
                /* Refuse a size that cannot possibly fit the budget before it
                 * is computed, so the shift cannot overflow. */
                if (size > (limit - (size_t)d) / 16) {
                    free(decoded.data);
                    return WF_ERR_ALLOC;
                }
                digits = 1;
                size = size * 16 + (size_t)d;
                i++;
            }
            if (!digits) {
                free(decoded.data);
                return WF_ERR_PARSE;
            }

            data_start = line_end + 2;
            if (size == 0) break; /* final chunk; any trailers are ignored */
            if (size > buf->len - data_start ||
                buf->len - data_start - size < 2 ||
                buf->data[data_start + size] != '\r' ||
                buf->data[data_start + size + 1] != '\n' ||
                wf_buffer_append_limit(&decoded, buf->data + data_start, size,
                                       limit) != 0) {
                free(decoded.data);
                return WF_ERR_PARSE;
            }
            off = data_start + size + 2;
        }

        /* Guarantee a non-NULL buffer even for an empty body, so callers can
         * index resp.body without a special case. */
        if (decoded.data == NULL &&
            wf_buffer_append_limit(&decoded, NULL, 0, limit) != 0) {
            free(decoded.data);
            return WF_ERR_ALLOC;
        }
        free(buf->data);
        *buf = decoded;
        body_have = buf->len;
    } else if (content_length > 0) {
        while (body_have < body_want) {
            char tmp[WF_MAC9_SMALL_READ];
            size_t need;
            long n;

            need = body_want - body_have;
            n = wf_macos9_recv(conn, tmp,
                               need < sizeof(tmp) ? need : sizeof(tmp));
            if (n < 0) return (wf_status)(-n);
            if (n == 0)
                break; /* short read: reported below rather than guessed at */
            if (wf_buffer_append_limit(buf, tmp, (size_t)n, limit) != 0)
                return WF_ERR_ALLOC;
            body_have += (size_t)n;
        }
        if (body_have != body_want) return WF_ERR_NETWORK;
    } else {
        /*
         * Neither Content-Length nor chunked: the body runs until the peer
         * closes, which is the documented semantics of a connection-delimited
         * HTTP/1.1 response body.
         */
        for (;;) {
            char tmp[WF_MAC9_SMALL_READ];
            long n = wf_macos9_recv(conn, tmp, sizeof(tmp));
            if (n < 0) return (wf_status)(-n);
            if (n == 0) break;
            if (wf_buffer_append_limit(buf, tmp, (size_t)n, limit) != 0)
                return WF_ERR_ALLOC;
            body_have += (size_t)n;
        }
    }

    /* The body is the trailing body_have bytes. Slide it to the front so the
     * caller's buffer holds only the body, NUL terminated. */
    if (header_done && !chunked)
        memmove(buf->data, buf->data + header_end, body_have);
    buf->len = body_have;
    buf->data[body_have] = '\0';
    return WF_OK;
}

/* ── Core request ───────────────────────────────────────────────────── */

/* Record the XRPC error envelope of a non-2xx response on the client so the
 * caller can surface the server's message. Prefers `message`, falls back to the
 * `error` code, and clears the field when the body carries no envelope. */
static void wf_xrpc_set_last_error(wf_xrpc_client *client,
                                   const wf_response *out) {
    char err_buf[WF_XRPC_MAC9_MAX_FIELD];
    char msg_buf[WF_XRPC_MAC9_MAX_FIELD];
    const char *chosen;
    char *copy;

    if (client == NULL || out == NULL) return;

    err_buf[0] = '\0';
    msg_buf[0] = '\0';
    if (!wf_error_envelope(out, err_buf, sizeof(err_buf))) {
        free(client->last_error);
        client->last_error = NULL;
        return;
    }
    (void)wf_json_find_string(out->body, out->body_len, "message", msg_buf,
                              sizeof(msg_buf));

    chosen = msg_buf[0] != '\0' ? msg_buf : err_buf;
    free(client->last_error);
    client->last_error = NULL;
    if (chosen[0] != '\0') {
        copy = mac9_strdup(chosen);
        if (copy != NULL) client->last_error = copy;
    }
}

/* Does this response indicate an expired or otherwise invalid access token? */
static int wf_xrpc_response_is_expired(const wf_response *out) {
    char err_buf[WF_XRPC_MAC9_MAX_FIELD];

    if (out == NULL) return 0;
    if (out->status == 401) return 1;
    if (!wf_error_envelope(out, err_buf, sizeof(err_buf))) return 0;
    return strcmp(err_buf, "ExpiredToken") == 0 ||
           strcmp(err_buf, "InvalidToken") == 0;
}

/* Build the User-Agent value once per process: the only formatted value the
 * transport needs, so it is the only reason a static buffer exists here. */
static const char *wf_user_agent(void) {
    static char agent[64];
    char *joined;

    if (agent[0] != '\0') return agent;

    joined =
        mac9_concat3("wolfram/", WOLFRAM_VERSION_STRING, "", sizeof(agent));
    if (joined == NULL) {
        strcpy(agent, "wolfram");
        return agent;
    }
    strcpy(agent, joined);
    free(joined);
    return agent;
}

/* Build the request head, send it, then read the response.
 *
 * This is the only place the transport touches the network, which is what keeps
 * the test handler seam (wf_xrpc_set_handler) able to replace I/O wholesale. */
static wf_status wf_xrpc_perform(wf_xrpc_client *client, const char *method,
                                 const char *url, const char *content_type,
                                 const void *body, size_t body_len,
                                 const wf_http_header *extra,
                                 size_t extra_count, wf_response *out) {
    struct wf_buffer req;
    struct wf_buffer resp;
    wf_macos9_conn *conn;
    char host[256];
    char path[WF_XRPC_MAC9_MAX_URL];
    unsigned short port;
    char *nonce;
    char *location;
    long status;
    size_t limit;
    size_t i;
    long sent;
    int ok;
    wf_status rc;

    free(client->last_error);
    client->last_error = NULL;
    memset(out, 0, sizeof(*out));

    /* The effective body cap: the client's own setting, or the shared maximum
     * when the caller never set one. Resolved before either I/O path runs so
     * the same limit governs both. */
    limit = client->max_response_bytes != 0 ? client->max_response_bytes
                                            : WF_XRPC_MAC9_MAX_RESPONSE_BYTES;

    /* Issue #95 requires https-only support to be available where a caller asks
     * for it. Checked before any I/O, and deliberately before the handler
     * dispatch below: a transport policy must not be something a handler (or a
     * test seam standing in for one) can switch off. */
    if (client->https_only && !wf_url_is_https(url)) return WF_ERR_INVALID_ARG;

    /* Test seam: a handler replaces real network I/O. */
    if (client->handler != NULL) {
        wf_http_header *harr;
        size_t hcount;
        wf_status s;

        harr = NULL;
        hcount = 0;
        if (extra != NULL && extra_count != 0) {
            harr = (wf_http_header *)calloc(extra_count, sizeof(*harr));
            if (harr == NULL) return WF_ERR_ALLOC;
            for (i = 0; i < extra_count; i++) {
                harr[i].name = mac9_strdup(extra[i].name);
                harr[i].value = mac9_strdup(extra[i].value);
            }
            hcount = extra_count;
        }
        s = client->handler(client->handler_userdata, method, url, content_type,
                            (const char *)body, body_len, harr, hcount, out);
        for (i = 0; i < hcount; i++) {
            free((void *)harr[i].name);
            free((void *)harr[i].value);
        }
        free(harr);
        if (s == WF_ERR_HTTP) wf_xrpc_set_last_error(client, out);
        /* The cap holds whoever produced the body. A handler owns the response
         * buffer rather than the transport, so without this the limit would
         * silently stop applying the moment one is installed, which is the
         * opposite of what a caller setting a limit is asking for. */
        if (out->body_len > limit) {
            wf_response_free(out);
            return WF_ERR_NETWORK;
        }
        return s;
    }

    if (wf_parse_url(url, host, sizeof(host), &port, path, sizeof(path)) !=
        WF_OK)
        return WF_ERR_INVALID_ARG;

    conn = wf_macos9_connect(host, port, NULL, NULL);
    if (conn == NULL) return WF_ERR_NETWORK;

    /* Request head. */
    memset(&req, 0, sizeof(req));
    ok = 1;
    ok &= wf_buffer_append_str(&req, method) == 0;
    ok &= wf_buffer_append_str(&req, " ") == 0;
    ok &= wf_buffer_append_str(&req, path) == 0;
    ok &= wf_buffer_append_str(&req, " HTTP/1.1\r\n") == 0;
    ok &= wf_buffer_append_header(&req, "Host", host) == 0;
    ok &= wf_buffer_append_header(&req, "User-Agent", wf_user_agent()) == 0;
    /* Connection: close. This transport has no persistent-connection or
     * redirect machinery, so each request owns its connection and the response
     * body is unambiguously delimited by the close. */
    ok &= wf_buffer_append_header(&req, "Connection", "close") == 0;

    /* The auth header is stored pre-formatted as "Authorization: <value>" and
     * written as one unit, so a bearer token never passes through a
     * header-name code path. */
    if (client->auth_header != NULL &&
        !wf_has_header(extra, extra_count, "Authorization")) {
        ok &= wf_buffer_append_str(&req, client->auth_header) == 0;
        ok &= wf_buffer_append_str(&req, "\r\n") == 0;
    }
    if (content_type != NULL &&
        !wf_has_header(extra, extra_count, "Content-Type"))
        ok &= wf_buffer_append_header(&req, "Content-Type", content_type) == 0;
    if (body != NULL && body_len > 0)
        ok &=
            wf_buffer_append_size_header(&req, "Content-Length", body_len) == 0;
    for (i = 0; i < extra_count; i++)
        ok &= wf_buffer_append_header(&req, extra[i].name, extra[i].value) == 0;
    ok &= wf_buffer_append_str(&req, "\r\n") == 0;

    if (!ok || req.len > WF_XRPC_MAC9_MAX_REQUEST_BYTES) {
        free(req.data);
        wf_macos9_close(conn);
        return WF_ERR_ALLOC;
    }

    /* Send head, then body. The shim handles macTLS's partial acceptance, so a
     * non-negative return means everything was handed over. */
    sent = wf_macos9_send(conn, req.data, req.len);
    free(req.data);
    if (sent < 0) {
        wf_macos9_close(conn);
        return (wf_status)(-sent);
    }
    if (body != NULL && body_len > 0) {
        sent = wf_macos9_send(conn, body, body_len);
        if (sent < 0) {
            wf_macos9_close(conn);
            return (wf_status)(-sent);
        }
    }

    /* Read the response, bounded by the cap resolved above. */
    memset(&resp, 0, sizeof(resp));
    nonce = NULL;
    location = NULL;
    status = 0;
    rc = wf_http_read_response(conn, &resp, &status, limit, &nonce, &location);
    wf_macos9_close(conn);

    if (rc != WF_OK) {
        free(resp.data);
        free(nonce);
        free(location);
        return rc;
    }

    out->status = status;
    out->body = resp.data; /* ownership transferred to the caller */
    out->body_len = resp.len;
    out->dpop_nonce = nonce;
    out->location = location;

    if (status < 200 || status >= 300) {
        wf_xrpc_set_last_error(client, out);
        return WF_ERR_HTTP;
    }
    return WF_OK;
}

/* Build the request headers (auth + proxy + optional content-type) for one
 * attempt. These carry header values; wf_xrpc_perform renders them onto the
 * wire. */
static wf_status wf_xrpc_build_headers(wf_xrpc_client *client, int is_post,
                                       const char *content_type,
                                       wf_http_header **out_headers,
                                       size_t *out_count) {
    wf_http_header *arr;
    size_t slots;
    size_t n;

    slots = 0;
    if (client->auth_header != NULL) slots++;
    if (client->proxy_header != NULL) slots++;
    if (is_post && content_type != NULL) slots++;
    if (slots == 0) slots = 1;
    arr = (wf_http_header *)calloc(slots, sizeof(*arr));
    if (arr == NULL) return WF_ERR_ALLOC;

    n = 0;
    if (client->auth_header != NULL) {
        arr[n].name = "Authorization";
        arr[n].value = client->auth_header + strlen("Authorization: ");
        n++;
    }
    if (client->proxy_header != NULL) {
        arr[n].name = "atproto-proxy";
        arr[n].value = client->proxy_header + strlen("atproto-proxy: ");
        n++;
    }
    if (is_post && content_type != NULL) {
        arr[n].name = "Content-Type";
        arr[n].value = content_type;
        n++;
    }
    *out_headers = arr;
    *out_count = n;
    return WF_OK;
}

/* Shared request path for the query (GET) and procedure (POST) variants, with
 * one refresh+retry on an expired token. */
static wf_status wf_xrpc_request(wf_xrpc_client *client, const char *nsid,
                                 const char *query_string, const void *body,
                                 size_t body_len, const char *content_type,
                                 int is_post, wf_response *out) {
    int had_auth;
    int attempt;
    wf_status status;
    char *url;

    if (client == NULL || nsid == NULL || out == NULL ||
        (body_len > 0 && body == NULL) || (is_post && content_type == NULL))
        return WF_ERR_INVALID_ARG;

    had_auth = client->auth_header != NULL;
    status = WF_OK;
    url = NULL;

    for (attempt = 0;; attempt++) {
        wf_http_header *headers;
        size_t hcount;

        free(url);
        url = mac9_concat3(client->base_url, "/xrpc/", nsid,
                           WF_XRPC_MAC9_MAX_URL);
        if (url == NULL) return WF_ERR_ALLOC;

        if (query_string != NULL && query_string[0] != '\0') {
            char *full;

            /* The query string is caller-supplied and unbounded, so length is
             * checked before it is combined rather than after. */
            if (strlen(url) + strlen(query_string) + 1 >=
                WF_XRPC_MAC9_MAX_URL) {
                free(url);
                return WF_ERR_INVALID_ARG;
            }
            full = mac9_concat3(url, "?", query_string, WF_XRPC_MAC9_MAX_URL);
            free(url);
            url = full;
            if (url == NULL) return WF_ERR_ALLOC;
        }

        headers = NULL;
        hcount = 0;
        status = wf_xrpc_build_headers(client, is_post, content_type, &headers,
                                       &hcount);
        if (status != WF_OK) break;

        status =
            wf_xrpc_perform(client, is_post ? "POST" : "GET", url, content_type,
                            body, body_len, headers, hcount, out);
        free(headers);

        /* One automatic refresh+retry when an authenticated request was
         * rejected as expired. The refreshing flag stops the callback's own
         * requests from recursing into another refresh. */
        if (attempt == 0 && had_auth && client->refresh_cb != NULL &&
            !client->refreshing && wf_xrpc_response_is_expired(out)) {
            wf_status refreshed;

            client->refreshing = 1;
            refreshed = client->refresh_cb(client->refresh_userdata);
            client->refreshing = 0;
            if (refreshed == WF_OK) {
                wf_response_free(out);
                continue;
            }
        }
        break;
    }

    free(url);
    return status;
}

/* ── Public API ─────────────────────────────────────────────────────── */

wf_xrpc_client *wf_xrpc_client_new(const char *service_base_url) {
    wf_xrpc_client *client;

    if (service_base_url == NULL || service_base_url[0] == '\0') return NULL;
    client = (wf_xrpc_client *)calloc(1, sizeof(*client));
    if (client == NULL) return NULL;
    client->base_url = wf_normalise_base(service_base_url);
    if (client->base_url == NULL) {
        free(client);
        return NULL;
    }
    return client;
}

void wf_xrpc_client_free(wf_xrpc_client *client) {
    if (client == NULL) return;
    free(client->base_url);
    free(client->auth_header);
    free(client->proxy_header);
    free(client->last_error);
    free(client);
}

const char *wf_xrpc_last_error(const wf_xrpc_client *client) {
    return client != NULL ? client->last_error : NULL;
}

void wf_xrpc_set_handler(wf_xrpc_client *client, wf_xrpc_handler_fn fn,
                         void *userdata) {
    if (client == NULL) return;
    client->handler = fn;
    client->handler_userdata = userdata;
}

void wf_xrpc_client_set_auth(wf_xrpc_client *client, const char *access_jwt) {
    char *header;

    if (client == NULL) return;
    header = NULL;
    if (access_jwt != NULL)
        header = mac9_concat3("Authorization: Bearer ", access_jwt, "",
                              WF_XRPC_MAC9_MAX_FIELD * 8);
    free(client->auth_header);
    client->auth_header = header;
}

wf_status wf_xrpc_client_set_proxy(wf_xrpc_client *client,
                                   const char *proxy_did) {
    char *header;

    if (client == NULL) return WF_ERR_INVALID_ARG;
    header = NULL;
    if (proxy_did != NULL)
        header = mac9_concat3("atproto-proxy: ", proxy_did, "",
                              WF_XRPC_MAC9_MAX_FIELD * 8);
    if (proxy_did != NULL && header == NULL) return WF_ERR_ALLOC;
    free(client->proxy_header);
    client->proxy_header = header;
    return WF_OK;
}

wf_status wf_xrpc_client_set_base_url(wf_xrpc_client *client,
                                      const char *service_base_url) {
    char *normalised;

    if (client == NULL || service_base_url == NULL ||
        service_base_url[0] == '\0')
        return WF_ERR_INVALID_ARG;
    normalised = wf_normalise_base(service_base_url);
    if (normalised == NULL) return WF_ERR_ALLOC;
    free(client->base_url);
    client->base_url = normalised;
    return WF_OK;
}

void wf_xrpc_client_set_refresh_handler(wf_xrpc_client *client,
                                        wf_xrpc_refresh_fn fn, void *userdata) {
    if (client == NULL) return;
    client->refresh_cb = fn;
    client->refresh_userdata = userdata;
}

void wf_xrpc_client_set_max_response_bytes(wf_xrpc_client *client,
                                           size_t max_bytes) {
    if (client == NULL) return;
    /* 0 restores the transport maximum. A larger request is clamped rather
     * than honoured, because the constant is what bounds every allocation this
     * transport makes on a memory-constrained machine. */
    if (max_bytes == 0 || max_bytes > WF_XRPC_MAC9_MAX_RESPONSE_BYTES)
        max_bytes = WF_XRPC_MAC9_MAX_RESPONSE_BYTES;
    client->max_response_bytes = max_bytes;
}

void wf_xrpc_client_set_https_only(wf_xrpc_client *client, int https_only) {
    if (client == NULL) return;
    client->https_only = https_only;
}

char *wf_xrpc_get_base_url(wf_xrpc_client *client) {
    if (client == NULL || client->base_url == NULL) return NULL;
    return mac9_strdup(client->base_url);
}

void wf_response_free(wf_response *res) {
    if (res == NULL) return;
    free(res->body);
    free(res->dpop_nonce);
    free(res->location);
    res->body = NULL;
    res->body_len = 0;
    res->dpop_nonce = NULL;
    res->location = NULL;
    res->status = 0;
}

wf_status wf_xrpc_query(wf_xrpc_client *client, const char *nsid,
                        const char *query_string, wf_response *out) {
    return wf_xrpc_request(client, nsid, query_string, NULL, 0, NULL, 0, out);
}

wf_status wf_xrpc_query_params(wf_xrpc_client *client, const char *nsid,
                               const wf_xrpc_param *params, size_t param_count,
                               wf_response *out) {
    char **names;
    char **values;
    char *query;
    size_t query_len;
    size_t off;
    size_t i;
    wf_status status;

    if (client == NULL || nsid == NULL || out == NULL ||
        (param_count > 0 && params == NULL))
        return WF_ERR_INVALID_ARG;
    if (param_count == 0) return wf_xrpc_query(client, nsid, NULL, out);

    query_len = 1;
    status = WF_OK;
    names = (char **)calloc(param_count, sizeof(*names));
    values = (char **)calloc(param_count, sizeof(*values));
    if (names == NULL || values == NULL) {
        free(names);
        free(values);
        return WF_ERR_ALLOC;
    }
    for (i = 0; i < param_count; i++) {
        if (params[i].name == NULL || params[i].value == NULL) {
            status = WF_ERR_INVALID_ARG;
            break;
        }
        names[i] = wf_url_encode(params[i].name);
        values[i] = wf_url_encode(params[i].value);
        if (names[i] == NULL || values[i] == NULL) {
            status = WF_ERR_ALLOC;
            break;
        }
        query_len += strlen(names[i]) + 1 + strlen(values[i]);
        if (i > 0) query_len++;
    }

    query = NULL;
    off = 0;
    if (status == WF_OK) {
        query = (char *)malloc(query_len);
        if (query == NULL) status = WF_ERR_ALLOC;
    }
    if (status == WF_OK) {
        /* Each pair is appended by hand. The desktop transport builds this
         * string with snprintf(query + off, ...), which is unavailable here. */
        for (i = 0; i < param_count; i++) {
            if (i > 0) query[off++] = '&';
            memcpy(query + off, names[i], strlen(names[i]));
            off += strlen(names[i]);
            query[off++] = '=';
            memcpy(query + off, values[i], strlen(values[i]));
            off += strlen(values[i]);
        }
        query[off] = '\0';
    }
    for (i = 0; i < param_count; i++) {
        free(names[i]);
        free(values[i]);
    }
    free(names);
    free(values);

    if (status == WF_OK) status = wf_xrpc_query(client, nsid, query, out);
    free(query);
    return status;
}

wf_status wf_xrpc_procedure(wf_xrpc_client *client, const char *nsid,
                            const char *json_body, wf_response *out) {
    size_t body_len;

    if (client == NULL || nsid == NULL || out == NULL)
        return WF_ERR_INVALID_ARG;
    body_len = json_body != NULL ? strlen(json_body) : 0;
    return wf_xrpc_request(client, nsid, NULL, json_body, body_len,
                           "application/json", 1, out);
}

wf_status wf_xrpc_upload_blob(wf_xrpc_client *client, const char *nsid,
                              const void *data, size_t data_len,
                              const char *content_type, wf_response *out) {
    if (client == NULL || nsid == NULL || data == NULL || data_len == 0 ||
        content_type == NULL || content_type[0] == '\0' || out == NULL)
        return WF_ERR_INVALID_ARG;
    return wf_xrpc_upload_blob_with_headers(client, nsid, data, data_len,
                                            content_type, NULL, 0, out);
}

wf_status wf_xrpc_upload_blob_with_headers(
    wf_xrpc_client *client, const char *nsid, const void *data, size_t data_len,
    const char *content_type, const wf_http_header *headers,
    size_t header_count, wf_response *out) {
    wf_http_header *all;
    size_t total;
    size_t i;
    char *url;
    wf_status status;

    if (client == NULL || nsid == NULL || data == NULL || data_len == 0 ||
        content_type == NULL || content_type[0] == '\0' || out == NULL ||
        (header_count != 0 && headers == NULL))
        return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    url = mac9_concat3(client->base_url, "/xrpc/", nsid, WF_XRPC_MAC9_MAX_URL);
    if (url == NULL) return WF_ERR_ALLOC;

    /* Combine caller headers with the standard Content-Type. */
    total = header_count + 1;
    all = (wf_http_header *)calloc(total, sizeof(*all));
    if (all == NULL) {
        free(url);
        return WF_ERR_ALLOC;
    }
    all[0].name = "Content-Type";
    all[0].value = content_type;
    for (i = 0; i < header_count; i++) all[1 + i] = headers[i];

    status = wf_xrpc_perform(client, "POST", url, content_type, data, data_len,
                             all, total, out);
    free(all);
    free(url);
    return status;
}

wf_status wf_http_get_with_headers(wf_xrpc_client *client, const char *url,
                                   const wf_http_header *extra,
                                   size_t extra_count, wf_response *out) {
    if (client == NULL || url == NULL || out == NULL ||
        (extra_count != 0 && extra == NULL))
        return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    return wf_xrpc_perform(client, "GET", url, NULL, NULL, 0, extra,
                           extra_count, out);
}

wf_status wf_http_get(wf_xrpc_client *client, const char *url,
                      wf_response *out) {
    return wf_http_get_with_headers(client, url, NULL, 0, out);
}

wf_status wf_http_get_limited(wf_xrpc_client *client, const char *url,
                              size_t max_bytes, wf_response *out) {
    size_t saved;
    size_t effective;
    wf_status status;

    if (client == NULL || url == NULL || out == NULL) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));

    effective = max_bytes;
    if (effective == 0 || effective > WF_XRPC_MAC9_MAX_RESPONSE_BYTES)
        effective = WF_XRPC_MAC9_MAX_RESPONSE_BYTES;

    /* This transport reads every response through one bounded reader, so a
     * per-request cap is applied by narrowing the client's cap for the duration
     * of the call and restoring it afterwards. The transport drives one
     * connection at a time on a single-threaded event loop, so there is no
     * concurrent request to observe the temporary value. */
    saved = client->max_response_bytes;
    client->max_response_bytes = effective;
    status = wf_http_get_with_headers(client, url, NULL, 0, out);
    client->max_response_bytes = saved;
    return status;
}

wf_status wf_http_post(wf_xrpc_client *client, const char *url,
                       const char *content_type, const char *body,
                       const wf_http_header *extra, size_t extra_count,
                       wf_response *out) {
    if (client == NULL || url == NULL || content_type == NULL || body == NULL ||
        out == NULL || (extra_count != 0 && extra == NULL))
        return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    return wf_xrpc_perform(client, "POST", url, content_type, body,
                           strlen(body), extra, extra_count, out);
}

wf_status wf_xrpc_error(const wf_response *resp, char **out_error,
                        char **out_message) {
    char err_buf[WF_XRPC_MAC9_MAX_FIELD];
    char msg_buf[WF_XRPC_MAC9_MAX_FIELD];
    char *e;
    char *m;

    if (out_error != NULL) *out_error = NULL;
    if (out_message != NULL) *out_message = NULL;
    if (resp == NULL) return WF_ERR_INVALID_ARG;
    if (resp->body == NULL || resp->body_len == 0) return WF_ERR_PARSE;

    err_buf[0] = '\0';
    msg_buf[0] = '\0';
    if (wf_json_find_string(resp->body, resp->body_len, "error", err_buf,
                            sizeof(err_buf)) == 0) {
        /* No usable envelope: either not a JSON object, or no string `error`
         * member. Both are WF_ERR_NOT_FOUND per the published contract. */
        return WF_ERR_NOT_FOUND;
    }

    e = mac9_strdup(err_buf);
    if (e == NULL) return WF_ERR_ALLOC;
    m = NULL;
    if (wf_json_find_string(resp->body, resp->body_len, "message", msg_buf,
                            sizeof(msg_buf)) != 0)
        m = mac9_strdup(msg_buf);

    if (out_error != NULL)
        *out_error = e;
    else
        free(e);
    if (out_message != NULL)
        *out_message = m;
    else
        free(m);
    return WF_OK;
}

/*
 * Same contract as the curl transport's wf_http_get_public, restricted to what
 * this transport can actually guarantee: https only, no redirect following,
 * and no credentials of any kind. Every request carries "Connection: close",
 * so there is no redirect chain whose hops could escape the https-only check.
 *
 * A stack copy of the client with the auth header, refresh callback and last
 * error cleared performs the request, so the caller's client is untouched.
 */
wf_status wf_http_get_public(wf_xrpc_client *client, const char *url,
                             size_t max_bytes, wf_response *out) {
    struct wf_xrpc_client anon;
    size_t saved;
    wf_status status;

    if (client == NULL || url == NULL || out == NULL) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (!wf_url_is_https(url)) return WF_ERR_INVALID_ARG;

    saved = client->max_response_bytes;
    if (max_bytes != 0 && (saved == 0 || max_bytes < saved))
        client->max_response_bytes = max_bytes;

    anon = *client;
    anon.auth_header = NULL;
    anon.proxy_header = NULL;
    anon.refresh_cb = NULL;
    anon.last_error = NULL;
    anon.https_only = 1;

    status = wf_xrpc_perform(&anon, "GET", url, NULL, NULL, 0, NULL, 0, out);

    client->max_response_bytes = saved;
    return status;
}
