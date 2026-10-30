/* G1-G3: something went wrong - what, what to do, and the code for a report; Y turns the top
 * screen to the technical details for a bug report. Also the notice that a power cut stopped the
 * last job (INTERRUPTED), shown once at start. */
#include <stdio.h>
#include <string.h>

#include "../ui.h"
#include "common.h"

static const Rect retry_button = {14, 136, BOTTOM_W - 28, 48};
/* G2: a download broke off - go on, or give it up */
static const Rect continue_button = {14, 40, BOTTOM_W - 28, 64};
static const Rect drop_button = {14, 116, BOTTOM_W - 28, 40};

static bool lost(const App *app)
{
    return app->error == ERR_NET_LOST && app->error_of_job;
}

/* A patch's download goes on from where it broke, and so does a CIA's data; a CIA itself was
 * written into the system as it came and AM threw the half away, so it starts over. */
static bool resumable(const App *app)
{
    return lost(app) && (app->job.item_kind == KIND_PATCH || app->job.failed_at == JS_DATA);
}

static bool can_retry(const App *app)
{
    return error_info(app->error)->retry && app->error != ERR_GAME_NOT_FOUND && app->error != ERR_INTERRUPTED &&
           app->job.kind != JOB_REMOVE;
}

static int targets(const App *app, Target *out)
{
    if (lost(app)) {
        out[0] = (Target){continue_button, 13};
        out[1] = (Target){drop_button, 11};
        return 2;
    }
    out[0] = (Target){retry_button, 13};
    return 1;
}

/* Y: the top screen shows the technical details instead of the explanation */
static bool more;

static void enter(App *app)
{
    (void)app;
    more = false;
}

static bool has_details(const App *app)
{
    return app->error_of_job;
}

/* The last step: press the button - after the steps above, if there are any. */
static const char *retry_step(const App *app)
{
    if (app->error == ERR_NET_LOST)
        return "Щойно Wi-Fi повернеться, натисніть «Продовжити».";
    if (app->error == ERR_FILE_CORRUPT)
        return "Натисніть «Спробувати знову» — завантажимо файл заново.";
    return error_info(app->error)->steps ? "Тоді поверніться сюди й натисніть «Спробувати знову»."
                                         : "Натисніть «Спробувати знову».";
}

static void back(App *app);

/* G2's "Скасувати встановлення": nothing of the download stays on the card. */
static void drop(App *app)
{
    if (resumable(app))
        job_discard_download(&app->job);
    back(app);
}

static void back(App *app)
{
    if (app->error_back == SCREEN_DETAILS || app->error_back == SCREEN_PICK)
        app_check(app);
    app_go(app, app->error_back);
}

static void update(App *app, const Input *in)
{
    if (in->down & KEY_B) {
        if (lost(app))
            drop(app);
        else
            back(app);
        return;
    }
    if (in->down & KEY_Y && has_details(app))
        more = !more;
    if (lost(app)) {
        if (in->tapped && rect_hit(continue_button, in->tap_x, in->tap_y))
            app_retry(app);
        else if (in->tapped && rect_hit(drop_button, in->tap_x, in->tap_y))
            drop(app);
        return;
    }
    if (!in->tapped || !rect_hit(retry_button, in->tap_x, in->tap_y))
        return;
    if (can_retry(app))
        app_retry(app);
    else
        back(app);
}

