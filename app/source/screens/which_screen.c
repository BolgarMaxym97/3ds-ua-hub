/* "Для якої гри?": games of several regions of a translation are on the console and none is the
 * console's region (MATCH_SEVERAL). The person picks the one to translate; the page then checks
 * that game. Also reached with Y from the page to pick another one. */
#include <stdio.h>
#include <string.h>

#include "../titles.h"
#include "../ui.h"
#include "common.h"

#define LIST_TOP 32
#define ROW_H 32
#define ROW_STEP 34
#define VISIBLE 5

static int scroll;

static const CatItem *item(const App *app)
{
    return &app->hub.catalog.items[app->page];
}

static const Match *match(const App *app)
{
    return &app->checks.m;
}

/* The candidate's game in hub.titles, -1 if it went away meanwhile. */
static int title_of(const App *app, int candidate)
{
    u64 tid = item(app)->variants[match(app)->candidates[candidate]].title_id;
    for (int i = 0; i < app->hub.titles.n; i++)
        if (app->hub.titles.items[i].title_id == tid)
            return i;
    return -1;
}

static int rows_shown(const App *app)
{
    int n = match(app)->n_candidates - scroll;
    return n < VISIBLE ? n : VISIBLE;
}

static Rect row_rect(int i)
{
    return (Rect){PAD, LIST_TOP + i * ROW_STEP, BOTTOM_W - 2 * PAD, ROW_H};
}

static void enter(App *app)
{
    /* stand on the game the page shows now: picked before, or the console's region */
    app->which_sel = 0;
    for (int i = 0; i < match(app)->n_candidates; i++)
        if (match(app)->candidates[i] == app->checks.variant)
            app->which_sel = i;
    scroll = app->which_sel >= VISIBLE ? app->which_sel - VISIBLE + 1 : 0;
    app->focus = app->which_sel - scroll;
}

static int targets(const App *app, Target *out)
{
    for (int i = 0; i < rows_shown(app); i++)
        out[i] = (Target){row_rect(i), 9};
    return rows_shown(app);
}

static bool dpad(App *app, NavDir dir)
{
    int n = match(app)->n_candidates;
    if (dir == NAV_LEFT || dir == NAV_RIGHT || !n)
        return true;
    app->which_sel += dir == NAV_DOWN ? 1 : -1;
    if (app->which_sel < 0)
        app->which_sel = 0;
    if (app->which_sel >= n)
        app->which_sel = n - 1;
    if (app->which_sel < scroll)
        scroll = app->which_sel;
    if (app->which_sel >= scroll + VISIBLE)
        scroll = app->which_sel - VISIBLE + 1;
    app->focus = app->which_sel - scroll;
    return true;
}

static void choose(App *app, int candidate)
{
    snprintf(app->which_slug, sizeof app->which_slug, "%s", item(app)->slug);
    app->which_variant = match(app)->candidates[candidate];
    app_go(app, SCREEN_DETAILS); /* its enter checks the picked game */
}

static void update(App *app, const Input *in)
{
    if (in->down & KEY_B) {
        app_go(app, SCREEN_DETAILS);
        return;
    }
    int row = app->focus;
    if (row >= 0 && scroll + row < match(app)->n_candidates)
        app->which_sel = scroll + row;
    if (!in->tapped)
        return;
    for (int i = 0; i < rows_shown(app); i++) {
        if (!rect_hit(row_rect(i), in->tap_x, in->tap_y))
            continue;
        if (title_of(app, scroll + i) >= 0)
            choose(app, scroll + i);
        return;
    }
}

/* ---------------------------------------------------------------- drawing */

static void version_line(char *out, size_t size, const CatVariant *v, const ConsoleTitle *t)
{
    char have[32] = "без оновлення";
    if (t->update_version >= 0)
        snprintf(have, sizeof have, "v%d.%d", t->update_version >> 10, (t->update_version >> 4) & 0x3F);
    if (version_fits(v, t->update_version))
        snprintf(out, size, "%s · підходить", have);
    else
        snprintf(out, size, "!%s, потрібна v%d.%d", have, v->game_version >> 10, (v->game_version >> 4) & 0x3F);
}

