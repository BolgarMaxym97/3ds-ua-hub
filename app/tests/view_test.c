/* Host test of source/view.c: tabs, the kind filter and the three sort orders. */
#include <stdio.h>
#include <string.h>

#include "../source/view.h"

static int failures;

static void expect(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

static CatItem item(const char *title, Kind kind, int installs)
{
    CatItem it;
    memset(&it, 0, sizeof it);
    strcpy(it.title, title);
    it.kind = kind;
    it.installs = installs;
    return it;
}

int main(void)
{
    /* in the catalog's order: newest first */
    CatItem items[] = {
        item("Zelda: Ocarina of Time 3D", KIND_PATCH, 5),
        item("Super Mario 3D Land", KIND_PATCH, 40),
        item("Гра у форматі CIA", KIND_CIA, 2),
        item("animal Crossing: New Leaf", KIND_PATCH, 40),
    };
    Catalog cat = {.items = items, .n = 4};
    ItemState states[] = {ITEM_UPDATE, ITEM_INSTALLED, ITEM_INSTALLED, ITEM_NEW};
    int out[4];

    ViewOpts all = {TAB_ALL, KINDS_ALL, SORT_NEW};
    int n = view_build(&cat, states, &all, out);
    expect(n == 4 && out[0] == 0 && out[1] == 1 && out[2] == 2 && out[3] == 3, "all: catalog order, newest first");

    ViewOpts installed = {TAB_INSTALLED, KINDS_ALL, SORT_NEW};
    n = view_build(&cat, states, &installed, out);
    expect(n == 3 && out[0] == 0 && out[1] == 1 && out[2] == 2, "installed: includes the ones with updates");

    ViewOpts updates = {TAB_UPDATES, KINDS_ALL, SORT_NEW};
    n = view_build(&cat, states, &updates, out);
    expect(n == 1 && out[0] == 0, "updates: only the ones with a newer version");

    ViewOpts cia = {TAB_ALL, KINDS_CIA, SORT_NEW};
    n = view_build(&cat, states, &cia, out);
    expect(n == 1 && out[0] == 2, "kind: CIA only");

    ViewOpts patches = {TAB_ALL, KINDS_PATCH, SORT_NEW};
    expect(view_build(&cat, states, &patches, out) == 3, "kind: patches only");

    ViewOpts by_name = {TAB_ALL, KINDS_ALL, SORT_NAME};
    n = view_build(&cat, states, &by_name, out);
    expect(n == 4 && out[0] == 3 && out[1] == 1 && out[2] == 0 && out[3] == 2,
           "name: A-Z, case does not matter, Cyrillic after Latin");

    ViewOpts popular = {TAB_ALL, KINDS_ALL, SORT_POPULAR};
    n = view_build(&cat, states, &popular, out);
    expect(n == 4 && out[0] == 3 && out[1] == 1 && out[2] == 0 && out[3] == 2, "popular: most installs, ties by name");

    expect(view_count(&cat, states, &all, TAB_INSTALLED) == 3 && view_count(&cat, states, &all, TAB_UPDATES) == 1,
           "count: per tab");
    expect(view_count(&cat, states, &cia, TAB_INSTALLED) == 1, "count: tabs respect the kind filter");

    items[2].pinned = true; /* the CIA, last by name and by popularity */
    n = view_build(&cat, states, &by_name, out);
    expect(n == 4 && out[0] == 2 && out[1] == 3 && out[2] == 1 && out[3] == 0, "pinned: first by name, the rest A-Z");
    n = view_build(&cat, states, &popular, out);
    expect(n == 4 && out[0] == 2 && out[1] == 3, "pinned: first by popularity");
    n = view_build(&cat, states, &all, out);
    expect(n == 4 && out[0] == 2 && out[1] == 0 && out[2] == 1 && out[3] == 3, "pinned: first by newest");
    items[2].pinned = false;

    Catalog empty = {.items = NULL, .n = 0};
    expect(view_build(&empty, states, &all, out) == 0, "empty catalog");

    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures != 0;
}
