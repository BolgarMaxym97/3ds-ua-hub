#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/fsutil.h"
#include "core/patch.h"
#include "shots.h"
#include "titles.h"
#include "ui.h"

#define ROOT "sdmc:"
#define BACKUP_DIR ROOT HUB_SUBDIR "/backup"

static const Screen *const screens[SCREEN_COUNT] = {
    [SCREEN_SPLASH] = &screen_splash,
    [SCREEN_CHECK] = &screen_check,
    [SCREEN_MESSAGE] = &screen_message,
    [SCREEN_CATALOG] = &screen_catalog,
    [SCREEN_FILTER] = &screen_filter,
    [SCREEN_DETAILS] = &screen_details,
    [SCREEN_PROGRESS] = &screen_progress,
    [SCREEN_DONE] = &screen_done,
    [SCREEN_NOTFOUND] = &screen_notfound,
    [SCREEN_WHICH] = &screen_which,
    [SCREEN_PICK] = &screen_pick,
    [SCREEN_REMOVE] = &screen_remove,
    [SCREEN_ERROR] = &screen_error,
    [SCREEN_HELP] = &screen_help,
    [SCREEN_LOG] = &screen_log,
    [SCREEN_GALLERY] = &screen_gallery,
    [SCREEN_NOGAME] = &screen_nogame,
    [SCREEN_FALLBACK] = &screen_fallback,
};

static void free_records(App *app)
{
    for (int i = 0; i < app->n_records; i++)
        record_free(&app->records[i]);
    free(app->records);
    app->records = NULL;
    app->n_records = 0;
}

static void load_records(App *app)
{
    char held[64] = ""; /* the checks point into the records: follow them to the new ones */
    if (app->checks.rec)
        snprintf(held, sizeof held, "%s", app->checks.rec->slug);
    free_records(app);
    if (record_load_all(ROOT, &app->records, &app->n_records)) {
        app->records = NULL;
        app->n_records = 0;
    }
    app->checks.rec = held[0] ? app_record(app, held) : NULL;
}

/* The games on the console again: names and icons are read only for games that are new, so this
 * is quick; the loader's own pass, if it is still running, lands later and changes nothing. */
static void rescan_titles(App *app)
{
    Titles fresh = {0};
    titles_scan(&fresh, &app->hub.titles, app->hub.titles.full, NULL);
    titles_free(&app->hub.titles);
    app->hub.titles = fresh;
    app->hub.titles_ready = true;
}

void app_init(App *app)
{
    memset(app, 0, sizeof *app);
    app->back = SCREEN_CATALOG;
    app->checked = -1;
    app->opts = (ViewOpts){TAB_ALL, KINDS_ALL, SORT_NAME};
    stats_init(&app->stats);
    /* before anything reads the card: finish or undo what a power cut stopped */
    app->recovered = patch_recover(ROOT, app->recovered_slug, sizeof app->recovered_slug);
    load_records(app);
    hub_start(&app->hub);
    app_go(app, SCREEN_SPLASH);
}

void app_exit(App *app)
{
    if (app->job_active) { /* START during a job is ignored, but HOME can still close the app */
        job_cancel(&app->job);
        while (!job_poll(&app->job))
            svcSleepThread(10 * 1000 * 1000);
    }
    stats_exit(&app->stats);
    shots_exit();
    hub_exit(&app->hub);
    free(app->states);
    free(app->list);
    free(app->picks);
    free(app->log);
    free_records(app);
}

void app_open_catalog(App *app)
{
    app_go(app, SCREEN_CATALOG);
    if (app->recovered != RECOVER_NONE && !app->recovered_told) { /* the power cut's notice, over the catalog */
        app->recovered_told = true;
        /* the title from the catalog, the slug if it no longer has the entry */
        snprintf(app->job_title, sizeof app->job_title, "%s", app->recovered_slug);
        for (int i = 0; i < app->hub.catalog.n; i++)
            if (!strcmp(app->hub.catalog.items[i].slug, app->recovered_slug))
                snprintf(app->job_title, sizeof app->job_title, "%s", app->hub.catalog.items[i].title);
        static const char *const outcome[] = {
            [RECOVER_INSTALLED] = "встановлення завершено",
            [RECOVER_ROLLED_BACK] = "повернуто як було",
            [RECOVER_REMOVED] = "видалення завершено",
            [RECOVER_CLEARED] = "недороблене прибрано",
        };
        hublog_add("error", app->job_title, "INTERRUPTED", outcome[app->recovered]);
        app_error(app, ERR_INTERRUPTED, NULL);
    }
}

