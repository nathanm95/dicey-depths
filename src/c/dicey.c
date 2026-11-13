#include "game.h"
#include "ui.h"
#include "anim.h"
#include "portraits.h"
#include "achieve.h"
#include "skin.h"

// ---------------------------------------------------------------------------
// Dicey Depths — title, menus and results.
//
// Inspired by Terry Cavanagh's Dicey Dungeons (2019). This is an original
// implementation for Pebble; none of its code or art came from that game.
// ---------------------------------------------------------------------------

static Window    *s_title_win;
static MenuLayer *s_title_menu;
static Layer     *s_title_bg;
static Window    *s_camp_win;
static MenuLayer *s_camp_menu;
static Window    *s_score_win;
static MenuLayer *s_score_menu;
static Layer     *s_score_bg;
static Window    *s_howto_win;
static ScrollLayer *s_howto_scroll;
static TextLayer   *s_howto_text;
static Window    *s_over_win;
static Layer     *s_over_layer;
static Layer     *s_over_bg;
static AppTimer  *s_over_frame;
static bool       s_over_won;
static uint8_t    s_over_floor;
static bool       s_over_endless;

// --- results ---------------------------------------------------------------
#define CONFETTI 14
static struct { int16_t x, y, vy; GColor col; } s_conf[CONFETTI];

static void over_frame(void *ctx) {
    s_over_frame = NULL;
    anim_tick(ANIM_FRAME_MS);
    GRect b = layer_get_bounds(s_over_layer);
    for (int i = 0; i < CONFETTI; i++) {
        s_conf[i].y = (int16_t)(s_conf[i].y + s_conf[i].vy);
        if (s_conf[i].y > b.size.h) {
            s_conf[i].y = (int16_t)(-4 - (rand() % 30));
            s_conf[i].x = (int16_t)(rand() % b.size.w);
        }
    }
    layer_mark_dirty(s_over_layer);
    s_over_frame = app_timer_register(ANIM_FRAME_MS, over_frame, NULL);
}

static void over_update(Layer *layer, GContext *ctx) {
    GRect b = layer_get_bounds(layer);

    if (s_over_won) {
        for (int i = 0; i < CONFETTI; i++) {
            graphics_context_set_fill_color(ctx, s_conf[i].col);
            graphics_fill_rect(ctx, GRect(s_conf[i].x, s_conf[i].y, 5, 8), 1, GCornersAll);
        }
    }

    int16_t cy = (int16_t)(b.size.h / 2);
    int16_t bob = anim_bob(1200, 4, 0);

    // The winner's die keeps bouncing; the loser's lies still.
    GRect die = GRect((int16_t)(b.size.w / 2 - 22), (int16_t)(cy - 68 + (s_over_won ? bob : 0)), 44, 44);
    ui_draw_die(ctx, die, s_over_won ? 6 : 1, false,
                s_over_won ? COL_GOLD : GColorLightGray, COL_INK);

    GRect band = ui_band(&b, (int16_t)(cy - 18), 30, 8);
    skin_text(ctx, s_over_won ? "WINNER" : "GAME OVER", ui_font_title(), band,
              s_over_won ? GColorYellow : GColorWhite, GTextAlignmentCenter);

    char line[48];
    if (s_over_won) snprintf(line, sizeof(line), "Lady Luck pays out.");
    // This run's own result. It used to print the best Endless run ever, which
    // a campaign loss has nothing to do with.
    else if (s_over_endless) snprintf(line, sizeof(line), "Floors cleared: %d", s_over_floor ? s_over_floor - 1 : 0);
    else snprintf(line, sizeof(line), "Out on floor %d of %d", s_over_floor, FLOOR_COUNT);
    GRect sub = ui_band(&b, (int16_t)(cy + 16), 24, 10);
    skin_text(ctx, line, ui_font_small(), sub, GColorWhite, GTextAlignmentCenter);

    GRect hint = ui_band(&b, (int16_t)(cy + 42), 22, 10);
    skin_text(ctx, "Tap or press any button", ui_font_tiny(), hint, GColorCeleste, GTextAlignmentCenter);
}

static void over_close(void) { if (s_over_win) window_stack_remove(s_over_win, true); }

static void over_dismiss(ClickRecognizerRef r, void *ctx) { over_close(); }

// The results screen draws into a plain Layer, so the system gesture bridge has
// nothing to map and gives it no touch at all. Take the raw stream instead, or
// the only way off this screen is a button.
// Deferred a tick, like the splash: closing unsubscribes from the touch
// service, and that must not happen inside the service's own callback.
static void over_touch_close(void *ctx) { over_close(); }

static void over_touch(const TouchEvent *e, void *ctx) {
    if (e->non_navigational) return;          // the contact that woke the watch
    if (e->type != TouchEvent_Liftoff) return;
    app_timer_register(0, over_touch_close, NULL);
}

static void over_click_config(void *ctx) {
    window_single_click_subscribe(BUTTON_ID_SELECT, over_dismiss);
    window_single_click_subscribe(BUTTON_ID_UP, over_dismiss);
    window_single_click_subscribe(BUTTON_ID_DOWN, over_dismiss);
    window_single_click_subscribe(BUTTON_ID_BACK, over_dismiss);
}

static void over_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect b = layer_get_bounds(root);
    s_over_bg = skin_backdrop_layer(root);
    s_over_layer = layer_create(b);
    layer_set_update_proc(s_over_layer, over_update);
    layer_add_child(root, s_over_layer);
    for (int i = 0; i < CONFETTI; i++) {
        s_conf[i].x = (int16_t)(rand() % b.size.w);
        s_conf[i].y = (int16_t)(rand() % b.size.h);
        s_conf[i].vy = (int16_t)(2 + rand() % 4);
        GColor palette[5] = { COL_GOLD, COL_FOE, COL_HERO, COL_OK, COL_FIRE };
        s_conf[i].col = palette[rand() % 5];
    }
    s_over_frame = app_timer_register(ANIM_FRAME_MS, over_frame, NULL);
}

