#include "common.h"

#include <stdio.h>
#include <string.h>

#include "../image.h"
#include "../ui.h"

/* The title plate on a banner: the ink colour at 84%, chosen over an outline or a gradient
 * after trying all of them on light, dark and busy banners (2026-10-25). */
#define PLATE RGBA(0x14, 0x1C, 0x2A, 0xD7)
#define PLATE_TEXT RGBA(0xCD, 0xD6, 0xE4, 0xFF)

/* A key with no label of its own ("L" of "L R Листати") stands close to the next. */
static float gap_after(const char *label, float gap)
{
    return label[0] ? gap : 4;
}

float hints_width(const char *const *pairs, float gap)
{
    float w = 0;
    for (int i = 0; pairs[i]; i += 2)
        w += ui_key_hint_width(pairs[i], pairs[i + 1]) + (pairs[i + 2] ? gap_after(pairs[i + 1], gap) : 0);
    return w;
}

void draw_hints(const char *const *pairs)
{
    float gap = 12; /* closer together when four do not fit otherwise */
    while (gap > HINT_GAP_MIN && hints_width(pairs, gap) > BOTTOM_W - 2 * PAD)
        gap -= 2;
    float x = PAD;
    for (int i = 0; pairs[i]; i += 2)
        x += ui_key_hint(x, FOOT_Y, pairs[i], pairs[i + 1]) + gap_after(pairs[i + 1], gap);
}

/* The placeholder colours of the design (B1). */
static const u32 COLOURS[] = {
    RGBA(0xC8, 0x10, 0x2E, 0xFF), RGBA(0x1E, 0x7A, 0x46, 0xFF), RGBA(0xD9, 0x57, 0x9B, 0xFF),
    RGBA(0xE0, 0x7A, 0x10, 0xFF), RGBA(0x3A, 0x45, 0x56, 0xFF), RGBA(0x8A, 0x3F, 0xB8, 0xFF),
    RGBA(0x14, 0x58, 0xC7, 0xFF),
};

u32 item_colour(const CatItem *it)
{
    u32 h = 2166136261u;
    for (const char *s = it->slug; *s; s++)
        h = (h ^ (u8)*s) * 16777619u;
    return COLOURS[h % (sizeof COLOURS / sizeof *COLOURS)];
}

/* The first letter of the first two words, whole UTF-8 letters. */
static void initials(char *out, const char *title)
{
    int n = 0, words = 0;
    bool start = true;
    for (const char *p = title; *p && words < 2; p++) {
        if (*p == ' ' || *p == ':' || *p == '-') {
            start = true;
            continue;
        }
        if (start && ((u8)*p & 0xC0) != 0x80) {
            int len = ((u8)*p & 0xE0) == 0xC0 ? 2 : ((u8)*p & 0xF0) == 0xE0 ? 3 : 1;
            if (len == 1 && !(*p >= '0' && *p <= '9') && !((*p | 0x20) >= 'a' && (*p | 0x20) <= 'z') && (u8)*p < 0x80)
                continue; /* "[" and such */
            for (int i = 0; i < len && p[i]; i++)
                out[n++] = len == 1 && p[i] >= 'a' && p[i] <= 'z' ? p[i] - 32 : p[i];
            words++;
            start = false;
        }
    }
    out[n] = 0;
}

void draw_item_icon(const CatItem *it, float x, float y, float size)
{
    const C2D_Image *img = it->icon.present ? image_get(it->icon.sha256) : NULL;
    if (img) {
        ui_rrect(x, y, size, size, size * 0.26f, COL_CARD_SHADOW);
        image_draw(img, x, y, size, size);
        return;
    }
    char letters[8];
    initials(letters, it->title);
    ui_rrect(x, y, size, size, size * 0.27f, item_colour(it));
    ui_text_center(x + size / 2, y + size * 0.24f, size * 0.38f, FONT_HEAVY, COL_WHITE, letters);
}

float draw_badge(float right, float y, const char *text, u32 bg, u32 fg)
{
    float w = ui_text_width(9.5f, FONT_HEAVY, text) + 12;
    ui_rrect(right - w, y, w, 13, 6.5f, bg);
    ui_text(right - w + 6, y + 0.5f, 9.5f, FONT_HEAVY, fg, text);
    return w;
}

