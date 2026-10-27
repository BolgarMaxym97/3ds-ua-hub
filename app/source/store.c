#include "store.h"

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/fsutil.h"
#include "core/sha256.h"
#include "paths.h"

static char *read_all(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
        buf[n] = 0;
        *len = (size_t)n;
    } else {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

void store_save_catalog(const char *body, size_t len, const char *sig)
{
    uint8_t digest[32];
    sha256(body, len, digest);
    if (fs_replace_verified(CATALOG_CACHE, body, len, digest) != 0)
        return;
    sha256(sig, strlen(sig), digest);
    fs_replace_verified(CATALOG_CACHE_SIG, sig, strlen(sig), digest);

    time_t now = time(NULL);
    struct tm *t = gmtime(&now); /* the console's clock is local time already */
    char stamp[32];
    int n = snprintf(stamp, sizeof stamp, "%02d.%02d, %02d:%02d", t->tm_mday, t->tm_mon + 1, t->tm_hour, t->tm_min);
    fs_write(CATALOG_CACHE_TIME, stamp, (size_t)n);
}

bool store_load_catalog(char **body, size_t *len, char *sig, size_t sig_size)
{
    fs_finish_replace(CATALOG_CACHE); /* a save the power cut short */
    fs_finish_replace(CATALOG_CACHE_SIG);
    size_t sig_len;
    char *s = read_all(CATALOG_CACHE_SIG, &sig_len);
    if (!s)
        return false;
    snprintf(sig, sig_size, "%s", s);
    free(s);
    *body = read_all(CATALOG_CACHE, len);
    return *body != NULL;
}

void store_catalog_time(char *out, size_t size)
{
    size_t len;
    char *s = read_all(CATALOG_CACHE_TIME, &len);
    snprintf(out, size, "%s", s ? s : "");
    free(s);
}

void store_image_path(char *out, size_t size, const char *sha)
{
    snprintf(out, size, IMAGE_CACHE "/%s.png", sha);
}