void app_go(App *app, ScreenId screen)
{
    /* back from the log is back to Help, whose own B still leads where Help was opened from */
    if ((screen == SCREEN_HELP || screen == SCREEN_FILTER) && app->screen != screen && app->screen != SCREEN_LOG)
        app->back = app->screen;
    app->screen = screen;
    app->focus = 0;
    app->entered_at = osGetTime();
    if (screens[screen]->enter)
        screens[screen]->enter(app);
}

const Record *app_record(const App *app, const char *slug)
{
    for (int i = 0; i < app->n_records; i++)
        if (!strcmp(app->records[i].slug, slug))
            return &app->records[i];
    return NULL;
}

static ItemState state_of(const App *app, const CatItem *it)
{
    const Record *rec = app_record(app, it->slug);
    if (!rec)
        return ITEM_NEW;
    /* a CIA removed from the system settings is not installed any more, whatever the record says */
    if (it->kind == KIND_CIA && titles_installed_version(rec->title_id) < 0)
        return ITEM_NEW;
    return version_cmp(it->version, rec->version) > 0 ? ITEM_UPDATE : ITEM_INSTALLED;
}

/* A CIA of the catalog put on the console without the Hub (FBI, Universal-Updater) gets its
 * record written now, as if the Hub had installed it: it shows as installed and can be removed. */
static void adopt_cias(App *app, const Catalog *cat)
{
    bool saved = false;
    for (int i = 0; i < cat->n; i++) {
        const CatItem *it = &cat->items[i];
        int v = app_record(app, it->slug) ? -1 : resolve_installed_cia(it, titles_installed_version);
        if (v < 0)
            continue;
        Record r = {0};
        snprintf(r.slug, sizeof r.slug, "%s", it->slug);
        snprintf(r.kind, sizeof r.kind, "cia");
        snprintf(r.version, sizeof r.version, "%s", it->version);
        snprintf(r.region, sizeof r.region, "%s", it->variants[v].region);
        snprintf(r.date, sizeof r.date, "не через Hub");
        r.title_id = it->variants[v].title_id;
        if (record_save(ROOT, &r) == 0)
            saved = true;
    }
    if (saved)
        load_records(app);
}

void app_rebuild_list(App *app)
{
    const Catalog *cat = &app->hub.catalog;
    adopt_cias(app, cat);
    int keep = app->list_n && app->sel < app->list_n ? app->list[app->sel] : -1;
    free(app->states);
    free(app->list);
    app->states = calloc(cat->n ? (size_t)cat->n : 1, sizeof *app->states);
    app->list = calloc(cat->n ? (size_t)cat->n : 1, sizeof *app->list);
    for (int i = 0; app->states && i < cat->n; i++)
        app->states[i] = state_of(app, &cat->items[i]);
    app->list_n = app->states && app->list ? view_build(cat, app->states, &app->opts, app->list) : 0;
    app->sel = 0;
    for (int i = 0; i < app->list_n; i++)
        if (app->list[i] == keep)
            app->sel = i;
    if (app->scroll > app->sel)
        app->scroll = app->sel;
}

/* ---------------------------------------------------------------- checks (C1-C6) */

static void version_text(char *out, size_t size, int v)
{
    if (v == UPDATE_NONE || v == 0) /* 0: a translation for the game without its update */
        snprintf(out, size, "без оновлення");
    else
        snprintf(out, size, "v%d.%d", v >> 10, (v >> 4) & 0x3F);
}

static void size_text(char *out, size_t size, u64 bytes)
{
    if (bytes < 1000 * 1024)
        snprintf(out, size, "%llu КБ", (bytes + 1023) / 1024);
    else if (bytes < 1000ull * 1024 * 1024)
        snprintf(out, size, "%llu МБ", (bytes + 512 * 1024) / (1024 * 1024));
    else {
        unsigned long long tenths = (unsigned long long)(bytes / (1024.0 * 1024 * 1024) * 10 + 0.5);
        snprintf(out, size, "%llu,%llu ГБ", tenths / 10, tenths % 10);
    }
}

static CheckRow *row(Checks *c, Mark mark, const char *label)
{
    CheckRow *r = &c->rows[c->n_rows++];
    r->mark = mark;
    snprintf(r->label, sizeof r->label, "%s", label);
    r->value[0] = 0;
    return r;
}

static void block(Checks *c, ErrorCode code, const char *detail)
{
    if (c->blocked != ERR_NONE)
        return; /* the first reason is the one to fix first */
    c->blocked = code;
    snprintf(c->blocked_detail, sizeof c->blocked_detail, "%s", detail ? detail : "");
}

static int title_index(const Titles *t, u64 tid)
{
    for (int i = 0; i < t->n; i++)
        if (t->items[i].title_id == tid)
            return i;
    return -1;
}

bool app_waiting(const App *app, const Record *rec)
{
    return rec && !strcmp(rec->kind, "patch") && app->hub.titles_ready && title_index(&app->hub.titles, rec->title_id) < 0;
}

