#include "image.h"

#include <math.h>
#include <stdio.h>
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
#define STBI_NO_STDIO /* files are read whole (read_file): stdio's small reads cost more than the decoding */
#include "../vendor/stb_image.h"
#pragma GCC diagnostic pop

/* Room for a screen of icons, a few banners and a screenshot with its thumbnails. */
#define SLOTS 32
#define MAX_SIDE 512 /* banners are 400 wide, screenshots 400x480; anything larger is refused, not scaled */
#define THUMB_STEP 4 /* a screenshot's thumbnail: 400x480 -> 100x120 */
/* Textures live in the 8 MB of linear memory, about 2.8 MB of which the screens leave free: a
 * banner takes 256 KB, a screenshot 1 MB. Past this the least recently drawn go. */
#define TEX_BUDGET (2048 * 1024)
#define JOBS 8 /* decodes waiting; past that the oldest request is dropped (scrolled past) */
#define FILE_MAX (1024 * 1024)

typedef enum { MODE_PLAIN, MODE_THUMB, MODE_SHOT } Mode;

typedef struct {
    char key[68];       /* the sha256, or "t:"/"s:" and it: a screenshot's thumbnail, in full */
    bool used, failed;
    bool pending;       /* being decoded on the image thread */
    bool missing;       /* not on the card yet (still downloading): asked again at `retry_at` */
    u32 retry_at;       /* a frame number */
    u32 last_used;
    u32 bytes;          /* of its texture */
    C3D_Tex tex;
    Tex3DS_SubTexture sub;
    C2D_Image img;
} Slot;

/* A request for the image thread, and what it hands back: pixels already tiled for the GPU, so
 * the screens' thread only copies them into a texture. */
typedef struct {
    char key[68];
    char sha[65];
    Mode mode;
} Job;

typedef struct {
    char key[68];
    u32 *tiled;         /* NULL: the file is not a picture this can show... */
    bool missing;       /* ...or not there at all */
    u16 w, h, tw, th;
} Done;

static Slot slots[SLOTS];
static u32 clock_tick, tex_bytes, frame;

static LightLock lock;
static LightEvent wake;
static Thread worker;
static volatile bool quit;
static Job jobs[JOBS];   /* newest last: the image on screen now goes first */
static int n_jobs;
static Done done[JOBS];
static int n_done;

static u32 pow2(u32 v)
{
    u32 p = 8;
    while (p < v)
        p <<= 1;
    return p;
}

/* RGBA rows into the GPU's 8x8 Morton-tiled RGBA8 (stored A, B, G, R), tw x th. */
static u32 *tile(const u8 *px, int w, int h, u32 tw, u32 th)
{
    u32 *dst = calloc(tw * th, 4);
    if (!dst)
        return NULL;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const u8 *p = px + (y * w + x) * 4;
            u32 at = (((y >> 3) * (tw >> 3) + (x >> 3)) << 6) +
                     ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3));
            dst[at] = (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
        }
    }
    return dst;
}

/* A box average of THUMB_STEP x THUMB_STEP pixels: scaled by the GPU alone, the text in the
 * picture breaks into specks. */
static u8 *shrink(const u8 *px, int w, int h, int *tw, int *th)
{
    *tw = w / THUMB_STEP;
    *th = h / THUMB_STEP;
    u8 *small = *tw > 0 && *th > 0 ? malloc((size_t)*tw * *th * 4) : NULL;
    for (int y = 0; small && y < *th; y++)
        for (int x = 0; x < *tw; x++)
            for (int c = 0; c < 4; c++) {
                u32 sum = 0;
                for (int dy = 0; dy < THUMB_STEP; dy++)
                    for (int dx = 0; dx < THUMB_STEP; dx++)
                        sum += px[((y * THUMB_STEP + dy) * w + x * THUMB_STEP + dx) * 4 + c];
                small[(y * *tw + x) * 4 + c] = (u8)(sum / (THUMB_STEP * THUMB_STEP));
            }
    return small;
}

/* The whole file in one read: on an Old 3DS, stdio's buffer-sized reads of a 100 KB banner took
 * longer than decoding it. */
