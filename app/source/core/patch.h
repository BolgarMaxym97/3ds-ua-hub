/*
 * Installing and removing a LayeredFS patch under <root>/luma/titles/<TID>/ (IDEA.md):
 *
 *  - the archive is unpacked into <TID>.hub-new first, the live folder swapped for it in two
 *    renames, the old one removed: a game sees the whole old set or the whole new one;
 *  - files in the live folder that are not the Hub's (another mod) are copied aside to
 *    3ds/3ds-ua-hub/backup/<TID>/ and put back on removal;
 *  - 3ds/3ds-ua-hub/pending.txt says what is under way; after a power cut patch_recover()
 *    finishes a swap that happened, or undoes one that did not.
 *
 * Plain C over fsutil: tests/patch_test.c runs it on the computer, cutting the power (fs_budget)
 * at every step.
 */
#ifndef CORE_PATCH_H
#define CORE_PATCH_H

#include <stdint.h>

#include "record.h"

typedef enum { PSTAGE_EXTRACT, PSTAGE_BACKUP, PSTAGE_SWAP, PSTAGE_DONE } PatchStage;

typedef struct {
    volatile int stage;           /* PatchStage */
    volatile uint32_t done, total; /* files */
    volatile uint64_t bytes, bytes_total; /* unpacked so far, of all; a big file moves these alone */
} PatchProgress;

typedef enum {
    PATCH_OK,
    PATCH_BAD_ARCHIVE,  /* not a zip this can read, or an entry fails its CRC (FILE_CORRUPT) */
    PATCH_WRITE,        /* the card refused (SD_WRITE); everything is as it was */
} PatchResult;

/* Installs `zip` for rec->title_id. `rec` carries slug, version, region...; its files are
 * filled in, and it is saved as the record. Replaces an earlier install of the same slug. */
PatchResult patch_install(const char *root, const char *zip, Record *rec, PatchProgress *progress);

/* Removes what `rec` installed and puts any other mod's files back. `progress` (may be NULL)
 * counts its files as they go. */
PatchResult patch_remove(const char *root, const Record *rec, PatchProgress *progress);

/* Called at start: completes or rolls back an operation a power cut interrupted.
 * Returns 1 when there was one, 0 when not. */
int patch_recover(const char *root);

/* For a game picked by hand: the archive's check.txt ("sha256  romfs/path" per line) against the
 * game's own files under `game_root` (its RomFS mounted). -1: the archive has no check.txt. */
int patch_check_game(const char *zip, const char *game_root, int *matched, int *total);

#endif