static bool is_ours(const App *app, u64 tid)
{
    for (int i = 0; i < app->n_records; i++)
        if (app->records[i].title_id == tid)
            return true;
    return false;
}

/* The variant an installed patch updates to: the one for its game, else its region's. */
static int variant_for_record(const CatItem *it, const Record *rec)
{
    for (int v = 0; v < it->n_variants; v++)
        if (it->variants[v].title_id == rec->title_id)
            return v;
    for (int v = 0; v < it->n_variants; v++)
        if (!strcmp(it->variants[v].region, rec->region))
            return v;
    return -1;
}

/* Game, version, Luma, another mod: the rows for a patch whose game is known. */
static void check_game_rows(App *app, Checks *c, const CatItem *it)
{
    const ConsoleTitle *t = &app->hub.titles.items[c->title];
    const CatVariant *v = &it->variants[c->variant];
    bool several = c->match == MATCH_SEVERAL;
    CheckRow *r = row(c, several ? MARK_WARN : MARK_OK, several ? "Кілька версій гри" : c->chosen ? "Гру вибрано" : "Гру знайдено");
    if (several)
        snprintf(r->value, sizeof r->value, "%d %s · виберіть", c->m.n_candidates, c->m.n_candidates < 5 ? "регіони" : "регіонів");
    else
        snprintf(r->value, sizeof r->value, "%s · %016llX", v->region, (unsigned long long)t->title_id);

    char have[32], need[32];
    version_text(have, sizeof have, t->update_version);
    if (several) {
        /* the version and another mod are the picked game's: told once it is picked */
    } else if (v->game_version < 0) {
        snprintf(row(c, MARK_OK, "Версія гри")->value, sizeof r->value, "%s · підходить", have);
    } else if (version_fits(v, t->update_version)) {
        snprintf(row(c, MARK_OK, "Версія гри")->value, sizeof r->value, "%s · підходить", have);
    } else {
        version_text(need, sizeof need, v->game_version);
        snprintf(row(c, MARK_BAD, "Версія гри")->value, sizeof r->value, "%s, потрібна %s", have, need);
        char detail[80];
        snprintf(detail, sizeof detail, "гри з оновленням %s", need);
        block(c, ERR_GAME_VERSION, detail);
    }

    if (job_is_emulator()) {
        snprintf(row(c, MARK_OK, "Патчі ігор у Luma")->value, sizeof r->value, "емулятор");
    } else if (job_luma_version() < LUMA_MIN_GAMES) {
        snprintf(row(c, MARK_BAD, "Luma3DS")->value, sizeof r->value, "застара");
        block(c, ERR_LUMA_OLD, NULL);
    } else {
        snprintf(row(c, MARK_OK, "Патчі ігор у Luma")->value, sizeof r->value, "%s",
                 job_game_patching_on() ? "увімкнено" : "увімкнемо самі");
    }

    char dir[64];
    snprintf(dir, sizeof dir, ROOT "/luma/titles/%016llX", (unsigned long long)t->title_id);
    if (!several && !is_ours(app, t->title_id) && fs_is_dir(dir))
        snprintf(row(c, MARK_WARN, "Інший мод гри")->value, sizeof r->value, "відкладемо його");
}

static void check_space(Checks *c, u64 need)
{
    u64 free_bytes = job_sd_free();
    char a[24], b[24];
    size_text(a, sizeof a, need);
    size_text(b, sizeof b, free_bytes);
    if (free_bytes && free_bytes < need) {
        snprintf(row(c, MARK_BAD, "Місце на SD")->value, sizeof c->rows[0].value, "%s з %s", b, a);
        char detail[64];
        snprintf(detail, sizeof detail, "%s, вільно %s", a, b);
        block(c, ERR_SD_SPACE, detail);
    } else {
        snprintf(row(c, MARK_OK, "Місце на SD")->value, sizeof c->rows[0].value, "%s з %s", a, free_bytes ? b : "?");
    }
}

