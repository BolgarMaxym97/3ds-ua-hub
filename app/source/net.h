/* HTTP GET from the Worker over libctru's httpc: the catalog into memory, files onto the card. */
#ifndef NET_H
#define NET_H

#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    NET_OK,
    NET_FAILED,     /* no connection at all: no Wi-Fi, no DNS, server unreachable (NET_OFF) */
    NET_LOST,       /* it broke off midway (NET_LOST) */
    NET_SERVER,     /* the server answered with an error (SERVER) */
    NET_NOT_FOUND,  /* 404: the catalog names a file the bucket does not have */
    NET_TOO_BIG,    /* more than the caller allows */
    NET_NO_MEMORY,
    NET_WRITE,      /* the card refused the bytes (SD_WRITE) */
    NET_CANCELLED,
} NetResult;

/* Progress for a screen to draw, written by the downloading thread. */
typedef struct {
    volatile u32 done, total;   /* bytes; total 0 while unknown */
    volatile bool cancel;       /* set by the screen; the download stops at the next chunk */
    u32 status;                 /* the HTTP status of the last answer, 0 without one */
    Result result;              /* the httpc call that failed, 0 if none: the error's details */
} NetProgress;

void net_init(void);
void net_exit(void);

/* GET HUB_URL + `path` into a malloc'd, NUL-terminated buffer of at most `max` bytes.
 * `sig` (may be NULL) receives the X-Signature header. */
NetResult net_get(const char *path, char **body, size_t *len, size_t max, char *sig, size_t sig_size);

/* GET HUB_URL + `path`, handing each chunk to `sink` (false stops with NET_WRITE): a CIA goes
 * straight into the system this way. */
typedef bool (*NetSink)(const u8 *data, u32 len, void *ctx);
NetResult net_stream(const char *path, NetSink sink, void *ctx, NetProgress *progress);

/* GET HUB_URL + `path` into `dest`, resuming from what `dest` already holds (Range). On NET_OK
 * `dest` has exactly `size` bytes; the caller checks the sha256. */
NetResult net_download(const char *path, const char *dest, u32 size, NetProgress *progress);

#endif
