#pragma once
#include <pebble.h>
#include "game.h"
#include "anim.h"

// ---------------------------------------------------------------------------
// Shared drawing.
//
// Both targets are colour touchscreens, but one is a 260x260 circle (gabbro /
// Round 2) and the other a 200x228 rectangle (emery / Time 2). Nothing here
// hardcodes a pixel size: every layout question is answered from the layer's
// own bounds, and on the round screen from how much width the circle actually
// gives you at that y. A band near the top of a round display is barely 100px
// wide, so assuming a rectangle would push content off the glass.
// ---------------------------------------------------------------------------

// Bright, flat and high contrast. The original's art direction is mid-century
// illustration rather than roguelike grime, and a watch needs the contrast.
#define COL_BG          GColorWhite
#define COL_INK         GColorBlack
#define COL_FOE         GColorFolly
#define COL_FOE_DIM     GColorMelon
#define COL_HERO        GColorBlueMoon
#define COL_HERO_DIM    GColorCeleste
#define COL_CARD        GColorPastelYellow
#define COL_CARD_SEL    GColorIcterine
#define COL_OK          GColorJaegerGreen
#define COL_BAD         GColorRed
#define COL_FIRE        GColorOrange
#define COL_GOLD        GColorYellow
#define COL_FRAME       GColorDarkGray

int16_t ui_half_width_at(const GRect *bounds, int16_t y);

// A MenuLayer hands a cell the full layer width. On a round display that is
// wider than the glass everywhere except the vertical centre, so a cell drawn by
// hand must inset itself or its text is sliced off at both ends. Insets to the
// inscribed square, which is safe at every y. A no-op on a rectangle.
GRect ui_cell_inset(GRect r);
GRect   ui_band(const GRect *bounds, int16_t y, int16_t h, int16_t pad);
uint32_t ui_isqrt(uint32_t n);

void ui_draw_panel(GContext *ctx, GRect r, GColor fill, GColor border, bool thick);
void ui_draw_hp_bar(GContext *ctx, GRect r, int16_t hp, int16_t ghost, int16_t max_hp, GColor fill);
void ui_draw_die(GContext *ctx, GRect r, uint8_t pips, bool hidden, GColor face, GColor pip);
void ui_draw_flame(GContext *ctx, GRect r, uint32_t t);
void ui_draw_text(GContext *ctx, const char *s, GFont font, GRect r, GTextAlignment align);
void ui_draw_text_c(GContext *ctx, const char *s, GFont font, GRect r, GColor col, GTextAlignment align);

GFont   ui_font_tiny(void);
GFont   ui_font_small(void);
GFont   ui_font_body(void);
GFont   ui_font_title(void);
int16_t ui_die_size(void);
