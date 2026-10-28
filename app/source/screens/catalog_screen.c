/* B1-B4: the catalog. Top: the highlighted entry; bottom: tabs, the list, key hints. */
#include <stdio.h>
#include <string.h>

#include "../image.h"
#include "../ui.h"
#include "common.h"

static const char *const tab_names[TAB_COUNT] = {"Усі", "Встановлені", "Оновлення"};

#define KEY_CAP 17
#define TABS_Y 9
#define TABS_H 23
#define TABS_X (PAD + KEY_CAP + 5)
#define TABS_W (BOTTOM_W - 2 * TABS_X)
#define BAR_Y 38
#define BAR_H 22
#define ROW_H 32
#define ROW_STEP 34
#define LIST_BOTTOM (FOOT_Y - 6)

static bool offline(const App *app)
{
    /* RUNNING here is a refresh from the bar: it stays up, saying so; TOO_NEW: an old copy (A4) */
    return app->hub.state == LOAD_OFFLINE || app->hub.state == LOAD_RUNNING || app->hub.state == LOAD_TOO_NEW;
}

static float list_top(const App *app)
{
    return offline(app) ? BAR_Y + BAR_H + 6 : BAR_Y + 2;
}

static int visible_rows(const App *app)
{
    return (int)((LIST_BOTTOM - list_top(app) + (ROW_STEP - ROW_H)) / ROW_STEP);
}

static Rect tab_rect(int i)
{
    float w = (TABS_W - 4 - 2 * 2) / 3.0f;
    return (Rect){TABS_X + 2 + i * (w + 2), TABS_Y + 2, w, TABS_H - 4};
}

static const Rect bar_button = {BOTTOM_W - PAD - 64, BAR_Y, 64, BAR_H};

static Rect row_rect(const App *app, int visible_index)
{
    return (Rect){PAD, list_top(app) + visible_index * ROW_STEP, BOTTOM_W - 2 * PAD, ROW_H};
}

/* Targets: the three tabs, the bar's button when offline, then the rows on screen. */
static int rows_base(const App *app)
{
    return TAB_COUNT + (offline(app) ? 1 : 0);
}

static int rows_shown(const App *app)
{
    int n = app->list_n - app->scroll;
    return n < visible_rows(app) ? n : visible_rows(app);
}

static int targets(const App *app, Target *out)
{
    int n = 0;
    for (int i = 0; i < TAB_COUNT; i++)
        out[n++] = (Target){tab_rect(i), 9};
    if (offline(app))
        out[n++] = (Target){bar_button, 8};
    for (int i = 0; i < rows_shown(app); i++)
        out[n++] = (Target){row_rect(app, i), 9};
    return n;
}

static void clamp_scroll(App *app)
{
    int max = app->list_n - visible_rows(app);
    if (app->scroll > max)
        app->scroll = max;
    if (app->scroll < 0)
        app->scroll = 0;
}

static void enter(App *app)
{
    if (app->sel < app->scroll)
        app->scroll = app->sel;
    if (app->sel >= app->scroll + visible_rows(app))
        app->scroll = app->sel - visible_rows(app) + 1;
    clamp_scroll(app);
    /* back from a page: stand on its row; otherwise on the tab */
    int row = app->sel - app->scroll;
    app->focus = app->list_n && row >= 0 && row < rows_shown(app) ? rows_base(app) + row : (int)app->opts.tab;
}

/* Up and down walk the whole list, scrolling it; only above the first row does the focus leave
 * for the tabs. */
static bool dpad(App *app, NavDir dir)
{
    int base = rows_base(app), row = app->focus - base;
    if (row < 0 || !app->list_n)
        return false;
    if (dir == NAV_LEFT || dir == NAV_RIGHT)
        return true;
    if (dir == NAV_UP && app->sel == 0)
        return false;
    app->sel += dir == NAV_DOWN ? 1 : -1;
    if (app->sel >= app->list_n)
        app->sel = app->list_n - 1;
    if (app->sel < app->scroll)
        app->scroll = app->sel;
    if (app->sel >= app->scroll + visible_rows(app))
        app->scroll = app->sel - visible_rows(app) + 1;
    app->focus = base + app->sel - app->scroll;
    return true;
}