void app_check(App *app)
{
    Checks *c = &app->checks;
    memset(c, 0, sizeof *c);
    c->variant = c->title = c->fallback = -1;
    if (app->page < 0 || app->page >= app->hub.catalog.n)
        return;
    const CatItem *it = &app->hub.catalog.items[app->page];
    const Titles *titles = &app->hub.titles;
    c->rec = app_record(app, it->slug);
    c->state = state_of(app, it);
    /* a game picked by hand is matched by its product code, which the first pass does not read */
    c->scanning = !app->hub.titles_ready || (c->rec && c->rec->manual && !titles->full);
    /* this entry needs a newer app, or the whole catalog does (A4: an old copy is on screen) */
    const char *need = it->min_app[0] && version_cmp(it->min_app, APP_VERSION) > 0 ? it->min_app
                       : app->hub.state == LOAD_TOO_NEW                          ? app->hub.need_app
                                                                                 : NULL;
    if (need && c->state != ITEM_INSTALLED) {
        /* nothing else matters until the app is updated: the one red row says why */
        char detail[48];
        if (need[0]) {
            snprintf(row(c, MARK_BAD, "Версія 3DS UA Hub")->value, sizeof c->rows[0].value, "потрібна v%s", need);
            snprintf(detail, sizeof detail, "v%s або новішої", need);
        } else {
            snprintf(row(c, MARK_BAD, "Версія 3DS UA Hub")->value, sizeof c->rows[0].value, "потрібна новіша");
            snprintf(detail, sizeof detail, "новішої за v" APP_VERSION);
        }
        block(c, ERR_APP_OUTDATED, detail);
        return;
    }

    if (it->kind == KIND_CIA) {
        c->variant = c->rec ? variant_for_record(it, c->rec) : -1;
        if (c->variant < 0)
            c->variant = resolve_cia(it, titles_console_region());
        if (c->variant >= 0)
            check_space(c, variant_bytes(&it->variants[c->variant]) + (u64)it->variants[c->variant].data_size +
                               16 * 1024 * 1024); /* a CIA's data: the zip and its unpacked files */
        snprintf(row(c, MARK_OK, "Встановлення програм")->value, sizeof c->rows[0].value, "доступно");
        return;
    }
    c->fallback = resolve_fallback(it, titles_console_region());
    if (c->scanning)
        return;
    if (c->state != ITEM_NEW && c->rec) {
        c->match = MATCH_FOUND;
        c->variant = c->rec->manual ? -1 : variant_for_record(it, c->rec);
        c->title = title_index(titles, c->rec->title_id);
        if (c->rec->manual && c->title >= 0)
            c->variant = resolve_manual(it, &titles->items[c->title]);
        c->waiting = app_waiting(app, c->rec);
        if (c->state == ITEM_INSTALLED)
            return;
        if (c->title < 0) { /* an update, and its game is gone (the cartridge is out) */
            snprintf(row(c, MARK_BAD, "Гра на консолі")->value, sizeof c->rows[0].value, "не знайдено");
            block(c, ERR_GAME_NOT_FOUND, NULL);
            return;
        }
        if (c->variant < 0) {
            block(c, ERR_GAME_REGION, c->rec->region);
            return;
        }
    } else {
        c->match = resolve_patch(it, titles->items, titles->n, titles_console_region(), &c->m);
        for (int i = 0; c->m.n_candidates > 1 && !strcmp(app->which_slug, it->slug) && i < c->m.n_candidates; i++) {
            if (c->m.candidates[i] != app->which_variant)
                continue;
            /* picked on "Для якої гри?": that one, whatever the console's region says */
            c->m.variant = app->which_variant;
            c->m.title = title_index(titles, it->variants[c->m.variant].title_id);
            c->match = MATCH_FOUND;
            c->chosen = true;
        }
        if (c->match == MATCH_SEVERAL) {
            /* games of several regions, none the console's: the rows show the first until one is
             * picked ("Вибрати гру" leads to the choice) */
            c->m.variant = c->m.candidates[0];
            c->m.title = title_index(titles, it->variants[c->m.variant].title_id);
        }
        c->variant = c->m.variant;
        c->title = c->m.title;
        if (c->match == MATCH_NOT_FOUND) {
            snprintf(row(c, MARK_BAD, "Гра на консолі")->value, sizeof c->rows[0].value, "не знайдено");
            block(c, ERR_GAME_NOT_FOUND, NULL);
            return;
        }
    }
    check_game_rows(app, c, it);
    check_space(c, (u64)it->variants[c->variant].size * 3);
}

/* ---------------------------------------------------------------- jobs */

/* No HOME Menu and no sleep while files are being written. */
static void exclusive(bool on)
{
    aptSetHomeAllowed(!on);
    aptSetSleepAllowed(!on);
}

static void start(App *app)
{
    exclusive(true);
    app->job_active = true;
    job_start(&app->job);
}

static void prepare(App *app, JobKind kind, const CatItem *it)
{
    Job *job = &app->job;
    memset(job, 0, sizeof *job);
    job->kind = kind;
    job->item_kind = it->kind;
    snprintf(job->slug, sizeof job->slug, "%s", it->slug);
    snprintf(job->title, sizeof job->title, "%s", it->title);
    snprintf(job->version, sizeof job->version, "%s", it->version);
    snprintf(app->job_title, sizeof app->job_title, "%s", it->title);
}

static void backup_dir(char *out, size_t size, u64 tid)
{
    snprintf(out, size, BACKUP_DIR "/%016llX", (unsigned long long)tid);
}

