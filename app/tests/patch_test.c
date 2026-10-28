/*
 * Host test of the install engine (source/core/patch.c, record.c, unzip.c) on a fake SD card:
 * install, update, another mod's files, removal, a damaged archive, the check of a game picked by
 * hand, and a power cut at every step of an install and of a removal.
 * Needs `zip` on the PATH. Run from the repository root: build/patch_test build/patch_sd
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>

#include "../source/core/fsutil.h"
#include "../source/core/patch.h"
#include "../source/core/record.h"
#include "../source/core/unzip.h"

#define TID 0x0004000000053F00ULL
#define LIVE "/luma/titles/0004000000053F00"

static int failures;
static char root[256], work[256];

static void expect(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

static void put(const char *rel, const char *text)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", work, rel);
    fs_write(path, text, strlen(text));
}

/* A patch archive with these files; returns its path. */
static const char *make_zip(const char *name, const char *const *files)
{
    static char zip[512];
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "rm -rf %s/z && mkdir -p %s/z", work, work);
    if (system(cmd))
        exit(2);
    for (int i = 0; files[i]; i += 2) {
        char rel[256];
        snprintf(rel, sizeof rel, "z/%s", files[i]);
        put(rel, files[i + 1]);
    }
    snprintf(zip, sizeof zip, "%s/%s.zip", work, name);
    snprintf(cmd, sizeof cmd, "rm -f %s && cd %s/z && zip -qrXD %s .", zip, work, zip);
    if (system(cmd))
        exit(2);
    return zip;
}

static int has(const char *rel, const char *text)
{
    char path[512], buf[64] = {0};
    snprintf(path, sizeof path, "%s%s", root, rel);
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    return !text || (n == strlen(text) && !memcmp(buf, text, n));
}

static int exists(const char *rel)
{
    char path[512];
    snprintf(path, sizeof path, "%s%s", root, rel);
    return fs_exists(path);
}

static Record mario(const char *version)
{
    Record r;
    memset(&r, 0, sizeof r);
    strcpy(r.slug, "super-mario-3d-land");
    strcpy(r.kind, "patch");
    strcpy(r.version, version);
    strcpy(r.region, "EUR");
    r.title_id = TID;
    strcpy(r.date, "25.10.2026");
    return r;
}

#define DATA "/3ds/butterscotch/undertale"

static int records(void);

/* A CIA that keeps its game on the card: its data is installed like a patch, into `dir`. */
static Record undertale(const char *version)
{
    Record r;
    memset(&r, 0, sizeof r);
    strcpy(r.slug, "undertale");
    strcpy(r.kind, "cia");
    strcpy(r.version, version);
    strcpy(r.region, "EUR");
    r.title_id = 0x0004000000EFAD00ULL;
    strcpy(r.dir, "3ds/butterscotch/undertale");
    strcpy(r.date, "26.10.2026");
    return r;
}

/* The game saved meanwhile: file0 is the game's, not the Hub's. */
static int data_v1(void)
{
    return has(DATA "/data.win", "game v1") && has(DATA "/mus/a.ogg", "music") && has(DATA "/file0", "save") &&
           records() == 1;
}

static int data_v2(void)
{
    return has(DATA "/data.win", "game v2") && !has(DATA "/mus/a.ogg", NULL) && has(DATA "/file0", "save") &&
           records() == 1;
}

static void reset(void)
{
    fs_budget = -1;
    fs_remove_tree(root);
    put("../sd/luma/config.ini", "[boot]\n");
}

static int records(void)
{
    Record *all;
    int n;
    record_load_all(root, &all, &n);
    for (int i = 0; i < n; i++)
        record_free(&all[i]);
    free(all);
    return n;
}

/* Old state: the other mod as it was, nothing of ours. New state: our v2 files, the record. */
static int old_state(void)
{
    return has(LIVE "/romfs/mod.bin", "other mod") && !has(LIVE "/romfs/font.szs", NULL) && records() == 0;
}

static int new_state(void)
{
    return has(LIVE "/romfs/font.szs", "font v1") && has(LIVE "/romfs/mod.bin", "other mod") == 0 &&
           exists(HUB_SUBDIR "/backup/0004000000053F00/romfs/mod.bin") && records() == 1;
}

