#include "job.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/fsutil.h"
#include "core/lumacfg.h"
#include "core/sha256.h"
#include "paths.h"
#include "titles.h"

#define ROOT "sdmc:"
#define DOWNLOADS HUB_DIR "/downloads"
#define LUMA_CONFIG "sdmc:/luma/config.ini"
#define LUMA_INFO 0x10000
#define LUMA_CFG_GAME_PATCHING (1u << 2) /* svcGetSystemInfo(0x10000, 3), see 3ds-ua's checks.c */
#define BATTERY_MIN 2
#define CITRA_INFO 0x20000

u32 job_luma_version(void)
{
    static s64 version = -1; /* asked once: Help draws it every frame */
    if (version < 0) {
        s64 out = 0;
        version = R_SUCCEEDED(svcGetSystemInfo(&out, LUMA_INFO, 0)) ? (u32)out : 0;
    }
    return (u32)version;
}

bool job_is_emulator(void)
{
    static int known = -1;
    if (known < 0) {
        s64 out = 0;
        /* Citra answered 1, Azahar 2026 answers 2 (2026-10-25): any non-zero */
        known = R_SUCCEEDED(svcGetSystemInfo(&out, CITRA_INFO, 0)) && out != 0;
    }
    return known;
}

bool job_game_patching_on(void)
{
    s64 out = 0;
    return R_SUCCEEDED(svcGetSystemInfo(&out, LUMA_INFO, 3)) && (out & LUMA_CFG_GAME_PATCHING);
}

u64 job_sd_free(void)
{
    FS_ArchiveResource sd;
    if (R_FAILED(FSUSER_GetArchiveResource(&sd, SYSTEM_MEDIATYPE_SD)))
        return 0;
    return (u64)sd.freeClusters * sd.clusterSize;
}

