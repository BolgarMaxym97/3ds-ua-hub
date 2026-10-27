/*
 * Which catalog entries a list shows, and in what order: the tabs (B1, B2), the filters and
 * sorting (B5). Plain C, no libctru: tests/view_test.c runs it on the computer.
 */
#ifndef VIEW_H
#define VIEW_H

#include <stdbool.h>

#include "catalog.h"

typedef enum { TAB_ALL, TAB_INSTALLED, TAB_UPDATES, TAB_COUNT } Tab;
typedef enum { KINDS_ALL, KINDS_PATCH, KINDS_CIA, KINDS_COUNT } KindFilter;
typedef enum { SORT_NEW, SORT_NAME, SORT_POPULAR, SORT_COUNT } SortOrder;

/* What the console has of an entry; until installing exists (step 4) everything is ITEM_NEW. */
typedef enum { ITEM_NEW, ITEM_INSTALLED, ITEM_UPDATE } ItemState;

typedef struct {
    Tab tab;
    KindFilter kinds;
    SortOrder sort;
} ViewOpts;

/* Fills `out` with indices into `cat->items`; returns how many. `states` has one per item. */
int view_build(const Catalog *cat, const ItemState *states, const ViewOpts *opts, int *out);

/* How many entries each tab would show with these filters (for the tab labels). */
int view_count(const Catalog *cat, const ItemState *states, const ViewOpts *opts, Tab tab);

#endif