float draw_text_fit(float x, float y, float max_w, float px, FontWeight weight, u32 color, const char *str)
{
    if (ui_text_width(px, weight, str) <= max_w)
        return ui_text(x, y, px, weight, color, str);
    char buf[256];
    size_t n = strlen(str);
    if (n > sizeof buf - 4)
        n = sizeof buf - 4;
    while (n > 0) {
        n--;
        while (n > 0 && ((u8)str[n] & 0xC0) == 0x80)
            n--; /* whole letters only */
        memcpy(buf, str, n);
        while (n > 0 && buf[n - 1] == ' ')
            n--;
        strcpy(buf + n, "…");
        if (ui_text_width(px, weight, buf) <= max_w)
            break;
    }
    return ui_text(x, y, px, weight, color, buf);
}

void draw_progress_row(float x, float y, float w, const char *label, int pct)
{
    ui_text(x, y, 12, FONT_BOLD, COL_INK, label);
    float bx = x + 80, bw = w - 80 - 56, by = y + 6;
    if (pct < 0) {
        /* unknown: a dashed outline, not an empty bar that would read as 0% */
        for (float dx = 0; dx < bw; dx += 6) {
            float dash = dx + 3 > bw ? bw - dx : 3;
            ui_rect(bx + dx, by, dash, 1, COL_FAINT);
            ui_rect(bx + dx, by + 6, dash, 1, COL_FAINT);
        }
        ui_rect(bx, by, 1, 7, COL_FAINT);
        ui_rect(bx + bw - 1, by, 1, 7, COL_FAINT);
        ui_text_right(x + w, y + 1, 10.5f, FONT_BOLD, COL_MUTED, "невідомо");
        return;
    }
    char text[16];
    snprintf(text, sizeof text, "%d%%", pct);
    ui_rrect(bx, by, bw, 7, 3.5f, COL_TRACK);
    if (pct > 0)
        ui_rrect(bx, by, bw * pct / 100, 7, 3.5f, COL_GREEN);
    ui_text_right(x + w, y, 12, FONT_HEAVY, COL_INK, text);
}

void draw_banner(const CatItem *it, float width)
{
    const C2D_Image *img = it->banner.present ? image_get(it->banner.sha256) : NULL;
    if (img)
        image_draw(img, 0, 0, width, BANNER_H);
    else
        ui_rect(0, 0, width, BANNER_H, item_colour(it));
    /* solid colour, white text: a white badge vanished on the white sky of a banner */
    draw_badge(width - 10, 10, it->kind == KIND_PATCH ? "Патч Luma" : "Програма · CIA",
               it->kind == KIND_PATCH ? COL_BLUE : COL_INK, COL_WHITE);

    char regions[64], sub[128];
    format_regions(regions, sizeof regions, it);
    snprintf(sub, sizeof sub, "%s%s", regions, it->third_party ? " · сторонній переклад" : "");
    /* the plate fits the text, up to most of the width */
    float max_text = width - 20 - 18 - 60;
    float tw = ui_text_width(18, FONT_HEAVY, it->title), sw = ui_text_width(10.5f, FONT_BOLD, sub);
    float w = (tw > sw ? tw : sw);
    if (w > max_text)
        w = max_text;
    ui_rrect(10, BANNER_H - 48, w + 18, 40, 10, PLATE);
    draw_text_fit(19, BANNER_H - 46, max_text, 18, FONT_HEAVY, COL_WHITE, it->title);
    draw_text_fit(19, BANNER_H - 25, max_text, 10.5f, FONT_BOLD, PLATE_TEXT, sub);
}

void format_size(char *out, size_t size, u32 bytes)
{
    if (bytes < 1000 * 1024)
        snprintf(out, size, "%lu КБ", (unsigned long)((bytes + 1023) / 1024));
    else if (bytes < 1000ul * 1024 * 1024)
        snprintf(out, size, "%lu МБ", (unsigned long)((bytes + 512 * 1024) / (1024 * 1024)));
    else {
        unsigned long tenths = (unsigned long)(bytes / (1024.0 * 1024 * 1024) * 10 + 0.5);
        snprintf(out, size, "%lu,%lu ГБ", tenths / 10, tenths % 10);
    }
}

const char *kind_name(Kind kind)
{
    return kind == KIND_PATCH ? "Патч на SD-карту" : "Програма · CIA";
}

