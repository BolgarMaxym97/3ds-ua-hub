/*
 * The anonymous report (worker/src/event.ts on the server side - keep the two in step).
 * From 3ds-ua's installer. Portable: the host test builds it too. Nothing here touches the
 * network.
 */
#ifndef REPORT_H
#define REPORT_H

#include <stddef.h>
#include <stdint.h>

#define REPORT_ID_LEN 8 /* Crockford base32, 40 bits */
#define REPORT_LOG_MAX 2048
#define REPORT_MAX 4096 /* the server refuses more */

typedef struct {
    const char *id;     /* REPORT_ID_LEN characters */
    const char *event;  /* install | update | remove | error | not_found */
    const char *app;
    /* optional: NULL leaves the field out */
    const char *model, *system, *luma, *region;
    const char *slug, *tv, *vregion, *tid, *kind; /* the translation, its version, the variant, the game */
    const char *stage, *code, *detail, *log;
    int manual, risky;  /* 0/1; < 0: left out */
    int64_t ms;         /* < 0: left out */
} Report;

/* 40 random bits -> 8 characters of 0-9 A-Z without I L O U. */
void report_id_encode(const uint8_t bytes[5], char out[REPORT_ID_LEN + 1]);
/* A well-formed id, as report_id_encode() makes them. */
int report_id_valid(const char *id);
/* "K7QM29XD" -> "K7QM-29XD" for the Help screen. */
void report_id_display(const char *id, char out[REPORT_ID_LEN + 2]);
/* The JSON body; the length, or 0 if it does not fit in `cap` (or REPORT_MAX). Text fields
 * are cut to what the server accepts. */
size_t report_json(const Report *r, char *out, size_t cap);

#endif
