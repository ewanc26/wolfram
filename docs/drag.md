# Dragging a list

A client that scrolls a list by dragging a finger down the touch screen needs the same two things however it draws: a way to tell a drag from a tap, and a way to turn pixels into whole rows without losing the pixels left over between frames. [`wolfram/drag.h`](../include/wolfram/drag.h) is that, and nothing else. What a row is (a fixed pitch, a card of varying height), and what scrolling does to the selection, stay with the client.

```c
wf_drag d;
wf_drag_begin(&d, touch_y);              /* the finger went down */
...
int rows = wf_drag_move(&d, touch_y, row_px);  /* every frame it is down */
if (rows) list_move(rows);               /* + : later rows come into view */
...
if (!wf_drag_end(&d)) handle_tap();      /* the finger came up */
```

- A touch is a tap until it has travelled `WF_DRAG_SLOP` (8) pixels. `wf_drag_move` returns 0 until then, and `wf_drag_end` returns 0, so the client can still treat the release as a tap.
- Past the slop it is a drag, and distance is counted from where the finger landed, so the content does not lag the finger by the slop. `wf_drag_end` returns nonzero: do not also treat the release as a tap.
- `wf_drag_move` returns whole rows: positive when the finger moved up (the content follows it, so later rows come into view), negative when it moved down. The leftover pixels are carried, so a slow drag still moves a row each time it has travelled one row's height, and reversing direction pays back what was owed.
- `step` is one row's height in the same units as `y` and must be at least 1; a smaller one, or a NULL `wf_drag`, returns 0.

It is strict C89 with no allocation and no clock, so every target builds it, the Mac OS 9 one included. `test/test_drag.c` covers it.