void format_regions(char *out, size_t size, const CatItem *it)
{
    out[0] = 0;
    for (int i = 0; i < it->n_variants; i++) {
        if (strstr(out, it->variants[i].region))
            continue;
        size_t n = strlen(out);
        snprintf(out + n, size - n, "%s%s", n ? ", " : "", it->variants[i].region);
    }
}

void format_progress(char *out, size_t size, const CatItem *it)
{
    char a[24], b[24]; /* "невідомо" is 16 bytes */
    snprintf(a, sizeof a, it->text_pct < 0 ? "невідомо" : "%d%%", it->text_pct);
    snprintf(b, sizeof b, it->textures_pct < 0 ? "невідомо" : "%d%%", it->textures_pct);
    snprintf(out, size, "%s · %s", a, b);
}

static const struct {
    u32 bg, fg, sub, shadow;
} STYLES[] = {
    [BTN_PRIMARY] = {COL_BLUE, COL_WHITE, COL_BLUE_BG, COL_BLUE_DARK},
    [BTN_SECONDARY] = {COL_CARD, COL_INK, COL_MUTED, COL_CARD_SHADOW},
    [BTN_DANGER] = {COL_RED, COL_WHITE, RGBA(0xFC, 0xDA, 0xD7, 0xFF), RGBA(0x7A, 0x14, 0x0D, 0xFF)},
    [BTN_DANGER_SOFT] = {COL_CARD, COL_RED, COL_MUTED, COL_CARD_SHADOW},
    [BTN_DISABLED] = {COL_DISABLED, COL_BODY, COL_BODY, 0}, /* muted on grey was lost on an Old 3DS */
};

static void button_body(Rect r, ButtonStyle style)
{
    float radius = r.h > 40 ? 13 : 11;
    if (STYLES[style].shadow)
        ui_rrect(r.x, r.y + 3, r.w, r.h, radius, STYLES[style].shadow);
    ui_rrect(r.x, r.y, r.w, r.h, radius, STYLES[style].bg);
}

void draw_button(Rect r, ButtonStyle style, Icon icon, const char *label, float px)
{
    button_body(r, style);
    float icon_w = px * 1.25f, gap = px * 0.45f;
    float w = icon_w + gap + ui_text_width(px, FONT_HEAVY, label);
    float x = r.x + (r.w - w) / 2;
    ui_icon(icon, x + icon_w / 2, r.y + r.h / 2, icon_w, STYLES[style].fg);
    ui_text(x + icon_w + gap, r.y + (r.h - px * 1.3f) / 2, px, FONT_HEAVY, STYLES[style].fg, label);
}

void draw_button2(Rect r, ButtonStyle style, Icon icon, const char *label, const char *sub)
{
    button_body(r, style);
    float lw = ui_text_width(15, FONT_HEAVY, label), sw = ui_text_width(11, FONT_BOLD, sub);
    float tw = lw > sw ? lw : sw, x = r.x + (r.w - tw - 30) / 2;
    ui_icon(icon, x + 10, r.y + r.h / 2, 20, STYLES[style].fg);
    ui_text(x + 30, r.y + r.h / 2 - 18, 15, FONT_HEAVY, STYLES[style].fg, label);
    ui_text(x + 30, r.y + r.h / 2 + 2, 11, FONT_BOLD, STYLES[style].sub, sub);
}

void draw_button2_left(Rect r, ButtonStyle style, Icon icon, const char *label, const char *sub)
{
    button_body(r, style);
    ui_icon(icon, r.x + 22, r.y + r.h / 2, 20, STYLES[style].fg);
    ui_text(r.x + 42, r.y + r.h / 2 - 18, 15, FONT_HEAVY, STYLES[style].fg, label);
    ui_text(r.x + 42, r.y + r.h / 2 + 2, 11, FONT_BOLD, STYLES[style].sub, sub);
}

void draw_mark(float cx, float cy, float r, Mark mark)
{
    static const u32 BG[] = {[MARK_OK] = COL_GREEN_BG, [MARK_WARN] = COL_YELLOW, [MARK_BAD] = RGBA(0xFC, 0xDA, 0xD7, 0xFF)};
    static const u32 FG[] = {[MARK_OK] = COL_GREEN_TEXT, [MARK_WARN] = COL_INK, [MARK_BAD] = COL_RED};
    static const Icon ICONS[] = {[MARK_OK] = ICON_CHECK, [MARK_WARN] = ICON_INFO, [MARK_BAD] = ICON_CROSS};
    ui_circle(cx, cy, r, BG[mark]);
    ui_icon(ICONS[mark], cx, cy, r * 1.25f, FG[mark]);
}

