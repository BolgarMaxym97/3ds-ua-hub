/* D1, E1: a job under way, step by step; D2 once Cancel is pressed. app_update() leaves this
 * screen when the job ends. */
#include <stdio.h>
#include <string.h>

#include "../ui.h"
#include "common.h"

static const Rect cancel_button = {14, 150, BOTTOM_W - 28, 36};

typedef struct {
    JobStage stage;
    const char *name;
} Step;

static const Step PATCH_STEPS[] = {
    {JS_CHECK, "Перевірка гри й консолі"}, {JS_DOWNLOAD, "Завантаження"}, {JS_VERIFY, "Перевірка цілісності файлів"},
    {JS_INSTALL, "Копіювання на SD-карту"}, {JS_LUMA, "Увімкнення патча в Luma"},
};
static const Step CIA_STEPS[] = {
    {JS_CHECK, "Перевірка консолі"}, {JS_DOWNLOAD, "Завантаження й запис"}, {JS_INSTALL, "Завершення встановлення"},
};
/* a CIA that keeps its game on the SD card */
static const Step CIA_DATA_STEPS[] = {
    {JS_CHECK, "Перевірка консолі"},       {JS_DOWNLOAD, "Завантаження програми"}, {JS_INSTALL, "Встановлення програми"},
    {JS_DATA, "Завантаження гри"},         {JS_DATA_COPY, "Копіювання гри на SD-карту"},
};
static const Step REMOVE_STEPS[] = {{JS_INSTALL, "Видалення файлів"}};
static const Step CIA_REMOVE_STEPS[] = {{JS_INSTALL, "Видалення гри з системи"}};
static const Step CIA_DATA_REMOVE_STEPS[] = {{JS_INSTALL, "Видалення гри з системи"}, {JS_DATA_COPY, "Видалення файлів гри"}};

/* When each stage began, and the download speed: this screen's own bookkeeping. */
static u64 began[JS_DONE + 1];
static JobStage seen;
static u32 speed, sample_bytes;
static u64 sample_at;
static bool sampled; /* the first second only sets the baseline: a resumed download starts high */

static const Step *steps(const App *app, int *n)
{
    const Job *job = &app->job;
    if (job->kind == JOB_REMOVE && job->removing.dir[0]) {
        *n = sizeof CIA_DATA_REMOVE_STEPS / sizeof *CIA_DATA_REMOVE_STEPS;
        return CIA_DATA_REMOVE_STEPS;
    }
    if (job->kind == JOB_REMOVE) {
        *n = 1;
        return job->item_kind == KIND_CIA ? CIA_REMOVE_STEPS : REMOVE_STEPS;
    }
    if (job->item_kind == KIND_CIA && job->variant.data.present) {
        *n = sizeof CIA_DATA_STEPS / sizeof *CIA_DATA_STEPS;
        return CIA_DATA_STEPS;
    }
    if (job->item_kind == KIND_CIA) {
        *n = sizeof CIA_STEPS / sizeof *CIA_STEPS;
        return CIA_STEPS;
    }
    *n = sizeof PATCH_STEPS / sizeof *PATCH_STEPS;
    return PATCH_STEPS;
}

static bool can_cancel(const App *app)
{
    return app->job.kind != JOB_REMOVE && (app->job.stage <= JS_DOWNLOAD || app->job.stage == JS_DATA) &&
           !app->job.net.cancel;
}

static void enter(App *app)
{
    memset(began, 0, sizeof began);
    seen = app->job.stage;
    began[seen] = osGetTime();
    speed = sample_bytes = 0;
    sampled = false;
    sample_at = osGetTime();
}

static int targets(const App *app, Target *out)
{
    if (!can_cancel(app))
        return 0;
    out[0] = (Target){cancel_button, 11};
    return 1;
}

static void update(App *app, const Input *in)
{
    JobStage now = app->job.stage;
    if (now != seen && now <= JS_DONE) {
        for (int s = seen + 1; s <= (int)now; s++)
            began[s] = osGetTime();
        seen = now;
    }
    u64 t = osGetTime();
    if (t - sample_at >= 1000) {
        u32 done = app->job.net.done;
        if (sampled && done >= sample_bytes)
            speed = (u32)((u64)(done - sample_bytes) * 1000 / (t - sample_at));
        sampled = true;
        sample_bytes = done;
        sample_at = t;
    }
    if (!can_cancel(app))
        return;
    if (in->down & KEY_B || (in->tapped && rect_hit(cancel_button, in->tap_x, in->tap_y)))
        job_cancel(&app->job);
}

/* ---------------------------------------------------------------- top */

