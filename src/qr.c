/*
 * qr.c -- QR Code encoder, byte mode, versions 1..10. See wolfram/qr.h.
 *
 * Structure follows ISO/IEC 18004: data bits (mode, count, bytes, terminator,
 * pad), Reed-Solomon error correction per block, interleaving, function
 * patterns, zig-zag placement, then the mask with the lowest penalty score.
 */

#include "wolfram/qr.h"

#include <stdlib.h>
#include <string.h>

#define QR_MAXN WF_QR_MAX_SIZE

/* [ecc][version-1], ecc order L, M, Q, H. */
static const uint8_t ECC_PER_BLOCK[4][WF_QR_MAX_VERSION] = {
    {7, 10, 15, 20, 26, 18, 20, 24, 30, 18},
    {10, 16, 26, 18, 24, 16, 18, 22, 22, 26},
    {13, 22, 18, 26, 18, 24, 18, 22, 20, 24},
    {17, 28, 22, 16, 22, 28, 26, 26, 24, 28}};
static const uint8_t NUM_BLOCKS[4][WF_QR_MAX_VERSION] = {
    {1, 1, 1, 1, 1, 2, 2, 2, 2, 4},
    {1, 1, 1, 2, 2, 4, 4, 4, 5, 5},
    {1, 1, 2, 2, 4, 4, 6, 6, 8, 8},
    {1, 1, 2, 4, 4, 4, 5, 6, 8, 8}};
/* The two format-information bits for each level (L=01, M=00, Q=11, H=10). */
static const int FORMAT_BITS[4] = {1, 0, 3, 2};

typedef struct qr {
    int ver, size;
    uint8_t mod[QR_MAXN * QR_MAXN];  /* 1 = dark */
    uint8_t func[QR_MAXN * QR_MAXN]; /* 1 = function pattern, not data */
} qr;

#define M(q, x, y) ((q)->mod[(y) * (q)->size + (x)])
#define F(q, x, y) ((q)->func[(y) * (q)->size + (x)])

static int raw_data_modules(int ver) {
    int r = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        int na = ver / 7 + 2;
        r -= (25 * na - 10) * na - 55;
        if (ver >= 7) r -= 36;
    }
    return r;
}

static int data_codewords(int ver, int ecc) {
    return raw_data_modules(ver) / 8 -
           ECC_PER_BLOCK[ecc][ver - 1] * NUM_BLOCKS[ecc][ver - 1];
}

/* ---- Reed-Solomon over GF(256), polynomial 0x11D -------------------- */

static uint8_t gf_mul(uint8_t x, uint8_t y) {
    int z = 0;
    for (int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return (uint8_t)z;
}

static void rs_divisor(int degree, uint8_t *out) {
    memset(out, 0, (size_t)degree);
    out[degree - 1] = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; i++) {
        for (int j = 0; j < degree; j++) {
            out[j] = gf_mul(out[j], root);
            if (j + 1 < degree) out[j] ^= out[j + 1];
        }
        root = gf_mul(root, 0x02);
    }
}

static void rs_remainder(const uint8_t *data, int len, const uint8_t *div,
                         int degree, uint8_t *out) {
    memset(out, 0, (size_t)degree);
    for (int i = 0; i < len; i++) {
        uint8_t factor = data[i] ^ out[0];
        memmove(out, out + 1, (size_t)degree - 1);
        out[degree - 1] = 0;
        for (int j = 0; j < degree; j++) out[j] ^= gf_mul(div[j], factor);
    }
}

/* ---- function patterns --------------------------------------------- */

static void set_func(qr *q, int x, int y, int dark) {
    M(q, x, y) = (uint8_t)dark;
    F(q, x, y) = 1;
}

static int align_positions(int ver, int *pos) {
    if (ver == 1) return 0;
    int n = ver / 7 + 2;
    int step = (ver * 4 + n * 2 + 1) / (n * 2 - 2) * 2;
    pos[0] = 6;
    for (int i = n - 1, p = ver * 4 + 10; i >= 1; i--, p -= step) pos[i] = p;
    return n;
}

static void draw_finder(qr *q, int cx, int cy) {
    for (int dy = -4; dy <= 4; dy++)
        for (int dx = -4; dx <= 4; dx++) {
            int x = cx + dx, y = cy + dy;
            if (x < 0 || x >= q->size || y < 0 || y >= q->size) continue;
            int d = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
            set_func(q, x, y, d != 2 && d != 4);
        }
}

static void draw_align(qr *q, int cx, int cy) {
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            int d = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
            set_func(q, cx + dx, cy + dy, d != 1);
        }
}

