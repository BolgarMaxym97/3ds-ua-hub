/* D5-D8: the game picked by hand. A on a game checks the translation's files against it
 * (check.txt in the archive, JOB_CHECK); A again installs - "на свій ризик" when they differ.
 * Y searches by name or Title ID. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../image.h"
#include "../titles.h"
#include "../ui.h"
#include "common.h"

#define LIST_TOP 32
#define ROW_H 32
#define ROW_STEP 34
#define VISIBLE 5

static const Rect search_box = {PAD, 6, BOTTOM_W - 2 * PAD, 22};

static const CatItem *item(const App *app)
{
    return &app->hub.catalog.items[app->page];
}

static const ConsoleTitle *title_at(const App *app, int pick)
{
    return &app->hub.titles.items[app->picks[pick]];
}

/* Case-blind for Latin letters, which is what game names on the HOME Menu mostly are. */
static bool contains(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] && tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == n)
            return true;
    }
    return !n;
}

/* Not a game a patch can go to: this app, or a CIA translation the Hub installed. */
static bool skip(const App *app, u64 tid)
{
    u64 self = 0;
    if (R_SUCCEEDED(APT_GetProgramID(&self)) && tid == self)
        return true;
    for (int i = 0; i < app->n_records; i++)
        if (app->records[i].title_id == tid && !strcmp(app->records[i].kind, "cia"))
            return true;
    return false;
}

static void build(App *app)
{
    const Titles *t = &app->hub.titles;
    int keep = app->n_picks && app->pick_sel < app->n_picks ? app->picks[app->pick_sel] : app->checked;
    free(app->picks);
    app->picks = calloc(t->n ? (size_t)t->n : 1, sizeof *app->picks);
    app->n_picks = 0;
    app->pick_sel = app->pick_scroll = 0;
    for (int i = 0; app->picks && t->full && i < t->n; i++) { /* the names first: the list is searched by them */
        char tid[20];
        format_tid(tid, sizeof tid, t->items[i].title_id);
        if (skip(app, t->items[i].title_id) || !(contains(t->items[i].name, app->query) || contains(tid, app->query)))
            continue;
        if (i == keep) /* back from an error or a search: stay on the same game */
            app->pick_sel = app->n_picks;
        app->picks[app->n_picks++] = i;
    }
    if (app->pick_sel >= VISIBLE)
        app->pick_scroll = app->pick_sel - VISIBLE + 1;
}

static bool searching(const App *app)
{
    return app->query[0] != 0;
}

static float list_top(const App *app)
{
    return searching(app) ? LIST_TOP + 4 : LIST_TOP;
}

static int rows_shown(const App *app)
{
    int n = app->n_picks - app->pick_scroll;
    return n < VISIBLE ? n : VISIBLE;
}

static Rect row_rect(const App *app, int i)
{
    return (Rect){PAD, list_top(app) + i * ROW_STEP, BOTTOM_W - 2 * PAD, ROW_H};
}

static int rows_base(const App *app)
{
    return searching(app) ? 1 : 0;
}

static void enter(App *app)
{
    build(app);
    app->focus = rows_base(app) + app->pick_sel - app->pick_scroll;
}

static int targets(const App *app, Target *out)
{
    int n = 0;
    if (searching(app))
        out[n++] = (Target){search_box, 8};
    for (int i = 0; i < rows_shown(app); i++)
        out[n++] = (Target){row_rect(app, i), 9};
    return n;
}

static bool dpad(App *app, NavDir dir)
{
    int row = app->focus - rows_base(app);
    if (row < 0 || !app->n_picks)
        return false;
    if (dir == NAV_LEFT || dir == NAV_RIGHT)
        return true;
    if (dir == NAV_UP && app->pick_sel == 0)
        return false;
    app->pick_sel += dir == NAV_DOWN ? 1 : -1;
    if (app->pick_sel >= app->n_picks)
        app->pick_sel = app->n_picks - 1;
    if (app->pick_sel < app->pick_scroll)
        app->pick_scroll = app->pick_sel;
    if (app->pick_sel >= app->pick_scroll + VISIBLE)
        app->pick_scroll = app->pick_sel - VISIBLE + 1;
    app->focus = rows_base(app) + app->pick_sel - app->pick_scroll;
    return true;
}

