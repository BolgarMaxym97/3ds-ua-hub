/*
 * Which variant of a translation fits this console (IDEA.md, «Пошук гри для патча»): by the
 * game's Title ID, else the one the person picks by hand. Plain C, no libctru:
 * tests/resolve_test.c runs it on the computer.
 */
#ifndef RESOLVE_H
#define RESOLVE_H

#include <stdbool.h>
#include <stdint.h>

#include "catalog.h"

#define UPDATE_NONE -1 /* no update title installed */

/* A game on the console, as titles.c finds it. */
typedef struct {
    uint64_t title_id;
    uint8_t media;          /* MEDIATYPE_SD or MEDIATYPE_GAME_CARD */
    char product[16];       /* "CTR-P-AREP" */
    int update_version;     /* its update title's version, or UPDATE_NONE */
    char name[64];          /* from its SMDH, UTF-8 */
} ConsoleTitle;

typedef enum {
    MATCH_FOUND,        /* one variant's game is here, at the version it needs */
    MATCH_SEVERAL,      /* games of several variants are here, none of the console's region: ask */
    MATCH_NOT_FOUND,    /* none of its games (D4) */
    MATCH_WRONG_VERSION,/* the game is here, its update is not the one the patch needs (G1) */
} MatchResult;

typedef struct {
    int variant;            /* index into item->variants */
    int title;              /* index into the titles, -1 for a CIA */
    int candidates[CAT_MAX_VARIANTS]; /* MATCH_SEVERAL: the variants whose games are here */
    int n_candidates;
} Match;

/* A patch, automatically: the variant whose Title ID is installed. `console_region` ("EUR")
 * settles a tie. */
MatchResult resolve_patch(const CatItem *item, const ConsoleTitle *titles, int n, const char *console_region, Match *out);

/* A patch on a game picked by hand: the variant of that game's region (by its product code),
 * else -1 when the translation has none for that region. */
int resolve_manual(const CatItem *item, const ConsoleTitle *title);

/* A CIA: the variant of the console's region, else the first. */
int resolve_cia(const CatItem *item, const char *console_region);

/* A CIA put on the console without the Hub (FBI, Universal-Updater): the variant whose title is
 * installed at exactly the catalog's version, else -1. `installed_version` gives a title's AM
 * version, or -1 when it is not there. Only an exact match counts: a translated CIA often keeps
 * the original game's Title ID, and the original must not pass for the translation. A CIA with SD
 * data is never adopted: the data would be missing. */
int resolve_installed_cia(const CatItem *item, int (*installed_version)(uint64_t title_id));

/* "2.1.0" as an AM title version (major << 10 | minor << 4 | micro), or -1 if it cannot be one. */
int cia_title_version(const char *version);

/* Does the installed update match what the variant needs? */
bool version_fits(const CatVariant *v, int update_version);

#endif
