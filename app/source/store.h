/* The catalog as last fetched, kept on the SD card for when there is no network (B3). */
#ifndef STORE_H
#define STORE_H

#include <stdbool.h>
#include <stddef.h>

/* Saves the body, its signature and the time; best effort: a failure only loses offline use. */
void store_save_catalog(const char *body, size_t len, const char *sig);

/* The saved body (malloc'd, NUL-terminated) and signature; false when there is none. The caller
 * still checks the signature: the card is no safer than the network. */
bool store_load_catalog(char **body, size_t *len, char *sig, size_t sig_size);

/* "25.10, 14:02" of the saved catalog, or "" when unknown. */
void store_catalog_time(char *out, size_t size);

/* Where an image with this sha256 lives in the cache. */
void store_image_path(char *out, size_t size, const char *sha256);

#endif
