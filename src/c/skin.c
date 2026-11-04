#include "skin.h"
#include "ui.h"

// The one backdrop in memory, and the window that asked for it.
static GBitmap  *s_bmp;
static uint32_t  s_rid;
static Window   *s_owner;
static SkinTheme s_theme;
static GColor    s_pal[16];   // the bitmap borrows this, so only one at a time

// --- loading ---------------------------------------------------------------
// File layout (tools/make_menu_art.py):
//   'D' 'B' version kind   w(u16 LE) h(u16 LE)   16 palette bytes (GColor8)
//   then an LZ stream that unpacks to (w+1)/2 * h bytes of 4-bit pixels,
//   leftmost pixel in the high nibble -- the GBitmap's own order.
// LZ tokens: t < 0x80 is t+1 literal bytes; otherwise copy (t & 0x7F) + 3
// bytes from `dist` back (u16 LE).
#define HDR 24

typedef struct {
    ResHandle h;
    size_t size, pos, at, n;
    uint8_t buf[128];
} Reader;

static int rd(Reader *r) {
    if (r->pos >= r->size) return -1;
    if (r->pos < r->at || r->pos >= r->at + r->n) {
        r->at = r->pos;
        size_t want = r->size - r->pos;
        if (want > sizeof(r->buf)) want = sizeof(r->buf);
        r->n = resource_load_byte_range(r->h, r->pos, r->buf, want);
        if (!r->n) return -1;
    }
    return r->buf[r->pos++ - r->at];
}

static bool unpack(Reader *r, uint8_t *dst, size_t n) {
    size_t o = 0;
    while (o < n) {
        int t = rd(r);
        if (t < 0) return false;
        if (t < 0x80) {
            for (int k = 0; k <= t; k++) {
                int v = rd(r);
                if (v < 0 || o >= n) return false;
                dst[o++] = (uint8_t)v;
            }
        } else {
            int lo = rd(r), hi = rd(r);
            if (lo < 0 || hi < 0) return false;
            size_t dist = (size_t)(lo | (hi << 8));
            size_t len = (size_t)(t & 0x7F) + 3;
            if (!dist || dist > o || o + len > n) return false;
            for (size_t k = 0; k < len; k++, o++) dst[o] = dst[o - dist];
        }
    }
    return true;
}

static GBitmap *load(uint32_t rid) {
    ResHandle h = resource_get_handle(rid);
    if (!h) return NULL;
    Reader *r = malloc(sizeof(Reader));
    if (!r) return NULL;
    memset(r, 0, sizeof(*r));
    r->h = h;
    r->size = resource_size(h);

    GBitmap *bmp = NULL;
    uint8_t hdr[HDR];
    bool ok = r->size > HDR;
    for (int k = 0; ok && k < HDR; k++) {
        int v = rd(r);
        if (v < 0) ok = false; else hdr[k] = (uint8_t)v;
    }
    if (ok && hdr[0] == 'D' && hdr[1] == 'B' && hdr[2] == 1) {
        int16_t w = (int16_t)(hdr[4] | (hdr[5] << 8));
        int16_t ht = (int16_t)(hdr[6] | (hdr[7] << 8));
        for (int k = 0; k < 16; k++) s_pal[k] = (GColor){ .argb = hdr[8 + k] };
        if (w > 0 && ht > 0 && w <= 320 && ht <= 320) {
            bmp = gbitmap_create_blank_with_palette(GSize(w, ht), GBitmapFormat4BitPalette, s_pal, false);
        }
        if (bmp) {
            size_t row = (size_t)((w + 1) / 2);
            size_t stride = gbitmap_get_bytes_per_row(bmp);
            uint8_t *px = gbitmap_get_data(bmp);
            if (stride < row || !unpack(r, px, row * (size_t)ht)) {
                gbitmap_destroy(bmp);
                bmp = NULL;
            } else if (stride != row) {
                // Unpacked tight; spread the rows out to the bitmap's stride,
                // bottom first so nothing is overwritten before it moves.
                for (int y = ht - 1; y > 0; y--) memmove(px + (size_t)y * stride, px + (size_t)y * row, row);
            }
        }
    }
    free(r);
    return bmp;
}

