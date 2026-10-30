#include "ui.h"

#include <math.h>
#include <string.h>

/* romfs:/gfx/nunito_*.bcfnt: Nunito (OFL) at 700 and 800, made by `make installer` at this em size. */
#define FONT_EM 24.0f
#define LINE_HEIGHT 1.32f
/* Nunito's line box has more room above the letters than below: drawn at its top, text sits
 * high next to the icons it labels. Measured on an Old 3DS. */
#define TEXT_NUDGE 1.5f
#define TEXT_BUF_GLYPHS 4096

static C2D_Font fonts[2];
static C2D_TextBuf text_buf;
/* Measuring parses text too; a buffer of its own, emptied per call, keeps word wrap from
 * eating the frame's buffer one trial line at a time. */
static C2D_TextBuf measure_buf;

bool ui_init(void)
{
    fonts[FONT_BOLD] = C2D_FontLoad("romfs:/gfx/nunito_700.bcfnt");
    fonts[FONT_HEAVY] = C2D_FontLoad("romfs:/gfx/nunito_800.bcfnt");
    text_buf = C2D_TextBufNew(TEXT_BUF_GLYPHS);
    measure_buf = C2D_TextBufNew(512);
    return fonts[FONT_BOLD] && fonts[FONT_HEAVY] && text_buf && measure_buf;
}

void ui_exit(void)
{
    C2D_TextBufDelete(text_buf);
    C2D_TextBufDelete(measure_buf);
    C2D_FontFree(fonts[FONT_BOLD]);
    C2D_FontFree(fonts[FONT_HEAVY]);
}

void ui_frame_begin(void)
{
    C2D_TextBufClear(text_buf);
}

/* ---------------------------------------------------------------- text */

static bool parse(C2D_Text *text, FontWeight weight, const char *str)
{
    if (!C2D_TextFontParse(text, fonts[weight], text_buf, str))
        return false;
    C2D_TextOptimize(text);
    return true;
}

float ui_text_width(float px, FontWeight weight, const char *str)
{
    C2D_Text text;
    float w = 0, h = 0, s = px / FONT_EM;
    C2D_TextBufClear(measure_buf);
    if (C2D_TextFontParse(&text, fonts[weight], measure_buf, str))
        C2D_TextGetDimensions(&text, s, s, &w, &h);
    return w;
}

float ui_text(float x, float y, float px, FontWeight weight, u32 color, const char *str)
{
    C2D_Text text;
    float w = 0, h = 0, s = px / FONT_EM;
    if (!parse(&text, weight, str))
        return 0;
    C2D_TextGetDimensions(&text, s, s, &w, &h);
    C2D_DrawText(&text, C2D_WithColor, x, y + TEXT_NUDGE * px / 12, 0.5f, s, s, color);
    return w;
}

float ui_text_center(float cx, float y, float px, FontWeight weight, u32 color, const char *str)
{
    float w = ui_text_width(px, weight, str);
    return ui_text(cx - w / 2, y, px, weight, color, str);
}

float ui_text_right(float rx, float y, float px, FontWeight weight, u32 color, const char *str)
{
    float w = ui_text_width(px, weight, str);
    return ui_text(rx - w, y, px, weight, color, str);
}

/* A last line that the text goes on after: cut to fit with "…". */
static void draw_cut(float x, float y, float width, float px, FontWeight weight, u32 color, const char *line)
{
    char buf[260];
    size_t n = strlen(line);
    for (;;) {
        while (n > 0 && line[n - 1] == ' ')
            n--;
        memcpy(buf, line, n);
        strcpy(buf + n, "…");
        if (!n || ui_text_width(px, weight, buf) <= width)
            break;
        do
            n--;
        while (n > 0 && ((u8)line[n] & 0xC0) == 0x80); /* whole letters only */
    }
    ui_text(x, y, px, weight, color, buf);
}

/* Greedy word wrap on spaces and '\n', measured with the font itself; draws only if `draw`. At
 * most `max_lines` lines (0: any), the last cut with "…" when there is more. */