static void draw_top(const App *app)
{
    const CatItem *it = item(app);
    ui_circle(44, 40, 20, COL_YELLOW);
    ui_icon(ICON_LIST, 44, 40, 22, COL_INK);
    ui_text(76, 22, 17, FONT_HEAVY, COL_INK, "Для якої гри?");
    char sub[160];
    snprintf(sub, sizeof sub, "%s · українізатор v%s", it->title, it->version);
    draw_text_fit(76, 46, TOP_W - 96, 10.5f, FONT_BOLD, COL_MUTED, sub);

    const char *region = titles_console_region();
    char text[512];
    snprintf(text, sizeof text,
             "На консолі кілька версій гри, і жодна не регіону консолі (%s). Файли в них різні, тож і переклад для "
             "кожної свій. Переклад стане тільки на вибрану гру, інші лишаться як є.",
             region[0] ? region : "невідомо");
    float y = 74 + ui_text_wrap(24, 74, TOP_W - 48, 10.5f, FONT_BOLD, COL_BODY, text) + 8;

    if (!match(app)->n_candidates)
        return;
    int t = title_of(app, app->which_sel);
    if (t < 0) {
        ui_text(24, y, 11, FONT_HEAVY, COL_RED, "Цієї гри вже немає на консолі: картридж вийнято?");
        return;
    }
    const ConsoleTitle *ct = &app->hub.titles.items[t];
    const CatVariant *v = &it->variants[match(app)->candidates[app->which_sel]];
    char tid[20], version[64], where[48];
    format_tid(tid, sizeof tid, ct->title_id);
    version_line(version, sizeof version, v, ct);
    snprintf(where, sizeof where, "%s · %s", v->region, ct->media == MEDIATYPE_GAME_CARD ? "картридж" : "на SD-карті");
    const char *rows[] = {"Гра", ct->name, "Регіон", where, "Title ID", tid, "Версія гри", version};
    draw_info_rows(24, y, TOP_W - 48, 22, rows, 4);
}

static void draw_bottom(const App *app)
{
    const CatItem *it = item(app);
    ui_text(PAD + 2, 9, 13, FONT_HEAVY, COL_INK, "Ігри на консолі");
    bool picked = !strcmp(app->which_slug, it->slug);
    for (int i = 0; i < rows_shown(app); i++) {
        int c = scroll + i, t = title_of(app, c);
        const CatVariant *v = &it->variants[match(app)->candidates[c]];
        Rect r = row_rect(i);
        if (c == app->which_sel)
            ui_rrect(r.x, r.y, r.w, r.h, 9, COL_CARD);
        const char *badge = picked && app->which_variant == match(app)->candidates[c] ? "вибрано" : NULL;
        float bw = badge ? ui_text_width(9.5f, FONT_HEAVY, badge) + 18 : 0;
        char sub[96], tid[20];
        format_tid(tid, sizeof tid, v->title_id);
        snprintf(sub, sizeof sub, "%s · %s", v->region, tid);
        if (t >= 0) {
            draw_title_icon(app, t, r.x + 6, r.y + 3, 26);
            draw_text_fit(r.x + 40, r.y + 3, r.w - 46 - bw, 11, FONT_HEAVY, COL_INK, app->hub.titles.items[t].name);
        } else {
            draw_text_fit(r.x + 40, r.y + 3, r.w - 46 - bw, 11, FONT_HEAVY, COL_MUTED, "немає на консолі");
        }
        ui_text(r.x + 40, r.y + 18, 9.5f, FONT_BOLD, COL_MUTED, sub);
        if (badge)
            draw_badge(r.x + r.w - 6, r.y + 10, badge, COL_GREEN_BG, COL_GREEN_TEXT);
    }
    if (match(app)->n_candidates > VISIBLE) {
        float track = VISIBLE * ROW_STEP - 2, h = track * VISIBLE / match(app)->n_candidates;
        float y = LIST_TOP + (track - h) * scroll / (match(app)->n_candidates - VISIBLE);
        ui_rrect(BOTTOM_W - 5, y, 3, h, 1.5f, COL_FAINT);
    }
    static const char *const hints[] = {"A", "Перекласти цю", "B", "Назад", NULL};
    draw_hints(hints);
}

const Screen screen_which = {
    .enter = enter, .update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets, .dpad = dpad,
};