/* "Деталі для звіту": everything someone fixing it would ask for, on one screen to photograph. */
static void draw_details(const App *app)
{
    const ErrorInfo *e = error_info(app->error);
    const Job *job = &app->job;
    ui_text(20, 10, 15, FONT_HEAVY, COL_INK, "Деталі для звіту");
    char id[REPORT_ID_LEN + 2];
    report_id_display(app->stats.id, id);
    char head[64];
    snprintf(head, sizeof head, "ID установки %s", id);
    ui_text_right(TOP_W - 20, 14, 10, FONT_BOLD, COL_MUTED, head);

    char code[64], result[16] = "—", item[128], tid[20], hub[64], console[96], report_buf[80];
    const char *report = app_report_text(app, report_buf, sizeof report_buf);
    snprintf(code, sizeof code, "%s · %s", e->code, app->error_stage && app->error_stage[0] ? app->error_stage : "—");
    if (job->sys_result)
        snprintf(result, sizeof result, "0x%08lX", (unsigned long)job->sys_result);
    snprintf(item, sizeof item, "%s · v%s · %s", job->slug, job->version, job->variant.region[0] ? job->variant.region : "—");
    format_tid(tid, sizeof tid, job->target_tid);
    snprintf(hub, sizeof hub, "v" APP_VERSION " · каталог від %s", app->hub.fetched_at[0] ? app->hub.fetched_at : "—");
    format_console(console, sizeof console, app);
    const char *detail = job->detail;
    while (*detail == ' ')
        detail++;
    const char *rows[] = {
        "Код · етап", code,
        "Код системи", result,
        "Подробиці", job->tech[0] ? job->tech : detail[0] ? detail : "—",
        "Файл / адреса", job->where[0] ? job->where : "—",
        "Переклад", item,
        "Title ID гри", job->target_tid ? tid : "—",
        "3DS UA Hub", hub,
        "Консоль", console,
        "Звіт", report,
    };
    draw_info_rows(20, 34, TOP_W - 40, 20, rows, 9);
}

/* INTERRUPTED: what the start did about the job a power cut stopped, by name. */
static void recovered_text(const App *app, char *out, size_t size)
{
    const char *t = app->job_title;
    switch (app->recovered) {
    case RECOVER_INSTALLED:
        snprintf(out, size, "Консоль вимкнулась посеред встановлення «%s». Ми його завершили: переклад встановлено й працює.", t);
        break;
    case RECOVER_ROLLED_BACK:
        snprintf(out, size,
                 "Консоль вимкнулась посеред встановлення «%s». Недороблене ми прибрали: гра така, як була до "
                 "встановлення. Щоб поставити переклад, відкрийте його в каталозі ще раз.", t);
        break;
    case RECOVER_REMOVED:
        snprintf(out, size, "Консоль вимкнулась посеред видалення «%s». Ми його завершили: перекладу більше немає, гра як була.", t);
        break;
    default:
        snprintf(out, size, "Консоль вимкнулась посеред роботи. Недороблене ми прибрали, встановлені переклади на місці.");
        break;
    }
}

static void draw_top(const App *app)
{
    if (more) {
        draw_details(app);
        return;
    }
    const ErrorInfo *e = error_info(app->error);
    bool notice = app->error == ERR_INTERRUPTED;
    /* G4's warnings (yellow) can go on as they are; the rest stopped (red) */
    bool warning = notice || app->error == ERR_NET_LOST || app->error == ERR_HASH_MISMATCH || app->error == ERR_LUMA_OLD ||
                   app->error == ERR_GAME_NOT_FOUND || app->error == ERR_APP_OUTDATED;
    ui_circle(44, 40, 20, warning ? COL_YELLOW : COL_RED);
    ui_icon(notice ? ICON_INFO : ICON_WARN, 44, 40, 22, warning ? COL_INK : COL_WHITE);
    float th = ui_text_wrap(76, 22, TOP_W - 96, 17, FONT_HEAVY, COL_INK, e->title);
    float y = 26 + th;
    if (!notice && app->job_title[0]) {
        /* SD_WRITE and AM_FAIL undid what they had started; the others stopped before writing */
        bool undone = app->error == ERR_SD_WRITE || app->error == ERR_AM_FAIL;
        char sub[160];
        snprintf(sub, sizeof sub, "%s · %s", app->job_title, undone ? "усе повернуто як було" : "нічого не змінено");
        draw_text_fit(76, y, TOP_W - 96, 10.5f, FONT_BOLD, COL_MUTED, sub);
        y += 16;
    }
    if (y < 82)
        y = 82; /* below the circle */
    else
        y += 10;
    char text[512]; /* the notice holds a title of up to 95 bytes */
    if (notice)
        recovered_text(app, text, sizeof text);
    else if (lost(app) && !resumable(app))
        snprintf(text, sizeof text, "Недописану гру прибрано. Завантаження почнеться спочатку, щойно Wi-Fi повернеться.");
    else
        snprintf(text, sizeof text, e->text, app->error_detail);
    y += ui_text_wrap(24, y, TOP_W - 48, 11.5f, FONT_BOLD, COL_BODY, text) + 10;
    if (notice)
        return;

    char code[64];
    if (app->error_stage && app->error_stage[0])
        snprintf(code, sizeof code, "%s · етап «%s»", e->code, app->error_stage);
    else
        snprintf(code, sizeof code, "%s", e->code);
    char report_buf[80];
    const char *rows[] = {"Код помилки", code, "Звіт", app_report_text(app, report_buf, sizeof report_buf)};
    draw_info_rows(24, y, TOP_W - 48, 22, rows, 2);
}

