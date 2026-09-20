#include "ui.h"

uint32_t ui_isqrt(uint32_t n) {
    uint32_t x = n, y = 0, bit = 1UL << 30;
    if (n == 0) return 0;
    while (bit > x) bit >>= 2;
    while (bit != 0) {
        if (x >= y + bit) { x -= y + bit; y = (y >> 1) + bit; }
        else              { y >>= 1; }
        bit >>= 2;
    }
    return y;
}

int16_t ui_half_width_at(const GRect *bounds, int16_t y) {
#if defined(PBL_ROUND)
    int16_t cx = bounds->size.w / 2, cy = bounds->size.h / 2;
    int32_t r = cx - 1;
    int32_t dy = y - cy;
    if (dy < 0) dy = -dy;
    if (dy >= r) return 0;
    return (int16_t)ui_isqrt((uint32_t)(r * r - dy * dy));
#else
    (void)y;
    return bounds->size.w / 2;
#endif
}

GRect ui_cell_inset(GRect r) {
#if defined(PBL_ROUND)
    // The inscribed square of the circle is width / sqrt(2); 707/1000 is close
    // enough and keeps it in integers.
    int16_t safe = (int16_t)(((int32_t)PBL_DISPLAY_WIDTH * 707) / 1000);
    int16_t margin = (int16_t)((PBL_DISPLAY_WIDTH - safe) / 2);
    return GRect((int16_t)(r.origin.x + margin), r.origin.y,
                 (int16_t)(r.size.w - margin * 2), r.size.h);
#else
    return r;
#endif
}

GRect ui_band(const GRect *bounds, int16_t y, int16_t h, int16_t pad) {
    // A band must fit the circle at BOTH its edges, so take the tighter of the
    // two. Measuring only the midpoint would let a tall band's corners spill
    // off the glass on the round display.
    int16_t a = ui_half_width_at(bounds, y);
    int16_t b = ui_half_width_at(bounds, y + h);
    int16_t hw = (a < b ? a : b) - pad;
    if (hw < 10) hw = 10;
    return GRect(bounds->size.w / 2 - hw, y, hw * 2, h);
}

void ui_draw_panel(GContext *ctx, GRect r, GColor fill, GColor border, bool thick) {
    if (r.size.w <= 0 || r.size.h <= 0) return;
    graphics_context_set_fill_color(ctx, fill);
    graphics_fill_rect(ctx, r, 4, GCornersAll);
    graphics_context_set_stroke_color(ctx, border);
    graphics_context_set_stroke_width(ctx, thick ? 3 : 1);
    graphics_draw_round_rect(ctx, r, 4);
    graphics_context_set_stroke_width(ctx, 1);
}

// `ghost` is the health the bar is still catching up from, drawn as a pale
// trailing block so a hit reads as a visible chunk coming off rather than a
// number quietly changing.
void ui_draw_hp_bar(GContext *ctx, GRect r, int16_t hp, int16_t ghost, int16_t max_hp, GColor fill) {
    if (max_hp <= 0) max_hp = 1;
    if (hp < 0) hp = 0;
    if (ghost < hp) ghost = hp;
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, r, 2, GCornersAll);

    int16_t inner = r.size.w - 2;
    int16_t gw = (int16_t)(((int32_t)inner * ghost) / max_hp);
    if (gw > 0) {
        graphics_context_set_fill_color(ctx, GColorLightGray);
        graphics_fill_rect(ctx, GRect(r.origin.x + 1, r.origin.y + 1, gw, r.size.h - 2), 2, GCornersAll);
    }
    int16_t w = (int16_t)(((int32_t)inner * hp) / max_hp);
    if (w > 0) {
        graphics_context_set_fill_color(ctx, fill);
        graphics_fill_rect(ctx, GRect(r.origin.x + 1, r.origin.y + 1, w, r.size.h - 2), 2, GCornersAll);
    }
    graphics_context_set_stroke_color(ctx, COL_INK);
    graphics_draw_round_rect(ctx, r, 2);
}

static const uint8_t s_pips[7][9] = {
    {0,0,0, 0,0,0, 0,0,0},
    {0,0,0, 0,1,0, 0,0,0},
    {1,0,0, 0,0,0, 0,0,1},
    {1,0,0, 0,1,0, 0,0,1},
    {1,0,1, 0,0,0, 1,0,1},
    {1,0,1, 0,1,0, 1,0,1},
    {1,0,1, 1,0,1, 1,0,1}
};

void ui_draw_die(GContext *ctx, GRect r, uint8_t pips, bool hidden, GColor face, GColor pip) {
    if (r.size.w < 6 || r.size.h < 6) return;
    graphics_context_set_fill_color(ctx, face);
    graphics_fill_rect(ctx, r, 4, GCornersAll);
    graphics_context_set_stroke_color(ctx, COL_INK);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_round_rect(ctx, r, 4);
    graphics_context_set_stroke_width(ctx, 1);

    if (hidden) {
        graphics_context_set_text_color(ctx, pip);
        graphics_draw_text(ctx, "?", ui_font_body(),
                           GRect(r.origin.x, r.origin.y + r.size.h / 2 - 13, r.size.w, r.size.h),
                           GTextOverflowModeFill, GTextAlignmentCenter, NULL);
        return;
    }
    if (pips < 1 || pips > 6) return;

    int16_t pr = r.size.w / 10;
    if (pr < 2) pr = 2;
    int16_t gap = r.size.w / 4;
    int16_t cx = r.origin.x + r.size.w / 2, cy = r.origin.y + r.size.h / 2;
    graphics_context_set_fill_color(ctx, pip);
    for (int i = 0; i < 9; i++) {
        if (!s_pips[pips][i]) continue;
        graphics_fill_circle(ctx, GPoint(cx + (i % 3 - 1) * gap, cy + (i / 3 - 1) * gap), pr);
    }
}

// A little licking flame for a burning die — three blobs that wobble out of
// phase, which is enough to read as fire at this size.
void ui_draw_flame(GContext *ctx, GRect r, uint32_t t) {
    int16_t cx = r.origin.x + r.size.w / 2;
    int16_t top = r.origin.y;
    for (int i = 0; i < 3; i++) {
        int16_t dx = (int16_t)((i - 1) * (r.size.w / 4));
        int16_t h  = (int16_t)(6 + anim_bob(360 + i * 90, 4, (uint16_t)(i * 120)));
        graphics_context_set_fill_color(ctx, i == 1 ? GColorYellow : COL_FIRE);
        graphics_fill_circle(ctx, GPoint(cx + dx, top - h / 2), (uint16_t)(3 + (i == 1 ? 1 : 0)));
    }
}

void ui_draw_text(GContext *ctx, const char *s, GFont font, GRect r, GTextAlignment align) {
    graphics_draw_text(ctx, s, font, r, GTextOverflowModeTrailingEllipsis, align, NULL);
}

void ui_draw_text_c(GContext *ctx, const char *s, GFont font, GRect r, GColor col, GTextAlignment align) {
    graphics_context_set_text_color(ctx, col);
    graphics_draw_text(ctx, s, font, r, GTextOverflowModeTrailingEllipsis, align, NULL);
}

GFont ui_font_tiny(void)  { return fonts_get_system_font(FONT_KEY_GOTHIC_14); }
GFont ui_font_small(void) { return fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD); }
GFont ui_font_body(void)  { return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD); }
GFont ui_font_title(void) { return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD); }
int16_t ui_die_size(void) { return PBL_IF_ROUND_ELSE(32, 28); }
