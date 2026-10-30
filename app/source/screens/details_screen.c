/* C1-C6: one translation. What it covers on top; below, what the console has for it and the
 * action: install (with the checks before it), update, run, remove. */
#include <stdio.h>
#include <string.h>

#include "../shots.h"
#include "../titles.h"
#include "../ui.h"
#include "common.h"

static const Rect main_button = {14, 136, BOTTOM_W - 28, 50};
static const Rect run_button = {14, 116, (BOTTOM_W - 28 - 10) / 2, 50};
static const Rect remove_button = {14 + (BOTTOM_W - 28 - 10) / 2 + 10, 116, (BOTTOM_W - 28 - 10) / 2, 50};

static const CatItem *item(const App *app)
{
    return &app->hub.catalog.items[app->page];
}

static bool installed(const App *app)
{
    return app->checks.state == ITEM_INSTALLED && app->checks.rec;
}

/* The installed game to start: a patch's own game, or the CIA itself. */
static bool can_run(const App *app)
{
    if (!app->checks.rec)
        return false;
    return item(app)->kind == KIND_CIA || app->checks.title >= 0;
}

static void enter(App *app)
{
    app_check(app);
    shots_want(item(app));
}

static int targets(const App *app, Target *out)
{
    if (installed(app)) {
        out[0] = (Target){run_button, 13};
        out[1] = (Target){remove_button, 13};
        return 2;
    }
    out[0] = (Target){main_button, 13};
    return 1;
}

static void run(App *app)
{
    const Checks *c = &app->checks;
    if (item(app)->kind == KIND_CIA)
        app_launch(app, c->rec->title_id, MEDIATYPE_SD);
    else
        app_launch(app, app->hub.titles.items[c->title].title_id, app->hub.titles.items[c->title].media);
}

/* Games of several regions are here: which one the patch goes to is asked, or can be changed. */
static bool can_choose(const App *app)
{
    const Checks *c = &app->checks;
    return item(app)->kind == KIND_PATCH && !c->scanning && c->state == ITEM_NEW && c->m.n_candidates > 1;
}

static bool must_choose(const App *app)
{
    return can_choose(app) && app->checks.match == MATCH_SEVERAL;
}

static void press_main(App *app)
{
    const Checks *c = &app->checks;
    if (c->scanning && item(app)->kind == KIND_PATCH)
        return;
    if (must_choose(app)) {
        app_go(app, SCREEN_WHICH);
    } else if (c->blocked == ERR_GAME_NOT_FOUND) {
        app_go(app, SCREEN_NOTFOUND);
    } else if (c->blocked != ERR_NONE || c->variant < 0) {
        app_check(app); /* "Перевірити знову": room may have been made, the cartridge put in */
    } else if (item(app)->kind == KIND_CIA) {
        app_install(app, c->variant, -1, false, false);
    } else {
        bool manual = c->rec && c->rec->manual, risky = c->rec && c->rec->risky;
        app_install(app, c->variant, c->title, manual, risky);
    }
}

static void update(App *app, const Input *in)
{
    if (in->down & KEY_B) {
        app_go(app, SCREEN_CATALOG);
        return;
    }
    if (in->down & KEY_X && app->checks.rec) {
        app_go(app, SCREEN_REMOVE);
        return;
    }
    if (in->down & KEY_R && item(app)->n_shots) {
        app->shot = 0;
        app_go(app, SCREEN_GALLERY);
        return;
    }
    if (in->down & KEY_Y && can_choose(app)) {
        app_go(app, SCREEN_WHICH);
        return;
    }
    if (!in->tapped)
        return;
    if (installed(app)) {
        if (rect_hit(run_button, in->tap_x, in->tap_y) && can_run(app))
            run(app);
        else if (rect_hit(remove_button, in->tap_x, in->tap_y))
            app_go(app, SCREEN_REMOVE);
    } else if (rect_hit(main_button, in->tap_x, in->tap_y)) {
        press_main(app);
    }
}

/* ---------------------------------------------------------------- top */

/* A paragraph under the card, in the lines left above the screen's edge (cut with "…"). */
static float paragraph(float y, const char *text)
{
    int lines = (int)((SCREEN_H - 4 - y) / (10.5f * 1.32f));
    return lines > 0 ? ui_text_wrap_lines(20, y, TOP_W - 40, 10.5f, FONT_BOLD, COL_BODY, text, lines) : 0;
}