static void draw_lost(const App *app)
{
    const Job *job = &app->job;
    ui_text(14, 12, 13, FONT_HEAVY, COL_INK, "Що зробити");
    if (resumable(app)) {
        char sub[64], done[16], left[16];
        format_size(done, sizeof done, job->net.done);
        format_size(left, sizeof left, job->variant.size > job->net.done ? job->variant.size - job->net.done : 0);
        snprintf(sub, sizeof sub, "з %s · лишилось %s", done, left);
        draw_button2(continue_button, BTN_PRIMARY, ICON_UPDATE, "Продовжити", sub);
    } else {
        draw_button2(continue_button, BTN_PRIMARY, ICON_UPDATE, "Спробувати знову", "завантаження почнеться спочатку");
    }
    draw_button(drop_button, BTN_SECONDARY, ICON_CROSS, job->kind == JOB_CHECK ? "Скасувати перевірку" : "Скасувати встановлення", 13);
    ui_text_center(BOTTOM_W / 2, 166, 10.5f, FONT_BOLD, COL_MUTED, "Спершу перевірте Wi-Fi: перемикач і Налаштування системи.");
    const char *hints[] = {"A", resumable(app) ? "Продовжити" : "Знову", "Y", more ? "Сховати деталі" : "Деталі для звіту",
                           "B", "Скасувати", NULL};
    draw_hints(hints);
}

static void draw_bottom(const App *app)
{
    if (lost(app)) {
        draw_lost(app);
        return;
    }
    const ErrorInfo *e = error_info(app->error);
    float y = 12;
    if (e->steps || can_retry(app)) {
        ui_text(14, y, 13, FONT_HEAVY, COL_INK, "Що зробити");
        y += 24;
        int n = 1;
        if (e->steps)
            y += draw_numbered(14, y, BOTTOM_W - 28, n++, e->steps) + 8;
        if (can_retry(app))
            draw_numbered(14, y, BOTTOM_W - 28, n, retry_step(app));
    }
    if (can_retry(app)) {
        bool resume = app->error == ERR_NET_LOST;
        draw_button(retry_button, BTN_PRIMARY, ICON_UPDATE, resume ? "Продовжити" : "Спробувати знову", 15);
        const char *hints[] = {"A", resume ? "Продовжити" : "Знову", "Y", more ? "Сховати деталі" : "Деталі для звіту",
                               "B", "Назад", NULL};
        if (!has_details(app)) {
            hints[2] = "B";
            hints[3] = "Назад";
            hints[4] = NULL;
        }
        draw_hints(hints);
    } else {
        const char *label = app->error_back == SCREEN_CATALOG ? "До каталогу" : "Назад";
        draw_button(retry_button, BTN_SECONDARY, ICON_LIST, label, 15);
        const char *hints[] = {"A", label, "Y", more ? "Сховати деталі" : "Деталі для звіту", NULL};
        if (!has_details(app))
            hints[2] = NULL;
        draw_hints(hints);
    }
}

const Screen screen_error = {.enter = enter, .update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets};
