# QR codes

[`wolfram/qr.h`](../include/wolfram/qr.h) encodes a string as a QR code and hands back the module matrix, so each client draws it its own way: filled rectangles, a texture, characters. The consoles can't open a browser, and a code on a link page lets someone scan it with a phone instead of typing a URL (Cobalt's use, [cobalt#101](https://github.com/ewanc26/cobalt/issues/101)).

```c
uint8_t *modules; int size;
if (wf_qr_encode("https://bsky.app/profile/ewancroft.uk", WF_QR_ECC_M, &modules, &size) == WF_OK) {
    /* modules[y * size + x] is 1 for dark. Draw at least a 4-module light border. */
    free(modules);
}
```

What it does: byte mode only, versions 1 to 10 (up to 271 bytes at level L, 122 at M, 86 at Q, 62 at H), levels L, M, Q and H, the smallest version that fits, and the mask with the lowest penalty under ISO/IEC 18004. Longer data returns `WF_ERR_INVALID_ARG`. It allocates only the returned matrix, uses no libc beyond `malloc` and `memcpy`, and builds on every target.

What it doesn't: numeric or alphanumeric modes (a URL in byte mode is a little bigger than in alphanumeric, which is fine at these sizes), versions above 10, ECI, structured append, Micro QR, or a quiet zone: the border is the caller's.

## How it is checked

I couldn't check a QR encoder against "it looks like one". Every version from 1 to 10 at every level was encoded for payloads of many lengths, drawn with a four-module quiet zone, and decoded back to the exact text with [zxing-cpp](https://github.com/zxing-cpp/zxing-cpp), an independent decoder: 88 of 92 round trips decoded and the other four were the empty string, which a decoder has no text to report. `test/vectors/qr.json` pins eight of those matrices (covering all four levels, versions 1, 3, 5, 7, 8 and 10, a version with version-information bits, and UTF-8 bytes), and `test/test_qr.c` checks the encoder reproduces them exactly, that each has the finder, timing and dark-module structure, and that version selection steps up at the right byte counts. The zxing check is not part of CI because it needs a Python package; rerun it if you change the encoder.
