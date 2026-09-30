// Host stubs. Drawing does nothing; geometry is real, because hit-testing is
// exactly what we are trying to test.
#include "pebble.h"
#include <math.h>

int32_t sin_lookup(int32_t a) { return (int32_t)(sin(a * 2 * M_PI / TRIG_MAX_ANGLE) * 0xFFFF); }
int32_t cos_lookup(int32_t a) { return (int32_t)(cos(a * 2 * M_PI / TRIG_MAX_ANGLE) * 0xFFFF); }

bool grect_contains_point(const GRect *r, const GPoint *p) {
    return p->x >= r->origin.x && p->x < r->origin.x + r->size.w &&
           p->y >= r->origin.y && p->y < r->origin.y + r->size.h;
}

GRect grect_inset(GRect r, GEdgeInsets_t i) {
    int32_t w = r.size.w - i.left - i.right;
    int32_t h = r.size.h - i.top - i.bottom;
    if (w < 0 || h < 0) return GRectZero;
    return GRect(r.origin.x + i.left, r.origin.y + i.top, w, h);
}

// --- everything below is deliberately inert -------------------------------
static struct Layer  { GRect b; } s_layers[64];
static struct Window { int used; }  s_windows[32];
static int s_nl, s_nw;
static GRect s_root = { { 0, 0 }, { 200, 228 } };

static bool s_round;
bool host_is_round(void) { return s_round; }
void host_set_round(bool r) { s_round = r; }
void host_set_screen(int16_t w, int16_t h) { s_root = GRect(0, 0, w, h); }

Layer *layer_create(GRect f) { Layer *l = &s_layers[s_nl++ % 64]; l->b = f; return l; }
void   layer_destroy(Layer *l) { (void)l; }
GRect  layer_get_bounds(const Layer *l) { return l ? l->b : s_root; }
void   layer_set_update_proc(Layer *l, void (*p)(Layer *, GContext *)) { (void)l; (void)p; }
void   layer_add_child(Layer *p, Layer *c) { (void)p; (void)c; }
void   layer_mark_dirty(Layer *l) { (void)l; }

Window *window_create(void) { Window *w = &s_windows[s_nw++ % 32]; return w; }
void    window_destroy(Window *w) { (void)w; }
Layer  *window_get_root_layer(Window *w) { (void)w; Layer *l = &s_layers[s_nl++ % 64]; l->b = s_root; return l; }
void    window_set_background_color(Window *w, GColor c) { (void)w; (void)c; }
void    window_set_window_handlers(Window *w, WindowHandlers h) { (void)w; (void)h; }
void    window_set_click_config_provider(Window *w, ClickConfigProvider p) { (void)w; (void)p; }
void    window_stack_push(Window *w, bool a) { (void)w; (void)a; }
void    window_stack_remove(Window *w, bool a) { (void)w; (void)a; }
void    window_set_touch_bridge_disabled(Window *w, bool o) { (void)w; (void)o; }
void    window_single_click_subscribe(ButtonId b, ClickHandler h) { (void)b; (void)h; }
void    window_long_click_subscribe(ButtonId b, uint16_t m, ClickHandler d, ClickHandler u) { (void)b; (void)m; (void)d; (void)u; }

static AppTimer s_timer;
AppTimer *app_timer_register(uint32_t ms, void (*cb)(void *), void *c) { (void)ms; (void)cb; (void)c; return &s_timer; }
void      app_timer_cancel(AppTimer *t) { (void)t; }

static GBitmap s_bmp;
GBitmap *gbitmap_create_with_resource(uint32_t id) { (void)id; return &s_bmp; }
void     gbitmap_destroy(GBitmap *b) { (void)b; }
GRect    gbitmap_get_bounds(GBitmap *b) { (void)b; return GRect(0, 0, 48, 48); }

void graphics_context_set_fill_color(GContext *c, GColor l) { (void)c; (void)l; }
void graphics_context_set_stroke_color(GContext *c, GColor l) { (void)c; (void)l; }
void graphics_context_set_stroke_width(GContext *c, uint8_t w) { (void)c; (void)w; }
void graphics_context_set_compositing_mode(GContext *c, GCompOp o) { (void)c; (void)o; }
void graphics_fill_rect(GContext *c, GRect r, uint16_t d, GCornerMask m) { (void)c; (void)r; (void)d; (void)m; }
void graphics_draw_round_rect(GContext *c, GRect r, uint16_t d) { (void)c; (void)r; (void)d; }
void graphics_draw_bitmap_in_rect(GContext *c, GBitmap *b, GRect r) { (void)c; (void)b; (void)r; }
void graphics_fill_circle(GContext *c, GPoint p, uint16_t r) { (void)c; (void)p; (void)r; }
void graphics_draw_circle(GContext *c, GPoint p, uint16_t r) { (void)c; (void)p; (void)r; }
void graphics_draw_line(GContext *c, GPoint a, GPoint b) { (void)c; (void)a; (void)b; }

void vibes_short_pulse(void) {}
void vibes_long_pulse(void) {}
void vibes_double_pulse(void) {}

static uint8_t s_persist[256][256];
static int     s_persist_len[256];
bool persist_exists(uint32_t k) { return s_persist_len[k & 0xFF] > 0; }
int  persist_read_data(uint32_t k, void *b, size_t n) {
    int have = s_persist_len[k & 0xFF];
    if (have <= 0) return 0;
    if ((int)n < have) have = (int)n;
    memcpy(b, s_persist[k & 0xFF], have); return have;
}
int persist_write_data(uint32_t k, const void *b, size_t n) {
    if (n > 256) n = 256;
    memcpy(s_persist[k & 0xFF], b, n); s_persist_len[k & 0xFF] = (int)n; return (int)n;
}
int  persist_read_int(uint32_t k) { int v = 0; persist_read_data(k, &v, sizeof v); return v; }
int  persist_write_int(uint32_t k, int32_t v) { return persist_write_data(k, &v, sizeof v); }
void persist_delete(uint32_t k) { s_persist_len[k & 0xFF] = 0; }
uint32_t heap_bytes_free(void) { return 70000; }

static TouchServiceHandler s_th;
void touch_service_subscribe(TouchServiceHandler h, void *c) { (void)c; s_th = h; }
void touch_service_unsubscribe(void) { s_th = NULL; }
bool touch_service_is_enabled(void) { return true; }
void app_touch_navigation_enable(bool on) { (void)on; }

void  graphics_context_set_text_color(GContext *c, GColor l) { (void)c; (void)l; }
void  graphics_draw_text(GContext *c, const char *t, GFont f, GRect r,
                         GTextOverflowMode o, GTextAlignment a, void *at) {
    (void)c; (void)f; (void)r; (void)o; (void)a; (void)at;
    // Touch every byte so ASan catches a string the game builds badly.
    if (t) { volatile char sink = 0; for (const char *p = t; *p; p++) sink = *p; (void)sink; }
}
GSize graphics_text_layout_get_content_size(const char *t, GFont f, GRect r,
                                            GTextOverflowMode o, GTextAlignment a) {
    (void)f; (void)o; (void)a;
    int len = t ? (int)strlen(t) : 0;
    return GSize(r.size.w, (int16_t)(14 * (1 + len / 16)));
}
static char s_font[8];
GFont fonts_get_system_font(const char *key) { (void)key; return s_font; }