static void set_tab(App *app, Tab tab)
{
    if (app->opts.tab == tab)
        return;
    app->opts.tab = tab;
    app_rebuild_list(app);
    app->sel = app->scroll = 0;
}

static void open_selected(App *app)
{
    app->page = app->list[app->sel];
    app_go(app, SCREEN_DETAILS);
}

static void update(App *app, const Input *in)
{
    static float drag;
    if (in->down & KEY_L && app->opts.tab > 0)
        set_tab(app, app->opts.tab - 1);
    if (in->down & KEY_R && app->opts.tab < TAB_COUNT - 1)
        set_tab(app, app->opts.tab + 1);
    if (in->down & (KEY_L | KEY_R))
        app->focus = app->opts.tab;
    if (in->down & KEY_X) {
        app_go(app, SCREEN_FILTER);
        return;
    }
    if (in->down & KEY_SELECT) {
        app_go(app, SCREEN_HELP);
        return;
    }
    if (in->down & KEY_Y && offline(app) && app->hub.state != LOAD_RUNNING)
        hub_start(&app->hub);

    /* the stylus drags the list: a row's height per row */
    if (in->touching && in->touch_y > list_top(app)) {
        drag += in->drag_dy;
        while (drag <= -ROW_STEP / 2.0f) {
            app->scroll++;
            drag += ROW_STEP;
        }
        while (drag >= ROW_STEP / 2.0f) {
            app->scroll--;
            drag -= ROW_STEP;
        }
        clamp_scroll(app);
    } else {
        drag = 0;
    }

    /* the focus on a row is the selection */
    int row = app->focus - rows_base(app);
    if (row >= 0 && app->scroll + row < app->list_n)
        app->sel = app->scroll + row;

    if (!in->tapped)
        return;
    for (int i = 0; i < TAB_COUNT; i++)
        if (rect_hit(tab_rect(i), in->tap_x, in->tap_y))
            set_tab(app, (Tab)i);
    if (offline(app) && rect_hit(bar_button, in->tap_x, in->tap_y) && app->hub.state != LOAD_RUNNING)
        hub_start(&app->hub);
    for (int i = 0; i < rows_shown(app); i++) {
        if (rect_hit(row_rect(app, i), in->tap_x, in->tap_y)) {
            app->sel = app->scroll + i;
            open_selected(app);
            return;
        }
    }
}

/* ---------------------------------------------------------------- top */

static void draw_empty_top(const App *app)
{
    const char *title = "Нічого не знайдено", *text = "Змініть фільтри: X на нижньому екрані.";
    if (app->opts.tab == TAB_INSTALLED) {
        title = "Ще нічого не встановлено";
        text = "Виберіть українізатор на вкладці «Усі» — L повертає туди.";
    } else if (app->opts.tab == TAB_UPDATES) {
        title = "Оновлень немає";
        text = "Усі встановлені переклади — найновіших версій.";
    } else if (!app->hub.catalog.n) {
        title = "Каталог поки порожній";
        text = "Українізатори з'являться тут, щойно їх додадуть.";
    }
    ui_circle(TOP_W / 2, 84, 30, COL_LINE);
    ui_icon(ICON_SEARCH, TOP_W / 2, 84, 30, COL_MUTED);
    ui_text_center(TOP_W / 2, 128, 19, FONT_HEAVY, COL_INK, title);
    ui_text_center(TOP_W / 2, 158, 11.5f, FONT_BOLD, COL_BODY, text);
}

#define ROW 21 /* a card row on the top screen */

static void card_row(float y, const char *label, const char *value, u32 value_colour, bool line)
{
    const float max_w = 230;
    ui_text(36, y + 4, 10.5f, FONT_BOLD, COL_MUTED, label);
    if (ui_text_width(10.5f, FONT_HEAVY, value) <= max_w)
        ui_text_right(TOP_W - 36, y + 4, 10.5f, FONT_HEAVY, value_colour, value);
    else
        draw_text_fit(TOP_W - 36 - max_w, y + 4, max_w, 10.5f, FONT_HEAVY, value_colour, value);
    if (line)
        ui_rect(36, y + ROW - 1, TOP_W - 72, 1, COL_LINE);
}

