/* The games on the console: SD card and game card, with their names and icons from the SMDH. */
#ifndef TITLES_H
#define TITLES_H

#include <3ds.h>

#include "resolve.h"

#define TITLE_ICON 48

typedef struct {
    ConsoleTitle *items;
    u8 (*icons)[TITLE_ICON * TITLE_ICON * 4]; /* RGBA, one per item; all zero when it had none */
    int n;
    bool card_present;
    bool full; /* every item has its name, icon and product code */
} Titles;

/* Fills `t` (freeing what it held) with the games' title IDs and update versions: a moment, from
 * the title database. `full` also reads each game's name, icon and product code - its SMDH and
 * NCCH, about 0.2 s a game on an Old 3DS (11 s for 61 games) - taking them from `known` (may be
 * NULL) for the games it already has, and stops early, not full, once `*cancel` is set. */
void titles_scan(Titles *t, const Titles *known, bool full, volatile bool *cancel);
void titles_free(Titles *t);
/* The console's region as the catalog spells it: "EUR", "USA"... */
const char *titles_console_region(void);
/* The installed version of a title (a CIA translation), or -1 when it is not there. */
int titles_installed_version(u64 title_id);

#endif