static void install(App *app, int variant, int title, bool manual, bool risky, bool nogame, bool fallback)
{
    app->last.nogame = nogame;
    app->last.fallback = fallback;
    app->last.variant = variant;
    app->last.title = title;
    app->last.manual = manual;
    app->last.risky = risky;
    const CatItem *it = &app->hub.catalog.items[app->page];
    const Record *rec = app_record(app, it->slug);
    prepare(app, JOB_INSTALL, it);
    Job *job = &app->job;
    job->variant = it->variants[variant];
    if (title >= 0) {
        job->target_tid = app->hub.titles.items[title].title_id;
        job->media = app->hub.titles.items[title].media;
    } else {
        job->target_tid = job->variant.title_id;
        job->media = MEDIATYPE_SD;
    }
    job->manual = manual;
    job->risky = risky;
    job->nogame = nogame;
    job->fallback = fallback;
    if (rec && it->kind == KIND_PATCH && rec->title_id != job->target_tid) {
        job->removing = *rec; /* its file list stays the records' until the job is over */
        job->replacing = true;
    }
    app->job_update = rec != NULL;
    snprintf(app->job_was, sizeof app->job_was, "%s", rec ? rec->version : "");
    start(app);
    app_go(app, SCREEN_PROGRESS);
}

void app_install(App *app, int variant, int title, bool manual, bool risky)
{
    install(app, variant, title, manual, risky, false, false);
}

void app_install_nogame(App *app, int variant)
{
    install(app, variant, -1, false, false, true, false);
}

void app_install_fallback(App *app, int variant)
{
    install(app, variant, -1, false, false, false, true);
}

void app_probe(App *app, int variant)
{
    const CatItem *it = &app->hub.catalog.items[app->page];
    prepare(app, JOB_PROBE, it);
    app->job.variant = it->variants[variant];
    app->job.target_tid = it->variants[variant].title_id;
    start(app);
}

void app_remove(App *app)
{
    const CatItem *it = &app->hub.catalog.items[app->page];
    const Record *rec = app_record(app, it->slug);
    if (!rec)
        return;
    prepare(app, JOB_REMOVE, it);
    app->job.removing = *rec; /* its file list stays the records' until the job is over */
    snprintf(app->job.version, sizeof app->job.version, "%s", rec->version);
    app->job.target_tid = rec->title_id;
    app->job.remove_game = rec->game_cia && app->remove_game;
    app->freed = 0;
    for (int i = 0; i < rec->n_files; i++)
        app->freed += rec->files[i].size;
    if (it->kind == KIND_CIA) {
        int v = variant_for_record(it, rec);
        app->freed += v >= 0 ? it->variants[v].size : 0; /* the files above are its data, if any */
    }
    char dir[96];
    backup_dir(dir, sizeof dir, rec->title_id);
    app->had_backup = fs_is_dir(dir);
    start(app);
    app_go(app, SCREEN_PROGRESS);
}

void app_check_game(App *app, int variant, int title)
{
    const CatItem *it = &app->hub.catalog.items[app->page];
    prepare(app, JOB_CHECK, it);
    app->job.variant = it->variants[variant];
    app->job.target_tid = app->hub.titles.items[title].title_id;
    app->job.media = app->hub.titles.items[title].media;
    app->checked = title;
    start(app);
}

void app_retry(App *app)
{
    if (app->job.kind == JOB_CHECK) {
        app_go(app, SCREEN_PICK);
        start(app);
        return;
    }
    if (app->job.kind == JOB_PROBE) {
        app_go(app, SCREEN_FALLBACK); /* checks the link again as it opens */
        return;
    }
    if (app->job.kind == JOB_INSTALL) {
        /* from the start: what is installed may have changed (a game moved to was taken off) */
        install(app, app->last.variant, app->last.title, app->last.manual, app->last.risky, app->last.nogame,
                app->last.fallback);
        return;
    }
    start(app);
    app_go(app, SCREEN_PROGRESS);
}

/* ---------------------------------------------------------------- after a job: log, report */

static const char *stage_name(JobStage s, JobKind kind)
{
    switch (s) {
    case JS_CHECK: return kind == JOB_PROBE ? "посилання" : "перевірка консолі";
    case JS_GAME: return "завантаження гри";
    case JS_GAME_INSTALL: return kind == JOB_REMOVE ? "видалення гри" : "встановлення гри";
    case JS_DOWNLOAD: return "завантаження";
    case JS_VERIFY: return kind == JOB_CHECK ? "перевірка гри" : "перевірка файлів";
    case JS_INSTALL: return kind == JOB_REMOVE ? "видалення" : "копіювання";
    case JS_DATA: return "завантаження гри";
    case JS_DATA_COPY: return kind == JOB_REMOVE ? "видалення файлів гри" : "копіювання гри";
    case JS_LUMA: return "Luma";
    default: return "";
    }
}

