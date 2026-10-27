/* Pieces several screens draw the same way (sizes are the design's, halved). */
#ifndef SCREENS_COMMON_H
#define SCREENS_COMMON_H

#include "../app.h"
#include "../catalog.h"
#include "../ui.h"

#define PAD 10
#define FOOT_Y (SCREEN_H - 9 - 17)

/* The key hints along the bottom: pairs of key, label; NULL ends. */
void draw_hints(const char *const *pairs);

/* The entry's icon, or until it is downloaded a square in its colour with two letters. */
void draw_item_icon(const CatItem *it, float x, float y, float size);
/* The colour an entry stands for where it has no banner or icon. */
u32 item_colour(const CatItem *it);

/* A pill with `text`, its right edge at `right`; returns its width. */
float draw_badge(float right, float y, const char *text, u32 bg, u32 fg);

/* Text cut with "…" to fit `max_w`; returns the drawn width. */
float draw_text_fit(float x, float y, float max_w, float px, FontWeight weight, u32 color, const char *str);

/* "Текст ━━━━━ 100%", or a dashed bar and «невідомо» for -1. */
void draw_progress_row(float x, float y, float w, const char *label, int pct);

/* The banner across the top screen, 400x120 (FILLING.md): the picture, or the entry's colour
 * until it is downloaded; the title and regions on a dark plate bottom left, readable on any
 * picture; the kind top right. */
#define BANNER_H 120
void draw_banner(const CatItem *it, float width);

/* Buttons (the design's 96-108 px ones, halved). */
typedef enum { BTN_PRIMARY, BTN_SECONDARY, BTN_DANGER, BTN_DANGER_SOFT, BTN_DISABLED } ButtonStyle;
void draw_button(Rect r, ButtonStyle style, Icon icon, const char *label, float px);
/* A button with a second line under its label ("застосунок закриється, гра відкриється"). */
void draw_button2(Rect r, ButtonStyle style, Icon icon, const char *label, const char *sub);

/* A green tick, an amber "!" or a red cross in a circle. */
void draw_mark(float cx, float cy, float r, Mark mark);
/* "Перед встановленням": the rows in a card; returns its height. */
float draw_check_rows(float x, float y, float w, const CheckRow *rows, int n);
/* Label on the left, value on the right, lines between: `pairs` is label, value...; a value
 * starting with '!' is drawn red. Returns the card's height. */
float draw_info_rows(float x, float y, float w, float row_h, const char *const *pairs, int n);
/* A numbered step: a blue circle with `n`, the text wrapped after it; returns the height. */
float draw_numbered(float x, float y, float w, int n, const char *text);

/* A game on the console: its HOME Menu icon, or its first letter on a grey square. */
void draw_title_icon(const App *app, int index, float x, float y, float size);
/* "New 3DS XL · 11.17.0-50E · Luma 13.1.2" (Help, the error's details) */
void format_console(char *out, size_t size, const App *app);
/* "0004000000053F00" */
void format_tid(char *out, size_t size, u64 tid);
/* "68 КБ", "41 МБ", "1,2 ГБ". */
void format_size(char *out, size_t size, u32 bytes);
/* "Патч на SD-карту" / "Окрема гра · CIA". */
const char *kind_name(Kind kind);
/* "EUR, USA" */
void format_regions(char *out, size_t size, const CatItem *it);
/* "100% · невідомо" */
void format_progress(char *out, size_t size, const CatItem *it);

#endif
