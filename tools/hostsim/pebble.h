#pragma once
// ---------------------------------------------------------------------------
// Enough of the Pebble SDK to compile the game on the host.
//
// This exists because pebble-qemu's touch device reports contact but no
// coordinates and never a PositionUpdate, so drag, hit-testing and the whole
// tap-tap path cannot be exercised in the emulator at all. They can be
// exercised here, under AddressSanitizer, which is the only automated way we
// have to test the code a player's finger actually runs.
//
// Drawing is stubbed to nothing. The point is the state machine, not pixels.
// ---------------------------------------------------------------------------
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

typedef struct { int16_t x, y; } GPoint;
typedef struct { int16_t w, h; } GSize;
typedef struct { GPoint origin; GSize size; } GRect;
typedef struct { uint8_t argb; } GColor;
typedef struct { int16_t top, right, bottom, left; } GEdgeInsets_t;

#define GPoint(x, y)          ((GPoint){ (int16_t)(x), (int16_t)(y) })
#define GSize(w, h)           ((GSize){ (int16_t)(w), (int16_t)(h) })
#define GRect(x, y, w, h)     ((GRect){ { (int16_t)(x), (int16_t)(y) }, { (int16_t)(w), (int16_t)(h) } })
#define GEdgeInsets(...)      ((GEdgeInsets_t){ __VA_ARGS__, __VA_ARGS__, __VA_ARGS__, __VA_ARGS__ })
#define GRectZero             ((GRect){ { 0, 0 }, { 0, 0 } })

static inline GColor gcolor_from(uint8_t v) { GColor c = { v }; return c; }
// The real 64-colour table, lifted from the SDK's gcolor_definitions.h
#define GColorArmyGreen              gcolor_from(0xD4)
#define GColorBabyBlueEyes           gcolor_from(0xEB)
#define GColorBlack                  gcolor_from(0xC0)
#define GColorBlue                   gcolor_from(0xC3)
#define GColorBlueMoon               gcolor_from(0xC7)
#define GColorBrass                  gcolor_from(0xE9)
#define GColorBrightGreen            gcolor_from(0xDC)
#define GColorBrilliantRose          gcolor_from(0xF6)
#define GColorBulgarianRose          gcolor_from(0xD0)
#define GColorCadetBlue              gcolor_from(0xDA)
#define GColorCeleste                gcolor_from(0xEF)
#define GColorChromeYellow           gcolor_from(0xF8)
#define GColorCobaltBlue             gcolor_from(0xC6)
#define GColorCyan                   gcolor_from(0xCF)
#define GColorDarkCandyAppleRed      gcolor_from(0xE0)
#define GColorDarkGray               gcolor_from(0xD5)
#define GColorDarkGreen              gcolor_from(0xC4)
#define GColorDukeBlue               gcolor_from(0xC2)
#define GColorElectricBlue           gcolor_from(0xDF)
#define GColorElectricUltramarine    gcolor_from(0xD3)
#define GColorFashionMagenta         gcolor_from(0xF2)
#define GColorFolly                  gcolor_from(0xF1)
#define GColorGreen                  gcolor_from(0xCC)
#define GColorIcterine               gcolor_from(0xFD)
#define GColorImperialPurple         gcolor_from(0xD1)
#define GColorInchworm               gcolor_from(0xED)
#define GColorIndigo                 gcolor_from(0xD2)
#define GColorIslamicGreen           gcolor_from(0xC8)
#define GColorJaegerGreen            gcolor_from(0xC9)
#define GColorJazzberryJam           gcolor_from(0xE1)
#define GColorKellyGreen             gcolor_from(0xD8)
#define GColorLavenderIndigo         gcolor_from(0xE7)
#define GColorLiberty                gcolor_from(0xD6)
#define GColorLightGray              gcolor_from(0xEA)
#define GColorLimerick               gcolor_from(0xE8)
#define GColorMagenta                gcolor_from(0xF3)
#define GColorMalachite              gcolor_from(0xCD)
#define GColorMayGreen               gcolor_from(0xD9)
#define GColorMediumAquamarine       gcolor_from(0xDE)
#define GColorMediumSpringGreen      gcolor_from(0xCE)
#define GColorMelon                  gcolor_from(0xFA)
#define GColorMidnightGreen          gcolor_from(0xC5)
#define GColorMintGreen              gcolor_from(0xEE)
#define GColorOrange                 gcolor_from(0xF4)
#define GColorOxfordBlue             gcolor_from(0xC1)
#define GColorPastelYellow           gcolor_from(0xFE)
#define GColorPictonBlue             gcolor_from(0xDB)
#define GColorPurple                 gcolor_from(0xE2)
#define GColorPurpureus              gcolor_from(0xE6)
#define GColorRajah                  gcolor_from(0xF9)
#define GColorRed                    gcolor_from(0xF0)
#define GColorRichBrilliantLavender  gcolor_from(0xFB)
#define GColorRoseVale               gcolor_from(0xE5)
#define GColorScreaminGreen          gcolor_from(0xDD)
#define GColorShockingPink           gcolor_from(0xF7)
#define GColorSpringBud              gcolor_from(0xEC)
#define GColorSunsetOrange           gcolor_from(0xF5)
#define GColorTiffanyBlue            gcolor_from(0xCA)
#define GColorVeryLightBlue          gcolor_from(0xD7)
#define GColorVividCerulean          gcolor_from(0xCB)
#define GColorVividViolet            gcolor_from(0xE3)
#define GColorWhite                  gcolor_from(0xFF)
#define GColorWindsorTan             gcolor_from(0xE4)
#define GColorYellow                 gcolor_from(0xFC)