static void over_appear(Window *window) {
    skin_show(window, s_over_won ? RESOURCE_ID_BD_STAGE_WIN : RESOURCE_ID_BD_DUN_LOSE, SKIN_STAGE);
    // No bridge call here either -- see combat.c's window_appear.
    if (touch_service_is_enabled()) touch_service_subscribe(over_touch, NULL);
}

static void over_disappear(Window *window) {
    touch_service_unsubscribe();
    skin_hide(window);
}

static void over_unload(Window *window) {
    if (s_over_frame) { app_timer_cancel(s_over_frame); s_over_frame = NULL; }
    layer_destroy(s_over_layer);
    s_over_layer = NULL;
    skin_backdrop_layer_destroy(s_over_bg);
    s_over_bg = NULL;
    window_destroy(s_over_win);
    s_over_win = NULL;
    if (s_title_menu) menu_layer_reload_data(s_title_menu);
}

void gameover_window_push(bool won, uint8_t floor, bool endless) {
    if (s_over_win) return;
    s_over_won = won;
    s_over_floor = floor;
    s_over_endless = endless;
    if (won) vibes_double_pulse(); else vibes_long_pulse();
    s_over_win = window_create();
    window_set_background_color(s_over_win, GColorBlack);
    window_set_click_config_provider(s_over_win, over_click_config);
    window_set_window_handlers(s_over_win, (WindowHandlers) {
        .load = over_load, .appear = over_appear,
        .disappear = over_disappear, .unload = over_unload
    });
    window_stack_push(s_over_win, true);
}

// --- best runs -------------------------------------------------------------
static uint16_t score_rows(MenuLayer *m, uint16_t s, void *c) {
    return g_prog.score_count ? g_prog.score_count : 1;
}

static GBitmap *s_hall_ic[2];    // trophy for first place, medal for the rest

static void score_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
    char t[32], s[40];
    if (!g_prog.score_count) {
        skin_cell(ctx, cell, "No runs yet", "Endless mode records here", s_hall_ic[0], SKIN_LOCKED);
        return;
    }
    const ScoreEntry *e = &g_prog.scores[idx->row];
    snprintf(t, sizeof(t), "%d. Floor %d", idx->row + 1, e->floors);
    snprintf(s, sizeof(s), "%d gold   level %d", e->gold, e->level);
    skin_cell(ctx, cell, t, s, s_hall_ic[idx->row == 0 ? 0 : 1], 0);
}

static void hall_icons_load(void) {
    if (!s_hall_ic[0]) s_hall_ic[0] = gbitmap_create_with_resource(RESOURCE_ID_IC_TROPHY);
    if (!s_hall_ic[1]) s_hall_ic[1] = gbitmap_create_with_resource(RESOURCE_ID_IC_MEDAL);
}

static void hall_icons_unload(void) {
    for (int i = 0; i < 2; i++) {
        if (s_hall_ic[i]) { gbitmap_destroy(s_hall_ic[i]); s_hall_ic[i] = NULL; }
    }
}

static void hall_appear(Window *window)    { skin_show(window, RESOURCE_ID_BD_STAGE_HALL, SKIN_STAGE); }
static void hall_disappear(Window *window) { skin_hide(window); }

static void score_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    hall_icons_load();
    s_score_bg = skin_backdrop_layer(root);
    s_score_menu = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_score_menu, NULL, (MenuLayerCallbacks) {
        .get_num_rows = score_rows, .draw_row = score_draw,
        .draw_background = skin_menu_background
    });
    skin_menu(s_score_menu);
    menu_layer_set_click_config_onto_window(s_score_menu, window);
    layer_add_child(root, menu_layer_get_layer(s_score_menu));
}

static void score_unload(Window *window) {
    menu_layer_destroy(s_score_menu);
    s_score_menu = NULL;
    skin_backdrop_layer_destroy(s_score_bg);
    s_score_bg = NULL;
    hall_icons_unload();
    window_destroy(s_score_win);
    s_score_win = NULL;
}

void scores_window_push(void) {
    // Each screen keeps its window and layers in file statics. An animated pop
    // unloads only when the slide ends, so a second push landing in that gap
    // would have the old unload destroy the new window. Refuse it instead.
    if (s_score_win) return;
    s_score_win = window_create();
    window_set_background_color(s_score_win, GColorBlack);
    window_set_window_handlers(s_score_win, (WindowHandlers) {
        .load = score_load, .appear = hall_appear, .disappear = hall_disappear,
        .unload = score_unload
    });
    window_stack_push(s_score_win, true);
}

// --- achievements ----------------------------------------------------------
static Window    *s_ach_win;
static MenuLayer *s_ach_menu;
static Layer     *s_ach_bg;

static uint16_t ach_rows(MenuLayer *m, uint16_t s, void *c) { return ACH_COUNT; }

static void ach_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
    AchId id = (AchId)idx->row;
    bool got = ach_has(id);
    // A locked one still names itself; hiding the goal hides the reason to try.
    // Its medal is a grey silhouette until it is earned.
    skin_cell(ctx, cell, ach_name(id), ach_blurb(id), s_hall_ic[1], got ? 0 : SKIN_LOCKED);
}