const char *app_model_name(void)
{
    static const char *const names[] = {"Old 3DS", "Old 3DS XL", "New 3DS", "Old 2DS", "New 3DS XL", "New 2DS XL"};
    u8 model = 0xFF;
    if (R_FAILED(CFGU_GetSystemModel(&model)) || model >= sizeof names / sizeof *names)
        return NULL;
    return names[model];
}

void app_job_tech(const Job *job, char *out, size_t size)
{
    char result[16] = "";
    if (job->sys_result)
        snprintf(result, sizeof result, "0x%08lX", (unsigned long)job->sys_result);
    const char *parts[] = {job->detail, job->tech, result, job->where};
    size_t n = 0;
    out[0] = 0;
    for (unsigned i = 0; i < sizeof parts / sizeof *parts; i++) {
        const char *p = parts[i];
        while (*p == ' ')
            p++; /* the detail starts with a space: it follows the error text */
        if (!*p || n >= size)
            continue;
        int w = snprintf(out + n, size - n, "%s%s", n ? " | " : "", p);
        if (w > 0)
            n += (size_t)w;
    }
}

const char *app_report_text(const App *app, char *out, size_t size)
{
    if (!app->report_sent)
        return app->report[0] ? app->report : "не надсилався";
    char id[REPORT_ID_LEN + 2];
    report_id_display(app->stats.id, id);
    snprintf(out, size, "%s · ID %s",
             app->stats.busy ? "надсилаю" : app->stats.delivered ? "надіслано" : "не дійшов, сервер недоступний", id);
    return out;
}

static void report_text(App *app, const char *how)
{
    app->report_sent = !strcmp(how, "sent");
    if (app->report_sent) {
        app->report[0] = 0;
    } else if (!strcmp(how, "off")) {
        snprintf(app->report, sizeof app->report, "вимкнено в Допомозі");
    } else if (!strcmp(how, "no wifi")) {
        snprintf(app->report, sizeof app->report, "без Wi-Fi, не надіслано");
    } else {
        snprintf(app->report, sizeof app->report, "не надіслано");
    }
}

static const char *send_report(App *app, const char *event, const char *code, const char *stage, const char *detail)
{
    const Job *job = &app->job;
    char luma[24] = "", tid[17] = "";
    u32 v = job_luma_version();
    if (v)
        snprintf(luma, sizeof luma, "%lu.%lu.%lu", GET_VERSION_MAJOR(v), GET_VERSION_MINOR(v), GET_VERSION_REVISION(v));
    if (job->target_tid)
        snprintf(tid, sizeof tid, "%016llX", (unsigned long long)job->target_tid);
    Report r = {
        .event = event,
        .app = APP_VERSION,
        .model = app_model_name(),
        .luma = luma[0] ? luma : NULL,
        .region = titles_console_region()[0] ? titles_console_region() : NULL,
        .slug = job->slug,
        .tv = job->version,
        .vregion = job->kind == JOB_REMOVE ? job->removing.region : job->variant.region,
        .tid = tid[0] ? tid : NULL,
        .kind = job->item_kind == KIND_PATCH ? "patch" : "cia",
        .stage = stage,
        .code = code,
        .detail = detail && detail[0] ? detail : NULL,
        .manual = job->kind == JOB_INSTALL ? job->manual : -1,
        .risky = job->kind == JOB_INSTALL ? job->risky : -1,
        .ms = job->finished > job->started ? (int64_t)(job->finished - job->started) : -1,
    };
    const char *how = stats_send(&app->stats, &r);
    report_text(app, how);
    return how;
}

void app_report_not_found(App *app)
{
    static char reported[64]; /* once per translation and run: B from D5 comes back here */
    const CatItem *it = &app->hub.catalog.items[app->page];
    if (!strcmp(reported, it->slug))
        return;
    snprintf(reported, sizeof reported, "%s", it->slug);
    char luma[24] = "";
    u32 v = job_luma_version();
    if (v)
        snprintf(luma, sizeof luma, "%lu.%lu.%lu", GET_VERSION_MAJOR(v), GET_VERSION_MINOR(v), GET_VERSION_REVISION(v));
    const char *region = titles_console_region();
    Report r = {
        .event = "not_found",
        .app = APP_VERSION,
        .model = app_model_name(),
        .luma = luma[0] ? luma : NULL,
        .region = region[0] ? region : NULL,
        .slug = it->slug,
        .tv = it->version,
        .kind = "patch",
        .manual = -1,
        .risky = -1,
        .ms = -1,
    };
    stats_send(&app->stats, &r);
    hublog_add("not_found", it->title, "ok", "гру не знайдено на консолі");
}