bool job_enable_game_patching(void)
{
    FILE *f = fopen(LUMA_CONFIG, "rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = len > 0 ? malloc((size_t)len + 1) : NULL;
    bool ok = text && fread(text, 1, (size_t)len, f) == (size_t)len;
    fclose(f);
    LumaConfig cfg;
    if (ok)
        ok = !lumacfg_parse(&cfg, text, (size_t)len);
    if (ok && *cfg.game != '1') {
        *cfg.game = '1';
        uint8_t sha[32];
        sha256(text, (size_t)len, sha);
        ok = !fs_replace_verified(LUMA_CONFIG, text, (size_t)len, sha);
    }
    free(text);
    return ok;
}

static void hex(const uint8_t digest[32], char out[65])
{
    for (int i = 0; i < 32; i++)
        snprintf(out + 2 * i, 3, "%02x", digest[i]);
}

static void size_text(char *out, size_t size, u64 bytes)
{
    if (bytes < 1024 * 1024)
        snprintf(out, size, "%llu КБ", (unsigned long long)((bytes + 1023) / 1024));
    else
        snprintf(out, size, "%llu МБ", (unsigned long long)((bytes + 512 * 1024) / (1024 * 1024)));
}

/* What the network said, for "Деталі для звіту". */
static void net_tech(Job *job, NetResult r)
{
    static const char *const names[] = {"OK", "FAILED", "LOST", "SERVER", "NOT_FOUND", "TOO_BIG", "NO_MEMORY", "WRITE", "CANCELLED"};
    int n = snprintf(job->tech, sizeof job->tech, "мережа %s", names[r]);
    if (job->net.status && n > 0 && (size_t)n < sizeof job->tech)
        n += snprintf(job->tech + n, sizeof job->tech - n, " · HTTP %lu", (unsigned long)job->net.status);
    if (n > 0 && (size_t)n < sizeof job->tech) {
        snprintf(job->tech + n, sizeof job->tech - n, " · %lu з %lu Б", (unsigned long)job->net.done,
                 (unsigned long)job->net.total);
    }
    job->sys_result = job->net.result;
}

static ErrorCode from_net(NetResult r)
{
    switch (r) {
    case NET_OK: return ERR_NONE;
    case NET_FAILED: return ERR_NET_OFF;
    case NET_LOST: return ERR_NET_LOST;
    case NET_WRITE: return ERR_SD_WRITE;
    case NET_CANCELLED: return ERR_NONE;
    default: return ERR_SERVER;
    }
}

/* Before anything is written: Luma (patches only), room on the card, the battery. */
static bool preflight(Job *job, u64 needed)
{
    if (job->item_kind == KIND_PATCH && !job_is_emulator() && job_luma_version() < LUMA_MIN_GAMES) {
        snprintf(job->tech, sizeof job->tech, "Luma 0x%08lX, потрібна 8.0+", (unsigned long)job_luma_version());
        job->error = ERR_LUMA_OLD;
        return false;
    }
    u64 free_bytes = job_sd_free();
    if (free_bytes && free_bytes < needed) {
        char need[32], have[32];
        size_text(need, sizeof need, needed);
        size_text(have, sizeof have, free_bytes);
        snprintf(job->detail, sizeof job->detail, "%s, вільно %s", need, have);
        snprintf(job->tech, sizeof job->tech, "потрібно %llu Б, вільно %llu Б", (unsigned long long)needed,
                 (unsigned long long)free_bytes);
        job->error = ERR_SD_SPACE;
        return false;
    }
    u8 level = 5, charging = 0;
    bool adapter = false;
    PTMU_GetBatteryLevel(&level);
    PTMU_GetAdapterState(&adapter);
    PTMU_GetBatteryChargeState(&charging);
    if (level < BATTERY_MIN && !adapter && !charging) {
        snprintf(job->tech, sizeof job->tech, "заряд %u з 5", level);
        job->error = ERR_BATTERY_LOW;
        return false;
    }
    return true;
}

/* The archive, fetched (or resumed) into downloads/ and checked against the catalog: the patch, or
 * a CIA's data (`stage` JS_DOWNLOAD or JS_DATA). */
static bool fetch_archive(Job *job, const CatFile *file, u32 bytes, JobStage stage, char *zip, size_t size)
{
    char path[160];
    snprintf(zip, size, DOWNLOADS "/%s.zip", file->sha256);
    snprintf(path, sizeof path, "/v1/f/%s", file->key);
    snprintf(job->where, sizeof job->where, "%s", path);
    fs_make_parents(zip);
    job->stage = stage;
    memset(&job->net, 0, sizeof job->net);
    NetResult r = net_download(path, zip, bytes, &job->net);
    if (r == NET_CANCELLED) {
        job->cancelled = true;
        fs_remove(zip); /* cancelling leaves nothing behind */
        return false;
    }
    if (r != NET_OK) {
        job->error = from_net(r);
        net_tech(job, r);
        if (r == NET_WRITE)
            snprintf(job->where, sizeof job->where, "%s", zip);
        if (r == NET_LOST) {
            char done[32], left[32];
            size_text(done, sizeof done, job->net.done);
            size_text(left, sizeof left, bytes - job->net.done);
            snprintf(job->detail, sizeof job->detail, " Завантажено %s, лишилось %s.", done, left);
        }
        return false;
    }
    if (stage == JS_DOWNLOAD)
        job->stage = JS_VERIFY; /* a CIA's data stays in JS_DATA: the step list has no row for it */
    uint8_t digest[32];
    char got[65] = "не прочитано";
    if (fs_sha256(zip, digest) == 0) {
        hex(digest, got);
        if (!strcmp(got, file->sha256))
            return true;
    }
    snprintf(job->tech, sizeof job->tech, "sha256 %.12s…, у каталозі %.12s…", got, file->sha256);
    fs_remove(zip); /* the next try downloads it whole */
    job->error = ERR_FILE_CORRUPT;
    return false;
}

/* A file that fails its hash is fetched once more from scratch before it counts as an error (G4):
 * a bad copy on the way, or a resumed part from a different file. */
static bool fetch_checked(Job *job, const CatFile *file, u32 bytes, JobStage stage, char *zip, size_t size)
{
    if (fetch_archive(job, file, bytes, stage, zip, size) || job->error != ERR_FILE_CORRUPT)
        return !job->error && !job->cancelled;
    job->error = ERR_NONE;
    job->tech[0] = 0;
    return fetch_archive(job, file, bytes, stage, zip, size);
}

static void today(char *out, size_t size)
{
    time_t now = time(NULL);
    struct tm *t = gmtime(&now);
    snprintf(out, size, "%02d.%02d.%04d", t->tm_mday % 100, (t->tm_mon + 1) % 100, (t->tm_year + 1900) % 10000);
}

static void fill_record(const Job *job, Record *rec)
{
    memset(rec, 0, sizeof *rec);
    snprintf(rec->slug, sizeof rec->slug, "%s", job->slug);
    snprintf(rec->kind, sizeof rec->kind, "%s", job->item_kind == KIND_PATCH ? "patch" : "cia");
    snprintf(rec->version, sizeof rec->version, "%s", job->version);
    snprintf(rec->region, sizeof rec->region, "%s", job->variant.region);
    rec->title_id = job->target_tid;
    rec->manual = job->manual;
    rec->risky = job->risky;
    if (job->item_kind == KIND_CIA && job->variant.data.present)
        snprintf(rec->dir, sizeof rec->dir, "%s", job->variant.data_dir);
    today(rec->date, sizeof rec->date);
}

static void install_patch(Job *job)
{
    char zip[200];
    if (!preflight(job, (u64)job->variant.size * 3) ||
        !fetch_checked(job, &job->variant.file, job->variant.size, JS_DOWNLOAD, zip, sizeof zip))
        return;
    job->stage = JS_INSTALL;
    if (job->replacing && patch_remove(ROOT, &job->removing, NULL) != PATCH_OK) {
        snprintf(job->where, sizeof job->where, "luma/titles/%016llX", (unsigned long long)job->removing.title_id);
        snprintf(job->tech, sizeof job->tech, "не знято переклад з попередньої гри");
        job->error = ERR_SD_WRITE;
        return;
    }
    Record rec;
    fill_record(job, &rec);
    PatchResult r = patch_install(ROOT, zip, &rec, &job->patch);
    record_free(&rec);
    if (r != PATCH_OK) {
        snprintf(job->where, sizeof job->where, "luma/titles/%016llX", (unsigned long long)job->target_tid);
        snprintf(job->tech, sizeof job->tech, "%s · файл %lu з %lu",
                 r == PATCH_BAD_ARCHIVE ? "архів не розпакувався" : "SD-карта не прийняла запис",
                 (unsigned long)job->patch.done + 1, (unsigned long)job->patch.total);
        job->error = r == PATCH_BAD_ARCHIVE ? ERR_FILE_CORRUPT : ERR_SD_WRITE;
        if (r == PATCH_BAD_ARCHIVE)
            fs_remove(zip);
        return;
    }
    fs_remove(zip);
    job->stage = JS_LUMA;
    if (!job_is_emulator() && !job_game_patching_on()) {
        job->needs_reboot = true;
        job->luma_by_hand = !job_enable_game_patching();
    }
}

/* ---------------------------------------------------------------- CIA */

typedef struct {
    Handle cia;
    u64 offset;
    Sha256 sha;
    Result failed; /* the write the system refused */
} CiaSink;

static bool to_cia(const u8 *data, u32 len, void *ctx)
{
    CiaSink *s = ctx;
    u32 written = 0;
    Result r = FSFILE_Write(s->cia, &written, s->offset, data, len, 0);
    if (R_FAILED(r) || written != len) {
        s->failed = r;
        return false;
    }
    sha256_update(&s->sha, data, len);
    s->offset += len;
    return true;
}

/* A CIA that keeps its game on the SD card: the data zip, fetched like a patch and unpacked into
 * its folder the same way (the game's saves there stay). The CIA is already in the system. */
static bool install_data(Job *job)
{
    char zip[200];
    if (!fetch_checked(job, &job->variant.data, job->variant.data_size, JS_DATA, zip, sizeof zip))
        return false;
    job->stage = JS_DATA_COPY;
    Record rec;
    fill_record(job, &rec);
    PatchResult r = patch_install(ROOT, zip, &rec, &job->patch); /* saves the record */
    record_free(&rec);
    if (r != PATCH_OK) {
        snprintf(job->where, sizeof job->where, "%s", job->variant.data_dir);
        snprintf(job->tech, sizeof job->tech, "%s · файл %lu з %lu",
                 r == PATCH_BAD_ARCHIVE ? "архів гри не розпакувався" : "SD-карта не прийняла запис",
                 (unsigned long)job->patch.done + 1, (unsigned long)job->patch.total);
        job->error = r == PATCH_BAD_ARCHIVE ? ERR_FILE_CORRUPT : ERR_SD_WRITE;
        if (r == PATCH_BAD_ARCHIVE)
            fs_remove(zip);
        return false;
    }
    fs_remove(zip);
    return true;
}

static void install_cia(Job *job)
{
    const CatVariant *v = &job->variant;
    /* the CIA, then the data's zip and its unpacked files side by side */
    if (!preflight(job, (u64)v->size + (v->data.present ? (u64)v->data_size * 2 : 0) + 16 * 1024 * 1024))
        return;
    bool was_there = titles_installed_version(job->target_tid) >= 0;
    CiaSink sink = {0};
    sha256_init(&sink.sha);
    Result started = AM_StartCiaInstall(MEDIATYPE_SD, &sink.cia);
    if (R_FAILED(started)) {
        job->sys_result = started;
        snprintf(job->tech, sizeof job->tech, "AM_StartCiaInstall");
        job->error = ERR_AM_FAIL;
        snprintf(job->detail, sizeof job->detail, " Код системи 0x%08lX.", (unsigned long)started);
        return;
    }
    char path[160];
    snprintf(path, sizeof path, "/v1/f/%s", job->variant.file.key);
    snprintf(job->where, sizeof job->where, "%s", path);
    job->stage = JS_DOWNLOAD; /* written into the system as it arrives: no copy on the card */
    NetResult r = net_stream(path, to_cia, &sink, &job->net);
    uint8_t digest[32];
    char got[65];
    sha256_final(&sink.sha, digest);
    hex(digest, got);
    if (r != NET_OK || sink.offset != job->variant.size || strcmp(got, job->variant.file.sha256)) {
        AM_CancelCIAInstall(sink.cia); /* nothing half-installed stays */
        if (r == NET_CANCELLED) {
            job->cancelled = true;
        } else if (r == NET_WRITE) {
            job->error = ERR_AM_FAIL;
            job->sys_result = sink.failed;
            snprintf(job->tech, sizeof job->tech, "FSFILE_Write · %llu Б записано", (unsigned long long)sink.offset);
        } else if (r != NET_OK) {
            job->error = from_net(r);
            net_tech(job, r);
        } else {
            job->error = ERR_FILE_CORRUPT;
            snprintf(job->tech, sizeof job->tech, "sha256 %.12s…, у каталозі %.12s… · %llu Б", got,
                     job->variant.file.sha256, (unsigned long long)sink.offset);
        }
        return;
    }
    job->stage = JS_INSTALL;
    Result finished = AM_FinishCiaInstall(sink.cia);
    if (R_FAILED(finished)) {
        job->sys_result = finished;
        snprintf(job->tech, sizeof job->tech, "AM_FinishCiaInstall");
        job->error = ERR_AM_FAIL;
        snprintf(job->detail, sizeof job->detail, " Код системи 0x%08lX.", (unsigned long)finished);
        return;
    }
    if (v->data.present) {
        /* nothing half-installed stays: a new CIA goes again without its game (an update keeps the
         * old data, so the program stays too); a broken download is kept to resume */
        if (!install_data(job) && !was_there)
            AM_DeleteTitle(MEDIATYPE_SD, job->target_tid);
        return;
    }
    Record rec;
    fill_record(job, &rec);
    if (record_save(ROOT, &rec)) {
        snprintf(job->where, sizeof job->where, HUB_SUBDIR "/installed/%s.txt", job->slug);
        job->error = ERR_SD_WRITE;
    }
}

/* ---------------------------------------------------------------- the job */

static void check_game(Job *job)
{
    char zip[200];
    if (!fetch_checked(job, &job->variant.file, job->variant.size, JS_DOWNLOAD, zip, sizeof zip))
        return;
    job->stage = JS_VERIFY;
    Result mounted = romfsMountFromTitle(job->target_tid, job->media, "game");
    if (R_FAILED(mounted)) {
        job->check_has = false; /* not an error: the pick screen says there was nothing to compare */
        return;
    }
    job->check_has = patch_check_game(zip, "game:", &job->check_matched, &job->check_total) == 0;
    romfsUnmount("game");
}

static void run(void *arg)
{
    Job *job = arg;
    switch (job->kind) {
    case JOB_INSTALL:
        if (job->item_kind == KIND_PATCH) {
            install_patch(job);
        } else {
            install_cia(job);
            /* once more, as for a patch: AM threw the bad copy away (the data retries on its own) */
            if (job->error == ERR_FILE_CORRUPT && job->stage < JS_DATA) {
                job->error = ERR_NONE;
                job->tech[0] = 0;
                install_cia(job);
            }
        }
        break;
    case JOB_REMOVE:
        job->stage = JS_INSTALL;
        if (!strcmp(job->removing.kind, "cia")) {
            Result r = AM_DeleteTitle(MEDIATYPE_SD, job->removing.title_id);
            /* gone already counts as removed */
            if (R_FAILED(r) && titles_installed_version(job->removing.title_id) >= 0) {
                job->sys_result = r;
                snprintf(job->tech, sizeof job->tech, "AM_DeleteTitle");
                job->error = ERR_AM_FAIL;
            } else if (job->removing.dir[0]) {
                job->stage = JS_DATA_COPY; /* the game's files; its saves stay */
                if (patch_remove(ROOT, &job->removing, &job->patch) != PATCH_OK) {
                    snprintf(job->where, sizeof job->where, "%s", job->removing.dir);
                    job->error = ERR_SD_WRITE;
                }
            } else if (record_delete(ROOT, job->removing.slug)) {
                snprintf(job->where, sizeof job->where, HUB_SUBDIR "/installed/%s.txt", job->removing.slug);
                job->error = ERR_SD_WRITE;
            }
        } else if (patch_remove(ROOT, &job->removing, &job->patch) != PATCH_OK) {
            snprintf(job->where, sizeof job->where, "luma/titles/%016llX", (unsigned long long)job->removing.title_id);
            job->error = ERR_SD_WRITE;
        }
        break;
    case JOB_CHECK:
        check_game(job);
        break;
    }
    job->failed_at = job->stage;
    job->stage = JS_DONE;
    job->finished = osGetTime();
    job->running = false;
}

void job_start(Job *job)
{
    job->error = ERR_NONE;
    job->detail[0] = job->where[0] = job->tech[0] = 0;
    job->sys_result = 0;
    job->cancelled = job->needs_reboot = job->luma_by_hand = false;
    job->check_has = false;
    job->check_matched = job->check_total = 0;
    job->stage = JS_CHECK;
    memset(&job->net, 0, sizeof job->net);
    memset(&job->patch, 0, sizeof job->patch);
    job->started = osGetTime();
    job->running = true;
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    job->thread = threadCreate(run, job, 0x10000, prio + 1, -2, false);
    if (!job->thread)
        run(job);
}

bool job_poll(Job *job)
{
    if (job->running)
        return false;
    if (job->thread) {
        threadJoin(job->thread, U64_MAX);
        threadFree(job->thread);
        job->thread = NULL;
    }
    return true;
}

void job_discard_download(const Job *job)
{
    char zip[200];
    snprintf(zip, sizeof zip, DOWNLOADS "/%s.zip", job->variant.file.sha256);
    fs_remove(zip);
    if (job->variant.data.present) {
        snprintf(zip, sizeof zip, DOWNLOADS "/%s.zip", job->variant.data.sha256);
        fs_remove(zip);
    }
}

void job_cancel(Job *job)
{
    job->net.cancel = true;
}
