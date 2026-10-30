/* Banners, icons and screenshots: PNG files from the cache, as citro2d images, a few kept in
 * linear memory. */
#ifndef IMAGE_H
#define IMAGE_H

#include <citro2d.h>
#include <stdbool.h>

void image_init(void);
void image_exit(void);
/* Called once a frame: screenshots are decoded one per frame. */
void image_frame(void);

/* The image whose PNG is cached under this sha256, decoded once and then reused; NULL while the
 * file is not there yet or cannot be read. Drawing code calls it every frame. */
const C2D_Image *image_get(const char *sha256);

/* A screenshot (both screens, 400x480) in full; only the last one asked for is kept. NULL until
 * it is cached and decoded: ask again next frame. */
const C2D_Image *image_get_shot(const char *sha256);
/* The same screenshot a quarter of its size, for a list of them. */
const C2D_Image *image_get_thumb(const char *sha256);

/* An image from RGBA rows already in memory (a game's SMDH icon), cached under `key`. */
const C2D_Image *image_get_pixels(const char *key, const u8 *rgba, int w, int h);

/* Draws `img` scaled into w x h at (x, y). */
void image_draw(const C2D_Image *img, float x, float y, float w, float h);
/* Draws `img` centred on (cx, cy), scaled by `scale` and turned `quarters` x 90 degrees clockwise. */
void image_draw_turned(const C2D_Image *img, float cx, float cy, float scale, int quarters);

#endif
