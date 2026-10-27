/* Host test of the D-pad focus (source/nav.c) on the layouts of the approved design. */
#include <stdio.h>

#include "../source/nav.h"

static int failures;

static void expect(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

int main(void)
{
    /* Catalog, bottom screen: three tabs, then the list rows (B1 in the design). */
    const Target catalog[] = {
        {{30, 9, 86, 19}, 9},   /* 0 tab Усі */
        {{118, 9, 86, 19}, 9},  /* 1 tab Встановлені */
        {{206, 9, 86, 19}, 9},  /* 2 tab Оновлення */
        {{10, 60, 300, 32}, 9}, /* 3 row */
        {{10, 94, 300, 32}, 9}, /* 4 row */
        {{10, 128, 300, 32}, 9},/* 5 row */
    };
    int n = sizeof catalog / sizeof *catalog;
    expect(nav_move(catalog, n, 0, NAV_RIGHT) == 1, "tab: right goes to the next tab");
    expect(nav_move(catalog, n, 2, NAV_RIGHT) == 2, "tab: right on the last tab stays");
    expect(nav_move(catalog, n, 1, NAV_DOWN) == 3, "tab: down goes to the first row");
    expect(nav_move(catalog, n, 3, NAV_DOWN) == 4, "row: down goes to the next row");
    expect(nav_move(catalog, n, 5, NAV_DOWN) == 5, "row: down on the last row stays");
    expect(nav_move(catalog, n, 4, NAV_LEFT) == 4, "row: left has nowhere to go");
    expect(nav_move(catalog, n, 3, NAV_UP) == 1, "row: up goes to the tab above its middle");

    /* Installed page (C2): a card, then Run and Remove side by side. */
    const Target page[] = {
        {{14, 140, 145, 48}, 13}, /* 0 Запустити */
        {{163, 140, 145, 48}, 13},/* 1 Видалити */
    };
    expect(nav_move(page, 2, 0, NAV_RIGHT) == 1, "page: right from Run reaches Remove");
    expect(nav_move(page, 2, 1, NAV_LEFT) == 0, "page: left from Remove reaches Run");
    expect(nav_move(page, 2, 0, NAV_DOWN) == 0, "page: down has nowhere to go");

    /* A wide button under two tiles never loses to the far one (3ds-ua's old bug). */
    const Target tiles[] = {
        {{14, 14, 292, 70}, 14},  /* 0 main */
        {{14, 92, 143, 56}, 11},  /* 1 tile */
        {{163, 92, 143, 56}, 11}, /* 2 tile */
        {{14, 156, 292, 32}, 10}, /* 3 wide */
    };
    expect(nav_move(tiles, 4, 0, NAV_DOWN) == 1, "tiles: down from the main button lands on a tile");
    expect(nav_move(tiles, 4, 2, NAV_DOWN) == 3, "tiles: down from a tile lands on the wide button");

    Rect r = {10, 10, 20, 20};
    expect(rect_hit(r, 10, 10) && rect_hit(r, 29, 29) && !rect_hit(r, 30, 10), "hit: edges");
    expect(nav_move(catalog, n, 99, NAV_DOWN) == 0, "a stale focus falls back to the first control");

    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures != 0;
}
