/* D4: none of the translation's games is on the console. Look again (a cartridge put in), or
 * pick the game by hand (D5-D8). */
#include <stdio.h>
#include <string.h>

#include "../ui.h"
#include "common.h"

static const Rect again_button = {14, 34, BOTTOM_W - 28, 56};
static const Rect pick_button = {14, 104, BOTTOM_W - 28, 56};

static int targets(const App *app, Target *out)
{
    (void)app;
    out[0] = (Target){again_button, 13};
    out[1] = (Target){pick_button, 13};
    return 2;
}

/* Scanning takes a second or two with a full card: the button says so for a frame first. */
static int scan_frames;
static bool searched; /* "Шукати знову" ran and found nothing either */

static void enter(App *app)
{
    scan_frames = 0;
    searched = false;
    app_report_not_found(app);
}

static void again(App *app)
{
    app_rescan(app);
    searched = true;
    if (app->checks.blocked != ERR_GAME_NOT_FOUND)
        app_go(app, SCREEN_DETAILS); /* found now: the page shows the checks */
}

static void pick(App *app)
{
    app->query[0] = 0;
    app->checked = -1;
    app_go(app, SCREEN_PICK);
}

static void update(App *app, const Input *in)
{
    if (scan_frames && ++scan_frames > 2) { /* "Шукаю…" is on the screen by now */
        scan_frames = 0;
        again(app);
        return;
    }
    if (scan_frames)
        return;
    if (in->down & KEY_B) {
        app_go(app, SCREEN_DETAILS);
        return;
    }
    if (in->down & KEY_X) {
        pick(app);
        return;
    }
    if (!in->tapped)
        return;
    if (rect_hit(again_button, in->tap_x, in->tap_y))
        scan_frames = 1;
    else if (rect_hit(pick_button, in->tap_x, in->tap_y))
        pick(app);
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
    const CatItem *it = &app->hub.catalog.items[app->page];
    ui_circle(44, 40, 20, COL_YELLOW);
    ui_icon(ICON_SEARCH, 44, 40, 22, COL_INK);
    ui_text(76, 22, 17, FONT_HEAVY, COL_INK, "Гру не знайдено на консолі");
    char sub[160];
    snprintf(sub, sizeof sub, "%s · українізатор v%s", it->title, it->version);
    draw_text_fit(76, 46, TOP_W - 96, 10.5f, FONT_BOLD, COL_MUTED, sub);
    ui_text(24, 74, 10.5f, FONT_BOLD, COL_BODY, "Шукали за Title ID у встановлених іграх і в картриджі:");

    int n = it->n_variants < 3 ? it->n_variants : 3;
    char names[3][48], tids[3][20];
    const char *pairs[6];
    for (int i = 0; i < n; i++) {
        snprintf(names[i], sizeof names[i], "%s (%s)", region_name(it->variants[i].region), it->variants[i].region);
        format_tid(tids[i], sizeof tids[i], it->variants[i].title_id);
        pairs[2 * i] = names[i];
        pairs[2 * i + 1] = tids[i];
    }
    float h = draw_info_rows(24, 92, TOP_W - 48, 22, pairs, n);
    ui_text_wrap(24, 100 + h, TOP_W - 48, 10, FONT_BOLD, COL_BODY,
                 "Чому так буває: картридж не вставлено; у вас версія іншого регіону; гру встановлено з іншим "
                 "Title ID (наприклад, як модифіковану копію).");
}

static void draw_bottom(const App *app)
{
    (void)app;
    ui_text(14, 10, 13, FONT_HEAVY, COL_INK, "Що зробити");
    if (scan_frames)
        draw_button2(again_button, BTN_DISABLED, ICON_SEARCH, "Шукаю…", "дивлюсь ігри на SD і картриджі");
    else
        draw_button2(again_button, BTN_PRIMARY, ICON_UPDATE, "Шукати знову",
                     searched ? "і цього разу не знайдено" : "вставте картридж і натисніть");
    draw_button2(pick_button, BTN_SECONDARY, ICON_LIST, "Вибрати гру вручну", "зі списку встановлених");
    static const char *const hints[] = {"A", "Вибрати", "X", "Вручну", "B", "Назад", NULL};
    draw_hints(hints);
}

const Screen screen_notfound = {.enter = enter, .update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets};
