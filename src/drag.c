/* drag.c -- wf_drag_* (see drag.h). C89. */

#include "wolfram/drag.h"

static int iabs(int v) {
    return v < 0 ? -v : v;
}

void wf_drag_begin(wf_drag *d, int y) {
    if (!d) return;
    d->active = 1;
    d->dragging = 0;
    d->start_y = y;
    d->last_y = y;
    d->carry = 0;
}

int wf_drag_move(wf_drag *d, int y, int step) {
    int rows;

    if (!d || !d->active || step < 1) return 0;
    if (!d->dragging) {
        if (iabs(y - d->start_y) < WF_DRAG_SLOP) return 0;
        d->dragging = 1;
        /* Pay out from where the finger landed, not from where it crossed the
         * slop, so the content does not lag the finger by the slop. */
        d->last_y = d->start_y;
    }
    d->carry += d->last_y - y;
    d->last_y = y;
    rows = d->carry / step; /* truncates toward zero */
    d->carry -= rows * step;
    return rows;
}

int wf_drag_end(wf_drag *d) {
    int was;

    if (!d) return 0;
    was = d->dragging;
    d->active = 0;
    d->dragging = 0;
    d->carry = 0;
    return was;
}