static void ach_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    hall_icons_load();
    s_ach_bg = skin_backdrop_layer(root);
    s_ach_menu = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_ach_menu, NULL, (MenuLayerCallbacks) {
        .get_num_rows = ach_rows, .draw_row = ach_draw,
        .draw_background = skin_menu_background
    });
    skin_menu(s_ach_menu);
    menu_layer_set_click_config_onto_window(s_ach_menu, window);
    layer_add_child(root, menu_layer_get_layer(s_ach_menu));
}

static void ach_unload(Window *window) {
    menu_layer_destroy(s_ach_menu);
    s_ach_menu = NULL;
    skin_backdrop_layer_destroy(s_ach_bg);
    s_ach_bg = NULL;
    hall_icons_unload();
    window_destroy(s_ach_win);
    s_ach_win = NULL;
}

static void achievements_window_push(void) {
    if (s_ach_win) return;
    s_ach_win = window_create();
    window_set_background_color(s_ach_win, GColorBlack);
    window_set_window_handlers(s_ach_win, (WindowHandlers) {
        .load = ach_load, .appear = hall_appear, .disappear = hall_disappear,
        .unload = ach_unload
    });
    window_stack_push(s_ach_win, true);
}

// --- how to play -----------------------------------------------------------
static const char HOWTO_TEXT[] =
"You are a walking six-sided die on a game show. Roll your dice, then spend "
"them on your equipment. You always see the dice BEFORE you commit.\n\n"
"CONTROLS\n"
"Drag a die onto a card. Or tap a die, then tap a card. Or use the buttons: "
"Up/Down to move, Select to pick a die and Select again to drop it.\n"
"Tap the enemy (or hold Select) to see exactly what it is carrying.\n"
"There is no walking out of a fight. Close the app part way through one "
"and it is waiting for you when you come back.\n\n"
"CARDS\n"
"Each card takes a die that matches its slot. The little box on the card says "
"what it will accept: E for even, O for odd, 4+ for four or higher, 3- for "
"three or lower, =6 for exactly a six. An empty box takes anything.\n"
"Fussy cards pay better. A card that only wants a 1 hits far harder than one "
"that takes any die - and is dead weight on the turn you do not roll one.\n"
"Some cards take any die and pay out normally, but do something extra on a "
"six: their line reads like dmg 6:BRN, meaning damage always, and Burn as "
"well if you feed it a six.\n"
"Rare cards take two dice and use the total.\n"
"A card fires ONCE per turn, so four cards and four dice means four actions - "
"a spare die is worth nothing without a card to put it in.\n\n"
"CLASSES\n"
"The Warrior rolls dice and rerolls them. The Robot has no dice at all - it "
"builds each number on a CPU meter: PUSH adds a little, BANK locks it in, and "
"going past six loses the lot. Aiming for the exact number a card wants is the "
"Robot's whole game.\n\n"
"THE WARRIOR\n"
"You always carry a Reroll card. Feed it a die and that die is rerolled and "
"handed straight back to you, unspent. It is the one card that works more than "
"once a turn: its uses refill each turn, one for every die you carry, so a "
"bigger handful also buys you more second chances.\n"
"Take damage to charge your Limit Break: FURY makes your next card fire twice.\n\n"
"STATUSES\n"
"BRN Burn - a die catches fire, 2 HP to put out\n"
"FRZ Frozen - your best die becomes a 1\n"
"SHK Shocked - a card is offline, drop any die on it\n"
"WKN Weakened - one card does half\n"
"BLD Blind - dice hidden until spent. A blind enemy plays its dice at random\n"
"PSN Poison - 3 HP a turn, through shields\n"
"FRY Fury - your next card fires twice\n"
"SIL Silence - no Limit Break\n\n"
"LATER ON\n"
"Deeper in, you carry six cards instead of four and the grid scrolls - swipe "
"it, or just keep moving the cursor and it follows.\n\n"
"THE FLOOR\n"
"Beat two of the three enemies to open the door. The third is optional - its "
"gold against your health.\n"
"In the backpack, hold Select on a spare card to throw it away.\n\n"
"ENDLESS\n"
"No boss - just go as deep as you can. From floor 4 each floor takes one "
"episode's rule, named at the top of the floor.\n\n"
"Inspired by Dicey Dungeons by Terry Cavanagh.";

// The one menu screen with no painted backdrop: it is a page of small text,
// and over the stage art it was hard work to read.
static void howto_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect b = layer_get_bounds(root);
    // On the round display a fixed inset cannot work: the circle is narrow at the
    // top and bottom of the screen and full width in the middle, so any single
    // inset either clips the first and last lines or wastes the middle ones.
    // text flow insets each LINE to the shape of the glass instead.
    int16_t inset = PBL_IF_ROUND_ELSE(2, 5);
    GRect frame = GRect(inset, 0, (int16_t)(b.size.w - inset * 2), b.size.h);

    s_howto_scroll = scroll_layer_create(frame);
    scroll_layer_set_click_config_onto_window(s_howto_scroll, window);
    scroll_layer_set_shadow_hidden(s_howto_scroll, true);

    s_howto_text = text_layer_create(GRect(0, 0, frame.size.w, 4000));
    text_layer_set_text(s_howto_text, HOWTO_TEXT);
    text_layer_set_font(s_howto_text, ui_font_tiny());
    text_layer_set_background_color(s_howto_text, GColorClear);
    text_layer_set_text_color(s_howto_text, GColorWhite);
    text_layer_set_overflow_mode(s_howto_text, GTextOverflowModeWordWrap);

    // The layer has to be in the hierarchy before text flow is switched on,
    // because the API measures where it actually sits on the screen.
    scroll_layer_add_child(s_howto_scroll, text_layer_get_layer(s_howto_text));
    layer_add_child(root, scroll_layer_get_layer(s_howto_scroll));