static void search(App *app)
{
    SwkbdState kb;
    char text[sizeof app->query];
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, sizeof text - 1);
    swkbdSetHintText(&kb, "Назва гри або Title ID");
    swkbdSetInitialText(&kb, app->query);
    if (swkbdInputText(&kb, text, sizeof text) != SWKBD_BUTTON_CONFIRM)
        return;
    snprintf(app->query, sizeof app->query, "%s", text);
    build(app);
    app->focus = rows_base(app) + app->pick_sel - app->pick_scroll;
}

/* ---------------------------------------------------------------- what the check said */

typedef enum { VERDICT_NONE, VERDICT_RUNNING, VERDICT_OK, VERDICT_RISK, VERDICT_REGION } Verdict;

static int variant_for(const App *app, int pick)
{
    return resolve_manual(item(app), title_at(app, pick));
}

static Verdict verdict(const App *app, int pick)
{
    if (!app->n_picks)
        return VERDICT_NONE;
    const ConsoleTitle *t = title_at(app, pick);
    int v = variant_for(app, pick);
    if (v < 0)
        return VERDICT_REGION;
    if (app->checked != app->picks[pick] || app->job.kind != JOB_CHECK)
        return VERDICT_NONE;
    if (app->job_active)
        return VERDICT_RUNNING;
    if (app->job.error != ERR_NONE || app->job.cancelled)
        return VERDICT_NONE;
    bool files = app->job.check_has && app->job.check_matched == app->job.check_total;
    return files && version_fits(&item(app)->variants[v], t->update_version) ? VERDICT_OK : VERDICT_RISK;
}

static void activate(App *app, int pick)
{
    int v = variant_for(app, pick);
    switch (verdict(app, pick)) {
    case VERDICT_NONE:
        app_check_game(app, v, app->picks[pick]);
        break;
    case VERDICT_OK:
        app_install(app, v, app->picks[pick], true, false);
        break;
    case VERDICT_RISK:
        app_install(app, v, app->picks[pick], true, true);
        break;
    default:
        break;
    }
}

static void update(App *app, const Input *in)
{
    if (in->down & KEY_B) {
        if (app->job_active) {
            job_cancel(&app->job); /* the check downloads the whole translation: B stops it */
        } else if (searching(app)) {
            app->query[0] = 0;
            build(app);
        } else {
            app_go(app, SCREEN_NOTFOUND);
        }
        return;
    }
    if (in->down & KEY_Y && !app->job_active) {
        search(app);
        return;
    }
    int row = app->focus - rows_base(app);
    if (row >= 0 && app->pick_scroll + row < app->n_picks)
        app->pick_sel = app->pick_scroll + row;
    if (!in->tapped || app->job_active)
        return;
    if (searching(app) && rect_hit(search_box, in->tap_x, in->tap_y)) {
        search(app);
        return;
    }
    for (int i = 0; i < rows_shown(app); i++) {
        if (!rect_hit(row_rect(app, i), in->tap_x, in->tap_y))
            continue;
        int pick = app->pick_scroll + i;
        if (pick != app->pick_sel && !app->keys_nav) {
            app->pick_sel = pick; /* the stylus: the first tap selects */
            app->focus = rows_base(app) + i;
        } else {
            activate(app, pick);
        }
        return;
    }
}

/* ---------------------------------------------------------------- drawing */