float draw_check_rows(float x, float y, float w, const CheckRow *rows, int n)
{
    const float h = 18;
    float card = n * h + 6;
    ui_card(x, y, w, card, 10, COL_CARD, 1.5f);
    for (int i = 0; i < n; i++) {
        float ry = y + 3 + i * h;
        bool loud = rows[i].mark != MARK_OK;
        draw_mark(x + 16, ry + h / 2, 6, rows[i].mark);
        ui_text(x + 28, ry + 2.5f, 10, loud ? FONT_HEAVY : FONT_BOLD, COL_INK, rows[i].label);
        u32 colour = rows[i].mark == MARK_BAD ? COL_RED : rows[i].mark == MARK_WARN ? COL_AMBER_TEXT : COL_MUTED;
        ui_text_right(x + w - 10, ry + 2.5f, 10, loud ? FONT_HEAVY : FONT_BOLD, colour, rows[i].value);
    }
    return card;
}

float draw_info_rows(float x, float y, float w, float row_h, const char *const *pairs, int n)
{
    float card = n * row_h;
    ui_card(x, y, w, card, 10, COL_CARD, 1.5f);
    for (int i = 0; i < n; i++) {
        float ry = y + i * row_h, ty = ry + (row_h - 13) / 2;
        const char *value = pairs[2 * i + 1];
        bool red = value[0] == '!';
        ui_text(x + 12, ty, 10.5f, FONT_BOLD, COL_MUTED, pairs[2 * i]);
        float label_w = ui_text_width(10.5f, FONT_BOLD, pairs[2 * i]) + 24;
        float vw = ui_text_width(10.5f, FONT_HEAVY, value + red);
        if (vw > w - label_w - 12)
            draw_text_fit(x + label_w, ty, w - label_w - 12, 10.5f, FONT_HEAVY, red ? COL_RED : COL_INK, value + red);
        else
            ui_text_right(x + w - 12, ty, 10.5f, FONT_HEAVY, red ? COL_RED : COL_INK, value + red);
        if (i + 1 < n)
            ui_rect(x + 12, ry + row_h - 1, w - 24, 1, COL_LINE);
    }
    return card;
}

float draw_numbered(float x, float y, float w, int n, const char *text)
{
    char num[4];
    snprintf(num, sizeof num, "%d", n);
    ui_circle(x + 8.5f, y + 8, 8.5f, COL_BLUE);
    ui_text_center(x + 8.5f, y + 2, 10, FONT_HEAVY, COL_WHITE, num);
    float h = ui_text_wrap(x + 24, y, w - 24, 10.5f, FONT_BOLD, COL_INK, text);
    return h > 17 ? h : 17;
}

void format_tid(char *out, size_t size, u64 tid)
{
    snprintf(out, size, "%016llX", (unsigned long long)tid);
}

void format_console(char *out, size_t size, const App *app)
{
    char luma[32] = "без Luma";
    u32 v = job_luma_version();
    if (job_is_emulator())
        snprintf(luma, sizeof luma, "емулятор");
    else if (v)
        snprintf(luma, sizeof luma, "Luma %lu.%lu.%lu", GET_VERSION_MAJOR(v), GET_VERSION_MINOR(v), GET_VERSION_REVISION(v));
    const char *model = app_model_name();
    snprintf(out, size, "%s · %s · %s", model ? model : "3DS", app->stats.system[0] ? app->stats.system : "?", luma);
}

void draw_title_icon(const App *app, int index, float x, float y, float size)
{
    const Titles *t = &app->hub.titles;
    char key[24];
    format_tid(key, sizeof key, t->items[index].title_id);
    const C2D_Image *img = t->icons[index][3] ? image_get_pixels(key, t->icons[index], TITLE_ICON, TITLE_ICON) : NULL;
    if (img) {
        image_draw(img, x, y, size, size);
        return;
    }
    char letter[2] = {t->items[index].name[0], 0};
    ui_rrect(x, y, size, size, size * 0.27f, COL_BODY);
    ui_text_center(x + size / 2, y + size * 0.24f, size * 0.38f, FONT_HEAVY, COL_WHITE, letter);
}