#if defined(PBL_ROUND)
    text_layer_enable_screen_text_flow_and_paging(s_howto_text, 4);
    scroll_layer_set_paging(s_howto_scroll, true);
#endif

    // Measure AFTER enabling flow — it changes where every line breaks.
    GSize sz = text_layer_get_content_size(s_howto_text);
    text_layer_set_size(s_howto_text, GSize(frame.size.w, (int16_t)(sz.h + 8)));
    scroll_layer_set_content_size(s_howto_scroll, GSize(frame.size.w, (int16_t)(sz.h + 16)));
}

static void howto_unload(Window *window) {
    text_layer_destroy(s_howto_text);
    scroll_layer_destroy(s_howto_scroll);
    window_destroy(s_howto_win);
    s_howto_win = NULL;
}

void howto_window_push(void) {
    if (s_howto_win) return;
    s_howto_win = window_create();
    window_set_background_color(s_howto_win, GColorOxfordBlue);
    window_set_window_handlers(s_howto_win, (WindowHandlers) {
        .load = howto_load, .unload = howto_unload
    });
    window_stack_push(s_howto_win, true);
}

// --- class select ----------------------------------------------------------
// Both classes are available from the start. With only two of an eventual six
// there is not enough roster for unlocking to feel like progression rather than
// a locked door; gate it behind a campaign clear later if that changes.
static Window    *s_class_win;
static MenuLayer *s_class_menu;
static GBitmap   *s_class_pic[CLASS_COUNT];
static Layer     *s_class_bg;
static uint8_t    s_pending_mode, s_pending_level;

static uint16_t class_rows(MenuLayer *m, uint16_t s, void *c) { return CLASS_COUNT; }
static int16_t  class_cell_h(MenuLayer *m, MenuIndex *i, void *c) { return 58; }

static void class_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
    skin_cell_bg(ctx, cell);
    GRect r = ui_cell_inset(layer_get_bounds(cell));
    const ClassDef *cd = class_def((uint8_t)idx->row);

    // Choosing a class from two lines of prose is a poor way to meet someone.
    GBitmap *pic = (idx->row < CLASS_COUNT) ? s_class_pic[idx->row] : NULL;
    int16_t tx = (int16_t)(r.origin.x + 4);
    if (pic) {
        GSize ps = gbitmap_get_bounds(pic).size;
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        graphics_draw_bitmap_in_rect(ctx, pic,
            GRect(tx, (int16_t)(r.origin.y + 4), ps.w, ps.h));
        graphics_context_set_compositing_mode(ctx, GCompOpAssign);
        tx = (int16_t)(tx + ps.w + 4);
    }
    int16_t tw = (int16_t)(r.origin.x + r.size.w - tx - 4);
    skin_text(ctx, cd->name, ui_font_body(), GRect(tx, r.origin.y, tw, 20),
              skin_title_col(cell, 0), GTextAlignmentLeft);
    skin_text(ctx, cd->blurb, ui_font_tiny(), GRect(tx, r.origin.y + 20, tw, 38),
              skin_sub_col(cell, 0), GTextAlignmentLeft);
}

// Starting a run tears down this menu and the campaign list beneath it. Doing
// that inside this MenuLayer's own select callback destroys the layer that is
// still running the callback, so it happens on the next tick instead.
static void class_start(void *ctx) {
    if (s_class_win) window_stack_remove(s_class_win, false);
    if (s_camp_win) window_stack_remove(s_camp_win, false);
    floor_window_push();
}

// --- abandon a run? ---------------------------------------------------------
// There is one save slot, so starting a run ends the one in progress. That
// used to happen without a word the moment a class was picked -- and an
// Endless run thrown away that way never reached the best-runs table.
static Window    *s_ab_win;
static MenuLayer *s_ab_menu;
static Layer     *s_ab_bg;
static uint8_t    s_ab_class;

static uint16_t ab_rows(MenuLayer *m, uint16_t s, void *c) { return 2; }
static int16_t  ab_hh(MenuLayer *m, uint16_t s, void *c)   { return 36; }

static void ab_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
    char b[40];
    if (g_run.mode == MODE_ENDLESS) snprintf(b, sizeof(b), "Endless, floor %d", g_run.floor);
    else snprintf(b, sizeof(b), "%s, floor %d", rule_name((RuleId)g_run.rule), g_run.floor);
    skin_header(ctx, cell, "You have a run going", b);
}

static void ab_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
    if (idx->row == 0) skin_cell(ctx, cell, "Keep it", "Back to the list", NULL, 0);
    else skin_cell(ctx, cell, "Start over",
                   g_run.mode == MODE_ENDLESS ? "Its floors go on the scores" : "That run is lost",
                   NULL, 0);
}

static void ab_start(void *ctx) {
    if (s_ab_win) window_stack_remove(s_ab_win, false);
    class_start(NULL);
}

static void ab_select(MenuLayer *m, MenuIndex *idx, void *c) {
    if (idx->row == 0) { window_stack_remove(s_ab_win, true); return; }
    // An Endless run ends the way a death would end it: on the scores.
    if (g_run.active && g_run.mode == MODE_ENDLESS) progress_submit_score();
    run_new((GameMode)s_pending_mode, s_pending_level, s_ab_class);
    app_timer_register(0, ab_start, NULL);
}

static void ab_appear(Window *window)    { skin_show(window, RESOURCE_ID_BD_STAGE_CLASS, SKIN_STAGE); }
static void ab_disappear(Window *window) { skin_hide(window); }

