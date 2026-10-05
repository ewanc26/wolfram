/*
 * update_core.c -- version comparison, SHA-256 and download verification.
 * Strict C89: no stdint, no // comments, no mid-block declarations, so the
 * Classic Mac OS target can build it. See wolfram/update.h.
 */

#include "wolfram/update.h"

#include <string.h>

/* ---- versions -------------------------------------------------------- */

typedef struct ver {
    const char *maj, *min, *pat; /* numeric text */
    size_t maj_n, min_n, pat_n;
    const char *pre; /* prerelease text, or NULL */
    size_t pre_n;
} ver;

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}
static int is_ident_char(char c) {
    return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           c == '-';
}

/* Parse one numeric part: digits, no leading zero unless exactly "0". */
static const char *num_part(const char *p, const char **start, size_t *n) {
    const char *s = p;
    while (is_digit(*p)) p++;
    if (p == s || (size_t)(p - s) > 18) return NULL;
    if (*s == '0' && p - s > 1) return NULL;
    *start = s;
    *n = (size_t)(p - s);
    return p;
}

static int parse_ver(const char *s, ver *v) {
    const char *p = s;
    memset(v, 0, sizeof(*v));
    if (!s) return 0;
    p = num_part(p, &v->maj, &v->maj_n);
    if (!p || *p++ != '.') return 0;
    p = num_part(p, &v->min, &v->min_n);
    if (!p || *p++ != '.') return 0;
    p = num_part(p, &v->pat, &v->pat_n);
    if (!p) return 0;
    if (*p == '-') {
        const char *id = ++p;
        const char *q = p;
        /* dot-separated identifiers: non-empty; numeric ones have no leading
         * zero */
        for (;;) {
            const char *st = q;
            int numeric = 1;
            while (is_ident_char(*q)) {
                if (!is_digit(*q)) numeric = 0;
                q++;
            }
            if (q == st) return 0;
            if (numeric && *st == '0' && q - st > 1) return 0;
            if (*q == '.') {
                q++;
                continue;
            }
            break;
        }
        v->pre = id;
        v->pre_n = (size_t)(q - id);
        p = q;
    }
    if (*p == '+') {
        /* build metadata: dot-separated non-empty [0-9A-Za-z-]+, ignored */
        const char *q = ++p;
        for (;;) {
            const char *st = q;
            while (is_ident_char(*q)) q++;
            if (q == st) return 0;
            if (*q == '.') {
                q++;
                continue;
            }
            break;
        }
        p = q;
    }
    return *p == '\0';
}

int wf_update_version_valid(const char *v) {
    ver x;
    return parse_ver(v, &x);
}