static void draw_top(const App *app)
{
    if (!app->list_n) {
        draw_empty_top(app);
        return;
    }
    const CatItem *it = &app->hub.catalog.items[app->list[app->sel]];
    draw_banner(it, TOP_W);

    char line[128], size[16], progress[48];
    format_size(size, sizeof size, (u32)variant_bytes(&it->variants[0]));
    snprintf(line, sizeof line, "v%s%s%s · %s", it->version, it->authors[0] ? " · " : "", it->authors, size);
    format_progress(progress, sizeof progress, it);

    ItemState state = app->states ? app->states[app->list[app->sel]] : ITEM_NEW;
    const Record *rec = state != ITEM_NEW ? app_record(app, it->slug) : NULL;
    ui_card(18, BANNER_H + 7, TOP_W - 36, 3 * ROW, 10, COL_CARD, 1.5f);
    if (rec) {
        /* B2: what the console has, and what is new */
        char have[48];
        snprintf(have, sizeof have, "v%s · %s", rec->version, rec->date);
        card_row(BANNER_H + 7, "Встановлено", have, state == ITEM_UPDATE ? COL_INK : COL_GREEN_TEXT, true);
        if (state == ITEM_UPDATE) {
            snprintf(line, sizeof line, "v%s · %s", it->version, size);
            card_row(BANNER_H + 7 + ROW, "Нова версія", line, COL_AMBER_TEXT, true);
        } else {
            card_row(BANNER_H + 7 + ROW, "Переклад", line, COL_INK, true);
        }
    } else {
        card_row(BANNER_H + 7, "Тип", kind_name(it->kind), COL_INK, true);
        card_row(BANNER_H + 7 + ROW, "Переклад", line, COL_INK, true);
    }
    card_row(BANNER_H + 7 + 2 * ROW, "Текст · текстури", progress,
             it->text_pct == 100 && it->textures_pct == 100 ? COL_GREEN_TEXT : COL_INK, false);
    /* three lines fit under the card */
    if (it->description)
        ui_text_wrap(18, BANNER_H + 13 + 3 * ROW, TOP_W - 36, 10, FONT_BOLD, COL_BODY, it->description);
}

/* ---------------------------------------------------------------- bottom */

static void draw_key_cap(float x, float y, const char *key)
{
    ui_rrect(x, y, KEY_CAP, KEY_CAP, KEY_CAP / 2.0f, COL_BODY);
    ui_text_center(x + KEY_CAP / 2.0f, y + 1.5f, 10, FONT_HEAVY, COL_WHITE, key);
}

static void draw_row(const App *app, int list_index, Rect r)
{
    int index = app->list[list_index];
    const CatItem *it = &app->hub.catalog.items[index];
    ItemState state = app->states ? app->states[index] : ITEM_NEW;
    bool selected = list_index == app->sel;
    if (selected)
        ui_rrect(r.x, r.y, r.w, r.h, 9, COL_CARD);
    draw_item_icon(it, r.x + 6, r.y + 3, 26);
    char sub[96], size[16], version[48];
    const Record *rec = state != ITEM_NEW ? app_record(app, it->slug) : NULL;
    format_size(size, sizeof size, (u32)variant_bytes(&it->variants[0]));
    if (rec && state == ITEM_UPDATE)
        snprintf(version, sizeof version, " · v%s, є v%s", rec->version, it->version);
    else if (rec)
        snprintf(version, sizeof version, " · v%s", rec->version);
    else
        version[0] = 0;
    snprintf(sub, sizeof sub, "%s%s · %s", it->kind == KIND_PATCH ? "Патч" : "Програма", version, size);
    const char *badge = state == ITEM_UPDATE ? "оновлення" : state == ITEM_INSTALLED ? "актуальна"
                        : it->kind == KIND_PATCH       ? "патч"
                                                       : "CIA";
    u32 bg = state == ITEM_UPDATE ? COL_AMBER_BG : state == ITEM_INSTALLED ? COL_GREEN_BG : it->kind == KIND_PATCH ? COL_BLUE_BG : COL_BODY;
    u32 fg = state == ITEM_UPDATE ? COL_AMBER_TEXT : state == ITEM_INSTALLED ? COL_GREEN_TEXT : it->kind == KIND_PATCH ? COL_BLUE : COL_WHITE;
    float bw = ui_text_width(9.5f, FONT_HEAVY, badge) + 12;
    draw_text_fit(r.x + 40, r.y + 3, r.w - 40 - bw - 12, 11, FONT_HEAVY, COL_INK, it->title);
    draw_text_fit(r.x + 40, r.y + 18, r.w - 40 - bw - 12, 9.5f, FONT_BOLD, COL_MUTED, sub);
    draw_badge(r.x + r.w - 6, r.y + 10, badge, bg, fg);
}