static float wrap(float x, float y, float width, float px, FontWeight weight, u32 color, const char *str, bool draw,
                  int max_lines)
{
    char line[256], trial[256];
    size_t line_len = 0;
    float line_h = px * LINE_HEIGHT, cy = y;
    int lines = 0;
    const char *p = str;
    line[0] = 0;
#define EMIT(more)                                                                                         \
    do {                                                                                                   \
        if (max_lines && lines + 1 == max_lines && (more)) {                                              \
            if (draw)                                                                                      \
                draw_cut(x, cy, width, px, weight, color, line);                                           \
            return cy + line_h - y;                                                                        \
        }                                                                                                  \
        if (draw)                                                                                          \
            ui_text(x, cy, px, weight, color, line);                                                       \
        cy += line_h;                                                                                      \
        lines++;                                                                                           \
    } while (0)
    while (*p) {
        const char *end = p;
        while (*end && *end != ' ' && *end != '\n')
            end++;
        size_t word = (size_t)(end - p);
        if (line_len + word + 2 >= sizeof(trial))
            word = sizeof(trial) - line_len - 2;
        memcpy(trial, line, line_len);
        size_t t = line_len;
        if (line_len)
            trial[t++] = ' ';
        memcpy(trial + t, p, word);
        trial[t + word] = 0;
        if (line_len && ui_text_width(px, weight, trial) > width) {
            EMIT(true);
            memcpy(line, p, word);
            line_len = word;
        } else {
            memcpy(line, trial, t + word + 1);
            line_len = t + word;
        }
        line[line_len] = 0;
        if (*end == '\n') {
            EMIT(end[1] != 0);
            line_len = 0;
            line[0] = 0;
        }
        p = *end ? end + 1 : end;
    }
    if (line_len)
        EMIT(false);
#undef EMIT
    return cy - y;
}

float ui_text_wrap(float x, float y, float width, float px, FontWeight weight, u32 color, const char *str)
{
    return wrap(x, y, width, px, weight, color, str, true, 0);
}

float ui_text_wrap_lines(float x, float y, float width, float px, FontWeight weight, u32 color, const char *str,
                         int max_lines)
{
    return wrap(x, y, width, px, weight, color, str, true, max_lines);
}

float ui_text_wrap_height(float width, float px, FontWeight weight, const char *str)
{
    return wrap(0, 0, width, px, weight, 0, str, false, 0);
}

/* ---------------------------------------------------------------- shapes */

void ui_rect(float x, float y, float w, float h, u32 color)
{
    C2D_DrawRectSolid(x, y, 0.3f, w, h, color);
}

/* A fan of triangles, not C2D_DrawCircleSolid: Azahar (2126.2, Vulkan and OpenGL alike) draws
 * no citro2d circles at all, and the screens are checked there before a console. */
void ui_circle(float cx, float cy, float r, u32 color)
{
    int segments = r < 4 ? 12 : r < 12 ? 20 : 32;
    float px = cx + r, py = cy;
    for (int i = 1; i <= segments; i++) {
        float a = 2 * (float)M_PI * i / segments;
        float x = cx + cosf(a) * r, y = cy + sinf(a) * r;
        C2D_DrawTriangle(cx, cy, color, px, py, color, x, y, color, 0.3f);
        px = x;
        py = y;
    }
}

/* A quarter of a disc, as a fan: the corners of a rounded rectangle. */
static void quarter(float cx, float cy, float r, float from, u32 color)
{
    int segments = r < 4 ? 3 : r < 12 ? 5 : 8;
    float px = cx + cosf(from) * r, py = cy + sinf(from) * r;
    for (int i = 1; i <= segments; i++) {
        float a = from + (float)M_PI / 2 * i / segments;
        float x = cx + cosf(a) * r, y = cy + sinf(a) * r;
        C2D_DrawTriangle(cx, cy, color, px, py, color, x, y, color, 0.3f);
        px = x;
        py = y;
    }
}

/* Built from pieces that never overlap, so a translucent colour comes out even (the title
 * plate on a banner): full circles at the corners would darken them. */
void ui_rrect(float x, float y, float w, float h, float r, u32 color)
{
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;
    ui_rect(x + r, y, w - 2 * r, h, color);
    ui_rect(x, y + r, r, h - 2 * r, color);
    ui_rect(x + w - r, y + r, r, h - 2 * r, color);
    quarter(x + r, y + r, r, (float)M_PI, color);
    quarter(x + w - r, y + r, r, (float)M_PI * 1.5f, color);
    quarter(x + w - r, y + h - r, r, 0, color);
    quarter(x + r, y + h - r, r, (float)M_PI / 2, color);
}