static void open_error(App *app, ErrorCode code, const char *detail, const char *stage, ScreenId back)
{
    app->error = code;
    snprintf(app->error_detail, sizeof app->error_detail, "%s", detail ? detail : "");
    app->error_stage = stage;
    app->error_back = back;
    app_go(app, SCREEN_ERROR);
}

void app_error(App *app, ErrorCode code, const char *detail)
{
    app->report[0] = 0;
    app->report_sent = false;
    app->error_of_job = false;
    open_error(app, code, detail, "перевірка гри", app->screen);
}

static void after_job(App *app)
{
    Job *job = &app->job;
    exclusive(false);
    app->job_active = false;
    const char *event = job->kind == JOB_REMOVE ? "remove" : job->kind == JOB_CHECK ? "check"
                        : job->kind == JOB_PROBE            ? "probe"
                        : app->job_update                   ? "update"
                                                            : "install";
    char detail[96];
    snprintf(detail, sizeof detail, "v%s · %llu с", job->version,
             (unsigned long long)((job->finished - job->started + 500) / 1000));
    if (job->nogame || job->game_installed || job->remove_game) {
        size_t n = strlen(detail);
        snprintf(detail + n, sizeof detail - n, "%s",
                 job->nogame ? " · без гри" : job->game_installed ? " · з грою з каталогу" : " · разом з грою");
    }

    if (job->kind != JOB_CHECK && job->kind != JOB_PROBE) { /* even a failed job may have changed what is installed */
        load_records(app);
        if (job->item_kind == KIND_CIA || job->game_installed || job->remove_game)
            rescan_titles(app); /* a game came or went: the list of games follows */
        app_rebuild_list(app);
    }
    if (job->kind == JOB_PROBE && job->error == ERR_NONE)
        return; /* J4 shows the link works */
    if (job->cancelled) {
        hublog_add(event, job->title, "скасовано", detail);
        app_check(app);
        app_go(app, job->kind == JOB_CHECK ? SCREEN_PICK : SCREEN_DETAILS);
        return;
    }
    if (job->error != ERR_NONE) {
        const char *code = error_info(job->error)->code;
        const char *stage = stage_name(job->failed_at, job->kind);
        const char *what = job->detail;
        while (*what == ' ')
            what++; /* the detail starts with a space: it follows the error text */
        hublog_add(event, job->title, code, what[0] ? what : stage);
        app->report[0] = 0;
        app->report_sent = false;
        if (job->kind != JOB_CHECK && (job->kind != JOB_PROBE || job->error != ERR_NET_OFF)) {
            /* the report carries the technical side too: what failed, on which file */
            char tech[256];
            app_job_tech(job, tech, sizeof tech);
            send_report(app, "error", code, stage, tech);
        }
        app->error_of_job = true;
        open_error(app, job->error, job->detail, stage,
                   job->kind == JOB_CHECK ? SCREEN_PICK : job->kind == JOB_PROBE ? SCREEN_NOTFOUND : SCREEN_DETAILS);
        return;
    }
    if (job->kind == JOB_CHECK) {
        char result[48];
        if (job->check_has)
            snprintf(result, sizeof result, "%d з %d файлів збігаються", job->check_matched, job->check_total);
        else
            snprintf(result, sizeof result, "без check.txt");
        hublog_add("check", job->title, "ok", result);
        return; /* the pick screen shows the result */
    }

    hublog_add(event, job->title, "ok", detail);
    send_report(app, event, NULL, NULL, NULL);
    if (job->kind == JOB_INSTALL) {
        char dir[96];
        backup_dir(dir, sizeof dir, job->target_tid);
        app->had_backup = job->item_kind == KIND_PATCH && fs_is_dir(dir);
    }
    app_check(app);
    app_go(app, SCREEN_DONE);
}

/* ---------------------------------------------------------------- the system */

void app_launch(App *app, u64 title_id, u8 media)
{
    u64 deadline = osGetTime() + STATS_WAIT_MS; /* the report of the install first */
    while (!stats_settled(&app->stats, deadline))
        svcSleepThread(20 * 1000 * 1000);
    static u8 param[0x300], hmac[0x20];
    if (R_SUCCEEDED(APT_PrepareToDoApplicationJump(0, title_id, media)))
        APT_DoApplicationJump(param, sizeof param, hmac);
}

/* A plain reboot. NS_RebootToTitle (back into the app) left an Old 3DS with Luma 13.4 on a
 * black screen in 3ds-ua (2026-10-22): never again. */
void app_reboot(App *app)
{
    u64 deadline = osGetTime() + STATS_WAIT_MS;
    while (!stats_settled(&app->stats, deadline))
        svcSleepThread(20 * 1000 * 1000);
    APT_HardwareResetAsync();
}

