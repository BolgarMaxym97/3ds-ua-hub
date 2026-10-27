/*
 * Every error the person can see: a code (for the log and the statistics), a title, what
 * happened and what to do - the table of board G4 in the design. The texts live in the app, so
 * they show without a network. Plain C: tests/errors_test.c checks the table.
 */
#ifndef ERRORS_H
#define ERRORS_H

#include <stdbool.h>

typedef enum {
    ERR_NONE,
    ERR_NET_OFF,        /* no connection before downloading */
    ERR_NET_LOST,       /* the connection broke during a download */
    ERR_SERVER,         /* the server answered with an error, or a file is missing there */
    ERR_SD_SPACE,
    ERR_SD_WRITE,
    ERR_GAME_NOT_FOUND,
    ERR_GAME_REGION,
    ERR_GAME_VERSION,
    ERR_HASH_MISMATCH,
    ERR_FILE_CORRUPT,
    ERR_LUMA_OLD,
    ERR_AM_FAIL,
    ERR_BATTERY_LOW,
    ERR_INTERRUPTED,
    ERR_APP_OUTDATED,
    ERR_COUNT,
} ErrorCode;

typedef struct {
    const char *code;   /* "NET_LOST": the log, the statistics, the small print */
    const char *title;
    const char *text;   /* may have one %s: the detail of this occurrence (a size, a version) */
    const char *steps;  /* what to do, or NULL */
    bool retry;         /* "Спробувати знову" makes sense */
} ErrorInfo;

const ErrorInfo *error_info(ErrorCode code);

#endif
