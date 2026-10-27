/*
 * Reading the patch archives: a zip's central directory, stored and deflated entries (zlib),
 * every entry's CRC checked. Plain C: tests/unzip_test.c runs it on the computer.
 */
#ifndef CORE_UNZIP_H
#define CORE_UNZIP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define ZIP_MAX_ENTRIES 4096
#define ZIP_MAX_FILE (256u * 1024 * 1024) /* larger entries are refused: a patch never has them */

typedef struct {
    char name[256];
    uint32_t crc, csize, usize, offset;
    uint16_t method;
} ZipEntry;

typedef struct {
    FILE *f;
    ZipEntry *entries;
    int n;
    volatile uint64_t *written; /* if set, zip_extract adds every byte it writes: progress inside a big file */
} Zip;

/* 0 when `path` is a zip this can read and every name is a safe relative path. */
int zip_open(Zip *z, const char *path);
void zip_close(Zip *z);
/* The entry with this name, or -1. */
int zip_find(const Zip *z, const char *name);
#define ZIP_WRITE_ERROR -2
/* Writes entry `i` to `dest` (parents made); `sha` (may be NULL) gets its sha256.
 * 0; -1 when the entry is damaged (bad data, CRC); ZIP_WRITE_ERROR when the card refused. */
int zip_extract(const Zip *z, int i, const char *dest, uint8_t sha[32]);
/* Entry `i` into a malloc'd, NUL-terminated buffer (small files: check.txt). */
char *zip_read(const Zip *z, int i, size_t *len);

#endif
