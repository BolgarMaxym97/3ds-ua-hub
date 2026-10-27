/*
 * What the Hub has installed: one text file per translation in 3ds/3ds-ua-hub/installed/,
 * naming the version, the game it went to and every file it wrote. Plain C: the host tests
 * run it on the computer under any root ("sdmc:" on the console).
 */
#ifndef CORE_RECORD_H
#define CORE_RECORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HUB_SUBDIR "/3ds/3ds-ua-hub"

typedef struct {
    char sha256[65];
    uint32_t size;
    char path[200]; /* relative to luma/titles/<TID>/, or to `dir` for a CIA's data */
} RecFile;

typedef struct {
    char slug[64];
    char kind[8];       /* "patch" or "cia" */
    char version[16];   /* of the translation */
    char region[4];     /* of the variant */
    uint64_t title_id;  /* the game it went to (picked by hand: that game's) */
    bool manual, risky;
    char date[24];      /* "25.10.2026" */
    bool luma_changed;  /* the Hub turned game patching on for it */
    char dir[96];       /* a CIA's data folder on the card ("3ds/butterscotch/undertale"), its files
                         * listed below; empty for a patch (luma/titles/<TID>) or a CIA without data */
    RecFile *files;
    int n_files;
} Record;

int record_format(const Record *r, char **out, size_t *len);
int record_parse(Record *r, const char *text);
void record_free(Record *r);

/* <root>/3ds/3ds-ua-hub/installed/<slug>.txt, replaced atomically. */
int record_save(const char *root, const Record *r);
int record_delete(const char *root, const char *slug);
/* Every record under <root>; *out is malloc'd (free each with record_free, then the array). */
int record_load_all(const char *root, Record **out, int *n);

#endif
