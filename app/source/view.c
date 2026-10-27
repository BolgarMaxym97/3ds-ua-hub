#include "view.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

static bool shown(const CatItem *it, ItemState state, const ViewOpts *opts, Tab tab)
{
    if (tab == TAB_INSTALLED && state == ITEM_NEW)
        return false;
    if (tab == TAB_UPDATES && state != ITEM_UPDATE)
        return false;
    if (opts->kinds == KINDS_PATCH && it->kind != KIND_PATCH)
        return false;
    if (opts->kinds == KINDS_CIA && it->kind != KIND_CIA)
        return false;
    return true;
}

/* qsort has no context argument in C99: the catalog being sorted, for the comparators. */
static const Catalog *sorting;
static SortOrder sort_order;

static int compare(const void *a, const void *b)
{
    int i = *(const int *)a, j = *(const int *)b;
    const CatItem *x = &sorting->items[i], *y = &sorting->items[j];
    if (sort_order == SORT_POPULAR && x->installs != y->installs)
        return x->installs > y->installs ? -1 : 1;
    if (sort_order == SORT_NAME || sort_order == SORT_POPULAR) {
        int c = strcasecmp(x->title, y->title);
        if (c)
            return c;
    }
    return i - j; /* the catalog's own order: newest first, and a stable tie-break */
}

int view_build(const Catalog *cat, const ItemState *states, const ViewOpts *opts, int *out)
{
    int n = 0;
    for (int i = 0; i < cat->n; i++)
        if (shown(&cat->items[i], states[i], opts, opts->tab))
            out[n++] = i;
    sorting = cat;
    sort_order = opts->sort;
    qsort(out, (size_t)n, sizeof *out, compare);
    return n;
}

int view_count(const Catalog *cat, const ItemState *states, const ViewOpts *opts, Tab tab)
{
    int n = 0;
    for (int i = 0; i < cat->n; i++)
        n += shown(&cat->items[i], states[i], opts, tab);
    return n;
}