static void draw_top(const App *app)
{
    const CatItem *it = item(app);
    char head[160];
    snprintf(head, sizeof head, "Куди поставити українізатор %s:", it->title);
    draw_text_fit(20, 12, TOP_W - 40, 11, FONT_BOLD, COL_MUTED, head);
    if (!app->n_picks) {
        char text[160] = "На консолі немає ігор";
        if (searching(app))
            snprintf(text, sizeof text, "Нічого не знайдено за «%s»", app->query);
        ui_text_center(TOP_W / 2, 80, 16, FONT_HEAVY, COL_INK, text);
        ui_text_wrap(40, 110, TOP_W - 80, 11, FONT_BOLD, COL_BODY,
                     "Спробуйте інше слово, частину назви або Title ID. Гра на картриджі? Вставте його й поверніться "
                     "на попередній екран.");
        return;
    }
    const ConsoleTitle *t = title_at(app, app->pick_sel);
    draw_title_icon(app, app->picks[app->pick_sel], 20, 34, 40);
    draw_text_fit(70, 36, TOP_W - 90, 15, FONT_HEAVY, COL_INK, t->name);
    char sub[96], tid[20];
    format_tid(tid, sizeof tid, t->title_id);
    snprintf(sub, sizeof sub, "%s · %s", tid, t->media == MEDIATYPE_GAME_CARD ? "картридж" : "встановлена на SD");
    ui_text(70, 58, 10, FONT_BOLD, COL_MUTED, sub);

    int v = variant_for(app, app->pick_sel);
    Verdict verd = verdict(app, app->pick_sel);
    const char *region = region_of_product(t->product);
    char code[80], files[80], version[96];
    snprintf(code, sizeof code, "%s · %s", t->product[0] ? t->product : "?", region ? region : "регіон невідомий");
    if (verd == VERDICT_RUNNING)
        snprintf(files, sizeof files, "перевіряю…");
    else if ((verd == VERDICT_OK || verd == VERDICT_RISK) && !app->job.check_has)
        snprintf(files, sizeof files, "!перевірки немає");
    else if (verd == VERDICT_OK || verd == VERDICT_RISK)
        snprintf(files, sizeof files, "%s%d з %d збігаються", app->job.check_matched == app->job.check_total ? "" : "!",
                 app->job.check_matched, app->job.check_total);
    else
        snprintf(files, sizeof files, v < 0 ? "—" : "A — перевірити");
    if (v >= 0) {
        const CatVariant *var = &it->variants[v];
        char have[32] = "без оновлення";
        if (t->update_version >= 0)
            snprintf(have, sizeof have, "v%d.%d", t->update_version >> 10, (t->update_version >> 4) & 0x3F);
        if (version_fits(var, t->update_version))
            snprintf(version, sizeof version, "%s · підходить", have);
        else
            snprintf(version, sizeof version, "!%s, потрібна v%d.%d", have, var->game_version >> 10,
                     (var->game_version >> 4) & 0x3F);
    } else {
        snprintf(version, sizeof version, "—");
    }
    const char *rows[] = {"Код гри (product code)", code, "Файли, які змінює переклад", files, "Версія гри", version};
    float h = draw_info_rows(20, 84, TOP_W - 40, 22, rows, 3);

    float y = 84 + h + 10;
    if (verd == VERDICT_REGION) {
        char regions[64], text[200];
        format_regions(regions, sizeof regions, it);
        snprintf(text, sizeof text, "Переклад не для цієї гри: він є для %s, а файли інших регіонів різні.", regions);
        ui_text_wrap(20, y, TOP_W - 40, 11, FONT_HEAVY, COL_RED, text);
    } else if (verd == VERDICT_OK) {
        ui_text_wrap(20, y, TOP_W - 40, 11, FONT_HEAVY, COL_GREEN_TEXT, "Можна встановлювати: це та сама гра з іншим Title ID.");
    } else if (verd == VERDICT_RISK) {
        float hh = ui_text_wrap(20, y, TOP_W - 40, 11, FONT_HEAVY, COL_AMBER_TEXT, "Встановлення на свій ризик.");
        ui_text_wrap(20, y + hh, TOP_W - 40, 10, FONT_BOLD, COL_BODY,
                     "Схоже, це інша ревізія гри: частина тексту може лишитись англійською або гра зависне. Видалити "
                     "переклад можна будь-коли.");
    } else if (verd == VERDICT_RUNNING) {
        char text[96] = "Звіряю файли гри…", a[16], b[16];
        format_size(a, sizeof a, app->job.net.done);
        format_size(b, sizeof b, app->job.net.total ? app->job.net.total : app->job.variant.size);
        if (app->job.stage == JS_DOWNLOAD)
            snprintf(text, sizeof text, "Завантажую переклад: %s з %s", a, b);
        ui_text(20, y, 11, FONT_HEAVY, COL_BLUE, text);
    } else {
        ui_text_wrap(20, y, TOP_W - 40, 10.5f, FONT_BOLD, COL_BODY,
                     "Виберіть гру — перевіримо, чи підходить переклад, ще до встановлення.");
    }
}