static void ab_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    s_ab_bg = skin_backdrop_layer(root);
    s_ab_menu = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_ab_menu, NULL, (MenuLayerCallbacks) {
        .get_num_rows = ab_rows, .draw_row = ab_draw, .select_click = ab_select,
        .get_header_height = ab_hh, .draw_header = ab_header,
        .draw_background = skin_menu_background
    });
    skin_menu(s_ab_menu);
    menu_layer_set_click_config_onto_window(s_ab_menu, window);
    layer_add_child(root, menu_layer_get_layer(s_ab_menu));
}

static void ab_unload(Window *window) {
    menu_layer_destroy(s_ab_menu);
    s_ab_menu = NULL;
    skin_backdrop_layer_destroy(s_ab_bg);
    s_ab_bg = NULL;
    window_destroy(s_ab_win);
    s_ab_win = NULL;
}

static void abandon_window_push(uint8_t klass) {
    if (s_ab_win) return;
    s_ab_class = klass;
    s_ab_win = window_create();
    window_set_background_color(s_ab_win, GColorBlack);
    window_set_window_handlers(s_ab_win, (WindowHandlers) {
        .load = ab_load, .appear = ab_appear, .disappear = ab_disappear,
        .unload = ab_unload
    });
    window_stack_push(s_ab_win, true);
}

static void class_select(MenuLayer *m, MenuIndex *idx, void *c) {
    if (g_run.active) { abandon_window_push((uint8_t)idx->row); return; }
    run_new((GameMode)s_pending_mode, s_pending_level, (uint8_t)idx->row);
    app_timer_register(0, class_start, NULL);
}

static void class_appear(Window *window)    { skin_show(window, RESOURCE_ID_BD_STAGE_CLASS, SKIN_STAGE); }
static void class_disappear(Window *window) { skin_hide(window); }

static void class_load(Window *window) {
    s_class_pic[CLASS_WARRIOR] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CLASS_WARRIOR);
    s_class_pic[CLASS_ROBOT]   = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CLASS_ROBOT);
    Layer *root = window_get_root_layer(window);
    s_class_bg = skin_backdrop_layer(root);
    s_class_menu = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_class_menu, NULL, (MenuLayerCallbacks) {
        .get_num_rows = class_rows, .draw_row = class_draw,
        .select_click = class_select, .get_cell_height = class_cell_h,
        .draw_background = skin_menu_background
    });
    skin_menu(s_class_menu);
    menu_layer_set_click_config_onto_window(s_class_menu, window);
    layer_add_child(root, menu_layer_get_layer(s_class_menu));
}

static void class_unload(Window *window) {
    for (int i = 0; i < CLASS_COUNT; i++) {
        if (s_class_pic[i]) { gbitmap_destroy(s_class_pic[i]); s_class_pic[i] = NULL; }
    }
    menu_layer_destroy(s_class_menu);
    s_class_menu = NULL;
    skin_backdrop_layer_destroy(s_class_bg);
    s_class_bg = NULL;
    window_destroy(s_class_win);
    s_class_win = NULL;
}

static void class_window_push(uint8_t mode, uint8_t level) {
    if (s_class_win) return;
    s_pending_mode = mode;
    s_pending_level = level;
    s_class_win = window_create();
    window_set_background_color(s_class_win, GColorBlack);
    window_set_window_handlers(s_class_win, (WindowHandlers) {
        .load = class_load, .appear = class_appear, .disappear = class_disappear,
        .unload = class_unload
    });
    window_stack_push(s_class_win, true);
}

// --- campaign select -------------------------------------------------------
// Each episode is its own stage set, and the backdrop follows the highlighted
// row the way a game show cuts to the next set. Each row shows the boss waiting
// at the end of it -- as a silhouette until the episode is open.
static Layer   *s_camp_bg;
static GBitmap *s_camp_boss[RULE_COUNT];
static GBitmap *s_camp_lock;

static const uint32_t s_episode_bd[RULE_COUNT] = {
    RESOURCE_ID_BD_EP1_OPENING, RESOURCE_ID_BD_EP2_GREED, RESOURCE_ID_BD_EP3_PARALLEL,
    RESOURCE_ID_BD_EP4_ELIMINATION, RESOURCE_ID_BD_EP5_SUDDEN,
    RESOURCE_ID_BD_BONUS_HOARD, RESOURCE_ID_BD_BONUS_SWARM, RESOURCE_ID_BD_BONUS_BACKSTAGE
};

static uint16_t camp_rows(MenuLayer *m, uint16_t s, void *c) {
    return (uint16_t)(RULE_CAMPAIGN_COUNT +
                      (bonus_rounds_unlocked() ? (RULE_COUNT - RULE_CAMPAIGN_COUNT) : 0));
}
static int16_t  camp_cell_h(MenuLayer *m, MenuIndex *i, void *c) { return 54; }

// Bonus Rounds only appear once they are earned, so a visible one is open.
static bool camp_locked(int row) {
    return !rule_is_bonus((uint8_t)row) && row >= g_prog.unlocked;
}

