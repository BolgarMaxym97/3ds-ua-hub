#include "titles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_HIGH 0x00040000u      /* applications: games and apps on the HOME Menu */
#define UPDATE_HIGH 0x0004000Eu   /* their update titles */
#define MAX_TITLES 512

/* The SMDH every title carries (3dbrew "SMDH"). */
typedef struct {
    u32 magic;
    u16 version, reserved;
    struct {
        u16 short_desc[0x40];
        u16 long_desc[0x80];
        u16 publisher[0x40];
    } titles[16];
    u8 settings[0x30];
    u8 reserved2[8];
    u16 small_icon[24 * 24];
    u16 large_icon[48 * 48];
} Smdh;

static bool read_smdh(u64 tid, FS_MediaType media, Smdh *out)
{
    static const u32 file_path[5] = {0x0, 0x0, 0x2, 0x6E6F6369 /* "icon" */, 0x0};
    u32 archive_path[4] = {(u32)(tid & 0xFFFFFFFF), (u32)(tid >> 32), media, 0x0};
    FS_Path archive = {PATH_BINARY, sizeof archive_path, archive_path};
    FS_Path file = {PATH_BINARY, sizeof file_path, file_path};
    Handle h;
    if (R_FAILED(FSUSER_OpenFileDirectly(&h, ARCHIVE_SAVEDATA_AND_CONTENT, archive, file, FS_OPEN_READ, 0)))
        return false;
    u32 got = 0;
    Result r = FSFILE_Read(h, &got, 0, out, sizeof *out);
    FSFILE_Close(h);
    return R_SUCCEEDED(r) && got == sizeof *out && out->magic == 0x48444D53; /* "SMDH" */
}

/* UTF-16 (BMP) to UTF-8, cut at whole letters. */
static void utf8(char *out, size_t size, const u16 *in, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i < max && in[i]; i++) {
        u16 c = in[i] == '\n' ? ' ' : in[i];
        int len = c < 0x80 ? 1 : c < 0x800 ? 2 : 3;
        if (n + len + 1 > size)
            break;
        if (len == 1) {
            out[n++] = (char)c;
        } else if (len == 2) {
            out[n++] = (char)(0xC0 | c >> 6);
            out[n++] = (char)(0x80 | (c & 0x3F));
        } else {
            out[n++] = (char)(0xE0 | c >> 12);
            out[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
            out[n++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[n] = 0;
}

/* The 48x48 icon: RGB565 in 8x8 Morton-ordered tiles, to RGBA rows. */
static void icon_rgba(u8 *out, const u16 *icon)
{
    for (int y = 0; y < 48; y++) {
        for (int x = 0; x < 48; x++) {
            int tile = (y / 8) * 6 + x / 8;
            int m = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
            u16 v = icon[tile * 64 + m];
            u8 *p = out + (y * 48 + x) * 4;
            p[0] = (u8)(((v >> 11) & 0x1F) * 255 / 31);
            p[1] = (u8)(((v >> 5) & 0x3F) * 255 / 63);
            p[2] = (u8)((v & 0x1F) * 255 / 31);
            p[3] = 255;
        }
    }
}

static int update_version(u64 tid)
{
    u64 update = (u64)UPDATE_HIGH << 32 | (tid & 0xFFFFFFFF);
    AM_TitleEntry info;
    if (R_FAILED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &update, &info)))
        return UPDATE_NONE;
    return info.version;
}

static int find(const Titles *t, u64 tid, FS_MediaType media)
{
    for (int i = 0; t && t->full && i < t->n; i++)
        if (t->items[i].title_id == tid && t->items[i].media == media)
            return i;
    return -1;
}

static void add(Titles *t, u64 tid, FS_MediaType media, Smdh *smdh, const Titles *known, bool full)
{
    ConsoleTitle *c = &t->items[t->n];
    memset(c, 0, sizeof *c);
    c->title_id = tid;
    c->media = media;
    c->update_version = update_version(tid);
    memset(t->icons[t->n], 0, sizeof t->icons[0]);
    int k = find(known, tid, media);
    if (k >= 0) {
        memcpy(c->product, known->items[k].product, sizeof c->product);
        memcpy(c->name, known->items[k].name, sizeof c->name);
        memcpy(t->icons[t->n], known->icons[k], sizeof t->icons[0]);
    } else if (full) {
        AM_GetTitleProductCode(media, tid, c->product);
        c->product[sizeof c->product - 1] = 0;
        if (read_smdh(tid, media, smdh)) {
            /* English, else the first language that has a name */
            int lang = smdh->titles[1].short_desc[0] ? 1 : 0;
            for (int i = 0; i < 16 && !smdh->titles[lang].short_desc[0]; i++)
                lang = i;
            utf8(c->name, sizeof c->name, smdh->titles[lang].short_desc, 0x40);
            icon_rgba(t->icons[t->n], smdh->large_icon);
        }
    }
    if (!c->name[0])
        snprintf(c->name, sizeof c->name, "%016llX", (unsigned long long)tid);
    t->n++;
}

void titles_free(Titles *t)
{
    free(t->items);
    free(t->icons);
    memset(t, 0, sizeof *t);
}

void titles_scan(Titles *t, const Titles *known, bool full, volatile bool *cancel)
{
    titles_free(t);
    t->items = calloc(MAX_TITLES, sizeof *t->items);
    t->icons = calloc(MAX_TITLES, sizeof *t->icons);
    Smdh *smdh = malloc(sizeof *smdh);
    u64 *ids = calloc(MAX_TITLES, sizeof *ids);
    if (!t->items || !t->icons || !smdh || !ids)
        goto done;

    t->full = full;
    static const FS_MediaType medias[] = {MEDIATYPE_SD, MEDIATYPE_GAME_CARD};
    for (unsigned m = 0; m < sizeof medias / sizeof *medias; m++) {
        u32 count = 0, read = 0;
        if (R_FAILED(AM_GetTitleCount(medias[m], &count)) || !count)
            continue;
        if (count > MAX_TITLES)
            count = MAX_TITLES;
        if (R_FAILED(AM_GetTitleList(&read, medias[m], count, ids)))
            continue;
        if (medias[m] == MEDIATYPE_GAME_CARD)
            t->card_present = read > 0;
        for (u32 i = 0; i < read && t->n < MAX_TITLES; i++) {
            if (full && cancel && *cancel)
                full = t->full = false; /* leaving: the rest without names */
            if ((u32)(ids[i] >> 32) == APP_HIGH)
                add(t, ids[i], medias[m], smdh, known, full);
        }
    }
done:
    free(smdh);
    free(ids);
}

const char *titles_console_region(void)
{
    u8 region = 0;
    if (R_FAILED(CFGU_SecureInfoGetRegion(&region)))
        return "";
    switch (region) {
    case CFG_REGION_JPN: return "JPN";
    case CFG_REGION_USA: return "USA";
    case CFG_REGION_EUR:
    case CFG_REGION_AUS: return "EUR";
    case CFG_REGION_CHN: return "CHN";
    case CFG_REGION_KOR: return "KOR";
    case CFG_REGION_TWN: return "TWN";
    default: return "";
    }
}

int titles_installed_version(u64 title_id)
{
    AM_TitleEntry info;
    if (R_FAILED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &title_id, &info)))
        return -1;
    return info.version;
}
