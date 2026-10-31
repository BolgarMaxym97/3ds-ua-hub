/* D4: none of the translation's games is on the console. Look again (a cartridge put in), get the
 * game from the catalog when it has one (J4), put the patch on with no game (J1), or pick the game
 * by hand (D5-D8). */
#include <stdio.h>
#include <string.h>

#include "../ui.h"
#include "common.h"

typedef enum { OPT_AGAIN, OPT_FALLBACK, OPT_NOGAME, OPT_PICK } Option;

#define OPT_H 44
#define OPT_GAP 6

/* The options this translation has, top to bottom: "Завантажити гру" only with a game to give. */
static int options(const App *app, Option *out)
{
    int n = 0;
    out[n++] = OPT_AGAIN;
    if (app->checks.fallback >= 0)
        out[n++] = OPT_FALLBACK;
    out[n++] = OPT_NOGAME;
    out[n++] = OPT_PICK;
    return n;
}

static Rect option_rect(int i)
{
    return (Rect){14, 8 + i * (OPT_H + OPT_GAP), BOTTOM_W - 28, OPT_H};
}

static int targets(const App *app, Target *out)
{
    Option opts[4];
    int n = options(app, opts);
    for (int i = 0; i < n; i++)
        out[i] = (Target){option_rect(i), 13};
    return n;
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

static void choose(App *app, Option o)
{
    switch (o) {
    case OPT_AGAIN: scan_frames = 1; break;
    case OPT_FALLBACK: app_go(app, SCREEN_FALLBACK); break;
    case OPT_NOGAME: app_go(app, SCREEN_NOGAME); break;
    case OPT_PICK: pick(app); break;
    }
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
    Option opts[4];
    int n = options(app, opts);
    for (int i = 0; i < n; i++)
        if (rect_hit(option_rect(i), in->tap_x, in->tap_y))
            choose(app, opts[i]);
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
                 app->checks.fallback >= 0
                     ? "Можна вставити картридж і шукати знову, завантажити гру з каталогу, поставити переклад наперед "
                       "(гру додасте пізніше) або вибрати гру вручну."
                     : "Можна вставити картридж і шукати знову, поставити переклад наперед (гру додасте пізніше) або "
                       "вибрати гру вручну, якщо її встановлено з іншим Title ID.");
}

static void draw_bottom(const App *app)
{
    Option opts[4];
    int n = options(app, opts);
    for (int i = 0; i < n; i++) {
        Rect r = option_rect(i);
        ButtonStyle style = i == 0 && !scan_frames ? BTN_PRIMARY : BTN_SECONDARY;
        switch (opts[i]) {
        case OPT_AGAIN:
            if (scan_frames)
                draw_button2_left(r, BTN_DISABLED, ICON_SEARCH, "Шукаю…", "дивлюсь ігри на SD і картриджі");
            else
                draw_button2_left(r, style, ICON_UPDATE, "Шукати знову",
                             searched ? "і цього разу не знайдено" : "вставте картридж і натисніть");
            break;
        case OPT_FALLBACK: {
            const CatItem *it = &app->hub.catalog.items[app->page];
            char sub[96], size[16];
            format_size(size, sizeof size, it->variants[app->checks.fallback].fallback_size);
            snprintf(sub, sizeof sub, "з каталогу, потім переклад · %s", size);
            draw_button2_left(r, style, ICON_DOWNLOAD, "Завантажити гру", sub);
            break;
        }
        case OPT_NOGAME:
            draw_button2_left(r, style, ICON_SHIELD, "Встановити без гри", "переклад на SD, гру додасте пізніше");
            break;
        case OPT_PICK:
            draw_button2_left(r, style, ICON_LIST, "Вибрати гру вручну", "зі списку встановлених");
            break;
        }
    }
    static const char *const hints[] = {"A", "Вибрати", "X", "Вручну", "B", "Назад", NULL};
    draw_hints(hints);
}

const Screen screen_notfound = {.enter = enter, .update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets};