static void camp_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
    skin_cell_bg(ctx, cell);
    GRect r = ui_cell_inset(layer_get_bounds(cell));
    bool locked = camp_locked(idx->row);
    // Which difficulties this episode has been cleared on, so a finished
    // campaign still has somewhere to go.
    char marks[8] = "";
    const char letter[DIFF_COUNT] = { 'N', 'E', 'H' };
    size_t mn = 0;
    for (int d = 0; d < DIFF_COUNT && mn < sizeof(marks) - 1; d++) {
        if ((g_prog.cleared[d] >> idx->row) & 1) marks[mn++] = letter[d];
    }
    marks[mn] = '\0';
    uint8_t flags = locked ? SKIN_LOCKED : 0;

    int16_t tx = (int16_t)(r.origin.x + 4);
    GBitmap *boss = (idx->row < RULE_COUNT) ? s_camp_boss[idx->row] : NULL;
    if (boss) {
        GSize bs = gbitmap_get_bounds(boss).size;
        skin_icon(ctx, boss, GPoint(tx, (int16_t)(r.origin.y + (r.size.h - bs.h) / 2)), locked);
        if (locked && s_camp_lock) {
            GSize ls = gbitmap_get_bounds(s_camp_lock).size;
            skin_icon(ctx, s_camp_lock, GPoint((int16_t)(tx + bs.w - ls.w + 2),
                                               (int16_t)(r.origin.y + r.size.h - ls.h - 1)), false);
        }
        tx = (int16_t)(tx + bs.w + 4);
    }
    int16_t tw = (int16_t)(r.origin.x + r.size.w - tx - 2);

    char t[48];
    snprintf(t, sizeof(t), "%s%s%s", rule_name((RuleId)idx->row), mn ? "  " : "", marks);
    skin_text(ctx, t, ui_font_small(), GRect(tx, r.origin.y + 1, tw, 18),
              skin_title_col(cell, flags), GTextAlignmentLeft);
    skin_text(ctx, locked ? "Locked - clear the one before it" : rule_blurb((RuleId)idx->row),
              ui_font_tiny(), GRect(tx, r.origin.y + 18, tw, 34),
              skin_sub_col(cell, flags), GTextAlignmentLeft);
}

static void camp_select(MenuLayer *m, MenuIndex *idx, void *c) {
    if (camp_locked(idx->row)) { vibes_short_pulse(); return; }
    class_window_push(MODE_CAMPAIGN, (uint8_t)idx->row);
}

static void camp_show_set(int row) {
    if (row < 0 || row >= RULE_COUNT || !s_camp_win) return;
    skin_show(s_camp_win, s_episode_bd[row], SKIN_STAGE);
    if (s_camp_bg) layer_mark_dirty(s_camp_bg);
}

static void camp_changed(MenuLayer *m, MenuIndex new_index, MenuIndex old_index, void *c) {
    camp_show_set(new_index.row);
}

static void camp_appear(Window *window) {
    camp_show_set(s_camp_menu ? menu_layer_get_selected_index(s_camp_menu).row : 0);
}
static void camp_disappear(Window *window) { skin_hide(window); }

static void camp_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    for (int i = 0; i < RULE_COUNT; i++) {
        uint32_t rid = portrait_resource_for(enemy_def(enemy_boss_for_level((uint8_t)i))->shape);
        s_camp_boss[i] = rid ? gbitmap_create_with_resource(rid) : NULL;
    }
    s_camp_lock = gbitmap_create_with_resource(RESOURCE_ID_IC_LOCK);
    s_camp_bg = skin_backdrop_layer(root);
    s_camp_menu = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_camp_menu, NULL, (MenuLayerCallbacks) {
        .get_num_rows = camp_rows, .draw_row = camp_draw,
        .select_click = camp_select, .get_cell_height = camp_cell_h,
        .selection_changed = camp_changed,
        .draw_background = skin_menu_background
    });
    skin_menu(s_camp_menu);
    menu_layer_set_click_config_onto_window(s_camp_menu, window);
    layer_add_child(root, menu_layer_get_layer(s_camp_menu));
}

static void camp_unload(Window *window) {
    menu_layer_destroy(s_camp_menu);
    s_camp_menu = NULL;
    skin_backdrop_layer_destroy(s_camp_bg);
    s_camp_bg = NULL;
    for (int i = 0; i < RULE_COUNT; i++) {
        if (s_camp_boss[i]) { gbitmap_destroy(s_camp_boss[i]); s_camp_boss[i] = NULL; }
    }
    if (s_camp_lock) { gbitmap_destroy(s_camp_lock); s_camp_lock = NULL; }
    window_destroy(s_camp_win);
    s_camp_win = NULL;
}

static void campaign_window_push(void) {
    if (s_camp_win) return;
    s_camp_win = window_create();
    window_set_background_color(s_camp_win, GColorBlack);
    window_set_window_handlers(s_camp_win, (WindowHandlers) {
        .load = camp_load, .appear = camp_appear, .disappear = camp_disappear,
        .unload = camp_unload
    });
    window_stack_push(s_camp_win, true);
}

// --- title -----------------------------------------------------------------
enum { ROW_CONTINUE = 0, ROW_CAMPAIGN, ROW_ENDLESS, ROW_SCORES,
       ROW_ACHIEVE, ROW_DIFF, ROW_HOWTO, ROW_MAX };

static bool has_run(void) { return g_run.active; }
static int  title_row_for(int row) { return has_run() ? row : row + 1; }

static uint16_t title_rows(MenuLayer *m, uint16_t s, void *c) {
    return (uint16_t)(has_run() ? ROW_MAX : ROW_MAX - 1);
}

static int16_t title_hh(MenuLayer *m, uint16_t s, void *c) { return PBL_IF_ROUND_ELSE(56, 44); }

// One icon per row, and one per difficulty for the difficulty row.
static GBitmap *s_title_ic[ROW_MAX];
static GBitmap *s_diff_ic[DIFF_COUNT];

static void title_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
    GRect r = ui_cell_inset(layer_get_bounds(cell));
    const char *label = "DICEY DEPTHS";
#ifdef DD_HEAP
    // Debug only: free heap on the title, so a fight can be measured by the
    // number before entering it and the number after coming back.
    static char hb[24];
    snprintf(hb, sizeof(hb), "HEAP %u", (unsigned)heap_bytes_free());
    label = hb;