typedef enum { GTextAlignmentLeft, GTextAlignmentCenter, GTextAlignmentRight } GTextAlignment;
typedef enum { GCompOpAssign, GCompOpSet } GCompOp;
typedef enum { GCornerNone = 0, GCornersAll = 0xF } GCornerMask;
typedef void *GFont;
typedef struct GContext GContext;
typedef struct GBitmap { int _; } GBitmap;
typedef struct Layer Layer;
typedef struct Window Window;
typedef struct AppTimer { int _; } AppTimer;

// The host build is rectangular (emery). host_set_screen swaps in gabbro's
// 260x260 so the round layout gets fuzzed too.
#ifndef PBL_DISPLAY_WIDTH
#define PBL_DISPLAY_WIDTH  200
#define PBL_DISPLAY_HEIGHT 228
#endif
#ifndef PBL_IF_ROUND_ELSE
#define PBL_IF_ROUND_ELSE(r, s) (host_is_round() ? (r) : (s))
#endif
#ifndef PBL_IF_RECT_ELSE
#define PBL_IF_RECT_ELSE(r, s) (host_is_round() ? (s) : (r))
#endif
bool host_is_round(void);
void host_set_screen(int16_t w, int16_t h);
void host_set_round(bool r);

#define TRIG_MAX_ANGLE 0x10000
#define TRIG_MAX_RATIO 0xFFFF
int32_t sin_lookup(int32_t a);
int32_t cos_lookup(int32_t a);
#define DEG_TO_TRIGANGLE(d) (((d) * TRIG_MAX_ANGLE) / 360)

bool  grect_contains_point(const GRect *r, const GPoint *p);
GRect grect_inset(GRect r, GEdgeInsets_t i);

typedef enum { TouchEvent_Touchdown, TouchEvent_Liftoff, TouchEvent_PositionUpdate } TouchEventType;
typedef struct TouchEvent { TouchEventType type; bool non_navigational; int16_t x, y; } TouchEvent;
typedef void (*TouchServiceHandler)(const TouchEvent *e, void *ctx);
void touch_service_subscribe(TouchServiceHandler h, void *ctx);
void touch_service_unsubscribe(void);
bool touch_service_is_enabled(void);
void app_touch_navigation_enable(bool on);
void window_set_touch_bridge_disabled(Window *w, bool off);