static int clean(void)
{
    return !exists(LIVE ".hub-new") && !exists(LIVE ".hub-old") && !exists(LIVE ".hub-del") &&
           !exists(HUB_SUBDIR "/pending.txt");
}

int main(int argc, char **argv)
{
    const char *arg = argc > 1 ? argv[1] : "build/patch_sd";
    fs_remove_tree(arg);
    mkdir(arg, 0755);
    static char base[PATH_MAX];
    if (!realpath(arg, base)) /* absolute: the zips are made after a cd */
        return 2;
    snprintf(root, sizeof root, "%s/sd", base);
    snprintf(work, sizeof work, "%s/work", base);
    static const char *const v1[] = {"romfs/font.szs", "font v1", "romfs/text.msbt", "text v1", "locale.txt", "EUR RU", NULL};
    static const char *const v2[] = {"romfs/font.szs", "font v2", "locale.txt", "EUR RU", NULL};
    static const char *const checked[] = {"romfs/font.szs", "font v1", "check.txt",
                                          "a91f75b4cc66f9bef1f0f1d7d7c6c1b4e4c3c1a1f3b1fa36c41de1d1e0e2d3a0  romfs/Font.szs\n", NULL};
    char v1zip[512], v2zip[512];
    snprintf(v1zip, sizeof v1zip, "%s", make_zip("v1", v1));
    snprintf(v2zip, sizeof v2zip, "%s", make_zip("v2", v2));

    /* a fresh install */
    reset();
    Record r = mario("1.0.0");
    PatchProgress progress;
    expect(patch_install(root, v1zip, &r, &progress) == PATCH_OK, "install: ok");
    expect(has(LIVE "/romfs/font.szs", "font v1") && has(LIVE "/locale.txt", "EUR RU"), "install: files in luma/titles/<TID>");
    expect(r.n_files == 3 && records() == 1 && clean(), "install: record of 3 files, nothing left over");
    expect(progress.stage == PSTAGE_DONE && progress.done == 3, "install: progress");

    /* an update drops files the new version no longer has */
    Record r2 = mario("1.0.1");
    expect(patch_install(root, v2zip, &r2, NULL) == PATCH_OK, "update: ok");
    expect(has(LIVE "/romfs/font.szs", "font v2") && !has(LIVE "/romfs/text.msbt", NULL), "update: new set only");
    expect(records() == 1 && !exists(HUB_SUBDIR "/backup") && clean(), "update: one record, our old files are not backed up");

    PatchProgress counted = {0};
    int n2 = r2.n_files;
    expect(patch_remove(root, &r2, &counted) == PATCH_OK && !exists(LIVE) && records() == 0 && clean(), "remove: folder and record gone");
    expect(n2 > 0 && counted.total == (uint32_t)n2 && counted.done == (uint32_t)n2, "remove: counts its files");
    /* a retry after a failed install that had already taken the old one off removes it again */
    expect(patch_remove(root, &r2, NULL) == PATCH_OK && !exists(LIVE) && records() == 0 && clean(), "remove again: harmless");
    record_free(&r);
    record_free(&r2);

    /* another mod in the folder: kept aside, back after removal */
    reset();
    put("../sd" LIVE "/romfs/mod.bin", "other mod");
    r = mario("1.0.0");
    expect(patch_install(root, v1zip, &r, NULL) == PATCH_OK && new_state() && clean(), "other mod: moved aside, ours live");
    expect(patch_remove(root, &r, NULL) == PATCH_OK && old_state() && !exists(HUB_SUBDIR "/backup/0004000000053F00") && clean(),
           "other mod: back in place after removal");
    record_free(&r);

    /* a damaged archive changes nothing */
    reset();
    put("../sd" LIVE "/romfs/mod.bin", "other mod");
    char broken[512];
    snprintf(broken, sizeof broken, "%s/broken.zip", work);
    char cmd[1200];
    snprintf(cmd, sizeof cmd, "cp %s %s && printf 'XXXXXXXX' | dd of=%s bs=1 seek=91 conv=notrunc 2>/dev/null", v1zip, broken, broken);
    if (system(cmd))
        return 2;
    r = mario("1.0.0");
    expect(patch_install(root, broken, &r, NULL) == PATCH_BAD_ARCHIVE && old_state() && clean(), "damaged archive: refused, nothing changed");
    put("not-a-zip.zip", "hello");
    char notzip[512];
    snprintf(notzip, sizeof notzip, "%s/not-a-zip.zip", work);
    expect(patch_install(root, notzip, &r, NULL) == PATCH_BAD_ARCHIVE && old_state(), "not a zip: refused");
    /* a path the record cannot hold (200 bytes) would be cut short there and never removed */
    char long_zip[512];
    snprintf(long_zip, sizeof long_zip, "%s/long.zip", work);
    snprintf(cmd, sizeof cmd, "rm -rf %s/z %s && mkdir -p %s/z/romfs && cd %s/z && printf x > romfs/%0200d.bin && zip -qrXD %s .",
             work, long_zip, work, work, 0, long_zip);
    if (system(cmd))
        return 2;
    expect(patch_install(root, long_zip, &r, NULL) == PATCH_BAD_ARCHIVE && old_state() && clean(),
           "path too long for the record: refused, nothing changed");
    record_free(&r);

    /* power cut at every step of an install over another mod: old or new, never between */
    int steps = 0, worst_ok = 1;
    for (long cut = 0;; cut++) {
        reset();
        put("../sd" LIVE "/romfs/mod.bin", "other mod");
        r = mario("1.0.0");
        fs_budget = cut;
        PatchResult res = patch_install(root, v1zip, &r, NULL);
        fs_budget = -1;
        record_free(&r);
        patch_recover(root); /* the next start */
        int ok = (old_state() || new_state()) && clean();
        if (!ok) {
            printf("     cut after %ld writes: neither old nor new\n", cut);
            worst_ok = 0;
        }
        steps++;
        if (res == PATCH_OK)
            break;
    }
    printf("     (%d cut points)\n", steps);
    expect(worst_ok, "power cut during install: every cut ends old or new, nothing left over");

    /* and of a removal: ours goes, the other mod comes back */
    worst_ok = 1;
    steps = 0;
    for (long cut = 0;; cut++) {
        reset();
        put("../sd" LIVE "/romfs/mod.bin", "other mod");
        r = mario("1.0.0");
        patch_install(root, v1zip, &r, NULL);
        fs_budget = cut;
        PatchResult res = patch_remove(root, &r, NULL);
        fs_budget = -1;
        patch_recover(root);
        int ok = (old_state() || (res != PATCH_OK && new_state())) && clean();
        if (!ok) {
            printf("     cut after %ld writes: removal left a mess\n", cut);
            worst_ok = 0;
        }
        record_free(&r);
        steps++;
        if (res == PATCH_OK)
            break;
    }
    printf("     (%d cut points)\n", steps);
    expect(worst_ok, "power cut during removal: finished on the next start, the other mod back");

    /* power cut at every step of an update v1 -> v2: v1 with its record, or v2 with its record */
    worst_ok = 1;
    steps = 0;
    for (long cut = 0;; cut++) {
        reset();
        r = mario("1.0.0");
        patch_install(root, v1zip, &r, NULL);
        record_free(&r);
        Record up = mario("1.0.1");
        fs_budget = cut;
        PatchResult res = patch_install(root, v2zip, &up, NULL);
        fs_budget = -1;
        record_free(&up);
        patch_recover(root);
        Record *all;
        int n;
        record_load_all(root, &all, &n);
        int v1 = n == 1 && !strcmp(all[0].version, "1.0.0") && has(LIVE "/romfs/text.msbt", "text v1");
        int v2 = n == 1 && !strcmp(all[0].version, "1.0.1") && has(LIVE "/romfs/font.szs", "font v2") && !has(LIVE "/romfs/text.msbt", NULL);
        for (int i = 0; i < n; i++)
            record_free(&all[i]);
        free(all);
        if (!((v1 || v2) && clean())) {
            printf("     cut after %ld writes: update left a mix\n", cut);
            worst_ok = 0;
        }
        steps++;
        if (res == PATCH_OK)
            break;
    }
    printf("     (%d cut points)\n", steps);
    expect(worst_ok, "power cut during an update: the old version or the new one, each with its record");

    /* a CIA's data folder: installed, updated and removed around the game's saves */
    static const char *const d1[] = {"data.win", "game v1", "mus/a.ogg", "music", NULL};
    static const char *const d2[] = {"data.win", "game v2", NULL};
    char d1zip[512], d2zip[512];
    snprintf(d1zip, sizeof d1zip, "%s", make_zip("d1", d1));
    snprintf(d2zip, sizeof d2zip, "%s", make_zip("d2", d2));
    reset();
    put("../sd/3ds/butterscotch/other/data.win", "another game");
    r = undertale("1.0");
    expect(patch_install(root, d1zip, &r, NULL) == PATCH_OK && has(DATA "/data.win", "game v1") && clean(),
           "data: unpacked into its folder");
    expect(!exists(LIVE) && !exists(HUB_SUBDIR "/backup"), "data: nothing under luma/titles, nothing backed up");
    put("../sd" DATA "/file0", "save");
    Record rd2 = undertale("1.1");
    expect(patch_install(root, d2zip, &rd2, NULL) == PATCH_OK && data_v2() && clean(), "data update: new files, the save kept");
    PatchProgress gone = {0};
    expect(patch_remove(root, &rd2, &gone) == PATCH_OK && has(DATA "/file0", "save") && !has(DATA "/data.win", NULL) &&
               records() == 0 && clean(),
           "data removal: ours gone, the save stays");
    expect(gone.total == 1 && gone.done == 1 && gone.bytes_total == 0, "data removal: counts its files");
    expect(has("/3ds/butterscotch/other/data.win", "another game"), "data: the folder next to it untouched");
    record_free(&r);
    record_free(&rd2);

    reset();
    r = undertale("1.0");
    patch_install(root, d1zip, &r, NULL);
    expect(patch_remove(root, &r, NULL) == PATCH_OK && !exists(DATA) && !exists("/3ds/butterscotch"), "data removal: empty folders go too");
    record_free(&r);

    /* power cut at every step of a data update with a save in the folder */
    worst_ok = 1;
    steps = 0;
    for (long cut = 0;; cut++) {
        reset();
        r = undertale("1.0");
        patch_install(root, d1zip, &r, NULL);
        record_free(&r);
        put("../sd" DATA "/file0", "save");
        Record up = undertale("1.1");
        fs_budget = cut;
        PatchResult res = patch_install(root, d2zip, &up, NULL);
        fs_budget = -1;
        record_free(&up);
        patch_recover(root);
        if (!((data_v1() || data_v2()) && clean())) {
            printf("     cut after %ld writes: data update left a mix or lost the save\n", cut);
            worst_ok = 0;
        }
        steps++;
        if (res == PATCH_OK)
            break;
    }
    printf("     (%d cut points)\n", steps);
    expect(worst_ok, "power cut during a data update: old or new, the save kept");

    /* and of a data removal: finished on the next start, the save kept */
    worst_ok = 1;
    steps = 0;
    for (long cut = 0;; cut++) {
        reset();
        r = undertale("1.0");
        patch_install(root, d1zip, &r, NULL);
        put("../sd" DATA "/file0", "save");
        fs_budget = cut;
        PatchResult res = patch_remove(root, &r, NULL);
        fs_budget = -1;
        patch_recover(root);
        int gone = !has(DATA "/data.win", NULL) && !has(DATA "/mus/a.ogg", NULL) && records() == 0;
        if (!((gone || (res != PATCH_OK && data_v1())) && has(DATA "/file0", "save") && clean())) {
            printf("     cut after %ld writes: data removal left a mess\n", cut);
            worst_ok = 0;
        }
        record_free(&r);
        steps++;
        if (res == PATCH_OK)
            break;
    }
    printf("     (%d cut points)\n", steps);
    expect(worst_ok, "power cut during a data removal: finished on the next start, the save kept");

    /* a deflated file whose first 32 KB of input inflate to exactly 32 KB: the output chunk fills
     * just as the input runs out, and the next inflate() says Z_BUF_ERROR ("nothing to do") - not
     * damage. Undertale's data.win and three .ogg hit it (2026-10-26); this file, from the LCG
     * below, hits it at the first chunk with Info-ZIP's zip 3.0. */
    snprintf(cmd, sizeof cmd, "rm -rf %s/edge && mkdir -p %s/edge", work, work);
    if (system(cmd))
        return 2;
    size_t edge_len = 200000;
    char *edge = malloc(edge_len);
    unsigned x = 12;
    for (size_t k = 0; k < edge_len; k++) {
        x = x * 1103515245u + 12345u;
        edge[k] = (char)((x >> 16) % 254);
    }
    char edge_path[512], edge_zip[512], edge_out[512];
    snprintf(edge_path, sizeof edge_path, "%s/edge/f.bin", work);
    fs_write(edge_path, edge, edge_len);
    snprintf(edge_zip, sizeof edge_zip, "%s/edge.zip", work);
    snprintf(cmd, sizeof cmd, "rm -f %s && zip -qXDj %s %s", edge_zip, edge_zip, edge_path);
    if (system(cmd))
        return 2;
    snprintf(edge_out, sizeof edge_out, "%s/edge-out.bin", work);
    Zip ez;
    uint8_t edge_sha[32];
    int extracted = zip_open(&ez, edge_zip) == 0 && zip_extract(&ez, 0, edge_out, edge_sha) == 0;
    if (extracted)
        zip_close(&ez);
    char *edge_back = malloc(edge_len);
    FILE *ef = fopen(edge_out, "rb");
    int same = ef && fread(edge_back, 1, edge_len, ef) == edge_len && !memcmp(edge_back, edge, edge_len);
    if (ef)
        fclose(ef);
    free(edge_back);
    free(edge);
    expect(extracted && same, "unzip: an output chunk that fills exactly as the input runs out is not damage");

    /* the check for a game picked by hand */
    reset();
    put("../game/Font.szs", "font v1");
    char game[512], cz[512];
    snprintf(game, sizeof game, "%s/game", base);
    /* check.txt with the real sha256 of "font v1" */
    char line[200];
    snprintf(cmd, sizeof cmd, "printf 'font v1' | shasum -a 256 | cut -c1-64 > %s/sha.txt", work);
    if (system(cmd))
        return 2;
    FILE *sf = fopen(strcat(strcpy(line, work), "/sha.txt"), "r");
    char sha[65] = "";
    if (!sf || fscanf(sf, "%64s", sha) != 1)
        return 2;
    fclose(sf);
    char check_line[160];
    snprintf(check_line, sizeof check_line, "%s  romfs/Font.szs\n%s  romfs/Missing.szs\n", sha, sha);
    const char *const with_check[] = {"romfs/font.szs", "font v1", "check.txt", check_line, NULL};
    snprintf(cz, sizeof cz, "%s", make_zip("checked", with_check));
    int matched, total;
    expect(patch_check_game(cz, game, &matched, &total) == 0 && matched == 1 && total == 2, "check: 1 of 2 game files match");
    expect(patch_check_game(v1zip, game, &matched, &total) == -1, "check: no check.txt, no verdict");
    (void)checked;
    r = mario("1.0.0");
    expect(patch_install(root, cz, &r, NULL) == PATCH_OK && !has(LIVE "/check.txt", NULL), "check.txt is not installed into the game");
    record_free(&r);

    /* a record survives a round trip, spaces in paths included */
    Record w = mario("2.0");
    w.manual = w.risky = true;
    RecFile files[] = {{"ab", 7, "romfs/Some Dir/file name.bin"}};
    w.files = files;
    w.n_files = 1;
    char *text;
    size_t len;
    Record back;
    expect(record_format(&w, &text, &len) == 0 && record_parse(&back, text) == 0 && back.manual && back.risky &&
               back.title_id == TID && back.n_files == 1 && !strcmp(back.files[0].path, "romfs/Some Dir/file name.bin"),
           "record: round trip");
    free(text);
    record_free(&back);

    fs_remove_tree(base);
    printf("%s\n", failures ? "FAILED" : "all passed");
    return failures != 0;
}
