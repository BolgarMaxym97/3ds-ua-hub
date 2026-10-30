/* I2, I3: a translation's screenshots, R from its page. On top the whole screenshot, both screens
 * of the game shrunk to half (200x240); below, its caption and the row of them to pick from. */
#include <stdio.h>
#include <string.h>

#include "../image.h"
#include "../shots.h"
#include "../ui.h"
#include "common.h"

#define PLATE RGBA(0x14, 0x1C, 0x2A, 0xD7) /* the counter's plate: the banner title's */
#define THUMBS_Y 72
#define THUMB_W 45
#define THUMB_H 54 /* 400x480 at the same ratio */

static const CatItem *item(const App *app)
{
    return &app->hub.catalog.items[app->page];
}

static Rect thumb_rect(int i)
{
    float gap = (BOTTOM_W - 2 * PAD - CAT_MAX_SHOTS * THUMB_W) / (float)(CAT_MAX_SHOTS - 1);
    return (Rect){PAD + i * (THUMB_W + gap), THUMBS_Y, THUMB_W, THUMB_H};
}

static void enter(App *app)
{
    shots_want(item(app)); /* the ones that failed get another try */
}

static void update(App *app, const Input *in)
{
    int n = item(app)->n_shots;
    if (in->down & KEY_B || !n) { /* a new catalog may have taken them away */
        app_go(app, SCREEN_DETAILS);
        return;
    }
    if (in->down & (KEY_L | KEY_DLEFT) && app->shot > 0)
        app->shot--;
    if (in->down & (KEY_R | KEY_DRIGHT) && app->shot < n - 1)
        app->shot++;
    if (in->tapped)
        for (int i = 0; i < n; i++)
            if (rect_hit(thumb_rect(i), in->tap_x, in->tap_y))
                app->shot = i;
    if (app->shot >= n)
        app->shot = n - 1;
}

static void draw_top(const App *app)
{
    const CatItem *it = item(app);
    ui_rect(0, 0, TOP_W, SCREEN_H, COL_INK);
    int i = app->shot < it->n_shots ? app->shot : 0;
    const char *sha = it->shots[i].file.sha256;
    ShotState state = shots_state(sha);
    const C2D_Image *img = state == SHOT_READY ? image_get_shot(sha) : NULL;
    if (img) {
        image_draw(img, (TOP_W - 200) / 2.0f, 0, 200, SCREEN_H);
    } else if (state == SHOT_FAILED) {
        ui_icon(ICON_CROSS, TOP_W / 2, 78, 26, COL_TRACK);
        ui_text_center(TOP_W / 2, 100, 15, FONT_HEAVY, COL_WHITE, "Скріншот не завантажено");
        ui_text_wrap(70, 124, TOP_W - 140, 10.5f, FONT_BOLD, COL_TRACK,
                     "Немає зв'язку з сервером. Завантажені раніше скріншоти можна гортати, а цей з'явиться, "
                     "коли відкриєте скріншоти знову з інтернетом.");
    } else {
        ui_spinner(TOP_W / 2, SCREEN_H / 2 - 8, 8, COL_WHITE);
        if (state == SHOT_LOADING)
            ui_text_center(TOP_W / 2, SCREEN_H / 2 + 10, 10.5f, FONT_BOLD, COL_TRACK, "Завантажую скріншот…");
    }

    char count[16];
    snprintf(count, sizeof count, "%d / %d", i + 1, it->n_shots);
    float w = ui_text_width(11, FONT_HEAVY, count) + 18;
    ui_rrect(TOP_W - 8 - w, 8, w, 22, 6, PLATE);
    ui_text_center(TOP_W - 8 - w / 2, 11.5f, 11, FONT_HEAVY, COL_WHITE, count);
}

static void draw_bottom(const App *app)
{
    const CatItem *it = item(app);
    int cur = app->shot < it->n_shots ? app->shot : 0;
    char head[160];
    snprintf(head, sizeof head, "%s · скріншоти перекладу", it->title);
    draw_text_fit(PAD, 9, BOTTOM_W - 2 * PAD, 9, FONT_BOLD, COL_MUTED, head);
    const char *caption = it->shots[cur].caption;
    if (caption && caption[0]) {
        if (ui_text_wrap_height(BOTTOM_W - 2 * PAD, 13, FONT_HEAVY, caption) <= 2 * 13 * 1.4f)
            ui_text_wrap(PAD, 23, BOTTOM_W - 2 * PAD, 13, FONT_HEAVY, COL_INK, caption);
        else
            draw_text_fit(PAD, 23, BOTTOM_W - 2 * PAD, 13, FONT_HEAVY, COL_INK, caption);
    }

    for (int i = 0; i < it->n_shots; i++) {
        Rect r = thumb_rect(i);
        const char *sha = it->shots[i].file.sha256;
        const C2D_Image *img = shots_state(sha) == SHOT_READY ? image_get_thumb(sha) : NULL;
        if (img) {
            ui_rrect(r.x, r.y + 1.5f, r.w, r.h, 5, COL_CARD_SHADOW);
            image_draw(img, r.x, r.y, r.w, r.h);
        } else {
            char n[12];
            snprintf(n, sizeof n, "%d", i + 1);
            ui_rrect(r.x, r.y, r.w, r.h, 5, COL_TRACK);
            ui_text_center(r.x + r.w / 2, r.y + r.h / 2 - 8, 12, FONT_HEAVY, COL_MUTED, n);
        }
        if (i == cur)
            ui_frame(r.x - 3.5f, r.y - 3.5f, r.w + 7, r.h + 7, 7, 2.5f, COL_BLUE);
    }

    static const char *const hints[] = {"L", "", "R", "Листати", "B", "До сторінки", NULL};
    draw_hints(hints);
}

const Screen screen_gallery = {
    .enter = enter, .update = update, .draw_top = draw_top, .draw_bottom = draw_bottom,
};
