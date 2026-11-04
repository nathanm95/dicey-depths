#include "game.h"
#include "ui.h"
#include "anim.h"
#include "skin.h"

// ---------------------------------------------------------------------------
// The splash.
//
// Dicey, the walking six-sided die you play as, drops in with a bounce and the
// title lands under him. It holds for a moment and then gets out of the way.
//
// A splash on a watch is a tax on every launch, so this one is short and any
// button or any tap skips it immediately. It is never the thing standing
// between you and a game you opened deliberately.
// ---------------------------------------------------------------------------

#define SPLASH_DROP_MS   700
#define SPLASH_HOLD_MS  2300    // total, including the drop

static Window   *s_window;
static Layer    *s_layer;
static Layer    *s_bg;
static GBitmap  *s_dicey;
static AppTimer *s_frame;
static AppTimer *s_auto;
static bool      s_leaving;

static void splash_close(void) {
    if (s_leaving || !s_window) return;
    s_leaving = true;
    window_stack_remove(s_window, true);
}

static void auto_close(void *ctx) { s_auto = NULL; splash_close(); }

static void frame(void *ctx) {
    s_frame = NULL;
    anim_tick(ANIM_FRAME_MS);
    if (s_layer) layer_mark_dirty(s_layer);
    s_frame = app_timer_register(ANIM_FRAME_MS, frame, NULL);
}

static void update(Layer *layer, GContext *ctx) {
    GRect b = layer_get_bounds(layer);
    uint32_t t = anim_now();

    // Nothing to fill: the title stage is painted behind this layer.

    // Dicey falls in and bounces to rest.
    int32_t p = (t >= SPLASH_DROP_MS) ? 1000 : (int32_t)((t * 1000) / SPLASH_DROP_MS);
    int32_t drop = ease_out_bounce(p);

    GSize ds = s_dicey ? gbitmap_get_bounds(s_dicey).size : GSize(0, 0);
    int16_t rest_y = (int16_t)(b.size.h / 2 - ds.h / 2 - PBL_IF_ROUND_ELSE(16, 18));
    int16_t y = (int16_t)lerp(-ds.h, rest_y, drop);
    // Once he has landed, keep him breathing rather than frozen.
    if (p >= 1000) y = (int16_t)(rest_y + anim_bob(1600, 3, 0));

    if (s_dicey) {
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        graphics_draw_bitmap_in_rect(ctx, s_dicey,
            GRect((int16_t)(b.size.w / 2 - ds.w / 2), y, ds.w, ds.h));
        graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    }

    // The title slides up under him once he has landed.
    int32_t tp = (t < SPLASH_DROP_MS) ? 0
               : (t > SPLASH_DROP_MS + 400 ? 1000
                  : (int32_t)(((t - SPLASH_DROP_MS) * 1000) / 400));
    if (tp > 0) {
        int16_t ty = (int16_t)lerp(b.size.h, rest_y + ds.h + 2, ease_out_quad(tp));
        GRect band = ui_band(&b, ty, 30, 6);
        skin_text(ctx, "DICEY DEPTHS", ui_font_title(), band, GColorYellow, GTextAlignmentCenter);
    }
}

static void click_skip(ClickRecognizerRef r, void *ctx) { splash_close(); }

static void click_config(void *ctx) {
    window_single_click_subscribe(BUTTON_ID_SELECT, click_skip);
    window_single_click_subscribe(BUTTON_ID_UP, click_skip);
    window_single_click_subscribe(BUTTON_ID_DOWN, click_skip);
    window_single_click_subscribe(BUTTON_ID_BACK, click_skip);
}

// Closing removes the window, whose .disappear unsubscribes from the touch
// service -- which must not happen from inside the touch service's own
// callback. So a tap closes on the next tick instead.
static void touch_close(void *ctx) { splash_close(); }

static void touch_handler(const TouchEvent *e, void *ctx) {
    if (e->non_navigational) return;    // the contact that woke the watch
    if (e->type == TouchEvent_Liftoff && !s_leaving) app_timer_register(0, touch_close, NULL);
}

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    s_bg = skin_backdrop_layer(root);
    s_layer = layer_create(layer_get_bounds(root));
    layer_set_update_proc(s_layer, update);
    layer_add_child(root, s_layer);
    s_dicey = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DICEY);
}

static void window_appear(Window *window) {
    // Dicey lands on the same stage the title shows next, so the hand-off from
    // splash to menu is the stage staying put and the menu arriving on it.
    skin_show(window, RESOURCE_ID_BD_STAGE_TITLE, SKIN_STAGE);
    anim_reset();
    // No bridge call here either -- see combat.c's window_appear.
    if (touch_service_is_enabled()) touch_service_subscribe(touch_handler, NULL);
    s_frame = app_timer_register(ANIM_FRAME_MS, frame, NULL);
    s_auto  = app_timer_register(SPLASH_HOLD_MS, auto_close, NULL);
}

static void window_disappear(Window *window) {
    skin_hide(window);
    touch_service_unsubscribe();
    if (s_frame) { app_timer_cancel(s_frame); s_frame = NULL; }
    if (s_auto)  { app_timer_cancel(s_auto);  s_auto = NULL; }
}

static void window_unload(Window *window) {
    if (s_dicey) { gbitmap_destroy(s_dicey); s_dicey = NULL; }
    layer_destroy(s_layer);
    s_layer = NULL;
    skin_backdrop_layer_destroy(s_bg);
    s_bg = NULL;
    window_destroy(s_window);
    s_window = NULL;
}

void splash_window_push(void) {
    // Each screen keeps its window and layers in file statics. An animated pop
    // unloads only when the slide ends, so a second push landing in that gap
    // would have the old unload destroy the new window. Refuse it instead.
    if (s_window) return;
    s_leaving = false;
    s_window = window_create();
    window_set_background_color(s_window, GColorBlack);
    window_set_click_config_provider(s_window, click_config);
    window_set_window_handlers(s_window, (WindowHandlers) {
        .load = window_load, .appear = window_appear,
        .disappear = window_disappear, .unload = window_unload
    });
    window_stack_push(s_window, true);
}