static void draw_format(qr *q, int ecc, int mask) {
    int data = FORMAT_BITS[ecc] << 3 | mask;
    int rem = data;
    for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    int bits = (data << 10 | rem) ^ 0x5412;
    for (int i = 0; i <= 5; i++) set_func(q, 8, i, (bits >> i) & 1);
    set_func(q, 8, 7, (bits >> 6) & 1);
    set_func(q, 8, 8, (bits >> 7) & 1);
    set_func(q, 7, 8, (bits >> 8) & 1);
    for (int i = 9; i < 15; i++) set_func(q, 14 - i, 8, (bits >> i) & 1);
    for (int i = 0; i < 8; i++)
        set_func(q, q->size - 1 - i, 8, (bits >> i) & 1);
    for (int i = 8; i < 15; i++)
        set_func(q, 8, q->size - 15 + i, (bits >> i) & 1);
    set_func(q, 8, q->size - 8, 1); /* the always-dark module */
}

static void draw_version(qr *q) {
    if (q->ver < 7) return;
    int rem = q->ver;
    for (int i = 0; i < 12; i++) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    int bits = q->ver << 12 | rem;
    for (int i = 0; i < 18; i++) {
        int dark = (bits >> i) & 1;
        int a = q->size - 11 + i % 3, b = i / 3;
        set_func(q, a, b, dark);
        set_func(q, b, a, dark);
    }
}

static void draw_function_patterns(qr *q) {
    for (int i = 0; i < q->size; i++) {
        set_func(q, 6, i, i % 2 == 0);
        set_func(q, i, 6, i % 2 == 0);
    }
    draw_finder(q, 3, 3);
    draw_finder(q, q->size - 4, 3);
    draw_finder(q, 3, q->size - 4);
    int pos[8];
    int n = align_positions(q->ver, pos);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            if ((i == 0 && j == 0) || (i == 0 && j == n - 1) ||
                (i == n - 1 && j == 0))
                continue;
            draw_align(q, pos[i], pos[j]);
        }
    draw_format(q, 0, 0); /* reserve the area; rewritten with the real mask */
    draw_version(q);
}

static void draw_codewords(qr *q, const uint8_t *data, int len) {
    int i = 0;
    for (int right = q->size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < q->size; vert++)
            for (int j = 0; j < 2; j++) {
                int x = right - j;
                int upward = ((right + 1) & 2) == 0;
                int y = upward ? q->size - 1 - vert : vert;
                if (!F(q, x, y) && i < len * 8) {
                    M(q, x, y) = (data[i >> 3] >> (7 - (i & 7))) & 1;
                    i++;
                }
            }
    }
}

static void apply_mask(qr *q, int mask) {
    for (int y = 0; y < q->size; y++)
        for (int x = 0; x < q->size; x++) {
            int inv;
            switch (mask) {
                case 0:
                    inv = (x + y) % 2 == 0;
                    break;
                case 1:
                    inv = y % 2 == 0;
                    break;
                case 2:
                    inv = x % 3 == 0;
                    break;
                case 3:
                    inv = (x + y) % 3 == 0;
                    break;
                case 4:
                    inv = (x / 3 + y / 2) % 2 == 0;
                    break;
                case 5:
                    inv = x * y % 2 + x * y % 3 == 0;
                    break;
                case 6:
                    inv = (x * y % 2 + x * y % 3) % 2 == 0;
                    break;
                default:
                    inv = ((x + y) % 2 + x * y % 3) % 2 == 0;
                    break;
            }
            if (!F(q, x, y) && inv) M(q, x, y) ^= 1;
        }
}

/* ---- penalty (ISO 18004 8.8.2) -------------------------------------- */

static int penalty(const qr *q) {
    int N = q->size, score = 0;
    int dark = 0;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) dark += q->mod[y * N + x];
    for (int pass = 0; pass < 2; pass++) { /* rows, then columns */
        for (int a = 0; a < N; a++) {
            int run = 1;
            uint32_t hist = 0; /* last 11 modules */
            for (int b = 0; b < N; b++) {
                int cur = pass ? q->mod[b * N + a] : q->mod[a * N + b];
                if (b > 0) {
                    int prev =
                        pass ? q->mod[(b - 1) * N + a] : q->mod[a * N + b - 1];
                    if (cur == prev) {
                        run++;
                        if (run == 5)
                            score += 3;
                        else if (run > 5)
                            score++;
                    } else {
                        run = 1;
                    }
                }
                hist = ((hist << 1) | (uint32_t)cur) & 0x7FF;
                if (b >= 10 && (hist == 0x05D || hist == 0x5D0)) score += 40;
            }
        }
    }
    for (int y = 0; y + 1 < N; y++)
        for (int x = 0; x + 1 < N; x++) {
            int c = q->mod[y * N + x];
            if (c == q->mod[y * N + x + 1] && c == q->mod[(y + 1) * N + x] &&
                c == q->mod[(y + 1) * N + x + 1])
                score += 3;
        }
    int total = N * N;
    int k = (abs(dark * 20 - total * 10) + total - 1) / total - 1;
    score += k * 10;
    return score;
}