static u8 *read_file(const char *path, int *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    u8 *buf = NULL;
    long size = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    if (size > 0 && size <= FILE_MAX && fseek(f, 0, SEEK_SET) == 0 && (buf = malloc((size_t)size)) &&
        fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = (int)size;
    return buf;
}

static Done decode(const Job *job)
{
    Done d = {0};
    snprintf(d.key, sizeof d.key, "%s", job->key);
    char path[160];
    store_image_path(path, sizeof path, job->sha);
    int len, w, h, n;
    u8 *file = read_file(path, &len);
    d.missing = !file && !fs_exists(path);
    u8 *px = file ? stbi_load_from_memory(file, len, &w, &h, &n, 4) : NULL;
    free(file);
    if (!px || w > MAX_SIDE || h > MAX_SIDE) {
        stbi_image_free(px);
        return d;
    }
    if (job->mode == MODE_THUMB) {
        u8 *small = shrink(px, w, h, &w, &h);
        stbi_image_free(px);
        px = small;
        if (!px)
            return d;
    }
    d.w = (u16)w;
    d.h = (u16)h;
    d.tw = (u16)pow2((u32)w);
    d.th = (u16)pow2((u32)h);
    d.tiled = tile(px, w, h, d.tw, d.th);
    if (job->mode == MODE_THUMB)
        free(px);
    else
        stbi_image_free(px);
    return d;
}

/* Below the screens: it decodes while they wait for the display. */
static void work(void *arg)
{
    (void)arg;
    while (!quit) {
        LightEvent_Wait(&wake);
        for (;;) {
            LightLock_Lock(&lock);
            if (quit || !n_jobs) {
                LightLock_Unlock(&lock);
                break;
            }
            Job job = jobs[--n_jobs];
            LightLock_Unlock(&lock);
            Done d = decode(&job);
            LightLock_Lock(&lock);
            if (n_done < JOBS)
                done[n_done++] = d;
            else
                free(d.tiled); /* the screens never took it: asked again, it comes again */
            LightLock_Unlock(&lock);
        }
    }
}

void image_init(void)
{
    memset(slots, 0, sizeof slots);
    LightLock_Init(&lock);
    LightEvent_Init(&wake, RESET_ONESHOT);
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    worker = threadCreate(work, NULL, 0x10000, prio + 1, -2, false);
}

static void release(Slot *s)
{
    if (s->bytes) { /* has a texture */
        C3D_TexDelete(&s->tex);
        tex_bytes -= s->bytes;
    }
    memset(s, 0, sizeof *s);
}

void image_exit(void)
{
    if (worker) {
        quit = true;
        LightEvent_Signal(&wake);
        threadJoin(worker, U64_MAX);
        threadFree(worker);
        worker = NULL;
    }
    for (int i = 0; i < n_done; i++)
        free(done[i].tiled);
    n_done = n_jobs = 0;
    for (int i = 0; i < SLOTS; i++)
        release(&slots[i]);
}

static Slot *find(const char *key)
{
    for (int i = 0; i < SLOTS; i++)
        if (slots[i].used && !strcmp(slots[i].key, key))
            return &slots[i];
    return NULL;
}

/* Frees the least recently drawn texture other than `keep`; false when there is none. */
static bool evict(const Slot *keep)
{
    Slot *oldest = NULL;
    for (int i = 0; i < SLOTS; i++) {
        Slot *s = &slots[i];
        if (s != keep && s->used && !s->pending && !s->failed && s->bytes &&
            (!oldest || s->last_used < oldest->last_used))
            oldest = s;
    }
    if (oldest)
        release(oldest);
    return oldest != NULL;
}

/* A free slot for `key`, or the least recently used one emptied. */
static Slot *take(const char *key)
{
    Slot *s = &slots[0];
    for (int i = 0; i < SLOTS; i++) {
        if (!slots[i].used) {
            s = &slots[i];
            break;
        }
        if (slots[i].last_used < s->last_used)
            s = &slots[i];
    }
    release(s); /* one still being decoded: its result finds no slot and is dropped */
    snprintf(s->key, sizeof s->key, "%s", key);
    s->used = true;
    s->last_used = ++clock_tick;
    return s;
}

/* Tiled pixels into the slot's texture, making room within the budget. */
static bool upload(Slot *s, const u32 *tiled, int w, int h, int tw, int th)
{
    u32 need = (u32)tw * th * 4;
    while (tex_bytes + need > TEX_BUDGET && evict(s))
        ;
    bool ok;
    while (!(ok = C3D_TexInit(&s->tex, tw, th, GPU_RGBA8)) && evict(s))
        ;
    if (!ok)
        return false;
    memcpy(s->tex.data, tiled, need);
    C3D_TexFlush(&s->tex);
    C3D_TexSetFilter(&s->tex, GPU_LINEAR, GPU_LINEAR);
    s->sub = (Tex3DS_SubTexture){(u16)w, (u16)h, 0.0f, 1.0f, (float)w / tw, 1.0f - (float)h / th};
    s->img = (C2D_Image){&s->tex, &s->sub};
    s->bytes = need;
    tex_bytes += need;
    return true;
}

void image_frame(void)
{
    frame++;
    Done d[JOBS];
    LightLock_Lock(&lock);
    int n = n_done;
    memcpy(d, done, sizeof *d * n);
    n_done = 0;
    LightLock_Unlock(&lock);
    for (int i = 0; i < n; i++) {
        Slot *s = find(d[i].key);
        if (s && s->pending) {
            s->pending = false;
            s->missing = d[i].missing;
            s->retry_at = frame + 60;
            s->failed = !s->missing && (!d[i].tiled || !upload(s, d[i].tiled, d[i].w, d[i].h, d[i].tw, d[i].th));
        }
        free(d[i].tiled);
    }
}

static void push(const char *key, const char *sha, Mode mode)
{
    LightLock_Lock(&lock);
    if (n_jobs == JOBS) {
        /* the oldest request was scrolled past: its slot is freed, and asked again if it comes back */
        Slot *s = find(jobs[0].key);
        if (s && s->pending)
            memset(s, 0, sizeof *s);
        memmove(jobs, jobs + 1, sizeof *jobs * (JOBS - 1));
        n_jobs--;
    }
    Job *j = &jobs[n_jobs++];
    snprintf(j->key, sizeof j->key, "%s", key);
    snprintf(j->sha, sizeof j->sha, "%s", sha);
    j->mode = mode;
    LightLock_Unlock(&lock);
    LightEvent_Signal(&wake);
}

static const C2D_Image *get(const char *sha, Mode mode)
{
    if (!sha || !sha[0])
        return NULL;
    char key[68];
    snprintf(key, sizeof key, "%s%s", mode == MODE_THUMB ? "t:" : mode == MODE_SHOT ? "s:" : "", sha);
    Slot *s = find(key);
    if (s && s->missing && (s32)(frame - s->retry_at) >= 0) { /* downloaded by now? */
        s->missing = false;
        s->pending = true;
        push(key, sha, mode);
    }
    if (s) {
        s->last_used = ++clock_tick;
        return s->pending || s->failed || s->missing ? NULL : &s->img;
    }
    /* Whether the file is there yet the image thread finds out: a check here waited on the card
     * behind the thread's reads, up to 60 ms of a frame on an Old 3DS. */
    if (!worker)
        return NULL;
    if (mode == MODE_SHOT) /* 1 MB each: only the one on screen is kept */
        for (int i = 0; i < SLOTS; i++)
            if (slots[i].used && !strncmp(slots[i].key, "s:", 2))
                release(&slots[i]);
    s = take(key);
    s->pending = true;
    push(key, sha, mode);
    return NULL;
}

const C2D_Image *image_get(const char *sha)
{
    return get(sha, MODE_PLAIN);
}

void image_prefetch(const char *sha)
{
    get(sha, MODE_PLAIN);
}

const C2D_Image *image_get_shot(const char *sha)
{
    return get(sha, MODE_SHOT);
}

const C2D_Image *image_get_thumb(const char *sha)
{
    return get(sha, MODE_THUMB);
}

const C2D_Image *image_get_pixels(const char *key, const u8 *rgba, int w, int h)
{
    Slot *s = find(key);
    if (s) {
        s->last_used = ++clock_tick;
        return s->failed ? NULL : &s->img;
    }
    s = take(key);
    u32 tw = pow2((u32)w), th = pow2((u32)h);
    u32 *tiled = rgba ? tile(rgba, w, h, tw, th) : NULL;
    s->failed = !tiled || !upload(s, tiled, w, h, (int)tw, (int)th);
    free(tiled);
    return s->failed ? NULL : &s->img;
}

void image_draw_turned(const C2D_Image *img, float cx, float cy, float scale, int quarters)
{
    C2D_DrawImageAtRotated(*img, cx, cy, 0.3f, quarters * (float)M_PI / 2, NULL, scale, scale);
}

void image_draw(const C2D_Image *img, float x, float y, float w, float h)
{
    /* the shapes' depth (ui.c): a badge drawn after the banner lands on top of it */
    C2D_DrawImageAt(*img, x, y, 0.3f, NULL, w / img->subtex->width, h / img->subtex->height);
}
