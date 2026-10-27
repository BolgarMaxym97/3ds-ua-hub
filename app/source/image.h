/* Banners and icons: PNG files from the cache, as citro2d images, a few kept in VRAM. */
#ifndef IMAGE_H
#define IMAGE_H

#include <citro2d.h>
#include <stdbool.h>

void image_init(void);
void image_exit(void);

/* The image whose PNG is cached under this sha256, decoded once and then reused; NULL while the
 * file is not there yet or cannot be read. Drawing code calls it every frame. */
const C2D_Image *image_get(const char *sha256);

/* An image from RGBA rows already in memory (a game's SMDH icon), cached under `key`. */
const C2D_Image *image_get_pixels(const char *key, const u8 *rgba, int w, int h);

/* Draws `img` scaled into w x h at (x, y). */
void image_draw(const C2D_Image *img, float x, float y, float w, float h);

#endif