static void draw_bottom(const App *app)
{
    const Titles *t = &app->hub.titles;
    if (searching(app)) {
        ui_rrect(search_box.x, search_box.y, search_box.w, search_box.h, 8, COL_CARD);
        ui_icon(ICON_SEARCH, search_box.x + 12, search_box.y + 11, 12, COL_MUTED);
        ui_text(search_box.x + 24, search_box.y + 4, 10.5f, FONT_HEAVY, COL_INK, app->query);
        ui_text_right(search_box.x + search_box.w - 8, search_box.y + 5, 9.5f, FONT_BOLD, COL_MUTED, "торкніться, щоб змінити");
    } else {
        int sd = 0;
        for (int i = 0; i < t->n; i++)
            sd += t->items[i].media == MEDIATYPE_SD;
        char count[64];
        snprintf(count, sizeof count, "%d на SD · %s", sd, t->card_present ? "картридж є" : "картриджа нема");
        ui_text(PAD + 2, 9, 13, FONT_HEAVY, COL_INK, "Встановлені ігри");
        ui_text_right(BOTTOM_W - PAD - 2, 12, 10, FONT_BOLD, COL_MUTED, count);
    }

    if (!t->full) { /* the loader is still reading the games' names */
        ui_spinner(BOTTOM_W / 2, LIST_TOP + 40, 7, COL_BLUE);
        ui_text_center(BOTTOM_W / 2, LIST_TOP + 56, 11, FONT_BOLD, COL_BODY, "Читаю назви ігор на консолі…");
    }
    for (int i = 0; i < rows_shown(app); i++) {
        int pick = app->pick_scroll + i;
        Rect r = row_rect(app, i);
        const ConsoleTitle *ct = title_at(app, pick);
        if (pick == app->pick_sel)
            ui_rrect(r.x, r.y, r.w, r.h, 9, COL_CARD);
        draw_title_icon(app, app->picks[pick], r.x + 6, r.y + 3, 26);
        char tid[20];
        format_tid(tid, sizeof tid, ct->title_id);
        Verdict v = verdict(app, pick);
        const char *badge = v == VERDICT_OK ? "підходить" : v == VERDICT_RISK ? "ризик" : NULL;
        float bw = badge ? ui_text_width(9.5f, FONT_HEAVY, badge) + 18 : 0;
        draw_text_fit(r.x + 40, r.y + 3, r.w - 46 - bw, 11, FONT_HEAVY, COL_INK, ct->name);
        ui_text(r.x + 40, r.y + 18, 9.5f, FONT_BOLD, COL_MUTED, tid);
        if (badge)
            draw_badge(r.x + r.w - 6, r.y + 10, badge, v == VERDICT_OK ? COL_GREEN_BG : COL_AMBER_BG,
                       v == VERDICT_OK ? COL_GREEN_TEXT : COL_AMBER_TEXT);
    }
    if (app->n_picks > VISIBLE) {
        float track = VISIBLE * ROW_STEP - 2, h = track * VISIBLE / app->n_picks;
        float y = list_top(app) + (track - h) * app->pick_scroll / (app->n_picks - VISIBLE);
        ui_rrect(BOTTOM_W - 5, y, 3, h, 1.5f, COL_FAINT);
    }

    Verdict v = app->n_picks ? verdict(app, app->pick_sel) : VERDICT_NONE;
    const char *a = v == VERDICT_OK ? "Встановити" : v == VERDICT_RISK ? "На свій ризик" : "Перевірити";
    const char *hints[] = {"A", a, "Y", "Пошук", "B", searching(app) ? "Скинути пошук" : "Назад", NULL};
    if (v == VERDICT_RUNNING) {
        hints[0] = "B";
        hints[1] = "Скасувати перевірку";
        hints[2] = NULL;
    } else if (v == VERDICT_REGION || !app->n_picks) {
        hints[0] = "Y";
        hints[1] = "Пошук";
        hints[2] = "B";
        hints[3] = searching(app) ? "Скинути пошук" : "Назад";
        hints[4] = NULL;
    }
    draw_hints(hints);
}

const Screen screen_pick = {
    .enter = enter, .update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets, .dpad = dpad,
};
