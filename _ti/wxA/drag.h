/*
 * drag.h -- turn a finger dragged along a list into whole rows to scroll.
 *
 * A touch screen gives a client a position every frame. This keeps the part
 * that is the same on every platform: the slop that separates a tap from a
 * drag, and the carry of leftover pixels, so a slow drag still moves a row once
 * it has travelled one row's height and nothing is lost between frames. What a
 * row is (pixels, card heights) and what scrolling does stay with the client.
 *
 * Pure C89, no allocation, no clock.
 */

#ifndef WOLFRAM_DRAG_H
#define WOLFRAM_DRAG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Pixels a touch must travel before it is a drag and not a tap. */
#define WF_DRAG_SLOP 8

typedef struct wf_drag {
    int active;   /* a touch is down */
    int dragging; /* it has travelled past WF_DRAG_SLOP */
    int start_y;
    int last_y;
    int carry; /* pixels travelled that have not made a whole row yet */
} wf_drag;

/* A touch went down at `y`. */
void wf_drag_begin(wf_drag *d, int y);

/*
 * The touch is now at `y`; `step` is the height of one row in the same units
 * (at least 1). Returns how many whole rows to scroll: positive when the finger
 * moved up (the content follows it, so later rows come into view), negative
 * when it moved down, 0 while the touch is still a tap or has not travelled a
 * row. Rows that would have been owed before the slop was passed are paid out
 * the moment it is, so the content does not lag the finger by the slop.
 */
int wf_drag_move(wf_drag *d, int y, int step);

/* The touch ended. Returns nonzero if it was a drag, which the caller should
 * not also treat as a tap. */
int wf_drag_end(wf_drag *d);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_DRAG_H */