static u32 total_bytes(const App *app)
{
    if (app->job.net.total)
        return app->job.net.total;
    return app->job.stage == JS_DATA ? app->job.variant.data_size : app->job.variant.size;
}

/* The current stage's own share done, 0-1; -1 while it cannot be known. */
static float fraction(const App *app)
{
    const Job *job = &app->job;
    if ((job->stage == JS_DOWNLOAD || job->stage == JS_DATA) && total_bytes(app))
        return (float)job->net.done / total_bytes(app);
    /* removing counts its files too */
    if (job->patch.total && ((job->stage == JS_INSTALL && job->item_kind == KIND_PATCH) || job->stage == JS_DATA_COPY))
        return job->patch.bytes_total ? (float)job->patch.bytes / job->patch.bytes_total
                                      : (float)job->patch.done / job->patch.total;
    if (job->stage >= JS_LUMA)
        return 1;
    return -1;
}

static void draw_cancelling(const App *app)
{
    ui_circle(48, 50, 24, COL_YELLOW);
    ui_icon(ICON_CROSS, 48, 50, 22, COL_INK);
    ui_text(86, 30, 19, FONT_HEAVY, COL_INK, "Скасовую встановлення");
    draw_text_fit(86, 56, TOP_W - 106, 11, FONT_BOLD, COL_MUTED, app->job_title);
    ui_text_wrap(24, 96, TOP_W - 48, 12, FONT_BOLD, COL_BODY,
                 app->job.item_kind == KIND_CIA
                     ? "Прибираю недописану гру. Це кілька секунд — не вимикайте консоль."
                     : "Повертаю все, як було до встановлення. Це кілька секунд — не вимикайте консоль і не виймайте SD-карту.");
}

static void draw_top(const App *app)
{
    const Job *job = &app->job;
    if (job->net.cancel) {
        draw_cancelling(app);
        return;
    }
    float f = fraction(app);
    const float cx = 78, cy = 112;
    ui_ring(cx, cy, 60, 49, f < 0 ? 0 : f, COL_BLUE, COL_TRACK);
    char big[16], small[48], a[16], b[16];
    if (f >= 0)
        snprintf(big, sizeof big, "%d%%", (int)(f * 100 + 0.5f));
    else
        snprintf(big, sizeof big, "…");
    small[0] = 0;
    if (job->stage == JS_DOWNLOAD || job->stage == JS_DATA) {
        format_size(a, sizeof a, job->net.done);
        format_size(b, sizeof b, total_bytes(app));
        snprintf(small, sizeof small, "%s з %s", a, b);
    } else if ((job->stage == JS_INSTALL || job->stage == JS_DATA_COPY) && job->patch.bytes_total > 4 * 1024 * 1024) {
        /* a big set: one file can take minutes, so the bytes show it moving */
        format_size(a, sizeof a, (u32)job->patch.bytes);
        format_size(b, sizeof b, (u32)job->patch.bytes_total);
        snprintf(small, sizeof small, "%s з %s", a, b);
    } else if ((job->stage == JS_INSTALL || job->stage == JS_DATA_COPY) && job->patch.total) {
        snprintf(small, sizeof small, "%lu з %lu файлів", (unsigned long)job->patch.done, (unsigned long)job->patch.total);
    }
    /* the number and its line as one block, centred in the ring */
    ui_text_center(cx, cy - 27, 28, FONT_HEAVY, COL_INK, big);
    ui_text_center(cx, cy + 6, 10.5f, FONT_BOLD, COL_MUTED, small);

    const float x = 160, w = TOP_W - x - 20;
    const CatItem *it = app->page < app->hub.catalog.n ? &app->hub.catalog.items[app->page] : NULL;
    if (it)
        draw_item_icon(it, x, 30, 26);
    ui_text_wrap(x + 34, 30, w - 34, 13, FONT_HEAVY, COL_INK, app->job_title);

    const char *what;
    switch (job->stage) {
    case JS_CHECK: what = "Перевіряю консоль"; break;
    case JS_DOWNLOAD: what = job->item_kind == KIND_CIA ? "Завантажую й записую гру" : "Завантажую переклад"; break;
    case JS_VERIFY: what = "Звіряю файли з каталогом"; break;
    case JS_INSTALL:
        what = job->kind == JOB_REMOVE ? "Видаляю" : job->item_kind == KIND_CIA ? "Завершую встановлення" : "Копіюю на SD-карту";
        break;
    case JS_DATA: /* the whole file is here: its sha256 takes a while on a big one */
        what = job->net.total && job->net.done >= job->net.total ? "Звіряю гру з каталогом" : "Завантажую гру";
        break;
    case JS_DATA_COPY: what = job->kind == JOB_REMOVE ? "Видаляю файли гри" : "Копіюю гру на SD-карту"; break;
    case JS_LUMA: what = "Вмикаю патчі в Luma"; break;
    default: what = "Завершую"; break;
    }
    ui_card(x, 76, w, 62, 10, COL_CARD, 1.5f);
    ui_text(x + 12, 82, 10.5f, FONT_BOLD, COL_MUTED, "Зараз");
    draw_text_fit(x + 12, 96, w - 24, 13, FONT_HEAVY, COL_INK, what);
    if ((job->stage == JS_DOWNLOAD || job->stage == JS_DATA) && speed) {
        char line[64], rate[16];
        format_size(rate, sizeof rate, speed);
        u32 left = total_bytes(app) > job->net.done ? total_bytes(app) - job->net.done : 0;
        snprintf(line, sizeof line, "%s/с · лишилось ~%lu с", rate, (unsigned long)(left / speed + 1));
        ui_text(x + 12, 117, 10, FONT_BOLD, COL_BLUE, line);
    }
    ui_text_wrap(x, 150, w, 10, FONT_BOLD, COL_BODY,
                 job->item_kind == KIND_CIA && job->kind == JOB_INSTALL && job->stage <= JS_INSTALL
                     ? "Файл пишеться одразу в систему, без копії на SD. Не вимикайте консоль."
                     : "Не вимикайте консоль і не виймайте SD-карту.");
}

