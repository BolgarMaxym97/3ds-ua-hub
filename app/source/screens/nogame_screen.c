/* J1: a patch put on with no game on the console. The files go to luma/titles/<TID> of the picked
 * variant and wait there: a cartridge put in or the game installed later, and Luma loads them.
 * Several variants: the person says for which game (the console's region first). */
#include <stdio.h>
#include <string.h>

#include "../titles.h"
#include "../ui.h"
#include "common.h"

#define ROW_H 28
#define ROW_STEP 31
#define LIST_TOP 30
#define VISIBLE 4

static const Rect install_button = {14, 160, BOTTOM_W - 28, 44};

static int order[CAT_MAX_VARIANTS]; /* the variants, the console's region first */
static int n_order, scroll;

static const CatItem *item(const App *app)
{
    return &app->hub.catalog.items[app->page];
}

static int shown(void)
{
    int n = n_order - scroll;
    return n < VISIBLE ? n : VISIBLE;
}

static Rect row_rect(int i)
{
    return (Rect){PAD, LIST_TOP + i * ROW_STEP, BOTTOM_W - 2 * PAD, ROW_H};
}

static void enter(App *app)
{
    const CatItem *it = item(app);
    const char *region = titles_console_region();
    n_order = 0;
    for (int v = 0; v < it->n_variants; v++)
        if (!strcmp(it->variants[v].region, region))
            order[n_order++] = v;
    for (int v = 0; v < it->n_variants; v++)
        if (strcmp(it->variants[v].region, region))
            order[n_order++] = v;
    app->nogame_sel = 0;
    scroll = 0;
    app->focus = 0;
}

static int targets(const App *app, Target *out)
{
    (void)app;
    out[0] = (Target){install_button, 13};
    int n = n_order > 1 ? shown() : 0;
    for (int i = 0; i < n; i++)
        out[1 + i] = (Target){row_rect(i), 9};
    return 1 + n;
}

/* Up and down walk the games; A stays on "Встановити". */
static bool dpad(App *app, NavDir dir)
{
    if (dir == NAV_LEFT || dir == NAV_RIGHT || n_order < 2)
        return true;
    app->nogame_sel += dir == NAV_DOWN ? 1 : -1;
    if (app->nogame_sel < 0)
        app->nogame_sel = 0;
    if (app->nogame_sel >= n_order)
        app->nogame_sel = n_order - 1;
    if (app->nogame_sel < scroll)
        scroll = app->nogame_sel;
    if (app->nogame_sel >= scroll + VISIBLE)
        scroll = app->nogame_sel - VISIBLE + 1;
    app->focus = 0;
    return true;
}

static void update(App *app, const Input *in)
{
    if (in->down & KEY_B) {
        app_go(app, SCREEN_NOTFOUND);
        return;
    }
    if (!in->tapped)
        return;
    if (rect_hit(install_button, in->tap_x, in->tap_y)) {
        app_install_nogame(app, order[app->nogame_sel]);
        return;
    }
    for (int i = 0; n_order > 1 && i < shown(); i++)
        if (rect_hit(row_rect(i), in->tap_x, in->tap_y))
            app->nogame_sel = scroll + i;
}

static const char *region_name(const char *region)
{
    static const char *const names[][2] = {
        {"EUR", "Європейська"}, {"USA", "Американська"}, {"JPN", "Японська"},
        {"KOR", "Корейська"},   {"CHN", "Китайська"},    {"TWN", "Тайванська"},
    };
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++)
        if (!strcmp(names[i][0], region))
            return names[i][1];
    return region;
}

static void draw_top(const App *app)
{
    const CatItem *it = item(app);
    ui_circle(44, 40, 20, COL_BLUE);
    ui_icon(ICON_SHIELD, 44, 40, 22, COL_WHITE);
    ui_text(76, 22, 17, FONT_HEAVY, COL_INK, "Встановити без гри?");
    char sub[160];
    snprintf(sub, sizeof sub, "%s · українізатор v%s", it->title, it->version);
    draw_text_fit(76, 46, TOP_W - 96, 10.5f, FONT_BOLD, COL_MUTED, sub);
    float y = 76;
    y += ui_text_wrap(24, y, TOP_W - 48, 11, FONT_BOLD, COL_BODY,
                      "Переклад ляже на SD-карту й чекатиме на гру. Вставте картридж або встановіть гру потрібної "
                      "версії, і Luma підхопить переклад сама: перевстановлювати нічого не треба.") + 12;
    const char *warn = "Гри немає, тож її версію перевірити не можемо. Якщо ваша гра іншої ревізії, частина тексту "
                       "може лишитися англійською.";
    float h = ui_text_wrap_height(TOP_W - 48 - 40, 10, FONT_BOLD, warn) + 16;
    ui_card(24, y, TOP_W - 48, h, 10, COL_CARD, 1.5f);
    draw_mark(42, y + 16, 8, MARK_WARN);
    ui_text_wrap(58, y + 8, TOP_W - 48 - 44, 10, FONT_BOLD, COL_BODY, warn);
}

static void draw_bottom(const App *app)
{
    const CatItem *it = item(app);
    const char *region = titles_console_region();
    if (n_order > 1) {
        ui_text(PAD + 2, 8, 13, FONT_HEAVY, COL_INK, "Для якої версії гри?");
        for (int i = 0; i < shown(); i++) {
            int k = scroll + i;
            const CatVariant *v = &it->variants[order[k]];
            Rect r = row_rect(i);
            bool on = k == app->nogame_sel;
            ui_card(r.x, r.y, r.w, r.h, 9, COL_CARD, on ? 0 : 1.5f);
            if (on)
                ui_frame(r.x, r.y, r.w, r.h, 9, 2, COL_BLUE);
            ui_circle(r.x + 14, r.y + r.h / 2, 6, on ? COL_BLUE : COL_FAINT);
            ui_circle(r.x + 14, r.y + r.h / 2, on ? 2.5f : 4, COL_CARD);
            char name[48], tid[20], line[64];
            snprintf(name, sizeof name, "%s (%s)", region_name(v->region), v->region);
            format_tid(tid, sizeof tid, v->title_id);
            snprintf(line, sizeof line, "%s%s", tid, !strcmp(v->region, region) ? " · як ваша консоль" : "");
            ui_text(r.x + 28, r.y + 2, 11, FONT_HEAVY, COL_INK, name);
            ui_text(r.x + 28, r.y + 15, 9, FONT_BOLD, COL_MUTED, line);
        }
    } else if (n_order == 1) {
        const CatVariant *v = &it->variants[order[0]];
        char where[64], tid[20];
        format_tid(tid, sizeof tid, v->title_id);
        snprintf(where, sizeof where, "%s (%s)", region_name(v->region), v->region);
        ui_text(PAD + 2, 8, 13, FONT_HEAVY, COL_INK, "Для гри");
        const char *rows[] = {"Версія", where, "Title ID", tid};
        draw_info_rows(PAD, 34, BOTTOM_W - 2 * PAD, 22, rows, 2);
    }
    draw_button(install_button, BTN_PRIMARY, ICON_DOWNLOAD, "Встановити без гри", 15);
    static const char *const several[] = {"A", "Встановити", "+", "Версія", "B", "Назад", NULL};
    static const char *const one[] = {"A", "Встановити", "B", "Назад", NULL};
    draw_hints(n_order > 1 ? several : one);
}

const Screen screen_nogame = {
    .enter = enter, .update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets, .dpad = dpad,
};