/* Compare digit strings without leading zeros as numbers, without overflow. */
static int cmp_num(const char *a, size_t an, const char *b, size_t bn) {
    int c;
    if (an != bn) return an < bn ? -1 : 1;
    c = memcmp(a, b, an);
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

static int ident_numeric(const char *s, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        if (!is_digit(s[i])) return 0;
    return 1;
}

/* semver 2.0.0 section 11: compare dot-separated prerelease identifiers. */
static int cmp_pre(const char *a, size_t an, const char *b, size_t bn) {
    const char *ae = a + an;
    const char *be = b + bn;
    while (a < ae && b < be) {
        const char *ad = a;
        const char *bd = b;
        size_t ai, bi;
        int an_num, bn_num, c;
        while (ad < ae && *ad != '.') ad++;
        while (bd < be && *bd != '.') bd++;
        ai = (size_t)(ad - a);
        bi = (size_t)(bd - b);
        an_num = ident_numeric(a, ai);
        bn_num = ident_numeric(b, bi);
        if (an_num && bn_num) {
            c = cmp_num(a, ai, b, bi);
        } else if (an_num != bn_num) {
            c = an_num ? -1 : 1; /* numeric < alphanumeric */
        } else {
            size_t m = ai < bi ? ai : bi;
            c = memcmp(a, b, m);
            if (c == 0 && ai != bi) c = ai < bi ? -1 : 1;
            c = c < 0 ? -1 : (c > 0 ? 1 : 0);
        }
        if (c) return c;
        a = ad < ae ? ad + 1 : ae;
        b = bd < be ? bd + 1 : be;
        if (a == ae && b != be) return -1; /* fewer fields is lower */
        if (b == be && a != ae) return 1;
    }
    return 0;
}

int wf_update_compare_versions(const char *a, const char *b, int *err) {
    ver x, y;
    int c;
    if (err) *err = 0;
    if (!parse_ver(a, &x) || !parse_ver(b, &y)) {
        if (err) *err = 1;
        return 0;
    }
    c = cmp_num(x.maj, x.maj_n, y.maj, y.maj_n);
    if (c) return c;
    c = cmp_num(x.min, x.min_n, y.min, y.min_n);
    if (c) return c;
    c = cmp_num(x.pat, x.pat_n, y.pat, y.pat_n);
    if (c) return c;
    if (!x.pre && !y.pre) return 0;
    if (!x.pre) return 1; /* a release outranks its prereleases */
    if (!y.pre) return -1;
    return cmp_pre(x.pre, x.pre_n, y.pre, y.pre_n);
}

/* ---- SHA-256 (FIPS 180-4) ------------------------------------------- */

#define M32 0xFFFFFFFFUL
#define ROR(x, n) ((((x) >> (n)) | ((x) << (32 - (n)))) & M32)

static const unsigned long K[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL,
    0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL, 0xd807aa98UL, 0x12835b01UL,
    0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL,
    0xc19bf174UL, 0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL, 0x983e5152UL,
    0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL,
    0x06ca6351UL, 0x14292967UL, 0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL,
    0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL,
    0xd6990624UL, 0xf40e3585UL, 0x106aa070UL, 0x19a4c116UL, 0x1e376c08UL,
    0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL,
    0x682e6ff3UL, 0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL};

static void sha256_block(wf_sha256 *c, const unsigned char *p) {
    unsigned long w[64];
    unsigned long a, b, cc, d, e, f, g, h, t1, t2, s0, s1;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((unsigned long)p[i * 4] << 24) |
               ((unsigned long)p[i * 4 + 1] << 16) |
               ((unsigned long)p[i * 4 + 2] << 8) | (unsigned long)p[i * 4 + 3];
    for (i = 16; i < 64; i++) {
        s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = (w[i - 16] + s0 + w[i - 7] + s1) & M32;
    }
    a = c->state[0];
    b = c->state[1];
    cc = c->state[2];
    d = c->state[3];
    e = c->state[4];
    f = c->state[5];
    g = c->state[6];
    h = c->state[7];
    for (i = 0; i < 64; i++) {
        s1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        t1 = (h + s1 + ((e & f) ^ ((~e & M32) & g)) + K[i] + w[i]) & M32;
        s0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        t2 = (s0 + ((a & b) ^ (a & cc) ^ (b & cc))) & M32;
        h = g;
        g = f;
        f = e;
        e = (d + t1) & M32;
        d = cc;
        cc = b;
        b = a;
        a = (t1 + t2) & M32;
    }
    c->state[0] = (c->state[0] + a) & M32;
    c->state[1] = (c->state[1] + b) & M32;
    c->state[2] = (c->state[2] + cc) & M32;
    c->state[3] = (c->state[3] + d) & M32;
    c->state[4] = (c->state[4] + e) & M32;
    c->state[5] = (c->state[5] + f) & M32;
    c->state[6] = (c->state[6] + g) & M32;
    c->state[7] = (c->state[7] + h) & M32;
}

void wf_sha256_init(wf_sha256 *c) {
    memset(c, 0, sizeof(*c));
    c->state[0] = 0x6a09e667UL;
    c->state[1] = 0xbb67ae85UL;
    c->state[2] = 0x3c6ef372UL;
    c->state[3] = 0xa54ff53aUL;
    c->state[4] = 0x510e527fUL;
    c->state[5] = 0x9b05688cUL;
    c->state[6] = 0x1f83d9abUL;
    c->state[7] = 0x5be0cd19UL;
}

void wf_sha256_update(wf_sha256 *c, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    size_t i;
    for (i = 0; i < len; i++) {
        c->buf[c->buf_len++] = p[i];
        c->count_lo = (c->count_lo + 1) & M32;
        if (c->count_lo == 0) c->count_hi = (c->count_hi + 1) & M32;
        if (c->buf_len == 64) {
            sha256_block(c, c->buf);
            c->buf_len = 0;
        }
    }
}

void wf_sha256_final(wf_sha256 *c, unsigned char out[WF_SHA256_DIGEST_LEN]) {
    unsigned long hi = c->count_hi, lo = c->count_lo;
    unsigned long bits_hi = ((hi << 3) | (lo >> 29)) & M32;
    unsigned long bits_lo = (lo << 3) & M32;
    unsigned char pad = 0x80;
    unsigned char len[8];
    unsigned char zero = 0;
    int i;
    wf_sha256_update(c, &pad, 1);
    while (c->buf_len != 56) wf_sha256_update(c, &zero, 1);
    len[0] = (unsigned char)(bits_hi >> 24);
    len[1] = (unsigned char)(bits_hi >> 16);
    len[2] = (unsigned char)(bits_hi >> 8);
    len[3] = (unsigned char)bits_hi;
    len[4] = (unsigned char)(bits_lo >> 24);
    len[5] = (unsigned char)(bits_lo >> 16);
    len[6] = (unsigned char)(bits_lo >> 8);
    len[7] = (unsigned char)bits_lo;
    wf_sha256_update(c, len, 8);
    for (i = 0; i < 8; i++) {
        out[i * 4] = (unsigned char)(c->state[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(c->state[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(c->state[i] >> 8);
        out[i * 4 + 3] = (unsigned char)c->state[i];
    }
}

void wf_sha256_buffer(const void *data, size_t len,
                      unsigned char out[WF_SHA256_DIGEST_LEN]) {
    wf_sha256 c;
    wf_sha256_init(&c);
    wf_sha256_update(&c, data, len);
    wf_sha256_final(&c, out);
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

wf_status wf_sha256_from_hex(const char *hex, size_t len,
                             unsigned char out[WF_SHA256_DIGEST_LEN]) {
    size_t i;
    if (!hex || !out || len != 64) return WF_ERR_PARSE;
    for (i = 0; i < 32; i++) {
        int hi = hex_val(hex[i * 2]);
        int lo = hex_val(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            memset(out, 0, WF_SHA256_DIGEST_LEN);
            return WF_ERR_PARSE;
        }
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return WF_OK;
}

/* ---- verification ---------------------------------------------------- */

void wf_update_verify_init(wf_update_verify *v, const wf_update_asset *asset) {
    memset(v, 0, sizeof(*v));
    wf_sha256_init(&v->sha);
    if (asset) {
        memcpy(v->expected, asset->sha256, WF_SHA256_DIGEST_LEN);
        v->want_size = asset->size;
    }
}

wf_status wf_update_verify_feed(wf_update_verify *v, const void *data,
                                size_t len) {
    if (!v || (!data && len)) return WF_ERR_INVALID_ARG;
    if (v->overflow) return WF_ERR_VALIDATION;
    if (len > v->want_size - v->got_size) {
        v->overflow = 1;
        return WF_ERR_VALIDATION;
    }
    wf_sha256_update(&v->sha, data, len);
    v->got_size += (unsigned long)len;
    return WF_OK;
}

wf_status wf_update_verify_final(wf_update_verify *v) {
    unsigned char digest[WF_SHA256_DIGEST_LEN];
    unsigned char diff = 0;
    int i;
    if (!v) return WF_ERR_INVALID_ARG;
    if (v->overflow || v->got_size != v->want_size) return WF_ERR_VALIDATION;
    wf_sha256_final(&v->sha, digest);
    for (i = 0; i < WF_SHA256_DIGEST_LEN; i++)
        diff = (unsigned char)(diff | (digest[i] ^ v->expected[i]));
    return diff == 0 ? WF_OK : WF_ERR_VALIDATION;
}