/* ---------------------------------------------------------------- bottom */

static void draw_bottom(const App *app)
{
    const Job *job = &app->job;
    int n;
    const Step *s = steps(app, &n);
    const float h = 25, x = 14, w = BOTTOM_W - 28;
    ui_card(x, 10, w, n * h, 10, COL_CARD, 1.5f);
    for (int i = 0; i < n; i++) {
        float y = 10 + i * h;
        bool done = job->stage > s[i].stage, now = job->stage == s[i].stage;
        if (done) {
            draw_mark(x + 18, y + h / 2, 7, MARK_OK);
        } else if (now) {
            ui_circle(x + 18, y + h / 2, 7, COL_BLUE);
            ui_icon(ICON_DOWNLOAD, x + 18, y + h / 2, 9, COL_WHITE);
        } else {
            char num[4];
            snprintf(num, sizeof num, "%d", i + 1);
            ui_circle(x + 18, y + h / 2, 7, COL_LINE);
            ui_text_center(x + 18, y + h / 2 - 6, 9.5f, FONT_HEAVY, COL_MUTED, num);
        }
        ui_text(x + 32, y + 6, 10.5f, now ? FONT_HEAVY : FONT_BOLD, COL_INK, s[i].name);
        char value[48] = "";
        if (done && began[s[i].stage] && i + 1 < n && began[s[i + 1].stage]) {
            u64 ms = began[s[i + 1].stage] - began[s[i].stage];
            snprintf(value, sizeof value, "%llu,%llu с", ms / 1000, ms % 1000 / 100);
        } else if (now && (s[i].stage == JS_DOWNLOAD || s[i].stage == JS_DATA)) {
            char a[16], b[16];
            format_size(a, sizeof a, job->net.done);
            format_size(b, sizeof b, total_bytes(app));
            snprintf(value, sizeof value, "%s / %s", a, b);
        }
        ui_text_right(x + w - 12, y + 6, 10.5f, FONT_BOLD, now ? COL_BLUE : COL_MUTED, value);
        if (i + 1 < n)
            ui_rect(x + 12, y + h - 1, w - 24, 1, COL_LINE);
    }

    if (job->net.cancel) {
        ui_text_center(BOTTOM_W / 2, 160, 10, FONT_BOLD, COL_MUTED, "Після цього повернемось на сторінку перекладу.");
        return;
    }
    if (can_cancel(app)) {
        draw_button(cancel_button, BTN_SECONDARY, ICON_CROSS, "Скасувати", 13);
        ui_key_hint(PAD, FOOT_Y, "B", "Скасувати");
        ui_text_right(BOTTOM_W - PAD, FOOT_Y + 2, 10, FONT_BOLD, COL_MUTED,
                      job->item_kind == KIND_CIA ? "Скасування прибере недописану гру" : "Скасування поверне гру як була");
    } else {
        ui_text_center(BOTTOM_W / 2, 160, 10, FONT_BOLD, COL_MUTED,
                       job->stage == JS_DATA_COPY ? "Цей крок не можна перервати — гра велика, це кілька хвилин."
                                                  : "Цей крок не можна перервати — кілька секунд.");
    }
}

const Screen screen_progress = {
    .enter = enter, .update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets,
};