#endif
    // A marquee title: a black outline all round, not just a drop shadow, so
    // it holds up over the brightest part of the stage.
    GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    GRect t = GRect(r.origin.x, (int16_t)(r.origin.y + PBL_IF_ROUND_ELSE(14, 2)), r.size.w, 34);
    graphics_context_set_text_color(ctx, GColorBlack);
    for (int dy = -1; dy <= 2; dy++) {
        for (int dx = -1; dx <= 2; dx++) {
            if (!dx && !dy) continue;
            graphics_draw_text(ctx, label, f, GRect(t.origin.x + dx, t.origin.y + dy, t.size.w, t.size.h),
                               GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
        }
    }
    graphics_context_set_text_color(ctx, GColorYellow);
    graphics_draw_text(ctx, label, f, t, GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void title_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
    char t[32], s[40];
    int row = title_row_for(idx->row);
    GBitmap *icon = (row == ROW_DIFF) ? s_diff_ic[difficulty_get() % DIFF_COUNT]
                  : (row >= 0 && row < ROW_MAX) ? s_title_ic[row] : NULL;
    switch (row) {
        case ROW_CONTINUE:
            snprintf(t, sizeof(t), "Continue");
            if (g_run.mode == MODE_ENDLESS) snprintf(s, sizeof(s), "Endless, floor %d", g_run.floor);
            else snprintf(s, sizeof(s), "%s, floor %d", rule_name((RuleId)g_run.rule), g_run.floor);
            break;
        case ROW_CAMPAIGN:
            snprintf(t, sizeof(t), "Campaign");
            // Count the campaign proper. RULE_COUNT includes the three Bonus
            // Rounds, which are an encore rather than part of the five.
            if (bonus_rounds_unlocked())
                snprintf(s, sizeof(s), "All %d cleared + bonus", RULE_CAMPAIGN_COUNT);
            else
                snprintf(s, sizeof(s), "%d of %d unlocked",
                         g_prog.unlocked, RULE_CAMPAIGN_COUNT);
            break;
        case ROW_ENDLESS:
            snprintf(t, sizeof(t), "Endless");
            if (g_prog.score_count) snprintf(s, sizeof(s), "Best: floor %d", g_prog.scores[0].floors);
            else snprintf(s, sizeof(s), "How deep can you get?");
            break;
        case ROW_SCORES:
            snprintf(t, sizeof(t), "Best runs");
            snprintf(s, sizeof(s), "%d recorded", g_prog.score_count);
            break;
        case ROW_ACHIEVE: {
            int got = 0;
            for (int i = 0; i < ACH_COUNT; i++) if (ach_has((AchId)i)) got++;
            snprintf(t, sizeof(t), "Achievements");
            snprintf(s, sizeof(s), "%d of %d", got, ACH_COUNT);
            break;
        }
        case ROW_DIFF:
            snprintf(t, sizeof(t), "Difficulty: %s", difficulty_name(difficulty_get()));
            snprintf(s, sizeof(s), "%s", difficulty_blurb(difficulty_get()));
            break;
        default:
            snprintf(t, sizeof(t), "How to play");
            snprintf(s, sizeof(s), "Rules, statuses, controls");
            break;
    }
    skin_cell(ctx, cell, t, s, icon, 0);
}

static void title_select(MenuLayer *m, MenuIndex *idx, void *c) {
    switch (title_row_for(idx->row)) {
        case ROW_CONTINUE: floor_window_push(); break;
        case ROW_CAMPAIGN: campaign_window_push(); break;
        case ROW_ENDLESS:  class_window_push(MODE_ENDLESS, 0); break;
        case ROW_SCORES:   scores_window_push(); break;
        case ROW_ACHIEVE:  achievements_window_push(); break;
        case ROW_DIFF:
            // Takes effect on the next run started; one already under way keeps
            // the difficulty it began with.
            difficulty_set((uint8_t)((difficulty_get() + 1) % DIFF_COUNT));
            menu_layer_reload_data(s_title_menu);
            break;
        default:           howto_window_push(); break;
    }
}

static void title_appear(Window *window) {
    skin_show(window, RESOURCE_ID_BD_STAGE_TITLE, SKIN_STAGE);
    if (s_title_menu) menu_layer_reload_data(s_title_menu);
}

static void title_disappear(Window *window) { skin_hide(window); }

static void title_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    static const uint32_t row_ic[ROW_MAX] = {
        RESOURCE_ID_IC_KEY, RESOURCE_ID_IC_FLAG, RESOURCE_ID_IC_STAIRS, RESOURCE_ID_IC_TROPHY,
        RESOURCE_ID_IC_MEDAL, 0, RESOURCE_ID_IC_BOOK
    };
    static const uint32_t diff_ic[DIFF_COUNT] = {   // Normal, Easy, Hard
        RESOURCE_ID_IC_SWORD, RESOURCE_ID_IC_FEATHER, RESOURCE_ID_IC_SKULL
    };
    for (int i = 0; i < ROW_MAX; i++) s_title_ic[i] = row_ic[i] ? gbitmap_create_with_resource(row_ic[i]) : NULL;
    for (int i = 0; i < DIFF_COUNT; i++) s_diff_ic[i] = gbitmap_create_with_resource(diff_ic[i]);
    s_title_bg = skin_backdrop_layer(root);
    s_title_menu = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_title_menu, NULL, (MenuLayerCallbacks) {
        .get_num_rows = title_rows, .draw_row = title_draw, .select_click = title_select,
        .get_header_height = title_hh, .draw_header = title_header,
        .draw_background = skin_menu_background
    });
    skin_menu(s_title_menu);
    menu_layer_set_click_config_onto_window(s_title_menu, window);
    layer_add_child(root, menu_layer_get_layer(s_title_menu));
}