static void draw_bottom(const App *app)
{
    draw_key_cap(PAD, TABS_Y + 3, "L");
    draw_key_cap(BOTTOM_W - PAD - KEY_CAP, TABS_Y + 3, "R");
    ui_rrect(TABS_X, TABS_Y, TABS_W, TABS_H, TABS_H / 2.0f, COL_TRACK);
    for (int i = 0; i < TAB_COUNT; i++) {
        Rect r = tab_rect(i);
        bool on = (int)app->opts.tab == i;
        if (on)
            ui_rrect(r.x, r.y, r.w, r.h, r.h / 2, COL_CARD);
        char label[32];
        snprintf(label, sizeof label, "%s · %d", tab_names[i],
                 app->states ? view_count(&app->hub.catalog, app->states, &app->opts, (Tab)i) : 0);
        ui_text_center(r.x + r.w / 2, r.y + 3.5f, 10, FONT_HEAVY, on ? COL_INK : COL_BODY, label);
    }

    if (offline(app)) {
        bool loading = app->hub.state == LOAD_RUNNING;
        char text[96];
        if (app->hub.state == LOAD_TOO_NEW)
            snprintf(text, sizeof text, "Оновіть Hub · каталог від %s", app->hub.fetched_at[0] ? app->hub.fetched_at : "?");
        else
            snprintf(text, sizeof text, loading ? "Оновлюю каталог…" : "Без інтернету · каталог від %s",
                     app->hub.fetched_at[0] ? app->hub.fetched_at : "?");
        ui_rrect(PAD, BAR_Y, BOTTOM_W - 2 * PAD, BAR_H, 8, COL_LINE);
        if (loading)
            ui_spinner(PAD + 12, BAR_Y + BAR_H / 2, 5.5f, COL_BLUE);
        else
            ui_icon(ICON_INFO, PAD + 12, BAR_Y + BAR_H / 2, 12, COL_BODY);
        ui_text(PAD + 22, BAR_Y + 5, 10.5f, FONT_HEAVY, COL_BODY, text);
        if (!loading)
            ui_text_right(bar_button.x + bar_button.w - 8, BAR_Y + 5, 10.5f, FONT_HEAVY, COL_BLUE, "Оновити");
    }

    if (!app->list_n) {
        ui_text_center(BOTTOM_W / 2, list_top(app) + 50, 11, FONT_BOLD, COL_MUTED,
                       app->opts.tab == TAB_ALL ? "Тут нічого немає" : "Порожньо");
    }
    for (int i = 0; i < rows_shown(app); i++)
        draw_row(app, app->scroll + i, row_rect(app, i));
    if (app->list_n > visible_rows(app)) {
        /* where in the list the screen is */
        float track = LIST_BOTTOM - list_top(app);
        float h = track * visible_rows(app) / app->list_n;
        float y = list_top(app) + (track - h) * app->scroll / (app->list_n - visible_rows(app));
        ui_rrect(BOTTOM_W - 5, y, 3, h, 1.5f, COL_FAINT);
    }

    static const char *const online_hints[] = {"A", "Відкрити", "X", "Фільтри", "SELECT", "Допомога", NULL};
    static const char *const offline_hints[] = {"A", "Відкрити", "Y", "Оновити", "SELECT", "Допомога", NULL};
    draw_hints(offline(app) && app->hub.state != LOAD_RUNNING ? offline_hints : online_hints);
}

const Screen screen_catalog = {
    .enter = enter, .update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets, .dpad = dpad,
};
