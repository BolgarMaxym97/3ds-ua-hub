/* B5: which entries the catalog shows and in what order. Takes effect at once. */
#include <stdio.h>

#include "../ui.h"
#include "common.h"

static const char *const kind_names[KINDS_COUNT] = {"Усі", "Патч", "CIA"};
static const char *const sort_names[SORT_COUNT] = {"Нові", "А–Я", "Популярні"};
static const char *const kind_long[KINDS_COUNT] = {"Патч і CIA", "Лише патчі", "Лише CIA"};
static const char *const sort_long[SORT_COUNT] = {"Нові спочатку", "За назвою", "Популярні спочатку"};

#define CHIP_W 94
#define CHIP_H 22
#define KIND_Y 30
#define SORT_Y 86

static Rect chip(int row, int i)
{
    return (Rect){PAD + 2 + i * (CHIP_W + 6), row ? SORT_Y : KIND_Y, CHIP_W, CHIP_H};
}

static int targets(const App *app, Target *out)
{
    (void)app;
    int n = 0;
    for (int row = 0; row < 2; row++)
        for (int i = 0; i < 3; i++)
            out[n++] = (Target){chip(row, i), 11};
    return n;
}

static void enter(App *app)
{
    app->focus = (int)app->opts.kinds;
}

static void apply(App *app, KindFilter kinds, SortOrder sort)
{
    if (app->opts.kinds == kinds && app->opts.sort == sort)
        return;
    app->opts.kinds = kinds;
    app->opts.sort = sort;
    app_rebuild_list(app);
    app->sel = app->scroll = 0;
}

static void update(App *app, const Input *in)
{
    if (in->down & (KEY_B | KEY_X)) {
        app_go(app, SCREEN_CATALOG);
        return;
    }
    if (in->down & KEY_Y)
        apply(app, KINDS_ALL, SORT_NAME);
    if (!in->tapped)
        return;
    for (int i = 0; i < 3; i++) {
        if (rect_hit(chip(0, i), in->tap_x, in->tap_y))
            apply(app, (KindFilter)i, app->opts.sort);
        if (rect_hit(chip(1, i), in->tap_x, in->tap_y))
            apply(app, app->opts.kinds, (SortOrder)i);
    }
}

static void draw_top(const App *app)
{
    ui_text(20, 16, 19, FONT_HEAVY, COL_INK, "Фільтри");
    char found[48];
    snprintf(found, sizeof found, "знайдено %d з %d", app->list_n, app->hub.catalog.n);
    ui_text_right(TOP_W - 20, 22, 12, FONT_BOLD, COL_MUTED, found);
    ui_card(20, 52, TOP_W - 40, 50, 10, COL_CARD, 1.5f);
    ui_text(34, 58, 11.5f, FONT_BOLD, COL_MUTED, "Тип");
    ui_text_right(TOP_W - 34, 58, 11.5f, FONT_HEAVY, COL_INK, kind_long[app->opts.kinds]);
    ui_rect(34, 77, TOP_W - 68, 1, COL_LINE);
    ui_text(34, 82, 11.5f, FONT_BOLD, COL_MUTED, "Сортування");
    ui_text_right(TOP_W - 34, 82, 11.5f, FONT_HEAVY, COL_INK, sort_long[app->opts.sort]);
}

static void draw_chip(Rect r, const char *label, bool on)
{
    ui_rrect(r.x, r.y + 1.5f, r.w, r.h, 11, on ? COL_BLUE_DARK : COL_CARD_SHADOW);
    ui_rrect(r.x, r.y, r.w, r.h, 11, on ? COL_BLUE : COL_CARD);
    ui_text_center(r.x + r.w / 2, r.y + 4, 10, FONT_HEAVY, on ? COL_WHITE : COL_INK, label);
}

static void draw_bottom(const App *app)
{
    ui_text(PAD + 2, 12, 10, FONT_HEAVY, COL_MUTED, "Тип українізатора");
    ui_text(PAD + 2, 68, 10, FONT_HEAVY, COL_MUTED, "Сортування");
    for (int i = 0; i < 3; i++) {
        draw_chip(chip(0, i), kind_names[i], (int)app->opts.kinds == i);
        draw_chip(chip(1, i), sort_names[i], (int)app->opts.sort == i);
    }
    static const char *const hints[] = {"A", "Вибрати", "Y", "Скинути", "B", "До списку", NULL};
    draw_hints(hints);
}

const Screen screen_filter = {
    .enter = enter, .update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets,
};