/* A one-pixel outline plus the shadow below: a white card on the light ground needs both to
 * stay visible at every viewing angle of the TN panels. */
void ui_card(float x, float y, float w, float h, float r, u32 color, float shadow)
{
    ui_rrect(x - 1, y - 1, w + 2, h + 2 + shadow, r + 1, COL_CARD_SHADOW);
    ui_rrect(x, y, w, h, r, color);
}

void ui_ring(float cx, float cy, float r_out, float r_in, float fraction, u32 color, u32 track)
{
    const int segments = 72;
    for (int pass = 0; pass < 2; pass++) {
        float upto = pass == 0 ? 1.0f : fraction;
        u32 c = pass == 0 ? track : color;
        int n = (int)ceilf(segments * upto);
        for (int i = 0; i < n; i++) {
            float a0 = -M_PI / 2 + 2 * M_PI * i / segments;
            float a1 = -M_PI / 2 + 2 * M_PI * fminf((i + 1.0f) / segments, upto);
            float x0o = cx + cosf(a0) * r_out, y0o = cy + sinf(a0) * r_out;
            float x1o = cx + cosf(a1) * r_out, y1o = cy + sinf(a1) * r_out;
            float x0i = cx + cosf(a0) * r_in, y0i = cy + sinf(a0) * r_in;
            float x1i = cx + cosf(a1) * r_in, y1i = cy + sinf(a1) * r_in;
            C2D_DrawTriangle(x0o, y0o, c, x1o, y1o, c, x0i, y0i, c, 0.3f);
            C2D_DrawTriangle(x1o, y1o, c, x1i, y1i, c, x0i, y0i, c, 0.3f);
        }
    }
}

/* One corner of a frame: a quarter ring from `from` radians, centred on (cx, cy). */
static void corner(float cx, float cy, float r_out, float r_in, float from, u32 color)
{
    const int segments = 8;
    for (int i = 0; i < segments; i++) {
        float a0 = from + (float)M_PI / 2 * i / segments, a1 = from + (float)M_PI / 2 * (i + 1) / segments;
        float x0o = cx + cosf(a0) * r_out, y0o = cy + sinf(a0) * r_out;
        float x1o = cx + cosf(a1) * r_out, y1o = cy + sinf(a1) * r_out;
        float x0i = cx + cosf(a0) * r_in, y0i = cy + sinf(a0) * r_in;
        float x1i = cx + cosf(a1) * r_in, y1i = cy + sinf(a1) * r_in;
        C2D_DrawTriangle(x0o, y0o, color, x1o, y1o, color, x0i, y0i, color, 0.3f);
        C2D_DrawTriangle(x1o, y1o, color, x1i, y1i, color, x0i, y0i, color, 0.3f);
    }
}

void ui_frame(float x, float y, float w, float h, float r, float t, u32 color)
{
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;
    if (r < t)
        r = t;
    ui_rect(x + r, y, w - 2 * r, t, color);
    ui_rect(x + r, y + h - t, w - 2 * r, t, color);
    ui_rect(x, y + r, t, h - 2 * r, color);
    ui_rect(x + w - t, y + r, t, h - 2 * r, color);
    corner(x + r, y + r, r, r - t, (float)M_PI, color);
    corner(x + w - r, y + r, r, r - t, (float)M_PI * 1.5f, color);
    corner(x + w - r, y + h - r, r, r - t, 0, color);
    corner(x + r, y + h - r, r, r - t, (float)M_PI / 2, color);
}

/* ---------------------------------------------------------------- icons */

/* Icons are drawn on a 24-unit grid, like the mock-up's stroke SVGs. */
typedef struct {
    float x, y, k, thick;
    u32 color;
} Pen;

static void line(const Pen *p, float x0, float y0, float x1, float y1)
{
    C2D_DrawLine(p->x + x0 * p->k, p->y + y0 * p->k, p->color, p->x + x1 * p->k, p->y + y1 * p->k, p->color, p->thick, 0.4f);
    ui_circle(p->x + x0 * p->k, p->y + y0 * p->k, p->thick / 2, p->color);
    ui_circle(p->x + x1 * p->k, p->y + y1 * p->k, p->thick / 2, p->color);
}

