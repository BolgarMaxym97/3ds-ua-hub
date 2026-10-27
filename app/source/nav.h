/*
 * Moving between controls with the D-pad: every control on the touch screen can be reached
 * with the D-pad and pressed with A, for consoles whose touch screen no longer reads right.
 * Plain C, no libctru: tests/nav_test.c runs it on the computer.
 */
#ifndef NAV_H
#define NAV_H

#include <stdbool.h>

#define NAV_MAX_TARGETS 24

typedef struct {
    float x, y, w, h;
} Rect;

typedef enum { NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT } NavDir;

/* A control the focus can stand on, and the corner radius its focus ring follows. */
typedef struct {
    Rect r;
    float radius;
} Target;

bool rect_hit(Rect r, int x, int y);

/* The next control in `dir` from `from`, the way the eye goes: the nearest row (or column) by
 * the gap between edges, and in it the one most nearly opposite. Up and down may land on a
 * control that shares no column only when nothing else is there; left and right never leave
 * the row. Returns `from` when nothing lies that way. */
int nav_move(const Target *t, int n, int from, NavDir dir);

#endif
