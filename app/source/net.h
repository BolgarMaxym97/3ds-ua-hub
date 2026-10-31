/* HTTP GET from the Worker over libctru's httpc: the catalog into memory, files onto the card. */
#ifndef NET_H
#define NET_H

#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>

#include "core/sha256.h"

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

/* As net_download, with the sha256 of every byte of `dest` in `sha` (initialised here) as they
 * arrive, so a big file needs no second pass: a part already on the card is read through it first.
 * A server that ignores the Range (200 to a resume) has the file started over from byte 0. */
NetResult net_download_hashed(const char *path, const char *dest, u32 size, Sha256 *sha, NetProgress *progress);

/* HEAD HUB_URL + `path`. NET_OK on 200, `*size` from its Content-Length. Otherwise `*status` is the
 * answer's (0 without one) and `reason` its X-Fallback-Error header, empty when it has none. */
NetResult net_head(const char *path, u32 *size, u32 *status, char *reason, size_t reason_size);

#endif