static void draw_top(const App *app)
{
    const CatItem *it = item(app);
    const Checks *c = &app->checks;
    draw_item_icon(it, 20, 13, 32);
    draw_text_fit(62, 12, TOP_W - 82, 16, FONT_HEAVY, COL_INK, it->title);
    char line[200], size[16], version[80];
    format_size(size, sizeof size, (u32)variant_bytes(&it->variants[c->variant >= 0 ? c->variant : 0]));
    if (c->state == ITEM_UPDATE && c->rec)
        snprintf(version, sizeof version, "переклад v%s, є новіший v%s", c->rec->version, it->version);
    else
        snprintf(version, sizeof version, "переклад v%s", it->version);
    snprintf(line, sizeof line, "%s · %s%s%s · %s", it->kind == KIND_PATCH ? "Патч" : "Програма", version,
             it->authors[0] ? " · " : "", it->authors, size);
    draw_text_fit(62, 32, TOP_W - 82, 10.5f, FONT_BOLD, COL_MUTED, line);

    ui_card(20, 58, TOP_W - 40, 56, 10, COL_CARD, 1.5f);
    draw_progress_row(33, 66, TOP_W - 66, "Текст", it->text_pct);
    draw_progress_row(33, 90, TOP_W - 66, "Текстури", it->textures_pct);

    float y = 124;
    if (c->blocked != ERR_NONE && c->blocked != ERR_GAME_NOT_FOUND && !installed(app)) {
        /* C5: why Install is off, and what to do */
        const ErrorInfo *e = error_info(c->blocked);
        char text[320], what[256];
        snprintf(what, sizeof what, e->text, c->blocked_detail);
        snprintf(text, sizeof text, "%s %s", what, e->steps ? e->steps : "");
        y += ui_text_wrap(20, y, TOP_W - 40, 11.5f, FONT_HEAVY, COL_RED, e->title) + 2;
        ui_text_wrap(20, y, TOP_W - 40, 10.5f, FONT_BOLD, COL_BODY, text);
        return;
    }
    if (must_choose(app)) {
        y += ui_text_wrap(20, y, TOP_W - 40, 11.5f, FONT_HEAVY, COL_AMBER_TEXT, "Для якої гри?") + 2;
        y += ui_text_wrap(20, y, TOP_W - 40, 10.5f, FONT_BOLD, COL_BODY,
                          "На консолі кілька версій цієї гри різних регіонів, і жодна не регіону консолі. Переклад для "
                          "кожної свій: виберіть, яку перекладати.") + 4;
    }
    if (it->third_party && (it->text_pct < 0 || it->textures_pct < 0))
        y += ui_text_wrap(20, y, TOP_W - 40, 10.5f, FONT_BOLD, COL_MUTED, "Сторонній переклад: прогрес автори не вказали.") + 2;
    if (it->kind == KIND_CIA && !c->rec)
        y += ui_text_wrap(20, y, TOP_W - 40, 10.5f, FONT_BOLD, COL_BODY,
                          "Ставиться як нова програма в меню HOME.") + 2;
    if (it->changelog && c->state == ITEM_UPDATE) {
        char what[256];
        snprintf(what, sizeof what, "Що нового у v%s: %s", it->version, it->changelog);
        y += paragraph(y, what) + 4;
    }
    if (it->description && y < 200)
        y += paragraph(y, it->description) + 4;
    if (it->changelog && c->state != ITEM_UPDATE && y < 200) {
        char what[256];
        snprintf(what, sizeof what, "Що нового у v%s: %s", it->version, it->changelog);
        paragraph(y, what);
    }
}

/* ---------------------------------------------------------------- bottom */

/* The hints, with "R Скріншоти" before "B Назад" when the entry has screenshots; B's own hint
 * goes when four do not fit (B still works). */
static void draw_page_hints(const App *app, const char *const *hints)
{
    const char *all[12];
    int n = 0;
    for (int i = 0; hints[i] && n < 8; i += 2) {
        if (item(app)->n_shots && !strcmp(hints[i], "B")) {
            all[n++] = "R";
            all[n++] = "Скріншоти";
        }
        all[n++] = hints[i];
        all[n++] = hints[i + 1];
    }
    all[n] = NULL;
    if (hints_width(all, HINT_GAP_MIN) > BOTTOM_W - 2 * PAD)
        for (int i = 0; all[i]; i += 2)
            if (!strcmp(all[i], "B")) {
                memmove(&all[i], &all[i + 2], (size_t)(n - i - 1) * sizeof *all);
                break;
            }
    draw_hints(all);
}

