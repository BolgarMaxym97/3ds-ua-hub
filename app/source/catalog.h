/*
 * The catalog from GET /v1/catalog (worker/src/catalog.ts writes it; keep the two in step):
 * parsed, and its Ed25519 signature checked against the key built into the app.
 * Plain C, no libctru: tests/catalog_test.c runs it on the computer.
 */
#ifndef CATALOG_H
#define CATALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CATALOG_VERSION 1 /* the catalog shape this app reads */
#define CAT_MAX_VARIANTS 8

typedef enum { KIND_PATCH, KIND_CIA } Kind;

typedef struct {
    bool present;
    char key[112];      /* R2 key, served at /v1/f/<key> */
    char sha256[65];    /* lower-case hex */
} CatFile;

typedef struct {
    char region[4];     /* EUR, USA, JPN, KOR, CHN, TWN */
    uint64_t title_id;
    char product[16];   /* "CTR-P-AREP"; empty when unknown */
    int game_version;   /* update title version it needs; -1: any */
    uint32_t size;
    CatFile file;
    /* a CIA that keeps its game on the SD card: a zip unpacked into SD:/<data_dir> */
    CatFile data;       /* data.present: there is one */
    uint32_t data_size;
    char data_dir[96];  /* "3ds/butterscotch/undertale" */
} CatVariant;

typedef struct {
    char slug[64];
    char title[96];
    Kind kind;
    char version[16];
    char released[16];
    char authors[64];
    char *description;  /* heap, may be NULL */
    char *changelog;    /* heap, may be NULL */
    int text_pct;       /* -1: unknown */
    int textures_pct;   /* -1: unknown */
    bool third_party;
    bool pinned;        /* kept at the top of the list, whatever the sort */
    char min_app[16];   /* empty: any */
    int installs;
    CatFile banner, icon;
    CatVariant variants[CAT_MAX_VARIANTS];
    int n_variants;
} CatItem;

typedef struct {
    CatItem *items;
    int n;
    char need_app[16];  /* CAT_TOO_NEW: the app version the catalog asks for ("app"), empty if it does not say */
} Catalog;

typedef enum {
    CAT_OK,
    CAT_BAD_SIGNATURE,  /* not signed with our key: never trust it */
    CAT_BAD_JSON,       /* signed, but not something this app can read */
    CAT_TOO_NEW,        /* a newer catalog shape: the app must be updated (A4) */
    CAT_NO_MEMORY,
} CatResult;

/* Checks `sig_hex` (128 hex chars, the X-Signature header) over the exact body bytes. */
bool catalog_verify(const uint8_t *body, size_t len, const char *sig_hex, const uint8_t public_key[32]);

/* Verifies, then parses. On CAT_OK the caller owns `out` and frees it with catalog_free(). */
CatResult catalog_load(Catalog *out, const char *body, size_t len, const char *sig_hex, const uint8_t public_key[32]);
void catalog_free(Catalog *cat);

/* What a variant downloads: its file, and a CIA's data on top. */
uint64_t variant_bytes(const CatVariant *v);

/* "1.10.0" > "1.9.2"; missing parts count as 0. */
int version_cmp(const char *a, const char *b);

/* The region letter at the end of a product code: "CTR-P-AREP" -> "EUR"; NULL if unknown. */
const char *region_of_product(const char *product);

#endif
