/* A3, A4 and SERVER (G4): no catalog to show, and what to do about it. */
#include <stdio.h>

#include "../ui.h"
#include "common.h"

static const Rect retry = {14, 112, BOTTOM_W - 28, 50};
static const Rect installed = {14, 136, BOTTOM_W - 28, 42}; /* A4 */

static bool too_new(const App *app)
{
    return app->hub.state == LOAD_TOO_NEW;
}

/* A4: the installed translations, from the last catalog this app could read */
static bool can_browse(const App *app)
{
    return too_new(app) && app->hub.catalog.n > 0;
}

static int targets(const App *app, Target *out)
{
    if (too_new(app)) {
        if (!can_browse(app))
            return 0;
        out[0] = (Target){installed, 11};
        return 1;
    }
    out[0] = (Target){retry, 13};
    return 1;
}

static void update(App *app, const Input *in)
{
    if (!in->tapped)
        return;
    if (can_browse(app) && rect_hit(installed, in->tap_x, in->tap_y)) {
        app->opts.tab = TAB_INSTALLED;
        app_rebuild_list(app);
        app->sel = app->scroll = 0;
        app_go(app, SCREEN_CATALOG);
    } else if (!too_new(app) && rect_hit(retry, in->tap_x, in->tap_y)) {
        hub_start(&app->hub);
        app_go(app, SCREEN_SPLASH);
    }
}

static void draw_top(const App *app)
{
    const char *title, *text;
    u32 colour = COL_YELLOW;
    switch (app->hub.state) {
    case LOAD_TOO_NEW:
        title = "Оновіть 3DS UA Hub";
        text = "Встановлені переклади працюють як і раніше. Нові встановлення й оновлення — після оновлення застосунку.";
        break;
    case LOAD_BROKEN:
        title = "Сервер не відповідає";
        text = "Це проблема на нашому боці, не на вашій консолі. Спробуйте за кілька хвилин.";
        colour = COL_RED;
        break;
    default:
        title = "Потрібен інтернет";
        text = "Каталог українізаторів ще жодного разу не завантажувався. Підключіть консоль до Wi-Fi — далі "
               "він зберігатиметься на SD-карті й відкриватиметься навіть без мережі.";
        break;
    }
    if (too_new(app)) {
        ui_logo(24, 20, 48);
        ui_text(86, 22, 19, FONT_HEAVY, COL_INK, title);
        ui_text(86, 48, 11, FONT_BOLD, COL_BODY, "Новий каталог потребує новішої версії застосунку");
        char need[32];
        snprintf(need, sizeof need, app->hub.need_app[0] ? "v%s або новіша" : "новіша", app->hub.need_app);
        const char *rows[] = {"У вас", "v" APP_VERSION, "Потрібна", need};
        float h = draw_info_rows(24, 82, TOP_W - 48, 24, rows, 2);
        ui_text_wrap(24, 94 + h, TOP_W - 48, 11, FONT_BOLD, COL_BODY, text);
        return;
    } else {
        ui_circle(48, 50, 24, colour);
        ui_icon(app->hub.state == LOAD_BROKEN ? ICON_WARN : ICON_INFO, 48, 50, 26,
                app->hub.state == LOAD_BROKEN ? COL_WHITE : COL_INK);
    }
    ui_text(86, 38, 19, FONT_HEAVY, COL_INK, title);
    ui_text_wrap(24, 92, TOP_W - 48, 12.5f, FONT_BOLD, COL_BODY, text);
}

static void draw_bottom(const App *app)
{
    if (app->hub.state == LOAD_TOO_NEW) {
        ui_text(14, 16, 14, FONT_HEAVY, COL_INK, "Як оновити");
        ui_card(14, 40, BOTTOM_W - 28, 84, 10, COL_CARD, 2);
        ui_circle(31, 61, 8.5f, COL_BLUE);
        ui_text_center(31, 55, 10, FONT_HEAVY, COL_WHITE, "1");
        ui_text_wrap(46, 52, BOTTOM_W - 74, 10.5f, FONT_BOLD, COL_INK, "Відкрийте Universal-Updater і знайдіть «3DS UA Hub».");
        ui_circle(31, 100, 8.5f, COL_BLUE);
        ui_text_center(31, 94, 10, FONT_HEAVY, COL_WHITE, "2");
        ui_text_wrap(46, 91, BOTTOM_W - 74, 10.5f, FONT_BOLD, COL_INK, "Встановіть нову версію й запустіть застосунок знову.");
        if (can_browse(app)) {
            draw_button(installed, BTN_SECONDARY, ICON_LIST, "Переглянути встановлені", 13);
            static const char *const hints[] = {"A", "Встановлені", "START", "Вийти", NULL};
            draw_hints(hints);
        } else {
            static const char *const hints[] = {"START", "Вийти", NULL};
            draw_hints(hints);
        }
        return;
    }
    if (app->hub.state != LOAD_BROKEN) {
        ui_card(14, 18, BOTTOM_W - 28, 80, 10, COL_CARD, 2);
        ui_circle(31, 38, 8.5f, COL_BLUE);
        ui_text_center(31, 32, 10, FONT_HEAVY, COL_WHITE, "1");
        ui_text_wrap(46, 29, BOTTOM_W - 74, 10.5f, FONT_BOLD, COL_INK, "Увімкніть бездротовий зв'язок перемикачем на корпусі.");
        ui_circle(31, 75, 8.5f, COL_BLUE);
        ui_text_center(31, 69, 10, FONT_HEAVY, COL_WHITE, "2");
        ui_text_wrap(46, 66, BOTTOM_W - 74, 10.5f, FONT_BOLD, COL_INK, "Відкрийте Налаштування системи, далі Інтернет і Налаштування з'єднання.");
    }
    ui_rrect(retry.x, retry.y + 3, retry.w, retry.h, 13, COL_BLUE_DARK);
    ui_rrect(retry.x, retry.y, retry.w, retry.h, 13, COL_BLUE);
    ui_icon(ICON_UPDATE, retry.x + 64, retry.y + retry.h / 2, 20, COL_WHITE);
    ui_text(retry.x + 82, retry.y + 15, 16, FONT_HEAVY, COL_WHITE, "Спробувати знову");
    static const char *const hints[] = {"A", "Знову", "START", "Вийти", NULL};
    draw_hints(hints);
}

const Screen screen_message = {.update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets};