static void release(void) {
    if (s_bmp) gbitmap_destroy(s_bmp);
    s_bmp = NULL;
    s_rid = 0;
    s_owner = NULL;
}

void skin_show(Window *owner, uint32_t resource_id, SkinTheme theme) {
    s_theme = theme;
    if (s_bmp && s_rid == resource_id) { s_owner = owner; return; }
    release();                     // free first: never two backdrops at once
    s_bmp = load(resource_id);
    if (s_bmp) { s_rid = resource_id; s_owner = owner; }
}

void skin_hide(Window *owner) {
    if (s_owner == owner) release();
}

void skin_release(void) { release(); }

// --- the backdrop layer ----------------------------------------------------
static void bg_update(Layer *layer, GContext *ctx) {
    GRect b = layer_get_bounds(layer);
    // Painted first in case the art failed to load or is smaller than the
    // screen: the menus are light text, so they need something dark beneath.
    graphics_context_set_fill_color(ctx, s_theme == SKIN_STAGE ? GColorOxfordBlue : GColorBlack);
    graphics_fill_rect(ctx, b, 0, GCornerNone);
    if (!s_bmp) return;
    GSize s = gbitmap_get_bounds(s_bmp).size;
    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    graphics_draw_bitmap_in_rect(ctx, s_bmp,
        GRect((int16_t)((b.size.w - s.w) / 2), (int16_t)((b.size.h - s.h) / 2), s.w, s.h));
#ifdef DD_HEAP
    // Debug only: free heap with this screen's backdrop loaded.
    static char hb[16];
    snprintf(hb, sizeof(hb), "%u", (unsigned)heap_bytes_free());
    skin_text(ctx, hb, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD),
              GRect(0, (int16_t)(b.size.h - PBL_IF_ROUND_ELSE(56, 30)), b.size.w, 28),
              GColorGreen, GTextAlignmentCenter);
#endif
}

Layer *skin_backdrop_layer(Layer *root) {
    Layer *l = layer_create(layer_get_bounds(root));
    if (!l) return NULL;
    layer_set_update_proc(l, bg_update);
    layer_add_child(root, l);
    return l;
}

void skin_backdrop_layer_destroy(Layer *layer) {
    if (layer) layer_destroy(layer);
}

// --- menus -----------------------------------------------------------------
void skin_menu(MenuLayer *menu) {
    if (!menu) return;
    menu_layer_set_normal_colors(menu, GColorClear, GColorWhite);
    menu_layer_set_highlight_colors(menu, GColorClear, GColorWhite);
}

void skin_menu_background(GContext *ctx, const Layer *bg_layer, bool highlight, void *context) {
    // Deliberately empty: the backdrop layer underneath is the background.
}

void skin_text(GContext *ctx, const char *s, GFont font, GRect r, GColor col, GTextAlignment al) {
    // A one-pixel drop shadow is what keeps light text legible over painted art.
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, s, font, GRect(r.origin.x + 1, r.origin.y + 1, r.size.w, r.size.h),
                       GTextOverflowModeTrailingEllipsis, al, NULL);
    graphics_context_set_text_color(ctx, col);
    graphics_draw_text(ctx, s, font, r, GTextOverflowModeTrailingEllipsis, al, NULL);
}

void skin_icon(GContext *ctx, GBitmap *icon, GPoint at, bool silhouette) {
    if (!icon) return;
    GSize s = gbitmap_get_bounds(icon).size;
    GRect r = GRect(at.x, at.y, s.w, s.h);
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    GBitmapFormat f = gbitmap_get_format(icon);
    int n = f == GBitmapFormat1BitPalette ? 2 : f == GBitmapFormat2BitPalette ? 4
          : f == GBitmapFormat4BitPalette ? 16 : 0;
    GColor *pal = n ? gbitmap_get_palette(icon) : NULL;
    if (silhouette && pal) {
        // No tint mode on colour Pebbles, so swap the palette for the draw: every
        // visible colour becomes one flat grey, transparency is left alone.
        GColor keep[16];
        memcpy(keep, pal, (size_t)n * sizeof(GColor));
        for (int i = 0; i < n; i++) {
            if (pal[i].a) pal[i] = (GColor){ .argb = (uint8_t)((pal[i].argb & 0xC0) | 0x15) };
        }
        graphics_draw_bitmap_in_rect(ctx, icon, r);
        memcpy(pal, keep, (size_t)n * sizeof(GColor));
    } else {
        graphics_draw_bitmap_in_rect(ctx, icon, r);
    }
    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
}

