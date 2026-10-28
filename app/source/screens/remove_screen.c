/* F1, F2: removing a translation (or a CIA game) - X or the button held for two seconds, so
 * it never happens by a slip. */
#include <stdio.h>
#include <string.h>

#include "../core/fsutil.h"
#include "../ui.h"
#include "common.h"

#define HOLD_MS 2000

static const Rect remove_button = {14, 40, BOTTOM_W - 28, 56};
static const Rect keep_button = {14, 108, BOTTOM_W - 28, 36};

static bool cia(const App *app)
{
    return app->hub.catalog.items[app->page].kind == KIND_CIA;
}

static void enter(App *app)
{
    app->hold_from = 0;
}

static int targets(const App *app, Target *out)
{
    (void)app;
    out[0] = (Target){remove_button, 13};
    out[1] = (Target){keep_button, 11};
    return 2;
}

static void update(App *app, const Input *in)
{
    if (in->down & KEY_B || (in->tapped && rect_hit(keep_button, in->tap_x, in->tap_y))) {
        app_go(app, SCREEN_DETAILS);
        return;
    }
    bool holding = in->held & KEY_X || (in->held & KEY_A && app->focus == 0) ||
                   (in->touching && rect_hit(remove_button, in->touch_x, in->touch_y));
    if (!holding) {
        app->hold_from = 0;
        return;
    }
    if (!app->hold_from)
        app->hold_from = osGetTime();
    if (osGetTime() - app->hold_from >= HOLD_MS) {
        app->hold_from = 0;
        app_remove(app);
    }
}

static void draw_top(const App *app)
{
    const CatItem *it = &app->hub.catalog.items[app->page];
    const Record *rec = app->checks.rec;
    draw_item_icon(it, 24, 20, 44);
    ui_text(80, 20, 19, FONT_HEAVY, COL_INK, cia(app) ? "Видалити програму?" : "Видалити українізатор?");
    char sub[160], size[16];
    u32 bytes = 0;
    for (int i = 0; rec && i < rec->n_files; i++)
        bytes += rec->files[i].size;
    format_size(size, sizeof size, cia(app) ? it->variants[0].size + bytes : bytes); /* a CIA's own, and its data */
    snprintf(sub, sizeof sub, "%s · v%s · %s", it->title, rec ? rec->version : it->version, size);
    draw_text_fit(80, 46, TOP_W - 100, 11, FONT_BOLD, COL_MUTED, sub);

    if (cia(app) && rec && rec->dir[0]) {
        /* a port that keeps its game, and its saves, in a folder on the card */
        const char *rows[] = {"Гра в меню HOME", "видалиться", "Файли гри на SD-карті", "видаляться",
                              "Збереження гри", "лишаються"};
        float h = draw_info_rows(24, 80, TOP_W - 48, 24, rows, 3);
        char text[320];
        snprintf(text, sizeof text, "Збереження лежать у SD:/%s/ поруч з файлами гри — Hub їх не чіпає.", rec->dir);
        ui_text_wrap(24, 90 + h, TOP_W - 48, 10.5f, FONT_BOLD, COL_BODY, text);
        return;
    }
    if (cia(app)) {
        const char *rows[] = {"Програма в меню HOME", "видалиться", "Збереження цієї програми", "!видаляться назавжди",
                              "Інші програми й збереження", "не зачіпаються"};
        float h = draw_info_rows(24, 80, TOP_W - 48, 24, rows, 3);
        ui_text_wrap(24, 90 + h, TOP_W - 48, 10.5f, FONT_BOLD, COL_BODY,
                     "Хочете зберегти прогрес? Спершу зробіть копію збережень через Checkpoint або JKSM.");
        return;
    }
    char backup[64];
    snprintf(backup, sizeof backup, "sdmc:" HUB_SUBDIR "/backup/%016llX", rec ? (unsigned long long)rec->title_id : 0ull);
    const char *rows[] = {"Файли перекладу", "видаляться", "Мод, який ми відклали",
                          rec && fs_is_dir(backup) ? "повернеться" : "не було",
                          "Гра й збереження", "не зачіпаються", "Налаштування Luma", "не змінюються"};
    draw_info_rows(24, 80, TOP_W - 48, 24, rows, 4);
}

static void draw_bottom(const App *app)
{
    ui_text(14, 12, 13, FONT_HEAVY, COL_INK, "Підтвердіть видалення");
    float held = app->hold_from ? (float)(osGetTime() - app->hold_from) / HOLD_MS : 0;
    if (held > 1)
        held = 1;
    const char *label = cia(app) ? "Видалити програму" : "Видалити";
    draw_button2(remove_button, BTN_DANGER, ICON_TRASH, label, "утримуйте 2 с, щоб підтвердити");
    if (held > 0) /* the hold fills the button from the left */
        ui_rrect(remove_button.x, remove_button.y + remove_button.h - 6, remove_button.w * held, 6, 3, COL_YELLOW);
    draw_button(keep_button, BTN_SECONDARY, ICON_CHECK, "Залишити", 13);
    if (!cia(app))
        ui_text_center(BOTTOM_W / 2, 158, 10.5f, FONT_BOLD, COL_MUTED, "Повернути переклад можна будь-коли з каталогу.");
    static const char *const hints[] = {"X", "Утримуйте — видалити", "B", "Залишити", NULL};
    draw_hints(hints);
}

const Screen screen_remove = {
    .enter = enter, .update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets,
};