static void title_unload(Window *window) {
    menu_layer_destroy(s_title_menu);
    s_title_menu = NULL;
    skin_backdrop_layer_destroy(s_title_bg);
    s_title_bg = NULL;
    for (int i = 0; i < ROW_MAX; i++) {
        if (s_title_ic[i]) { gbitmap_destroy(s_title_ic[i]); s_title_ic[i] = NULL; }
    }
    for (int i = 0; i < DIFF_COUNT; i++) {
        if (s_diff_ic[i]) { gbitmap_destroy(s_diff_ic[i]); s_diff_ic[i] = NULL; }
    }
}

int main(void) {
    srand((unsigned)time(NULL));

    // Third-party apps get no touch navigation unless they ask, and without it
    // every MenuLayer here would be button-only on a touchscreen watch. The
    // combat window opts back out for itself, because it owns its own drags.
    app_touch_navigation_enable(true);

    progress_load();
    load_run();
#ifdef DD_SHOWCASE
    // Debug only, for store recordings: a campaign part-way through, in memory
    // and never saved, so the episode list has open sets and locked bosses.
    g_prog.unlocked = 3;
    g_prog.cleared[DIFF_NORMAL] = 0x3;
    g_prog.cleared[DIFF_EASY] = 0x1;
    g_run.active = false;
#endif

    s_title_win = window_create();
    window_set_background_color(s_title_win, GColorBlack);
    window_set_window_handlers(s_title_win, (WindowHandlers) {
        .load = title_load, .appear = title_appear,
        .disappear = title_disappear, .unload = title_unload
    });
    window_stack_push(s_title_win, true);
#ifndef DD_BOOT_SCREEN
    splash_window_push();   // sits on top of the title and gets out of the way
#endif

#ifdef DD_BOOT_FLOOR
#ifndef DD_BOOT_MODE
#define DD_BOOT_MODE 0
#endif
    run_new((GameMode)DD_BOOT_MODE, 0, DD_BOOT_CLASS);
    g_run.floor = DD_BOOT_FLOOR;
    g_run.level = DD_BOOT_FLOOR;
#ifdef DD_FLOOR_RULE
    g_run.rule = DD_FLOOR_RULE;         // an Endless floor's ruleset, for the header
#endif
#ifdef DD_FLOOR_DIFF
    g_run.difficulty = DD_FLOOR_DIFF;   // 2 = Hard, so run_build_floor draws a curse
#endif
    g_run.max_hp = (int16_t)(g_run.max_hp + 30);
    g_run.hp = g_run.max_hp;
    {   // a kit that can actually finish a boss, so the WIN path is reachable
        static const uint8_t strong[MAX_EQUIP] = {14, 11, 20, 12, 8, 0};
        for (uint8_t i = 0; i < run_equip_slots() && i < MAX_EQUIP; i++)
            g_run.equip[i] = strong[i];
    }
    run_build_floor();
    save_run();
    floor_window_push();
#endif

#ifdef DD_BOOT_COMBAT
    // Debug entry point: skip the menus and drop straight into a fight, so the
    // combat screen can be captured in an emulator that cannot send SELECT.
    run_new(MODE_CAMPAIGN, DD_BOOT_RULE, DD_BOOT_CLASS);
#ifdef DD_BOOT_KIT
    {
        static const uint8_t kit[MAX_EQUIP] = {0, 1, ITEM_REROLL, 42, 47, 14};
        uint8_t n = run_equip_slots();
        for (uint8_t i = 0; i < n; i++) g_run.equip[i] = kit[i];
    }
#endif
    floor_window_push();
    combat_window_push(DD_BOOT_ENEMY, false);
#endif

#ifdef DD_BOOT_SCREEN
    // Debug only: open one menu screen directly, for screenshots and store
    // assets. The emulator cannot send SELECT reliably, and walking the menus
    // blind depends on what the last test left saved. Progress is faked in
    // memory (never saved) so the campaign list has something to show.
    g_prog.unlocked = 3;
    g_prog.cleared[DIFF_NORMAL] = 0x3;
    g_prog.cleared[DIFF_EASY] = 0x1;
    if (DD_BOOT_SCREEN >= 8) {
        run_new(MODE_CAMPAIGN, 0, 0);
        g_run.floor = (uint8_t)(DD_BOOT_ROW ? DD_BOOT_ROW : 1);
        g_run.gold = 40;
        run_build_floor();
        floor_window_push();
    }
    switch (DD_BOOT_SCREEN) {
        case 1: campaign_window_push();
                if (DD_BOOT_ROW && s_camp_menu)
                    menu_layer_set_selected_index(s_camp_menu, MenuIndex(0, DD_BOOT_ROW), MenuRowAlignCenter, false);
                break;
        case 2: class_window_push(MODE_CAMPAIGN, 0); break;
        case 3: scores_window_push(); break;
        case 4: achievements_window_push(); break;
        case 5: howto_window_push(); break;
        case 6: gameover_window_push(true, 3, false); break;
        case 7: gameover_window_push(false, 2, false); break;
        // Put the counter on a real node of its kind: the shop keeps its sold
        // marks in its node's arg, so borrowing an enemy node shows nonsense.
        case 8: case 9: case 10: {
            static const uint8_t kinds[3] = { NODE_SHOP, NODE_ANVIL, NODE_TRADE };
            g_run.nodes[3].kind = kinds[DD_BOOT_SCREEN - 8];
            g_run.nodes[3].arg = 0;
            g_run.nodes[3].used = false;
            if (DD_BOOT_SCREEN == 8) shop_window_push(3);
            else if (DD_BOOT_SCREEN == 9) anvil_window_push(3);
            else trade_window_push(3);
            break;
        }
        case 11: backpack_window_push(); break;
        default: break;   // 12: the floor itself
    }
#endif

    app_event_loop();
    window_destroy(s_title_win);
    return 0;
}
