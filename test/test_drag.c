/*
 * test_drag.c -- wf_drag: tap versus drag, rows owed, carry, direction, and
 * argument edges.
 */

#include "wolfram/drag.h"

#include "test.h"

int main(void) {
    wf_drag d;

    /* A touch that stays within the slop is a tap and scrolls nothing. */
    wf_drag_begin(&d, 100);
    WF_CHECK(wf_drag_move(&d, 100, 50) == 0);
    WF_CHECK(wf_drag_move(&d, 100 + WF_DRAG_SLOP - 1, 50) == 0);
    WF_CHECK(wf_drag_move(&d, 100 - WF_DRAG_SLOP + 1, 50) == 0);
    WF_CHECK(wf_drag_end(&d) == 0);

    /* Dragging up one row's height scrolls one row forward, paid out as the
     * finger moves, with the slop distance counted. */
    wf_drag_begin(&d, 200);
    WF_CHECK(wf_drag_move(&d, 195, 50) == 0); /* inside the slop */
    WF_CHECK(wf_drag_move(&d, 180, 50) == 0); /* past it: 20 of 50 px owed */
    WF_CHECK(wf_drag_move(&d, 150, 50) == 1); /* 50 px in total: one row */
    WF_CHECK(wf_drag_move(&d, 150, 50) == 0); /* not moving */
    WF_CHECK(wf_drag_move(&d, 50, 50) == 2);  /* 100 more: two rows */
    WF_CHECK(wf_drag_end(&d) == 1);

    /* Down is negative, and a reversal costs the carry it owes. */
    wf_drag_begin(&d, 100);
    WF_CHECK(wf_drag_move(&d, 160, 50) ==
             -1);                             /* 60 down: one row, 10 carried */
    WF_CHECK(wf_drag_move(&d, 150, 50) == 0); /* the carry back to zero */
    WF_CHECK(wf_drag_move(&d, 100, 50) ==
             1); /* back up 50 px: one row forward */
    WF_CHECK(wf_drag_end(&d) == 1);

    /* Slow drags lose nothing: 1 px at a time still makes a row per 10 px. */
    {
        int y, rows = 0;
        wf_drag_begin(&d, 500);
        for (y = 499; y >= 400; y--) rows += wf_drag_move(&d, y, 10);
        WF_CHECK(rows == 10);
        WF_CHECK(wf_drag_end(&d) == 1);
    }

    /* A new touch starts clean. */
    wf_drag_begin(&d, 10);
    WF_CHECK(wf_drag_move(&d, 12, 5) == 0);
    WF_CHECK(wf_drag_end(&d) == 0);

    /* Without a touch, or with a bad step or NULL, nothing happens. */
    WF_CHECK(wf_drag_move(&d, 0, 10) == 0);
    wf_drag_begin(&d, 0);
    WF_CHECK(wf_drag_move(&d, 100, 0) == 0);
    WF_CHECK(wf_drag_move(&d, 100, -3) == 0);
    WF_CHECK(wf_drag_move(NULL, 0, 10) == 0);
    WF_CHECK(wf_drag_end(NULL) == 0);
    wf_drag_begin(NULL, 0);

    WF_TEST_SUMMARY();
}
