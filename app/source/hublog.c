#include "hublog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/fsutil.h"
#include "paths.h"

#define LOG_PATH HUB_DIR "/hub.log"
#define LOG_KEEP (64 * 1024) /* past this the file starts over from its second half */

static void clean(char *dst, size_t size, const char *src)
{
    snprintf(dst, size, "%s", src ? src : "");
    for (char *p = dst; *p; p++)
        if (*p == '\t' || *p == '\n' || *p == '\r')
            *p = ' ';
}

static void trim_if_long(void)
{
    long size = fs_size(LOG_PATH);
    if (size < LOG_KEEP)
        return;
    FILE *f = fopen(LOG_PATH, "rb");
    char *buf = malloc((size_t)size + 1);
    if (!f || !buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        if (f)
            fclose(f);
        free(buf);
        return;
    }
    fclose(f);
    buf[size] = 0;
    char *half = strchr(buf + size / 2, '\n');
    if (half)
        fs_write(LOG_PATH, half + 1, strlen(half + 1));
    free(buf);
}

void hublog_add(const char *event, const char *title, const char *status, const char *detail)
{
    trim_if_long();
    fs_make_parents(LOG_PATH);
    FILE *f = fopen(LOG_PATH, "ab");
    if (!f)
        return;
    time_t now = time(NULL);
    struct tm *t = gmtime(&now); /* the console's clock is local time already */
    char e[16], ti[96], st[24], de[160];
    clean(e, sizeof e, event);
    clean(ti, sizeof ti, title);
    clean(st, sizeof st, status);
    clean(de, sizeof de, detail);
    fprintf(f, "%02d.%02d %02d:%02d\t%s\t%s\t%s\t%s\n", t->tm_mday, t->tm_mon + 1, t->tm_hour, t->tm_min, e, ti, st, de);
    fclose(f);
}

int hublog_read(LogEntry *out)
{
    long size = fs_size(LOG_PATH);
    FILE *f = size > 0 ? fopen(LOG_PATH, "rb") : NULL;
    if (!f)
        return 0;
    char *buf = malloc((size_t)size + 1);
    size_t got = buf ? fread(buf, 1, (size_t)size, f) : 0;
    fclose(f);
    if (!buf)
        return 0;
    buf[got] = 0;
    int n = 0;
    /* from the end: newest first */
    for (char *end = buf + got; end > buf && n < LOG_SHOWN;) {
        if (end[-1] == '\n')
            *--end = 0;
        char *start = end;
        while (start > buf && start[-1] != '\n')
            start--;
        LogEntry *e = &out[n];
        memset(e, 0, sizeof *e);
        char *fields[5] = {0};
        int k = 0;
        for (char *p = start; k < 5; k++) {
            fields[k] = p;
            char *tab = strchr(p, '\t');
            if (!tab)
                break;
            *tab = 0;
            p = tab + 1;
        }
        if (fields[4]) {
            snprintf(e->when, sizeof e->when, "%s", fields[0]);
            snprintf(e->event, sizeof e->event, "%s", fields[1]);
            snprintf(e->title, sizeof e->title, "%s", fields[2]);
            snprintf(e->status, sizeof e->status, "%s", fields[3]);
            snprintf(e->detail, sizeof e->detail, "%s", fields[4]);
            n++;
        }
        end = start;
        if (end > buf)
            *--end = 0;
    }
    free(buf);
    return n;
}