static void poly(const Pen *p, const float *pts, int n, bool closed)
{
    for (int i = 0; i + 1 < n; i++)
        line(p, pts[2 * i], pts[2 * i + 1], pts[2 * i + 2], pts[2 * i + 3]);
    if (closed)
        line(p, pts[2 * n - 2], pts[2 * n - 1], pts[0], pts[1]);
}

static void arc(const Pen *p, float cx, float cy, float r, float from_deg, float to_deg)
{
    const int steps = 18;
    for (int i = 0; i < steps; i++) {
        float a0 = (from_deg + (to_deg - from_deg) * i / steps) * (float)M_PI / 180;
        float a1 = (from_deg + (to_deg - from_deg) * (i + 1) / steps) * (float)M_PI / 180;
        line(p, cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r);
    }
}

void ui_icon(Icon icon, float cx, float cy, float size, u32 color)
{
    Pen p = {cx - size / 2, cy - size / 2, size / 24.0f, fmaxf(1.5f, size / 11.0f), color};
    switch (icon) {
    case ICON_CHECK: {
        const float pts[] = {5, 12.5f, 10, 17, 19, 7};
        poly(&p, pts, 3, false);
        break;
    }
    case ICON_INFO:
        line(&p, 12, 10.5f, 12, 17);
        ui_circle(cx, p.y + 6.5f * p.k, p.thick * 0.7f, color);
        break;
    case ICON_WARN: {
        const float pts[] = {12, 3.5f, 21, 19.5f, 3, 19.5f};
        poly(&p, pts, 3, true);
        line(&p, 12, 10, 12, 13.5f);
        ui_circle(cx, p.y + 16.5f * p.k, p.thick * 0.6f, color);
        break;
    }
    case ICON_DOWNLOAD: {
        const float head[] = {7, 11, 12, 16, 17, 11};
        line(&p, 12, 4, 12, 15);
        poly(&p, head, 3, false);
        line(&p, 5, 20, 19, 20);
        break;
    }
    case ICON_HELP:
        arc(&p, 12, 12, 9, 0, 360);
        arc(&p, 12, 9.5f, 2.6f, 180, 400);
        line(&p, 12, 12.2f, 12, 13.5f);
        ui_circle(cx, p.y + 17 * p.k, p.thick * 0.6f, color);
        break;
    case ICON_LIST: {
        const float tick[] = {9, 11, 12, 14, 20, 6};
        const float frame[] = {20, 12, 20, 18, 18, 20, 6, 20, 4, 18, 4, 6, 6, 4, 15, 4};
        poly(&p, frame, 8, false);
        poly(&p, tick, 3, false);
        break;
    }
    case ICON_SWAP: {
        const float a[] = {7, 4, 3, 8, 7, 12};
        const float b[] = {17, 12, 21, 16, 17, 20};
        poly(&p, a, 3, false);
        line(&p, 3, 8, 17, 8);
        poly(&p, b, 3, false);
        line(&p, 21, 16, 7, 16);
        break;
    }
    case ICON_TRASH: {
        const float can[] = {6, 7, 7, 20, 17, 20, 18, 7};
        const float lid[] = {9, 7, 9, 4, 15, 4, 15, 7};
        line(&p, 4, 7, 20, 7);
        poly(&p, lid, 4, false);
        poly(&p, can, 4, false);
        break;
    }
    case ICON_UPDATE: {
        arc(&p, 12, 12, 8, 200, 330);
        arc(&p, 12, 12, 8, 20, 150);
        const float h1[] = {18, 3, 18, 7, 14, 7};
        const float h2[] = {6, 21, 6, 17, 10, 17};
        poly(&p, h1, 3, false);
        poly(&p, h2, 3, false);
        break;
    }
    case ICON_POWER:
        line(&p, 12, 3, 12, 12);
        arc(&p, 12, 13, 7.5f, -50, 230);
        break;
    case ICON_SHIELD: {
        const float s[] = {12, 3, 20, 6, 20, 12, 17, 18, 12, 21, 7, 18, 4, 12, 4, 6};
        const float t[] = {8.5f, 12, 11, 14.5f, 15.5f, 9.5f};
        poly(&p, s, 8, true);
        poly(&p, t, 3, false);
        break;
    }
    case ICON_BATTERY: {
        const float body[] = {3, 8, 19, 8, 19, 17, 3, 17};
        poly(&p, body, 4, true);
        line(&p, 21.5f, 11, 21.5f, 14);
        break;
    }
    case ICON_SEARCH:
        arc(&p, 10.5f, 10.5f, 6.5f, 0, 360);
        line(&p, 15.5f, 15.5f, 21, 21);
        break;
    case ICON_PLAY: /* filled, like the design's */
        C2D_DrawTriangle(p.x + 7 * p.k, p.y + 4.5f * p.k, color, p.x + 7 * p.k, p.y + 19.5f * p.k, color,
                         p.x + 19.5f * p.k, p.y + 12 * p.k, color, 0.4f);
        break;
    case ICON_CROSS:
        line(&p, 6, 6, 18, 18);
        line(&p, 18, 6, 6, 18);
        break;
    case ICON_LOCK: {
        const float body[] = {5, 11, 19, 11, 19, 20, 5, 20};
        poly(&p, body, 4, true);
        arc(&p, 12, 8, 4, 180, 360);
        line(&p, 8, 8, 8, 11);
        line(&p, 16, 8, 16, 11);
        break;
    }
    }
}

