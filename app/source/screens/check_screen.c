/* A2: the console check at start, shown only when something stops the Hub working: the SD card
 * refuses writes, Luma is missing or too old for game patches, AM is out of reach, the battery
 * is low. Everything else the translation's page checks for its own game. */
#include <stdio.h>
#include <string.h>

#include "../core/fsutil.h"
#include "../paths.h"
#include "../ui.h"
#include "common.h"

#define ROWS 7
#define BATTERY_MIN 2 /* as job.c's preflight */

static const Rect again_button = {14, 150, BOTTOM_W - 28, 44};

static CheckRow rows[ROWS];
static int n_rows;
static const char *action; /* what to do about the first problem, NULL: none */

static CheckRow *add(Mark mark, const char *label)
{
    CheckRow *r = &rows[n_rows++];
    r->mark = mark;
    snprintf(r->label, sizeof r->label, "%s", label);
    r->value[0] = 0;
    return r;
}

static void problem(const char *text)
{
    if (!action)
        action = text; /* the first one is the one to fix first */
}

static bool sd_writable(void)
{
    const char *probe = HUB_DIR "/.probe";
    fs_make_parents(probe);
    FILE *f = fopen(probe, "wb");
    bool ok = f && fwrite("ok", 1, 2, f) == 2;
    if (f && fclose(f) != 0)
        ok = false;
    fs_remove(probe);
    return ok;
}

static void size_text(char *out, size_t size, u64 bytes)
{
    if (bytes >= 1000ull * 1024 * 1024) {
        unsigned long long tenths = (unsigned long long)(bytes / (1024.0 * 1024 * 1024) * 10 + 0.5);
        snprintf(out, size, "%llu,%llu ГБ", tenths / 10, tenths % 10);
    } else {
        snprintf(out, size, "%llu МБ", (unsigned long long)(bytes / (1024 * 1024)));
    }
}

static void run(App *app)
{
    n_rows = 0;
    action = NULL;

    bool sd = sd_writable();
    char free_text[24];
    size_text(free_text, sizeof free_text, job_sd_free());
    if (!sd) {
        problem("SD-карта не дає записувати. Вимкніть консоль, вийміть карту й пересуньте перемикач Lock угору. Не "
                "допомогло — перевірте карту на комп'ютері.");
    }

    u32 luma = job_luma_version();
    if (job_is_emulator()) {
        snprintf(add(MARK_OK, "Luma3DS")->value, sizeof rows[0].value, "емулятор, не потрібна");
    } else if (!luma) {
        snprintf(add(MARK_WARN, "Luma3DS")->value, sizeof rows[0].value, "не знайдено");
        problem("Luma3DS не знайдено, а переклади-патчі працюють лише з нею (v8.0 або новіша). Встановіть її за "
                "3ds.hacks.guide. CIA-ігри ставляться й так.");
    } else if (luma < LUMA_MIN_GAMES) {
        snprintf(add(MARK_WARN, "Luma3DS")->value, sizeof rows[0].value, "v%lu.%lu · застара", GET_VERSION_MAJOR(luma),
                 GET_VERSION_MINOR(luma));
        problem("Ця версія Luma не вміє підвантажувати переклади ігор. Оновіть Luma за інструкцією 3ds.hacks.guide "
                "і запустіть застосунок знову. CIA-ігри ставляться й так.");
    } else {
        snprintf(add(MARK_OK, "Luma3DS")->value, sizeof rows[0].value, "v%lu.%lu.%lu", GET_VERSION_MAJOR(luma),
                 GET_VERSION_MINOR(luma), GET_VERSION_REVISION(luma));
    }
    if (!job_is_emulator() && luma >= LUMA_MIN_GAMES)
        snprintf(add(MARK_OK, "Патчі ігор у Luma")->value, sizeof rows[0].value, "%s",
                 job_game_patching_on() ? "увімкнено" : "увімкнемо самі при встановленні");

    bool online = app->hub.state == LOAD_ONLINE, asking = app->hub.state == LOAD_RUNNING;
    snprintf(add(online || asking ? MARK_OK : MARK_WARN, "Wi-Fi і сервер каталогу")->value, sizeof rows[0].value, "%s",
             online ? "доступні" : asking ? "перевіряю…" : "немає · каталог з SD-карти");

    snprintf(add(sd ? MARK_OK : MARK_BAD, "SD-карта")->value, sizeof rows[0].value, sd ? "запис працює · %s вільно" : "запис не працює",
             free_text);

    u32 count = 0;
    bool am = R_SUCCEEDED(AM_GetTitleCount(MEDIATYPE_SD, &count));
    snprintf(add(am ? MARK_OK : MARK_WARN, "Встановлення CIA (AM)")->value, sizeof rows[0].value, "%s",
             am ? "доступ є" : "немає доступу");
    if (!am)
        problem("Немає доступу до встановлення програм (AM): окремі ігри (CIA) не встановляться. Переклади-патчі "
                "працюють. Перезапустіть консоль і спробуйте знову.");

    const Titles *t = &app->hub.titles;
    int on_sd = 0;
    for (int i = 0; i < t->n; i++)
        on_sd += t->items[i].media == MEDIATYPE_SD;
    if (app->hub.titles_ready)
        snprintf(add(MARK_OK, "Ігри на консолі")->value, sizeof rows[0].value, "%d на SD · %s", on_sd,
                 t->card_present ? "картридж є" : "картриджа нема");
    else
        snprintf(add(MARK_OK, "Ігри на консолі")->value, sizeof rows[0].value, "шукаю…");

    u8 level = 5, charging = 0;
    bool adapter = false;
    PTMU_GetBatteryLevel(&level);
    PTMU_GetAdapterState(&adapter);
    PTMU_GetBatteryChargeState(&charging);
    bool battery = level >= BATTERY_MIN || adapter || charging;
    snprintf(add(battery ? MARK_OK : MARK_WARN, "Заряд")->value, sizeof rows[0].value, "%s",
             battery ? (adapter || charging ? "заряджається" : "достатньо") : "мало");
    if (!battery)
        problem("Мало заряду: консоль може вимкнутися посеред встановлення. Підключіть зарядку.");
}

bool console_check_needed(App *app)
{
    run(app);
    return action != NULL;
}

static void enter(App *app)
{
    (void)app;
}

static int targets(const App *app, Target *out)
{
    (void)app;
    out[0] = (Target){again_button, 13};
    return 1;
}

static void update(App *app, const Input *in)
{
    if (in->down & KEY_B) {
        app_open_catalog(app);
        return;
    }
    if (in->tapped && rect_hit(again_button, in->tap_x, in->tap_y)) {
        run(app);
        if (!action)
            app_open_catalog(app); /* fixed: on to the catalog */
    }
}

static void draw_top(const App *app)
{
    (void)app;
    ui_text(20, 12, 19, FONT_HEAVY, COL_INK, "Перевірка консолі");
    draw_check_rows(20, 44, TOP_W - 40, rows, n_rows);
}

static void draw_bottom(const App *app)
{
    (void)app;
    ui_text(14, 12, 14, FONT_HEAVY, COL_INK, "Потрібна дія");
    ui_text_wrap(14, 36, BOTTOM_W - 28, 10.5f, FONT_BOLD, COL_BODY, action ? action : "Усе гаразд.");
    draw_button(again_button, BTN_PRIMARY, ICON_UPDATE, "Перевірити знову", 15);
    static const char *const hints[] = {"A", "Перевірити знову", "B", "До каталогу все одно", NULL};
    draw_hints(hints);
}

const Screen screen_check = {
    .enter = enter, .update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets,
};
