/* J8: the patch's game cannot be fetched by the Hub (no fallback, or its link failed), but the catalog names its page
 * on the storage: a QR code of it on the top screen, for a phone. The person gets the CIA there,
 * installs it (FBI from the card, or FBI's QR install from the phone's screen), then "Шукати гру". */
#include <stdio.h>
#include <string.h>

#include "../../vendor/qrcodegen.h"
#include "../ui.h"
#include "common.h"

static const Rect search_button = {14, 168, (BOTTOM_W - 28 - 10) / 2, 40};
static const Rect nogame_button = {14 + (BOTTOM_W - 28 - 10) / 2 + 10, 168, (BOTTOM_W - 28 - 10) / 2, 40};

static uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
static int qr_size; /* modules a side, 0: could not be made */
static int scan_frames;
static bool searched; /* "Шукати гру" ran and found nothing */

static const CatVariant *variant(const App *app)
{
    return &app->hub.catalog.items[app->page].variants[app->checks.qr];
}

static void enter(App *app)
{
    if (app->checks.qr < 0) {
        app_go(app, SCREEN_NOTFOUND);
        return;
    }
    static uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    qr_size = qrcodegen_encodeText(variant(app)->qr_url, tmp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                   qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)
                  ? qrcodegen_getSize(qr)
                  : 0;
    scan_frames = 0;
    searched = false;
}

static int targets(const App *app, Target *out)
{
    (void)app;
    out[0] = (Target){search_button, 13};
    out[1] = (Target){nogame_button, 13};
    return 2;
}

static void search(App *app)
{
    app_rescan(app);
    searched = true;
    if (app->checks.blocked != ERR_GAME_NOT_FOUND)
        app_go(app, SCREEN_DETAILS); /* the game is here now: the page offers "Встановити" */
}

static void update(App *app, const Input *in)
{
    if (scan_frames && ++scan_frames > 2) { /* "Шукаю…" is on the screen by now */
        scan_frames = 0;
        search(app);
        return;
    }
    if (scan_frames)
        return;
    if (in->down & KEY_B) {
        app_go(app, SCREEN_NOTFOUND);
        return;
    }
    if (in->down & KEY_X) {
        app_go(app, SCREEN_NOGAME);
        return;
    }
    if (!in->tapped)
        return;
    if (rect_hit(search_button, in->tap_x, in->tap_y))
        scan_frames = 1;
    else if (rect_hit(nogame_button, in->tap_x, in->tap_y))
        app_go(app, SCREEN_NOGAME);
}

/* The code on a white card, with its quiet zone; dark runs of a row drawn as one rectangle. */
static void draw_qr(float x, float y, float side)
{
    ui_card(x, y, side, side, 10, COL_WHITE, 1.5f);
    if (!qr_size)
        return;
    int px = (int)((side - 8) / (qr_size + 8)); /* whole pixels a module: sharp edges */
    if (px < 1)
        px = 1;
    float ox = x + (side - qr_size * px) / 2, oy = y + (side - qr_size * px) / 2;
    for (int r = 0; r < qr_size; r++) {
        for (int c = 0; c < qr_size;) {
            if (!qrcodegen_getModule(qr, c, r)) {
                c++;
                continue;
            }
            int start = c;
            while (c < qr_size && qrcodegen_getModule(qr, c, r))
                c++;
            ui_rect(ox + start * px, oy + r * px, (c - start) * px, px, COL_INK);
        }
    }
}

static void draw_top(const App *app)
{
    const CatItem *it = &app->hub.catalog.items[app->page];
    const CatVariant *v = variant(app);
    const float side = 216, x = 14 + side + 14, w = TOP_W - x - 14;
    draw_qr(14, 12, side);
    float y = 18;
    y += ui_text_wrap(x, y, w, 15, FONT_HEAVY, COL_INK, "Завантажте гру з телефону") + 6;
    char sub[160], tid[20];
    format_tid(tid, sizeof tid, v->title_id);
    snprintf(sub, sizeof sub, "%s · %s · %s", it->title, v->region, tid);
    y += ui_text_wrap(x, y, w, 9.5f, FONT_BOLD, COL_MUTED, sub) + 8;
    if (app->qr_why[0]) { /* came here because the direct download failed */
        char why[160];
        snprintf(why, sizeof why, "Автоматично не вийшло: %s.", app->qr_why);
        y += ui_text_wrap(x, y, w, 10, FONT_HEAVY, COL_AMBER_TEXT, why) + 6;
        y += ui_text_wrap(x, y, w, 10, FONT_BOLD, COL_BODY, "Скануйте QR телефоном: це сторінка саме цієї гри.") + 8;
    } else {
        y += ui_text_wrap(x, y, w, 10.5f, FONT_BOLD, COL_BODY,
                          "Скануйте QR телефоном: відкриється сторінка саме цієї гри, тож не помилитеся з версією.") + 8;
    }
    const char *shown = v->qr_url + (strncmp(v->qr_url, "https://", 8) ? 7 : 8); /* the address without its scheme */
    draw_text_fit(x, y, w, 9, FONT_BOLD, COL_MUTED, shown);
}

static void draw_bottom(const App *app)
{
    (void)app;
    float y = 10;
    y += draw_numbered(14, y, BOTTOM_W - 28, 1, "Скануйте QR і завантажте CIA зі сторінки.") + 5;
    y += draw_numbered(14, y, BOTTOM_W - 28, 2,
                       "Скопіюйте CIA на SD-карту й встановіть через FBI. Або у FBI: Remote Install, Scan QR Code, "
                       "наведіть консоль на QR з телефону.") + 5;
    draw_numbered(14, y, BOTTOM_W - 28, 3,
                  searched ? "Гри поки немає. Встановіть її й натисніть «Шукати гру» ще раз."
                           : "Поверніться сюди й натисніть «Шукати гру».");
    if (scan_frames)
        draw_button(search_button, BTN_DISABLED, ICON_SEARCH, "Шукаю…", 13);
    else
        draw_button(search_button, BTN_PRIMARY, ICON_UPDATE, "Шукати гру", 13);
    draw_button(nogame_button, BTN_SECONDARY, ICON_SHIELD, "Без гри", 13);
    static const char *const hints[] = {"A", "Вибрати", "X", "Без гри", "B", "Назад", NULL};
    draw_hints(hints);
}

const Screen screen_qr = {.enter = enter, .update = update, .draw_top = draw_top, .draw_bottom = draw_bottom, .targets = targets};
