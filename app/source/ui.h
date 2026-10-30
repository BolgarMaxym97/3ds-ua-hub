/*
 * Drawing for the Hub's screens: the palette and shapes of the approved design
 * (https://claude.ai/artifact/FE7cw2U38XgMqKk3mu7ftn), at the console's own resolution: the
 * design is drawn at x2, so every size here is half. Taken from 3ds-ua's installer.
 */
#ifndef UI_H
#define UI_H

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

#define TOP_W 400
#define BOTTOM_W 320
#define SCREEN_H 240

/* Palette (mock-up), with the background and tints pushed darker: on the console's TN panels
 * the mock-up's #F6F7FA ground and white cards are one and the same colour. Packed like
 * C2D_Color32(), but usable in constant initialisers. */
#define COL_CARD_SHADOW RGBA(0xC3, 0xCA, 0xD6, 0xFF)
#define RGBA(r, g, b, a) ((u32)(r) | (u32)(g) << 8 | (u32)(b) << 16 | (u32)(a) << 24)
#define COL_BG          RGBA(0xE4, 0xE8, 0xEF, 0xFF)
#define COL_CARD        RGBA(0xFF, 0xFF, 0xFF, 0xFF)
#define COL_INK         RGBA(0x1B, 0x24, 0x33, 0xFF)
#define COL_BODY        RGBA(0x3A, 0x45, 0x56, 0xFF)
#define COL_MUTED       RGBA(0x4D, 0x58, 0x69, 0xFF)
#define COL_FAINT       RGBA(0x8A, 0x94, 0xA4, 0xFF)
#define COL_BLUE        RGBA(0x14, 0x58, 0xC7, 0xFF)
#define COL_BLUE_DARK   RGBA(0x0B, 0x3C, 0x8C, 0xFF)
#define COL_BLUE_BG     RGBA(0xD3, 0xE1, 0xF7, 0xFF)
#define COL_YELLOW      RGBA(0xF5, 0xC4, 0x00, 0xFF)
#define COL_AMBER_BG    RGBA(0xFF, 0xE7, 0xB3, 0xFF)
#define COL_AMBER_TEXT  RGBA(0x7A, 0x4A, 0x00, 0xFF)
#define COL_AMBER       RGBA(0xB8, 0x6B, 0x00, 0xFF)
#define COL_GREEN       RGBA(0x1E, 0x8E, 0x5A, 0xFF)
#define COL_GREEN_BG    RGBA(0xCB, 0xEB, 0xDA, 0xFF)
#define COL_GREEN_TEXT  RGBA(0x17, 0x66, 0x3F, 0xFF)
#define COL_RED         RGBA(0xB4, 0x23, 0x18, 0xFF)
#define COL_DISABLED    RGBA(0xC4, 0xCB, 0xD7, 0xFF)
#define COL_TRACK       RGBA(0xC6, 0xCD, 0xD9, 0xFF)
#define COL_LINE        RGBA(0xDD, 0xE2, 0xE9, 0xFF)
#define COL_FLAG_BLUE   RGBA(0x00, 0x57, 0xB7, 0xFF)
#define COL_FLAG_YELLOW RGBA(0xFF, 0xD5, 0x00, 0xFF)
#define COL_FLAG_SHADOW RGBA(0x00, 0x3D, 0x80, 0xFF)
#define COL_WHITE       RGBA(0xFF, 0xFF, 0xFF, 0xFF)

typedef enum { FONT_BOLD, FONT_HEAVY } FontWeight;

typedef enum {
    ICON_CHECK, ICON_INFO, ICON_WARN, ICON_DOWNLOAD, ICON_HELP, ICON_LIST, ICON_SWAP,
    ICON_TRASH, ICON_UPDATE, ICON_POWER, ICON_SHIELD, ICON_BATTERY, ICON_SEARCH, ICON_PLAY,
    ICON_CROSS, ICON_LOCK,
} Icon;

bool ui_init(void);
void ui_exit(void);
void ui_frame_begin(void); /* clears the per-frame text buffer */

/* Text. `px` is the em size in screen pixels. Returns the drawn width. */
float ui_text(float x, float y, float px, FontWeight weight, u32 color, const char *str);
float ui_text_center(float cx, float y, float px, FontWeight weight, u32 color, const char *str);
float ui_text_right(float rx, float y, float px, FontWeight weight, u32 color, const char *str);
/* Word-wrapped to `width`; returns the height used. */
float ui_text_wrap(float x, float y, float width, float px, FontWeight weight, u32 color, const char *str);
/* At most `max_lines` lines, the last cut with "…" when the text goes on. */
float ui_text_wrap_lines(float x, float y, float width, float px, FontWeight weight, u32 color, const char *str,
                         int max_lines);
/* The height ui_text_wrap() would use, without drawing. */
float ui_text_wrap_height(float width, float px, FontWeight weight, const char *str);
float ui_text_width(float px, FontWeight weight, const char *str);

/* Shapes. */
void ui_rect(float x, float y, float w, float h, u32 color);
void ui_rrect(float x, float y, float w, float h, float r, u32 color);
void ui_card(float x, float y, float w, float h, float r, u32 color, float shadow);
void ui_circle(float cx, float cy, float r, u32 color);
void ui_ring(float cx, float cy, float r_out, float r_in, float fraction, u32 color, u32 track);
/* An outline `t` px thick along a rounded rectangle, nothing inside: the D-pad focus. */
void ui_frame(float x, float y, float w, float h, float r, float t, u32 color);
void ui_icon(Icon icon, float cx, float cy, float size, u32 color);
/* Something under way: dots round a circle, the bright one going round. */
void ui_spinner(float cx, float cy, float r, u32 color);

/* A round key cap with a letter (A, B, Y) or a pill (START), then the label. */
float ui_key_hint(float x, float y, const char *key, const char *label);
float ui_key_hint_width(const char *key, const char *label);

/* The app's icon, «Вузол»: a blue tile, a yellow hub with UA and six nodes (H3 in the design).
 * `size` is the tile's side; below 40 px the UA is left out, as on the HOME Menu icon. */
void ui_logo(float x, float y, float size);

#endif
