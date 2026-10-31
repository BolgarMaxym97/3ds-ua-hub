/* J4: the patch's game from the catalog. The link is checked as the screen opens (JOB_PROBE, through
 * the Worker); a broken one goes straight to J7 (the error screen). Then the game downloads and
 * installs, and the patch after it (J5, J6). */
#include <stdio.h>
#include <string.h>

#include "../ui.h"
#include "common.h"

static const Rect go_button = {14, 132, BOTTOM_W - 28, 50};

static const CatItem *item(const App *app)
{
    return &app->hub.catalog.items[app->page];
}

static const CatVariant *variant(const App *app)
{
    return &item(app)->variants[app->checks.fallback];
}

static bool probing(const App *app)
{
    return app->job_active && app->job.kind == JOB_PROBE;
}

/* The game file on the card and the game in the system side by side, then the patch. */
static u64 needed(const App *app)
{
    const CatVariant *v = variant(app);
    return (u64)v->fallback_size * 2 + (u64)v->size * 3 + 16 * 1024 * 1024;
}

static bool room(const App *app)
{
    u64 free_bytes = job_sd_free();
    return !free_bytes || free_bytes >= needed(app);
}

static bool luma_ok(void)
{
    return job_is_emulator() || job_luma_version() >= LUMA_MIN_GAMES;
}

static bool ready(const App *app)
{
    return !probing(app) && app->job.kind == JOB_PROBE && app->job.error == ERR_NONE && room(app) && luma_ok();
}

static void enter(App *app)
{
    if (app->checks.fallback < 0) {
        app_go(app, SCREEN_DETAILS);
        return;
    }
    app_probe(app, app->checks.fallback);
}

static int targets(const App *app, Target *out)
{
    (void)app;
    out[0] = (Target){go_button, 13};
    return 1;
}

static void update(App *app, const Input *in)
{
    if (probing(app))
        return; /* a few seconds at most: the Worker gives the game's server 15 */
    if (in->down & KEY_B) {
        app_go(app, SCREEN_NOTFOUND);
        return;
    }
    if (in->tapped && rect_hit(go_button, in->tap_x, in->tap_y) && ready(app))
        app_install_fallback(app, app->checks.fallback);
}

static void draw_top(const App *app)
{
    const CatItem *it = item(app);
    const CatVariant *v = variant(app);
    draw_item_icon(it, 20, 18, 40);
    ui_text(72, 16, 17, FONT_HEAVY, COL_INK, "Завантажити гру з каталогу?");
    char sub[160], tid[20];
    format_tid(tid, sizeof tid, v->title_id);
    snprintf(sub, sizeof sub, "%s · %s · %s", it->title, v->region, tid);
    draw_text_fit(72, 40, TOP_W - 92, 10.5f, FONT_BOLD, COL_MUTED, sub);

    char game[16], patch[16], third[64];
    format_size(game, sizeof game, v->fallback_size);
    format_size(patch, sizeof patch, v->size);
    snprintf(third, sizeof third, "Поставимо українізатор v%s", it->version);
    const char *steps[][2] = {{"Завантажимо гру (CIA)", game}, {"Встановимо її в меню HOME", ""}, {third, patch}};
    const float y0 = 70, h = 24;
    ui_card(20, y0, TOP_W - 40, 3 * h + 6, 10, COL_CARD, 1.5f);
    for (int i = 0; i < 3; i++) {
        float ry = y0 + 3 + i * h;
        ui_circle(38, ry + h / 2, 8, COL_LINE);
        char n[4];
        snprintf(n, sizeof n, "%d", i + 1);
        ui_text_center(38, ry + h / 2 - 6.5f, 9.5f, FONT_HEAVY, COL_BODY, n);
        ui_text(54, ry + 5, 11, FONT_BOLD, COL_INK, steps[i][0]);
        ui_text_right(TOP_W - 32, ry + 5, 10.5f, FONT_BOLD, COL_MUTED, steps[i][1]);
        if (i < 2)
            ui_rect(30, ry + h, TOP_W - 60, 1, COL_LINE);
    }
    ui_text_wrap(22, y0 + 3 * h + 18, TOP_W - 44, 10, FONT_BOLD, COL_BODY,
                 "Гра з'явиться в меню HOME окремою програмою. Видалити її можна тут, разом з перекладом, або в "
                 "Керуванні даними.");
}

static void draw_bottom(const App *app)
{
    const CatVariant *v = variant(app);
    ui_text(PAD + 2, 8, 13, FONT_HEAVY, COL_INK, "Перед завантаженням");
    CheckRow rows[3];
    memset(rows, 0, sizeof rows);
    if (probing(app) || app->job.kind != JOB_PROBE) {
        rows[0] = (CheckRow){MARK_WARN, "Посилання на гру", "перевіряю…"};
    } else {
        char size[16];
        format_size(size, sizeof size, v->fallback_size);
        rows[0].mark = MARK_OK;
        snprintf(rows[0].label, sizeof rows[0].label, "Посилання на гру");
        snprintf(rows[0].value, sizeof rows[0].value, "працює · %s", size);
    }
    char need[16], have[24];
    format_size(need, sizeof need, (u32)(needed(app) > 0xFFFFFFFFull ? 0xFFFFFFFFull : needed(app)));
    u64 free_bytes = job_sd_free();
    if (free_bytes >= 0xFFFFFFFFull)
        snprintf(have, sizeof have, "%llu ГБ", (unsigned long long)(free_bytes >> 30));
    else
        format_size(have, sizeof have, (u32)free_bytes);
    rows[1].mark = room(app) ? MARK_OK : MARK_BAD;
    snprintf(rows[1].label, sizeof rows[1].label, "Місце на SD");
    snprintf(rows[1].value, sizeof rows[1].value, "%s з %s", need, have);
    snprintf(rows[2].label, sizeof rows[2].label, job_is_emulator() ? "Патчі ігор у Luma" : !luma_ok() ? "Luma3DS" : "Патчі ігор у Luma");
    rows[2].mark = luma_ok() ? MARK_OK : MARK_BAD;
    snprintf(rows[2].value, sizeof rows[2].value, "%s",
             job_is_emulator() ? "емулятор" : !luma_ok() ? "застара" : job_game_patching_on() ? "увімкнено" : "увімкнемо самі");
    draw_check_rows(PAD, 32, BOTTOM_W - 2 * PAD, rows, 3);
    if (probing(app))
        ui_spinner(BOTTOM_W - PAD - 70, 32 + 3 + 9, 5, COL_AMBER);

    draw_button(go_button, ready(app) ? BTN_PRIMARY : BTN_DISABLED, ICON_DOWNLOAD, "Завантажити й встановити", 15);
    static const char *const hints[] = {"A", "Почати", "B", "Назад", NULL};
    if (probing(app))
        ui_text_center(BOTTOM_W / 2, FOOT_Y + 2, 10.5f, FONT_BOLD, COL_MUTED, "Перевіряю посилання на гру…");
    else
        draw_hints(hints);
}

const Screen screen_fallback = {.enter = enter, .update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets};