void ui_spinner(float cx, float cy, float r, u32 color)
{
    const int dots = 8;
    int lead = (int)(osGetTime() / 100 % dots);
    for (int i = 0; i < dots; i++) {
        float a = 2 * (float)M_PI * i / dots - (float)M_PI / 2;
        int behind = (lead - i + dots) % dots; /* 0: the bright one, then fading */
        u32 alpha = behind < 4 ? 255 - behind * 50 : 60;
        ui_circle(cx + cosf(a) * r, cy + sinf(a) * r, r * 0.26f, (color & 0x00FFFFFF) | alpha << 24);
    }
}

static float key_cap_width(const char *key)
{
    return strlen(key) > 1 ? ui_text_width(8, FONT_HEAVY, key) + 12 : 17;
}

float ui_key_hint_width(const char *key, const char *label)
{
    return key_cap_width(key) + 4 + ui_text_width(11, FONT_BOLD, label);
}

float ui_key_hint(float x, float y, const char *key, const char *label)
{
    float h = 17;
    float w = key_cap_width(key);
    ui_rrect(x, y, w, h, h / 2, COL_INK);
    ui_text_center(x + w / 2, y + (strlen(key) > 1 ? 3.5f : 1.5f), strlen(key) > 1 ? 8 : 10, FONT_HEAVY, COL_WHITE, key);
    return w + 4 + ui_text(x + w + 4, y + 1, 11, FONT_BOLD, COL_MUTED, label);
}

void ui_logo(float x, float y, float size)
{
    /* On a 100-unit grid, like the design's SVG. */
    static const float nodes[6][2] = {{50, 14}, {81, 32}, {81, 68}, {50, 86}, {19, 68}, {19, 32}};
    bool small = size < 40;
    float k = size / 100, cx = x + size / 2, cy = y + size / 2;
    ui_rrect(x, y + size * 0.02f, size, size, size * 0.26f, COL_FLAG_SHADOW);
    ui_rrect(x, y, size, size, size * 0.26f, COL_FLAG_BLUE);
    k *= 0.82f; /* the design draws the 100-unit mark at 82% of the tile */
    for (int i = 0; i < 6; i++) {
        float nx = cx + (nodes[i][0] - 50) * k, ny = cy + (nodes[i][1] - 50) * k;
        C2D_DrawLine(cx, cy, COL_WHITE, nx, ny, COL_WHITE, (small ? 6 : 4) * k, 0.35f);
        ui_circle(nx, ny, (small ? 9 : 7) * k, COL_FLAG_YELLOW);
    }
    ui_circle(cx, cy, 20 * k, COL_FLAG_YELLOW);
    if (!small)
        ui_text_center(cx, cy - 11 * k, 20 * k, FONT_HEAVY, COL_FLAG_BLUE, "UA");
}
