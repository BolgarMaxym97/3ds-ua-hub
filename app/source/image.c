#include "image.h"

#include <stdlib.h>
#include <string.h>

#include "core/fsutil.h"
#include "store.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "../vendor/stb_image.h"
#pragma GCC diagnostic pop

/* A list shows 5 icons, a page one banner; room for a scroll's worth more before reuse. */
#define SLOTS 24
#define MAX_SIDE 512 /* banners are 400 wide; anything larger is refused, not scaled */

typedef struct {
    char sha[65];
    bool used, failed;
    u32 last_used;
    C3D_Tex tex;
    Tex3DS_SubTexture sub;
    C2D_Image img;
} Slot;

static Slot slots[SLOTS];
static u32 clock_tick;

void image_init(void)
{
    memset(slots, 0, sizeof slots);
}

static void release(Slot *s)
{
    if (s->used && !s->failed)
        C3D_TexDelete(&s->tex);
    memset(s, 0, sizeof *s);
}

void image_exit(void)
{
    for (int i = 0; i < SLOTS; i++)
        release(&slots[i]);
}

static u32 pow2(u32 v)
{
    u32 p = 8;
    while (p < v)
        p <<= 1;
    return p;
}

/* RGBA rows into the GPU's 8x8 Morton-tiled RGBA8 (stored A, B, G, R). */
static bool upload(Slot *s, const u8 *px, int w, int h)
{
    u32 tw = pow2((u32)w), th = pow2((u32)h);
    if (!C3D_TexInit(&s->tex, tw, th, GPU_RGBA8))
        return false;
    u32 *dst = s->tex.data;
    memset(dst, 0, tw * th * 4);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const u8 *p = px + (y * w + x) * 4;
            u32 at = (((y >> 3) * (tw >> 3) + (x >> 3)) << 6) +
                     ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3));
            dst[at] = (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
        }
    }
    C3D_TexFlush(&s->tex);
    C3D_TexSetFilter(&s->tex, GPU_LINEAR, GPU_LINEAR);
    s->sub = (Tex3DS_SubTexture){(u16)w, (u16)h, 0.0f, 1.0f, (float)w / tw, 1.0f - (float)h / th};
    s->img = (C2D_Image){&s->tex, &s->sub};
    return true;
}

/* The slot for `key`: found (*hit), or a free or least recently used one, emptied. */
static Slot *slot_for(const char *key, bool *hit)
{
    clock_tick++;
    Slot *free_slot = NULL, *oldest = &slots[0];
    for (int i = 0; i < SLOTS; i++) {
        Slot *s = &slots[i];
        if (s->used && !strcmp(s->sha, key)) {
            s->last_used = clock_tick;
            *hit = true;
            return s;
        }
        if (!s->used && !free_slot)
            free_slot = s;
        if (s->last_used < oldest->last_used)
            oldest = s;
    }
    *hit = false;
    Slot *s = free_slot ? free_slot : oldest;
    release(s);
    snprintf(s->sha, sizeof s->sha, "%s", key);
    s->used = true;
    s->last_used = clock_tick;
    return s;
}

const C2D_Image *image_get(const char *sha)
{
    if (!sha || !sha[0])
        return NULL;
    for (int i = 0; i < SLOTS; i++) /* a hit costs no file check */
        if (slots[i].used && !strcmp(slots[i].sha, sha)) {
            slots[i].last_used = ++clock_tick;
            return slots[i].failed ? NULL : &slots[i].img;
        }
    char path[160];
    store_image_path(path, sizeof path, sha);
    if (!fs_exists(path))
        return NULL; /* not downloaded yet: ask again next frame */
    bool hit;
    Slot *s = slot_for(sha, &hit);
    int w, h, n;
    u8 *px = stbi_load(path, &w, &h, &n, 4);
    s->failed = !px || w > MAX_SIDE || h > MAX_SIDE || !upload(s, px, w, h);
    stbi_image_free(px);
    return s->failed ? NULL : &s->img;
}

const C2D_Image *image_get_pixels(const char *key, const u8 *rgba, int w, int h)
{
    bool hit;
    Slot *s = slot_for(key, &hit);
    if (!hit)
        s->failed = !rgba || !upload(s, rgba, w, h);
    return s->failed ? NULL : &s->img;
}

void image_draw(const C2D_Image *img, float x, float y, float w, float h)
{
    /* the shapes' depth (ui.c): a badge drawn after the banner lands on top of it */
    C2D_DrawImageAt(*img, x, y, 0.3f, NULL, w / img->subtex->width, h / img->subtex->height);
}
