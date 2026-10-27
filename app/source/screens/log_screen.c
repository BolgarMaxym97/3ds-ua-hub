/* H2: the Hub's journal (3ds/3ds-ua-hub/hub.log), newest first; the highlighted line in full on
 * top. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/fsutil.h"
#include "../ui.h"
#include "common.h"

#define LIST_TOP 34
#define ROW_H 28
#define ROW_STEP 30
#define VISIBLE 5

static int rows_shown(const App *app)
{
    int n = app->log_n - app->log_scroll;
    return n < VISIBLE ? n : VISIBLE;
}

static Rect row_rect(int i)
{
    return (Rect){PAD, LIST_TOP + i * ROW_STEP, BOTTOM_W - 2 * PAD, ROW_H};
}

static void load(App *app)
{
    if (!app->log)
        app->log = calloc(LOG_SHOWN, sizeof *app->log);
    app->log_n = app->log ? hublog_read(app->log) : 0;
    app->log_sel = app->log_scroll = 0;
}

/* X once arms the clearing, X again within 3 s clears: one slip must not lose the log */
static u64 armed_at;

static bool armed(void)
{
    return armed_at && osGetTime() - armed_at < 3000;
}

static void enter(App *app)
{
    load(app);
    armed_at = 0;
}

static int targets(const App *app, Target *out)
{
    for (int i = 0; i < rows_shown(app); i++)
        out[i] = (Target){row_rect(i), 9};
    return rows_shown(app);
}

static bool dpad(App *app, NavDir dir)
{
    if (dir == NAV_LEFT || dir == NAV_RIGHT || !app->log_n)
        return true;
    app->log_sel += dir == NAV_DOWN ? 1 : -1;
    if (app->log_sel < 0)
        app->log_sel = 0;
    if (app->log_sel >= app->log_n)
        app->log_sel = app->log_n - 1;
    if (app->log_sel < app->log_scroll)
        app->log_scroll = app->log_sel;
    if (app->log_sel >= app->log_scroll + VISIBLE)
        app->log_scroll = app->log_sel - VISIBLE + 1;
    app->focus = app->log_sel - app->log_scroll;
    return true;
}

static void update(App *app, const Input *in)
{
    if (in->down & KEY_B) {
        app_go(app, SCREEN_HELP);
        return;
    }
    if (in->down & KEY_X && app->log_n) {
        if (armed()) {
            fs_remove(HUB_DIR "/hub.log");
            load(app);
            armed_at = 0;
        } else {
            armed_at = osGetTime();
        }
        return;
    }
    for (int i = 0; in->tapped && i < rows_shown(app); i++) {
        if (rect_hit(row_rect(i), in->tap_x, in->tap_y)) {
            app->log_sel = app->log_scroll + i;
            app->focus = i;
        }
    }
}

static bool is_ok(const LogEntry *e)
{
    return !strcmp(e->status, "ok");
}

static const char *const EVENTS[][3] = {
    /* in the file, in a list, as a heading */
    {"install", "встановлено", "Встановлено"}, {"update", "оновлено", "Оновлено"},
    {"remove", "видалено", "Видалено"},        {"check", "перевірено", "Перевірено"},
    {"error", "помилка", "Помилка"},          {"not_found", "гри немає", "Гру не знайдено"},
    {"start", "довгий запуск", "Довгий запуск"},
};

static const char *event_name(const LogEntry *e, int form)
{
    for (unsigned i = 0; i < sizeof EVENTS / sizeof *EVENTS; i++)
        if (!strcmp(e->event, EVENTS[i][0]))
            return EVENTS[i][form];
    return e->event;
}

/* "встановлено", "помилка"... (form 1), or capitalised for the heading (form 2) */
static const char *outcome(const LogEntry *e, int form)
{
    if (!strcmp(e->status, "скасовано"))
        return form == 2 ? "Скасовано" : "скасовано";
    if (!is_ok(e))
        return form == 2 ? "Помилка" : "помилка";
    return event_name(e, form);
}

static u32 outcome_colour(const LogEntry *e)
{
    if (!strcmp(e->event, "not_found"))
        return COL_MUTED;
    return !strcmp(e->status, "скасовано") ? COL_AMBER_TEXT : is_ok(e) ? COL_GREEN_TEXT : COL_RED;
}

static void draw_top(const App *app)
{
    if (!app->log_n) {
        ui_circle(TOP_W / 2, 84, 30, COL_LINE);
        ui_icon(ICON_LIST, TOP_W / 2, 84, 30, COL_MUTED);
        ui_text_center(TOP_W / 2, 128, 19, FONT_HEAVY, COL_INK, "Журнал порожній");
        ui_text_center(TOP_W / 2, 158, 11.5f, FONT_BOLD, COL_BODY, "Тут з'являться встановлення, оновлення й помилки.");
        return;
    }
    const LogEntry *e = &app->log[app->log_sel];
    char head[64];
    snprintf(head, sizeof head, "%s · %s", outcome(e, 2), e->when);
    ui_text(20, 16, 17, FONT_HEAVY, outcome_colour(e), head);
    draw_text_fit(20, 42, TOP_W - 40, 12, FONT_HEAVY, COL_INK, e->title[0] ? e->title : "3DS UA Hub");
    const char *rows[] = {"Подія", event_name(e, 2), "Результат", is_ok(e) ? "успішно" : e->status};
    float h = draw_info_rows(20, 66, TOP_W - 40, 22, rows, 2);
    if (e->detail[0])
        ui_text_wrap(20, 76 + h, TOP_W - 40, 11, FONT_BOLD, COL_BODY, e->detail);
}

static void draw_bottom(const App *app)
{
    ui_text(PAD + 2, 9, 13, FONT_HEAVY, COL_INK, "Журнал");
    ui_text_right(BOTTOM_W - PAD - 2, 12, 10, FONT_BOLD, COL_MUTED, "SD:/3ds/3ds-ua-hub/hub.log");
    for (int i = 0; i < rows_shown(app); i++) {
        const LogEntry *e = &app->log[app->log_scroll + i];
        Rect r = row_rect(i);
        if (app->log_scroll + i == app->log_sel)
            ui_rrect(r.x, r.y, r.w, r.h, 8, COL_CARD);
        ui_text(r.x + 8, r.y + 7, 10, FONT_BOLD, COL_MUTED, e->when);
        const char *what = outcome(e, 1);
        float ow = ui_text_width(10.5f, FONT_HEAVY, what);
        draw_text_fit(r.x + 76, r.y + 6, r.w - 76 - ow - 18, 10.5f, FONT_HEAVY, COL_INK, e->title[0] ? e->title : "3DS UA Hub");
        ui_text_right(r.x + r.w - 8, r.y + 7, 10.5f, FONT_HEAVY, outcome_colour(e), what);
    }
    if (!app->log_n)
        ui_text_center(BOTTOM_W / 2, 90, 11, FONT_BOLD, COL_MUTED, "Порожньо");
    const char *hints[] = {"X", armed() ? "Ще раз — очистити" : "Очистити", "B", "Назад", NULL};
    draw_hints(app->log_n ? hints : hints + 2);
}

const Screen screen_log = {
    .enter = enter, .update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets, .dpad = dpad,
};
