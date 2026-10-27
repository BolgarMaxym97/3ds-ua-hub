/* H1: about the app - what to send when something does not work (the install ID, the console),
 * the statistics switch, refreshing the catalog and the log. */
#include <stdio.h>
#include <string.h>

#include "../titles.h"
#include "../ui.h"
#include "common.h"

static const Rect stats_card = {14, 10, BOTTOM_W - 28, 72};
static const Rect refresh_button = {14, 100, (BOTTOM_W - 28 - 10) / 2, 44};
static const Rect log_button = {14 + (BOTTOM_W - 28 - 10) / 2 + 10, 100, (BOTTOM_W - 28 - 10) / 2, 44};

static int targets(const App *app, Target *out)
{
    (void)app;
    out[0] = (Target){stats_card, 10};
    out[1] = (Target){refresh_button, 11};
    out[2] = (Target){log_button, 11};
    return 3;
}

static void update(App *app, const Input *in)
{
    if (in->down & (KEY_B | KEY_SELECT)) {
        app_go(app, app->back);
        return;
    }
    if (!in->tapped)
        return;
    if (rect_hit(stats_card, in->tap_x, in->tap_y)) {
        stats_set_enabled(&app->stats, !app->stats.enabled);
    } else if (rect_hit(refresh_button, in->tap_x, in->tap_y) && app->hub.state != LOAD_RUNNING) {
        hub_start(&app->hub);
        app_go(app, SCREEN_SPLASH);
    } else if (rect_hit(log_button, in->tap_x, in->tap_y)) {
        app_go(app, SCREEN_LOG);
    }
}

/* 1 українізатор, 2 українізатори, 5 українізаторів */
static const char *plural(int n)
{
    if (n % 10 == 1 && n % 100 != 11)
        return "українізатор";
    if (n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14))
        return "українізатори";
    return "українізаторів";
}

static void draw_top(const App *app)
{
    ui_logo(20, 14, 40);
    ui_text(70, 14, 19, FONT_HEAVY, COL_INK, "3DS UA Hub");
    char sub[128];
    snprintf(sub, sizeof sub, "v" APP_VERSION " · каталог від %s · %d %s", app->hub.fetched_at[0] ? app->hub.fetched_at : "—",
             app->hub.catalog.n, plural(app->hub.catalog.n));
    draw_text_fit(70, 38, TOP_W - 90, 10.5f, FONT_BOLD, COL_MUTED, sub);

    char id[REPORT_ID_LEN + 2], console[96], installed[64], size[16];
    report_id_display(app->stats.id, id);
    format_console(console, sizeof console, app);
    u32 bytes = 0;
    for (int i = 0; i < app->n_records; i++)
        for (int f = 0; f < app->records[i].n_files; f++)
            bytes += app->records[i].files[f].size;
    format_size(size, sizeof size, bytes);
    snprintf(installed, sizeof installed, "%d %s · %s", app->n_records, plural(app->n_records), size);
    const char *rows[] = {"ID установки", id, "Консоль", console, "Журнал", "SD:/3ds/3ds-ua-hub/hub.log", "Встановлено",
                          installed};
    float h = draw_info_rows(20, 64, TOP_W - 40, 22, rows, 4);
    ui_text_wrap(20, 72 + h, TOP_W - 40, 10.5f, FONT_BOLD, COL_BODY,
                 "Щось не працює? Сфотографуйте цей екран і надішліть у t.me/side_quest_ua — за ID знайдемо ваш звіт.");
}

static void draw_bottom(const App *app)
{
    ui_card(stats_card.x, stats_card.y, stats_card.w, stats_card.h, 10, COL_CARD, 1.5f);
    ui_text(stats_card.x + 12, stats_card.y + 9, 12, FONT_HEAVY, COL_INK, "Анонімна статистика");
    /* the switch */
    float sx = stats_card.x + stats_card.w - 46, sy = stats_card.y + 9;
    bool on = app->stats.enabled;
    ui_rrect(sx, sy, 34, 18, 9, on ? COL_GREEN : COL_TRACK);
    ui_circle(on ? sx + 25 : sx + 9, sy + 9, 7, COL_WHITE);
    ui_text_wrap(stats_card.x + 12, stats_card.y + 31, stats_card.w - 24, 10.5f, FONT_BOLD, COL_MUTED,
                 "Що встановлено, версії, помилки й етап. Без IP, імені, Friend Code.");

    draw_button(refresh_button, app->hub.state == LOAD_RUNNING ? BTN_DISABLED : BTN_SECONDARY, ICON_UPDATE, "Оновити", 12);
    draw_button(log_button, BTN_SECONDARY, ICON_LIST, "Журнал", 12);
    static const char *const hints[] = {"A", "Вибрати", "B", "До каталогу", "START", "Вийти", NULL};
    draw_hints(hints);
}

const Screen screen_help = {.update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets};
