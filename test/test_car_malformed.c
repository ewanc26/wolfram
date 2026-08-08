/**
 * test_car_malformed.c — regression coverage for an integer-overflow bug
 * in wf_car_parse's bounds checks (src/repo/car.c).
 *
 * wf_read_varint decodes a LEB128 varint from untrusted bytes into a
 * uint64_t -- up to 10 input bytes, so any value up to UINT64_MAX is
 * attacker-reachable. Both the header-length check (`pos + hdr_len > len`)
 * and the per-block section-length check (`pos + section_len > len`) added
 * that untrusted uint64_t directly to a size_t `pos` without checking for
 * overflow: for a large enough length, the addition itself wraps around
 * (mod 2^64) to a small value, PASSING the bounds check while the decoded
 * length is still enormous.
 *
 * For the header case this is directly, reliably exploitable: a
 * wrapped-through hdr_len is handed to wf_cbor_parse(data+pos, hdr_len) as
 * an upper bound, and if the CBOR bytes there declare a nested byte-string
 * with an inflated length of their own (still well inside hdr_len, so
 * nothing rejects it), libcbor's decoder reads that many bytes from a
 * buffer nowhere near that large. Confirmed empirically with
 * AddressSanitizer against the pre-fix code -- test_header_length_overflow
 * below is exactly that payload, and reproduces a real
 * stack-buffer-overflow (100000-byte read from a 47-byte buffer) via
 * cbor_builder_byte_string_callback <- cbor_stream_decode <- cbor_load <-
 * wf_cbor_parse <- wf_car_parse. This is reachable from any CAR the SDK
 * parses from the network: firehose commit blocks,
 * com.atproto.sync.getRepo/getRecord responses, and repo import.
 *
 * The section-length site (test_section_length_overflow) has the same
 * broken arithmetic but a narrower blast radius on this specific 64-bit
 * build: the only section_len values that actually trigger the wraparound
 * are themselves close to UINT64_MAX (since pos is always small in
 * practice), and the code's very next step -- malloc(section_len - 36) --
 * reliably fails for a request that size, short-circuiting before the
 * out-of-bounds memcpy that would otherwise follow. That's incidental
 * protection, not a reason to leave the check wrong: on a 32-bit size_t
 * build (this SDK targets Wii/3DS among other embedded platforms), the
 * same wraparound is reachable with a section_len small enough to
 * successfully allocate, restoring the direct exploit. Fixed the same way
 * either way.
 *
 * Both fixed by rewriting the check as `length > len - pos` (subtraction,
 * which can't overflow since pos <= len always holds at that point)
 * instead of `pos + length > len` (addition, which can).
 */

#include "wolfram/repo/car.h"

#include "test.h"

#include <stdlib.h>
#include <string.h>

/* A 10-byte LEB128 varint decoding to UINT64_MAX: nine continuation bytes
 * (0xFF, contributing 7 set bits each = bits 0-62) followed by a
 * terminator byte (0x01, contributing bit 63). */
static const unsigned char HUGE_VARINT[10] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                              0xFF, 0xFF, 0xFF, 0xFF, 0x01};

static void test_header_length_overflow(void) {
    /* The huge varint as hdr_len, then a CBOR byte-string (major type 2,
     * 0x5A = 4-byte length prefix follows) declaring 100000 bytes -- well
     * within the wrapped-through hdr_len "budget", but the real buffer
     * has only a handful of bytes left. Run this test under ASan for the
     * strongest signal (that's how the bug was originally confirmed); the
     * fixed code rejects the input long before wf_cbor_parse ever sees
     * it, so there's nothing for ASan to catch either way once fixed. */
    unsigned char bstr_hdr[] = {0x5A, 0x00, 0x01, 0x86,
                                0xA0}; /* bytes(100000) */
    unsigned char car[10 + sizeof(bstr_hdr) + 32];
    memcpy(car, HUGE_VARINT, sizeof(HUGE_VARINT));
    memcpy(car + sizeof(HUGE_VARINT), bstr_hdr, sizeof(bstr_hdr));
    memset(car + sizeof(HUGE_VARINT) + sizeof(bstr_hdr), 0x41,
           sizeof(car) - sizeof(HUGE_VARINT) - sizeof(bstr_hdr));

    wf_car out;
    wf_status status = wf_car_parse(car, sizeof(car), &out);
    WF_CHECK(status == WF_ERR_INVALID_ARG);
    if (status == WF_OK) wf_car_free(&out);
}

static void test_section_length_overflow(void) {
    /* A minimal but genuinely valid CAR header (version 1, empty roots
     * array), so parsing reaches the block loop, then the huge varint as
     * a block's section length. */
    static const unsigned char valid_header[] = {
        0x11, /* header length: 17 bytes follow */
        0xa2, /* map(2) */
        0x67, 'v', 'e', 'r', 's', 'i', 'o', 'n', /* "version" */
        0x01,                                    /* 1 */
        0x65, 'r', 'o', 'o', 't', 's',           /* "roots" */
        0x80,                                    /* array(0) */
    };
    unsigned char car[sizeof(valid_header) + 10 + 32];
    memcpy(car, valid_header, sizeof(valid_header));
    memcpy(car + sizeof(valid_header), HUGE_VARINT, sizeof(HUGE_VARINT));
    memset(car + sizeof(valid_header) + sizeof(HUGE_VARINT), 0,
           sizeof(car) - sizeof(valid_header) - sizeof(HUGE_VARINT));

    wf_car out;
    wf_status status = wf_car_parse(car, sizeof(car), &out);
    WF_CHECK(status == WF_ERR_INVALID_ARG);
    if (status == WF_OK) wf_car_free(&out);
}

static void test_varint_overflow_rejected(void) {
    /* A 10-byte varint whose final byte is 0x02 contributes bit 63 and then
     * a lost bit -- 0x02 is 10 in binary, so decoding without an overflow
     * guard would silently drop that bit and produce UINT64_MAX with a
     * "successful" 10-byte read. The reader must reject the byte outright
     * (as wf_cbor_varint does) rather than misdecode it. */
    static const unsigned char overflowing[10] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                  0xFF, 0xFF, 0xFF, 0xFF, 0x02};
    wf_car out;
    wf_status status = wf_car_parse(overflowing, sizeof(overflowing), &out);
    WF_CHECK(status == WF_ERR_INVALID_ARG);
    if (status == WF_OK) wf_car_free(&out);
}

static void test_varint_nonminimal_rejected(void) {
    /* Header length 1 encoded non-minimally (0x80 0x00 = 0x80 | (0 << 7),
     * which is 1 in LEB128 but with a redundant continuation byte). The
     * SDK's canonical encoding never produces this; accepting it would let
     * an attacker smuggle ambiguous length encodings into the parser. */
    static const unsigned char nonminimal[4] = {0x80, 0x00, 0xa0, 0x00};
    wf_car out;
    wf_status status = wf_car_parse(nonminimal, sizeof(nonminimal), &out);
    WF_CHECK(status == WF_ERR_INVALID_ARG);
    if (status == WF_OK) wf_car_free(&out);

    /* An unterminated varint (10 continuation bytes) is also malformed. */
    static const unsigned char unterminated[10] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    status = wf_car_parse(unterminated, sizeof(unterminated), &out);
    WF_CHECK(status == WF_ERR_INVALID_ARG);
    if (status == WF_OK) wf_car_free(&out);
}

int main(void) {
    test_header_length_overflow();
    test_section_length_overflow();
    test_varint_overflow_rejected();
    test_varint_nonminimal_rejected();
    WF_TEST_SUMMARY();
}