GColor skin_title_col(const Layer *cell, uint8_t flags) {
    if (flags & SKIN_LOCKED) return GColorLightGray;
    if (menu_cell_layer_is_highlighted(cell)) return GColorYellow;
    if (flags & SKIN_DONE) return GColorLightGray;
    return GColorWhite;
}

GColor skin_sub_col(const Layer *cell, uint8_t flags) {
    if (flags & SKIN_LOCKED) return GColorLightGray;
    if (menu_cell_layer_is_highlighted(cell)) return GColorWhite;
    if (flags & SKIN_DONE) return GColorLightGray;
    return GColorCeleste;
}

static GRect cell_box(const Layer *cell) {
    GRect b = layer_get_bounds(cell);
    // On the round display the focused row is always at the vertical centre,
    // where the glass is nearly full width, so the bar can be wider than the
    // inscribed-square text area.
    int16_t m = PBL_IF_ROUND_ELSE(16, 3);
    return GRect((int16_t)(b.origin.x + m), (int16_t)(b.origin.y + 1),
                 (int16_t)(b.size.w - m * 2), (int16_t)(b.size.h - 2));
}

void skin_cell_bg(GContext *ctx, const Layer *cell) {
    if (!menu_cell_layer_is_highlighted(cell)) return;
    GRect r = cell_box(cell);
    if (s_theme == SKIN_STAGE) ui_draw_panel(ctx, r, GColorDarkCandyAppleRed, GColorYellow, false);
    else ui_draw_panel(ctx, r, GColorArmyGreen, GColorChromeYellow, false);
}

void skin_cell(GContext *ctx, const Layer *cell, const char *title, const char *sub,
               GBitmap *icon, uint8_t flags) {
    skin_cell_bg(ctx, cell);
    GRect r = ui_cell_inset(layer_get_bounds(cell));
    int16_t x = (int16_t)(r.origin.x + 6);
    if (icon) {
        GSize s = gbitmap_get_bounds(icon).size;
        skin_icon(ctx, icon, GPoint(x, (int16_t)(r.origin.y + (r.size.h - s.h) / 2)), flags & SKIN_LOCKED);
        x = (int16_t)(x + s.w + 5);
    }
    int16_t w = (int16_t)(r.origin.x + r.size.w - x - 4);
    // Centred on the round display unless an icon anchors the row to the left.
    GTextAlignment al = (icon || !PBL_IF_ROUND_ELSE(true, false)) ? GTextAlignmentLeft : GTextAlignmentCenter;
    bool two = sub && sub[0];
    int16_t ty = two ? (int16_t)(r.origin.y + 1) : (int16_t)(r.origin.y + (r.size.h - 24) / 2);
    skin_text(ctx, title, ui_font_body(), GRect(x, ty, w, 22), skin_title_col(cell, flags), al);
    if (two) {
        skin_text(ctx, sub, ui_font_tiny(), GRect(x, (int16_t)(r.origin.y + 21), w, r.size.h - 21),
                  skin_sub_col(cell, flags), al);
    }
}

void skin_header(GContext *ctx, const Layer *cell, const char *a, const char *b) {
    GRect r = ui_cell_inset(layer_get_bounds(cell));
    skin_text(ctx, a, ui_font_small(), GRect(r.origin.x + 2, r.origin.y, r.size.w - 4, 18),
              GColorYellow, GTextAlignmentCenter);
    if (b && b[0]) {
        skin_text(ctx, b, ui_font_tiny(), GRect(r.origin.x + 2, (int16_t)(r.origin.y + 16), r.size.w - 4, 18),
                  GColorWhite, GTextAlignmentCenter);
    }
}
