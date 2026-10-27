#include "nav.h"

bool rect_hit(Rect r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* From 3ds-ua's installer (app.c, move_focus), where by centres alone Down from the main
 * button jumped over the two tiles under it to the wide button at the bottom. */
int nav_move(const Target *t, int n, int from, NavDir dir)
{
    if (from < 0 || from >= n)
        return 0;
    const Rect *a = &t[from].r;
    bool vertical = dir == NAV_UP || dir == NAV_DOWN;
    int sign = dir == NAV_UP || dir == NAV_LEFT ? -1 : 1;
    int best = from;
    float best_cost = 1e9f;
    for (int i = 0; i < n; i++) {
        if (i == from)
            continue;
        const Rect *b = &t[i].r;
        float a0 = vertical ? a->y : a->x, a1 = a0 + (vertical ? a->h : a->w);
        float b0 = vertical ? b->y : b->x, b1 = b0 + (vertical ? b->h : b->w);
        float centre = sign * ((b0 + b1) / 2 - (a0 + a1) / 2);
        if (centre <= 1)
            continue; /* not that way */
        float gap = sign > 0 ? b0 - a1 : a0 - b1;
        float c0 = vertical ? a->x : a->y, c1 = c0 + (vertical ? a->w : a->h);
        float d0 = vertical ? b->x : b->y, d1 = d0 + (vertical ? b->w : b->h);
        bool shares_band = d0 < c1 && c0 < d1;
        if (!vertical && !shares_band)
            continue; /* left and right stay in the row: a full-width button has no neighbours */
        float across = (d0 + d1) / 2 - (c0 + c1) / 2;
        float cost = (shares_band ? 0 : 10000) + (gap > 0 ? gap : 0) * 10 + (across < 0 ? -across : across);
        if (cost < best_cost) {
            best_cost = cost;
            best = i;
        }
    }
    return best;
}