static void draw_installed(const App *app)
{
    const CatItem *it = item(app);
    const Record *rec = app->checks.rec;
    ui_circle(21, 19, 3.5f, COL_GREEN);
    ui_text(30, 11, 13, FONT_HEAVY, COL_GREEN_TEXT, "Встановлено · найновіша версія");

    char game[48], when[48], tid[20];
    format_tid(tid, sizeof tid, rec->title_id);
    snprintf(game, sizeof game, "%s · %s", rec->region, tid);
    snprintf(when, sizeof when, "%s · v%s", rec->date, rec->version);
    const char *how = rec->risky ? "вручну, на свій ризик" : rec->manual ? "вибрано вручну" : "знайдено автоматично";
    const char *patch_rows[] = {"Для гри", game, "Встановлено", when, "Спосіб", how};
    const char *cia_rows[] = {"Title ID", tid, "Встановлено", when, "Збереження", "окремі, не з оригіналу"};
    draw_info_rows(14, 34, BOTTOM_W - 28, 22, it->kind == KIND_PATCH ? patch_rows : cia_rows, 3);

    draw_button(run_button, can_run(app) ? BTN_PRIMARY : BTN_DISABLED, ICON_PLAY, "Запустити", 14);
    draw_button(remove_button, BTN_DANGER_SOFT, ICON_TRASH, "Видалити", 14);
    if (!can_run(app))
        ui_text_center(BOTTOM_W / 2, 176, 10, FONT_BOLD, COL_MUTED, "Гри немає на консолі: вставте картридж");
    static const char *const hints[] = {"A", "Вибрати", "X", "Видалити", "B", "Назад", NULL};
    draw_page_hints(app, hints);
}

static void draw_bottom(const App *app)
{
    const CatItem *it = item(app);
    const Checks *c = &app->checks;
    if (installed(app)) {
        draw_installed(app);
        return;
    }
    ui_text(14, 9, 13, FONT_HEAVY, COL_INK, "Перед встановленням");
    if (c->scanning && it->kind == KIND_PATCH) {
        ui_card(14, 32, BOTTOM_W - 28, 40, 10, COL_CARD, 1.5f);
        ui_text_center(BOTTOM_W / 2, 45, 10.5f, FONT_BOLD, COL_MUTED, "Шукаю гру на консолі…");
    } else {
        draw_check_rows(14, 32, BOTTOM_W - 28, c->rows, c->n_rows);
    }

    bool update = c->state == ITEM_UPDATE;
    char label[48];
    if (update)
        snprintf(label, sizeof label, "Оновити до v%s", it->version);
    else
        snprintf(label, sizeof label, "%s", it->kind == KIND_PATCH ? "Встановити" : "Встановити програму");
    const char *a_hint = update ? "Оновити" : "Встановити";
    if (must_choose(app)) {
        draw_button2(main_button, BTN_PRIMARY, ICON_LIST, "Вибрати гру", "на консолі кілька регіонів гри");
        a_hint = "Вибрати гру";
    } else if (c->blocked == ERR_GAME_NOT_FOUND) {
        draw_button(main_button, BTN_PRIMARY, ICON_SEARCH, "Знайти гру", 16);
        a_hint = "Знайти гру";
    } else if (c->blocked != ERR_NONE || (c->scanning && it->kind == KIND_PATCH)) {
        if (c->scanning && it->kind == KIND_PATCH)
            draw_button(main_button, BTN_DISABLED, ICON_LOCK, label, 16);
        else
            draw_button2(main_button, BTN_DISABLED, ICON_LOCK, label,
                         c->blocked == ERR_APP_OUTDATED ? "спершу оновіть 3DS UA Hub"
                                                        : "виправте й натисніть, щоб перевірити знову");
        a_hint = "Перевірити знову";
    } else {
        draw_button(main_button, BTN_PRIMARY, update ? ICON_UPDATE : ICON_DOWNLOAD, label, 16);
    }
    const char *hints[] = {"A", a_hint, "X", "Видалити", "B", "Назад", NULL};
    if (!c->rec) {
        hints[2] = "B";
        hints[3] = "Назад";
        hints[4] = NULL;
    }
    if (can_choose(app) && !must_choose(app)) { /* the picked (or the console's region) game: another one */
        hints[2] = "Y";
        hints[3] = "Інша гра";
        hints[4] = "B";
        hints[5] = "Назад";
    }
    draw_page_hints(app, c->scanning && it->kind == KIND_PATCH ? hints + 2 : hints);
}

const Screen screen_details = {
    .enter = enter, .update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets,
};