/* ---- encode --------------------------------------------------------- */

wf_status wf_qr_encode_bytes(const void *data, size_t len, wf_qr_ecc ecc,
                             uint8_t **modules, int *size) {
    if (!modules || !size) return WF_ERR_INVALID_ARG;
    *modules = NULL;
    *size = 0;
    if ((!data && len) || (int)ecc < 0 || (int)ecc > 3)
        return WF_ERR_INVALID_ARG;
    int ver = 0, cap = 0;
    for (int v = 1; v <= WF_QR_MAX_VERSION; v++) {
        int count_bits = v < 10 ? 8 : 16;
        cap = data_codewords(v, ecc);
        if (4 + count_bits + (long)len * 8 <= (long)cap * 8) {
            ver = v;
            break;
        }
    }
    if (!ver) return WF_ERR_INVALID_ARG;
    cap = data_codewords(ver, ecc);

    /* Data bits. */
    uint8_t dc[400];
    memset(dc, 0, sizeof dc);
    int nbits = 0;
#define PUT(val, cnt)                                                          \
    do {                                                                       \
        for (int b_ = (cnt) - 1; b_ >= 0; b_--) {                              \
            if (((val) >> b_) & 1)                                             \
                dc[nbits >> 3] |= (uint8_t)(0x80 >> (nbits & 7));              \
            nbits++;                                                           \
        }                                                                      \
    } while (0)
    PUT(4, 4); /* byte mode 0100 */
    PUT((unsigned)len, ver < 10 ? 8 : 16);
    for (size_t i = 0; i < len; i++) PUT(((const uint8_t *)data)[i], 8);
    int cap_bits = cap * 8;
    int term = cap_bits - nbits < 4 ? cap_bits - nbits : 4;
    nbits += term;
    nbits = (nbits + 7) / 8 * 8;
    for (uint8_t pad = 0xEC; nbits < cap_bits; pad ^= 0xEC ^ 0x11) {
        PUT(pad, 8);
    }
#undef PUT

    /* Error correction, interleaved. */
    int nblocks = NUM_BLOCKS[ecc][ver - 1], eccl = ECC_PER_BLOCK[ecc][ver - 1];
    int raw = raw_data_modules(ver) / 8;
    int nshort = nblocks - raw % nblocks, shortlen = raw / nblocks;
    uint8_t div[40];
    rs_divisor(eccl, div);
    uint8_t all[600];
    int k = 0;
    uint8_t eccbuf[16][40];
    const uint8_t *blk[16];
    int blklen[16];
    for (int i = 0; i < nblocks; i++) {
        int dl = shortlen - eccl + (i < nshort ? 0 : 1);
        blk[i] = dc + k;
        blklen[i] = dl;
        rs_remainder(dc + k, dl, div, eccl, eccbuf[i]);
        k += dl;
    }
    int o = 0;
    for (int i = 0; i <= shortlen - eccl; i++)
        for (int j = 0; j < nblocks; j++)
            if (i < blklen[j]) all[o++] = blk[j][i];
    for (int i = 0; i < eccl; i++)
        for (int j = 0; j < nblocks; j++) all[o++] = eccbuf[j][i];

    qr *q = calloc(1, sizeof *q);
    if (!q) return WF_ERR_ALLOC;
    q->ver = ver;
    q->size = ver * 4 + 17;
    draw_function_patterns(q);
    draw_codewords(q, all, o);

    int best = 0, best_score = 1 << 30;
    for (int m = 0; m < 8; m++) {
        apply_mask(q, m);
        draw_format(q, ecc, m);
        int s = penalty(q);
        if (s < best_score) {
            best = m;
            best_score = s;
        }
        apply_mask(q, m); /* undo (xor) */
    }
    apply_mask(q, best);
    draw_format(q, ecc, best);

    int n = q->size;
    uint8_t *out = malloc((size_t)n * (size_t)n);
    if (!out) {
        free(q);
        return WF_ERR_ALLOC;
    }
    memcpy(out, q->mod, (size_t)n * (size_t)n);
    free(q);
    *modules = out;
    *size = n;
    return WF_OK;
}

wf_status wf_qr_encode(const char *text, wf_qr_ecc ecc, uint8_t **modules,
                       int *size) {
    if (!text) return WF_ERR_INVALID_ARG;
    return wf_qr_encode_bytes(text, strlen(text), ecc, modules, size);
}
