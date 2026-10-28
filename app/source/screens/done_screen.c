/* D3, E2, F3: a job went through. Installed: start the game (or restart the console when Luma's
 * game patching had to be turned on); removed: back to the catalog. */
#include <stdio.h>
#include <string.h>

#include "../ui.h"
#include "common.h"

static const Rect first_button = {14, 92, BOTTOM_W - 28, 52};
static const Rect second_button = {14, 154, BOTTOM_W - 28, 36};
static const Rect only_button = {14, 120, BOTTOM_W - 28, 50};

static bool removed(const App *app)
{
    return app->job.kind == JOB_REMOVE;
}

static int targets(const App *app, Target *out)
{
    if (removed(app)) {
        out[0] = (Target){only_button, 13};
        return 1;
    }
    out[0] = (Target){first_button, 13};
    out[1] = (Target){second_button, 11};
    return 2;
}

static void first(App *app)
{
    const Job *job = &app->job;
    if (job->needs_reboot && !job->luma_by_hand)
        app_reboot(app);
    else if (job->item_kind == KIND_CIA)
        app_launch(app, job->variant.title_id, MEDIATYPE_SD);
    else if (!job->needs_reboot)
        app_launch(app, job->target_tid, job->media);
}

static void update(App *app, const Input *in)
{
    if (in->down & KEY_B) {
        app_go(app, SCREEN_CATALOG);
        return;
    }
    if (!in->tapped)
        return;
    if (removed(app)) {
        if (rect_hit(only_button, in->tap_x, in->tap_y))
            app_go(app, SCREEN_CATALOG);
        return;
    }
    if (rect_hit(first_button, in->tap_x, in->tap_y))
        first(app);
    else if (rect_hit(second_button, in->tap_x, in->tap_y))
        app_go(app, SCREEN_CATALOG);
}

static void duration(char *out, size_t size, const Job *job)
{
    u64 s = (job->finished - job->started + 500) / 1000;
    if (s < 60)
        snprintf(out, size, "%llu с", s);
    else
        snprintf(out, size, "%llu хв %02llu с", s / 60, s % 60);
}

static void draw_top(const App *app)
{
    const Job *job = &app->job;
    bool cia = job->item_kind == KIND_CIA;
    ui_circle(48, 48, 24, COL_GREEN);
    ui_icon(ICON_CHECK, 48, 48, 26, COL_WHITE);

    char title[96], sub[160], version[48], tid[20], path[64], took[32], freed[16];
    format_tid(tid, sizeof tid, job->target_tid);
    duration(took, sizeof took, job);
    if (removed(app)) {
        snprintf(title, sizeof title, "%s", cia ? "Програму видалено" : "Переклад видалено");
        snprintf(sub, sizeof sub, cia ? "%s прибрано з меню HOME" : "%s знову як була", app->job_title);
        format_size(freed, sizeof freed, app->freed);
        const char *patch_rows[] = {"Звільнено", freed, "Відкладений мод", app->had_backup ? "повернуто на місце" : "не було",
                                    "Збереження гри", "на місці"};
        /* a port keeps its saves in its folder on the card, and they stay there */
        const char *cia_rows[] = {"Звільнено", freed, "Збереження",
                                  job->removing.dir[0] ? "лишились у теці гри" : "видалено разом з програмою"};
        draw_info_rows(24, 96, TOP_W - 48, 24, cia ? cia_rows : patch_rows, cia ? 2 : 3);
    } else {
        snprintf(title, sizeof title, "%s", cia ? "Програму встановлено!" : "Готово!");
        snprintf(sub, sizeof sub, cia ? "%s уже в меню HOME" : "%s тепер українською", app->job_title);
        if (app->job_update && app->job_was[0])
            snprintf(version, sizeof version, "v%s · було v%s", job->version, app->job_was);
        else
            snprintf(version, sizeof version, "v%s", job->version);
        snprintf(path, sizeof path, "SD:/luma/titles/%s", tid);
        const char *patch_rows[] = {"Версія перекладу", version, "Куди встановлено", path, "Тривалість", took,
                                    "Відкладений мод", "SD:/3ds/3ds-ua-hub/backup/…"};
        char cia_tid[20];
        format_tid(cia_tid, sizeof cia_tid, job->variant.title_id);
        char data_path[112];
        snprintf(data_path, sizeof data_path, "SD:/%s", job->variant.data_dir);
        const char *cia_rows[] = {"Версія перекладу", version, "Title ID", cia_tid, "Тривалість", took,
                                  "Файли гри", data_path};
        draw_info_rows(24, 96, TOP_W - 48, 24, cia ? cia_rows : patch_rows,
                       cia ? (job->variant.data.present ? 4 : 3) : app->had_backup ? 4 : 3);
    }
    ui_text(86, 30, 20, FONT_HEAVY, COL_INK, title);
    draw_text_fit(86, 58, TOP_W - 106, 11.5f, FONT_BOLD, COL_MUTED, sub);
}

static void draw_bottom(const App *app)
{
    const Job *job = &app->job;
    if (removed(app)) {
        ui_text_wrap(14, 20, BOTTOM_W - 28, 11, FONT_BOLD, COL_BODY,
                     job->item_kind == KIND_CIA
                         ? "Повернути програму можна будь-коли: відкрийте її в каталозі й натисніть «Встановити програму»."
                         : "Повернути переклад можна будь-коли: відкрийте гру в каталозі й натисніть «Встановити».");
        draw_button(only_button, BTN_PRIMARY, ICON_LIST, "До каталогу", 15);
        static const char *const hints[] = {"A", "До каталогу", NULL};
        draw_hints(hints);
        return;
    }
    if (job->needs_reboot) {
        ui_card(14, 12, BOTTOM_W - 28, 68, 10, COL_AMBER_BG, 0);
        ui_text_wrap(24, 18, BOTTOM_W - 48, 10.5f, FONT_BOLD, COL_AMBER_TEXT,
                     job->luma_by_hand
                         ? "Патчі ігор у Luma вимкнені, а файл налаштувань змінити не вдалося. Увімкніть «Enable game "
                           "patching» у меню Luma (тримайте SELECT, вмикаючи консоль)."
                         : "Патчі ігор у Luma були вимкнені — ми їх увімкнули. Переклад запрацює після перезапуску консолі.");
        if (job->luma_by_hand)
            draw_button2(first_button, BTN_DISABLED, ICON_POWER, "Перезавантажити", "спершу увімкніть патчі в Luma");
        else
            draw_button2(first_button, BTN_PRIMARY, ICON_POWER, "Перезавантажити", "консоль увімкнеться знову");
        draw_button(second_button, BTN_SECONDARY, ICON_LIST, "Пізніше", 13);
        static const char *const hints[] = {"A", "Вибрати", "B", "До каталогу", NULL};
        draw_hints(hints);
        return;
    }
    if (job->item_kind == KIND_CIA)
        draw_button2(first_button, BTN_PRIMARY, ICON_PLAY, "Запустити програму", "Hub закриється, програма відкриється");
    else
        draw_button2(first_button, BTN_PRIMARY, ICON_PLAY, "Запустити гру", "застосунок закриється, гра відкриється");
    draw_button(second_button, BTN_SECONDARY, ICON_LIST, "До каталогу", 13);
    static const char *const hints[] = {"A", "Запустити", "B", "До каталогу", NULL};
    draw_hints(hints);
}

const Screen screen_done = {.update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets};
