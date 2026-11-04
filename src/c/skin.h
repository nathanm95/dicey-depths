#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// Menu skin: every list screen draws over a full-screen painted backdrop.
//
// The screens stay MenuLayers, so scrolling, the round display's centre focus
// and the system's touch navigation all keep working exactly as before. The
// rows are made see-through and the cells are drawn here instead: shadowed
// text over the art, our own highlight bar, an optional icon.
//
// Backdrops are 4-bit packed resources (tools/make_menu_art.py), not PNGs. A
// 260x260 PNG decodes through a compressed copy and an inflate buffer on top
// of the bitmap itself, which is more than the Round 2 has free. This format
// unpacks straight into the bitmap from a 64-byte read buffer. Only one is ever
// in memory: the window on top shows its own in .appear, and whichever window
// was covered lets go of it in .disappear.
// ---------------------------------------------------------------------------

typedef enum { SKIN_STAGE = 0, SKIN_DUNGEON } SkinTheme;

// Row flags for skin_cell.
#define SKIN_LOCKED  1   // greyed out; the icon is drawn as a silhouette
#define SKIN_DONE    2   // spent or beaten; dimmer text, icon kept

// Ownership is by window, not by resource: two screens may share a backdrop,
// and the covered one letting go must not pull it from under the one on top.
// Either order of .appear/.disappear leaves exactly one backdrop loaded.
void skin_show(Window *owner, uint32_t resource_id, SkinTheme theme);
void skin_hide(Window *owner);
// Frees the backdrop whoever owns it. For a screen without art that needs the
// memory: combat, whose window loads before the floor beneath it disappears.
void skin_release(void);

// Creates the layer that paints the backdrop and adds it to `root` -- call it
// before adding the MenuLayer so the menu sits on top.
Layer *skin_backdrop_layer(Layer *root);
void skin_backdrop_layer_destroy(Layer *layer);

// See-through rows; the highlight is drawn by skin_cell.
void skin_menu(MenuLayer *menu);
// Pass as MenuLayerCallbacks.draw_background on every skinned menu. Without it
// the MenuLayer fills its whole frame first -- a transparent normal colour does
// not stop that fill (PebbleOS menu_layer.c, prv_draw_background) -- and the
// backdrop disappears under a white sheet.
void skin_menu_background(GContext *ctx, const Layer *bg_layer, bool highlight, void *context);

// A standard two-line row: title, subtitle, optional icon on the left.
void skin_cell(GContext *ctx, const Layer *cell, const char *title, const char *sub,
               GBitmap *icon, uint8_t flags);
// The highlight bar on its own, for cells that lay themselves out.
void skin_cell_bg(GContext *ctx, const Layer *cell);
// One or two centred lines for a section header.
void skin_header(GContext *ctx, const Layer *cell, const char *a, const char *b);

void skin_text(GContext *ctx, const char *s, GFont font, GRect r, GColor col, GTextAlignment al);
void skin_icon(GContext *ctx, GBitmap *icon, GPoint at, bool silhouette);

GColor skin_title_col(const Layer *cell, uint8_t flags);
GColor skin_sub_col(const Layer *cell, uint8_t flags);
