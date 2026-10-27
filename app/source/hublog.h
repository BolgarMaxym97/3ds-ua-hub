/* The Hub's journal: one line per operation in 3ds/3ds-ua-hub/hub.log (H2 shows the latest). */
#ifndef HUBLOG_H
#define HUBLOG_H

#include <stdbool.h>

#define LOG_SHOWN 40

typedef struct {
    char when[16];      /* "25.10 15:41" */
    char event[16];     /* install, update, remove, error, check, start */
    char title[96];
    char status[24];    /* "ok" or an error code */
    char detail[160];   /* timings, the version, the system's result code */
} LogEntry;

/* Appends a line; best effort. */
void hublog_add(const char *event, const char *title, const char *status, const char *detail);
/* The latest entries, newest first; returns how many (up to LOG_SHOWN). */
int hublog_read(LogEntry *out);

#endif