typedef enum { BUTTON_ID_BACK, BUTTON_ID_UP, BUTTON_ID_SELECT, BUTTON_ID_DOWN } ButtonId;
typedef void *ClickRecognizerRef;
typedef void (*ClickHandler)(ClickRecognizerRef r, void *ctx);
typedef void (*ClickConfigProvider)(void *ctx);
void window_single_click_subscribe(ButtonId b, ClickHandler h);
void window_long_click_subscribe(ButtonId b, uint16_t ms, ClickHandler down, ClickHandler up);

typedef struct { void (*load)(Window *); void (*appear)(Window *);
                 void (*disappear)(Window *); void (*unload)(Window *); } WindowHandlers;
Window *window_create(void);
void    window_destroy(Window *w);
Layer  *window_get_root_layer(Window *w);
void    window_set_background_color(Window *w, GColor c);
void    window_set_window_handlers(Window *w, WindowHandlers h);
void    window_set_click_config_provider(Window *w, ClickConfigProvider p);
void    window_stack_push(Window *w, bool animated);
void    window_stack_remove(Window *w, bool animated);

Layer *layer_create(GRect f);
void   layer_destroy(Layer *l);
GRect  layer_get_bounds(const Layer *l);
void   layer_set_update_proc(Layer *l, void (*proc)(Layer *, GContext *));
void   layer_add_child(Layer *p, Layer *c);
void   layer_mark_dirty(Layer *l);

AppTimer *app_timer_register(uint32_t ms, void (*cb)(void *), void *ctx);
void      app_timer_cancel(AppTimer *t);

GBitmap *gbitmap_create_with_resource(uint32_t id);
void     gbitmap_destroy(GBitmap *b);
GRect    gbitmap_get_bounds(GBitmap *b);

void graphics_context_set_fill_color(GContext *c, GColor col);
void graphics_context_set_stroke_color(GContext *c, GColor col);
void graphics_context_set_stroke_width(GContext *c, uint8_t w);
void graphics_context_set_compositing_mode(GContext *c, GCompOp op);
void graphics_fill_rect(GContext *c, GRect r, uint16_t radius, GCornerMask m);
void graphics_draw_round_rect(GContext *c, GRect r, uint16_t radius);
void graphics_draw_bitmap_in_rect(GContext *c, GBitmap *b, GRect r);
void graphics_fill_circle(GContext *c, GPoint p, uint16_t r);
void graphics_draw_circle(GContext *c, GPoint p, uint16_t r);
void graphics_draw_line(GContext *c, GPoint a, GPoint b);

void vibes_short_pulse(void);
void vibes_long_pulse(void);
void vibes_double_pulse(void);

bool     persist_exists(uint32_t key);
int      persist_read_data(uint32_t key, void *buf, size_t n);
int      persist_write_data(uint32_t key, const void *buf, size_t n);
int      persist_read_int(uint32_t key);
int      persist_write_int(uint32_t key, int32_t v);
void     persist_delete(uint32_t key);
uint32_t heap_bytes_free(void);

typedef enum { GTextOverflowModeWordWrap, GTextOverflowModeTrailingEllipsis, GTextOverflowModeFill } GTextOverflowMode;
typedef struct { GFont font; GTextOverflowMode overflow_mode; GTextAlignment alignment; } GTextAttributes;
void  graphics_context_set_text_color(GContext *c, GColor col);
void  graphics_draw_text(GContext *c, const char *t, GFont f, GRect r,
                         GTextOverflowMode o, GTextAlignment a, void *attr);
GSize graphics_text_layout_get_content_size(const char *t, GFont f, GRect r,
                                            GTextOverflowMode o, GTextAlignment a);
GFont fonts_get_system_font(const char *key);
#define FONT_KEY_GOTHIC_14 "g14"
#define FONT_KEY_GOTHIC_14_BOLD "g14b"
#define FONT_KEY_GOTHIC_18 "g18"
#define FONT_KEY_GOTHIC_18_BOLD "g18b"
#define FONT_KEY_GOTHIC_24 "g24"
#define FONT_KEY_GOTHIC_24_BOLD "g24b"
#define FONT_KEY_GOTHIC_28 "g28"
#define FONT_KEY_GOTHIC_28_BOLD "g28b"
