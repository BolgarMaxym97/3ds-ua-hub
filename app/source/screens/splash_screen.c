/* A1: the logo while the catalog loads; then the catalog, or A3 / A4 / a broken server. */
#include "../ui.h"
#include "common.h"

#define MIN_MS 700 /* long enough to read, short enough not to wait for */

static void update(App *app, const Input *in)
{
    (void)in;
    /* the copy from the card opens the catalog while the server answers ("early") */
    if ((app->hub.state == LOAD_RUNNING && !app->hub.early) || osGetTime() - app->entered_at < MIN_MS)
        return;
    bool catalog = app->hub.state == LOAD_ONLINE || app->hub.state == LOAD_OFFLINE || app->hub.early;
    /* A2 once, at start, and only when something is wrong: a refresh from Help skips it */
    static bool checked;
    if (catalog && !checked) {
        checked = true;
        if (console_check_needed(app)) {
            app_go(app, SCREEN_CHECK);
            return;
        }
    }
    if (catalog)
        app_open_catalog(app);
    else
        app_go(app, SCREEN_MESSAGE);
}

static void draw_top(const App *app)
{
    (void)app;
    ui_logo(TOP_W / 2 - 36, 46, 72);
    ui_text_center(TOP_W / 2, 132, 26, FONT_HEAVY, COL_INK, "3DS UA Hub");
    ui_text_center(TOP_W / 2, 166, 12, FONT_BOLD, COL_MUTED, "українізатори ігор · v" APP_VERSION);
}

static void draw_step(float y, bool done, bool now, int n, const char *label, const char *value)
{
    float cx = 34, cy = y + 13;
    ui_circle(cx, cy, 7, done ? COL_GREEN_BG : now ? COL_BLUE : COL_LINE);
    if (done) {
        ui_icon(ICON_CHECK, cx, cy, 10, COL_GREEN_TEXT);
    } else {
        char digit[4];
        snprintf(digit, sizeof digit, "%d", n);
        ui_text_center(cx, cy - 6, 9.5f, FONT_HEAVY, now ? COL_WHITE : COL_MUTED, digit);
    }
    ui_text(48, y + 6, 11, now ? FONT_HEAVY : FONT_BOLD, COL_INK, label);
    if (value)
        ui_text_right(BOTTOM_W - 26, y + 7, 10.5f, FONT_BOLD, now ? COL_BLUE : COL_MUTED, value);
}

static void draw_bottom(const App *app)
{
    bool loading = app->hub.state == LOAD_RUNNING;
    ui_card(14, 18, BOTTOM_W - 28, 58, 10, COL_CARD, 2);
    draw_step(22, !loading, loading, 1, "Оновлюю каталог", loading ? "з'єднуюсь…" : NULL);
    ui_rect(26, 49, BOTTOM_W - 52, 1, COL_LINE);
    draw_step(50, false, !loading, 2, "Відкриваю каталог", NULL);

    /* indeterminate: a block sliding along the track while the server answers */
    float t = (float)((osGetTime() - app->entered_at) % 1400) / 1400.0f;
    ui_rrect(14, 92, BOTTOM_W - 28, 7, 3.5f, COL_TRACK);
    ui_rrect(14 + (BOTTOM_W - 28 - 70) * t, 92, 70, 7, 3.5f, COL_BLUE);
    ui_text_center(BOTTOM_W / 2, 206, 10, FONT_BOLD, COL_MUTED, "Без інтернету відкриємо каталог з останнього запуску");
}

const Screen screen_splash = {.update = update, .draw_top = draw_top, .draw_bottom = draw_bottom};