void app_rescan(App *app)
{
    rescan_titles(app);
    app_check(app);
}

/* ---------------------------------------------------------------- input and drawing */

static int targets(const App *app, Target *out)
{
    const Screen *s = screens[app->screen];
    return s->targets ? s->targets(app, out) : 0;
}

/* D-pad moves the focus; A becomes a tap on the focused control, so every screen keeps one
 * set of handlers for touch and buttons. The first press only shows where the focus is. */
static Input with_buttons(App *app, const Input *raw)
{
    Input in = *raw;
    Target t[NAV_MAX_TARGETS];
    int n = targets(app, t);
    if (raw->tapped)
        app->keys_nav = false;
    static const struct {
        u32 key;
        NavDir dir;
    } dpad[] = {{KEY_DUP, NAV_UP}, {KEY_DDOWN, NAV_DOWN}, {KEY_DLEFT, NAV_LEFT}, {KEY_DRIGHT, NAV_RIGHT}};
    for (unsigned i = 0; i < sizeof dpad / sizeof *dpad; i++) {
        if (!(raw->down & dpad[i].key) || !n)
            continue;
        if (app->keys_nav) {
            const Screen *s = screens[app->screen];
            if (!s->dpad || !s->dpad(app, dpad[i].dir))
                app->focus = nav_move(t, n, app->focus, dpad[i].dir);
            n = targets(app, t); /* a list may have scrolled */
        }
        app->keys_nav = true;
    }
    if (n && app->focus >= n)
        app->focus = n - 1;
    if (n && raw->down & KEY_A) {
        in.down &= ~KEY_A;
        in.tapped = true;
        in.tap_x = (int)(t[app->focus].r.x + t[app->focus].r.w / 2);
        in.tap_y = (int)(t[app->focus].r.y + t[app->focus].r.h / 2);
    }
    return in;
}

/* A new catalog numbers its entries anew: keep the highlighted row on the same translation. */
static void follow_sel(App *app, const char *slug)
{
    for (int i = 0; i < app->list_n; i++)
        if (!strcmp(app->hub.catalog.items[app->list[i]].slug, slug))
            app->sel = i;
}

/* After a refresh the entries may sit elsewhere: follow the open one by its slug. */
static void follow_page(App *app, const char *slug)
{
    for (int i = 0; i < app->hub.catalog.n; i++) {
        if (!strcmp(app->hub.catalog.items[i].slug, slug)) {
            app->page = i;
            app_check(app);
            shots_want(&app->hub.catalog.items[i]); /* a new catalog may name other pictures */
            return;
        }
    }
    if (app->screen != SCREEN_CATALOG && app->screen != SCREEN_SPLASH && app->screen != SCREEN_MESSAGE)
        app_go(app, SCREEN_CATALOG);
}

void app_update(App *app, const Input *raw)
{
    char slug[64] = "", sel_slug[64] = "";
    if (app->page < app->hub.catalog.n)
        snprintf(slug, sizeof slug, "%s", app->hub.catalog.items[app->page].slug);
    if (app->sel < app->list_n)
        snprintf(sel_slug, sizeof sel_slug, "%s", app->hub.catalog.items[app->list[app->sel]].slug);
    int titles_gen = app->hub.titles_gen;
    if (hub_poll(&app->hub)) {
        app_rebuild_list(app);
        if (sel_slug[0])
            follow_sel(app, sel_slug);
        if (slug[0])
            follow_page(app, slug);
        if (app->screen == SCREEN_CATALOG && app->focus >= TAB_COUNT)
            screens[SCREEN_CATALOG]->enter(app); /* the offline bar went: the rows moved up */
    }
    if (titles_gen != app->hub.titles_gen) { /* the games came, or their names did */
        app_check(app);
        if (app->screen == SCREEN_PICK)
            screens[SCREEN_PICK]->enter(app);
    }
    if (app->job_active && job_poll(&app->job))
        after_job(app);

    Input in = with_buttons(app, raw);
    if (in.down & KEY_START && !app->job_active) {
        app->quit = true;
        return;
    }
    screens[app->screen]->update(app, &in);
}

void app_draw_top(const App *app)
{
    screens[app->screen]->draw_top(app);
}

void app_draw_bottom(const App *app)
{
    screens[app->screen]->draw_bottom(app);
    /* Over the content, as an outline: a control inside a track (the tabs) stays readable. Ink, not
     * blue: around the blue main buttons a blue ring was lost on a console's screen. */
    Target t[NAV_MAX_TARGETS];
    int n = targets(app, t);
    if (app->keys_nav && n && app->focus < n) {
        const Rect *r = &t[app->focus].r;
        ui_frame(r->x - 3, r->y - 3, r->w + 6, r->h + 6, t[app->focus].radius + 3, 2, COL_INK);
    }
}
