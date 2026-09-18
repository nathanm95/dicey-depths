#include "game.h"
#include "ui.h"
#include "anim.h"
#include "portraits.h"

// ---------------------------------------------------------------------------
// Dicey Depths — title, menus and results.
//
// Inspired by Terry Cavanagh's Dicey Dungeons (2019). This is an original
// implementation for Pebble; none of its code or art came from that game.
// ---------------------------------------------------------------------------

static Window    *s_title_win;
static MenuLayer *s_title_menu;
static Window    *s_camp_win;
static MenuLayer *s_camp_menu;
static Window    *s_score_win;
static MenuLayer *s_score_menu;
static Window    *s_howto_win;
static ScrollLayer *s_howto_scroll;
static TextLayer   *s_howto_text;
static Window    *s_over_win;
static Layer     *s_over_layer;
static AppTimer  *s_over_frame;
static bool       s_over_won;

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
    graphics_context_set_fill_color(ctx, COL_BG);
    graphics_fill_rect(ctx, b, 0, GCornerNone);

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
    ui_draw_text_c(ctx, s_over_won ? "WINNER" : "GAME OVER", ui_font_title(), band, COL_INK,
                   GTextAlignmentCenter);

    char line[48];
    if (s_over_won) snprintf(line, sizeof(line), "Lady Luck pays out.");
    else snprintf(line, sizeof(line), "Floors cleared: %d", g_prog.score_count ? g_prog.scores[0].floors : 0);
    GRect sub = ui_band(&b, (int16_t)(cy + 16), 24, 10);
    ui_draw_text_c(ctx, line, ui_font_tiny(), sub, COL_INK, GTextAlignmentCenter);

    GRect hint = ui_band(&b, (int16_t)(cy + 42), 22, 10);
    ui_draw_text_c(ctx, "Any button to continue", ui_font_tiny(), hint, COL_FRAME, GTextAlignmentCenter);
}

static void over_dismiss(ClickRecognizerRef r, void *ctx) { window_stack_remove(s_over_win, true); }

static void over_click_config(void *ctx) {
    window_single_click_subscribe(BUTTON_ID_SELECT, over_dismiss);
    window_single_click_subscribe(BUTTON_ID_UP, over_dismiss);
    window_single_click_subscribe(BUTTON_ID_DOWN, over_dismiss);
    window_single_click_subscribe(BUTTON_ID_BACK, over_dismiss);
}

static void over_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect b = layer_get_bounds(root);
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

static void over_unload(Window *window) {
    if (s_over_frame) { app_timer_cancel(s_over_frame); s_over_frame = NULL; }
    layer_destroy(s_over_layer);
    s_over_layer = NULL;
    window_destroy(s_over_win);
    s_over_win = NULL;
    if (s_title_menu) menu_layer_reload_data(s_title_menu);
}

void gameover_window_push(bool won) {
    s_over_won = won;
    if (won) vibes_double_pulse(); else vibes_long_pulse();
    s_over_win = window_create();
    window_set_background_color(s_over_win, COL_BG);
    window_set_click_config_provider(s_over_win, over_click_config);
    window_set_window_handlers(s_over_win, (WindowHandlers) {
        .load = over_load, .unload = over_unload
    });
    window_stack_push(s_over_win, true);
}

// --- best runs -------------------------------------------------------------
static uint16_t score_rows(MenuLayer *m, uint16_t s, void *c) {
    return g_prog.score_count ? g_prog.score_count : 1;
}

static void score_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
    char t[32], s[40];
    if (!g_prog.score_count) {
        menu_cell_basic_draw(ctx, cell, "No runs yet", "Endless mode records here", NULL);
        return;
    }
    const ScoreEntry *e = &g_prog.scores[idx->row];
    snprintf(t, sizeof(t), "%d. Floor %d", idx->row + 1, e->floors);
    snprintf(s, sizeof(s), "%d gold   level %d", e->gold, e->level);
    menu_cell_basic_draw(ctx, cell, t, s, NULL);
}

static void score_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    s_score_menu = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_score_menu, NULL, (MenuLayerCallbacks) {
        .get_num_rows = score_rows, .draw_row = score_draw
    });
    menu_layer_set_highlight_colors(s_score_menu, COL_GOLD, GColorBlack);
    menu_layer_set_click_config_onto_window(s_score_menu, window);
    layer_add_child(root, menu_layer_get_layer(s_score_menu));
}

static void score_unload(Window *window) {
    menu_layer_destroy(s_score_menu);
    s_score_menu = NULL;
    window_destroy(s_score_win);
    s_score_win = NULL;
}

void scores_window_push(void) {
    s_score_win = window_create();
    window_set_background_color(s_score_win, COL_BG);
    window_set_window_handlers(s_score_win, (WindowHandlers) {
        .load = score_load, .unload = score_unload
    });
    window_stack_push(s_score_win, true);
}

// --- how to play -----------------------------------------------------------
static const char HOWTO_TEXT[] =
"You are a walking six-sided die on a game show. Roll your dice, then spend "
"them on your equipment. You always see the dice BEFORE you commit.\n\n"
"CONTROLS\n"
"Drag a die onto a card. Or tap a die, then tap a card. Or use the buttons: "
"Up/Down to move, Select to pick a die and Select again to drop it.\n"
"Tap the enemy (or hold Select) to see exactly what it is carrying.\n\n"
"CARDS\n"
"Each card takes a die that matches its slot: any, odd, even, 4+, 3- or an "
"exact number. Rare cards take two dice and use the total.\n\n"
"THE WARRIOR\n"
"Reroll any die up to three times a fight. Take damage to charge your Limit "
"Break: FURY makes your next card fire twice.\n\n"
"STATUSES\n"
"BRN Burn - a die catches fire, 2 HP to put out\n"
"FRZ Frozen - your best die becomes a 1\n"
"SHK Shocked - a card is offline, drop any die on it\n"
"WKN Weakened - one card does half\n"
"BLD Blind - dice hidden until spent\n"
"PSN Poison - 3 HP a turn, through shields\n"
"FRY Fury - your next card fires twice\n"
"SIL Silence - no Limit Break\n\n"
"THE FLOOR\n"
"Beat two of the three enemies to open the door. The third is optional - its "
"gold against your health.\n\n"
"Inspired by Dicey Dungeons by Terry Cavanagh.";

static void howto_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect b = layer_get_bounds(root);
    int16_t inset = PBL_IF_ROUND_ELSE(26, 5);
    GRect frame = GRect(inset, 0, (int16_t)(b.size.w - inset * 2), b.size.h);

    s_howto_scroll = scroll_layer_create(frame);
    scroll_layer_set_click_config_onto_window(s_howto_scroll, window);
    scroll_layer_set_shadow_hidden(s_howto_scroll, true);

    s_howto_text = text_layer_create(GRect(0, 0, frame.size.w, 4000));
    text_layer_set_text(s_howto_text, HOWTO_TEXT);
    text_layer_set_font(s_howto_text, ui_font_tiny());
    text_layer_set_background_color(s_howto_text, GColorClear);
    text_layer_set_text_color(s_howto_text, COL_INK);
    text_layer_set_overflow_mode(s_howto_text, GTextOverflowModeWordWrap);

    GSize sz = text_layer_get_content_size(s_howto_text);
    text_layer_set_size(s_howto_text, GSize(frame.size.w, (int16_t)(sz.h + 8)));
    scroll_layer_set_content_size(s_howto_scroll, GSize(frame.size.w, (int16_t)(sz.h + 16)));
    scroll_layer_add_child(s_howto_scroll, text_layer_get_layer(s_howto_text));
    layer_add_child(root, scroll_layer_get_layer(s_howto_scroll));
}

static void howto_unload(Window *window) {
    text_layer_destroy(s_howto_text);
    scroll_layer_destroy(s_howto_scroll);
    window_destroy(s_howto_win);
    s_howto_win = NULL;
}

void howto_window_push(void) {
    s_howto_win = window_create();
    window_set_background_color(s_howto_win, COL_BG);
    window_set_window_handlers(s_howto_win, (WindowHandlers) {
        .load = howto_load, .unload = howto_unload
    });
    window_stack_push(s_howto_win, true);
}

// --- campaign select -------------------------------------------------------
static uint16_t camp_rows(MenuLayer *m, uint16_t s, void *c) { return RULE_COUNT; }
static int16_t  camp_cell_h(MenuLayer *m, MenuIndex *i, void *c) { return 46; }

static void camp_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
    GRect r = layer_get_bounds(cell);
    bool locked = idx->row >= g_prog.unlocked;
    bool done   = (g_prog.cleared >> idx->row) & 1;
    // Drawing the cell by hand means honouring the highlight ourselves —
    // otherwise the selected row is dark text on a dark fill.
    bool hi = menu_cell_layer_is_highlighted(cell);
    GColor title_col = hi ? GColorWhite : (locked ? GColorDarkGray : COL_INK);
    GColor blurb_col = hi ? GColorWhite : (locked ? GColorDarkGray : COL_FRAME);

    char t[48];
    snprintf(t, sizeof(t), "%d. %s%s", idx->row + 1, rule_name((RuleId)idx->row), done ? "  *" : "");
    ui_draw_text_c(ctx, t, ui_font_small(),
                   GRect(r.origin.x + 4, r.origin.y + 1, r.size.w - 8, 18),
                   title_col, GTextAlignmentLeft);
    ui_draw_text_c(ctx, locked ? "Locked - clear the one before it" : rule_blurb((RuleId)idx->row),
                   ui_font_tiny(), GRect(r.origin.x + 4, r.origin.y + 18, r.size.w - 8, 28),
                   blurb_col, GTextAlignmentLeft);
}

static void camp_select(MenuLayer *m, MenuIndex *idx, void *c) {
    if (idx->row >= g_prog.unlocked) { vibes_short_pulse(); return; }
    run_new(MODE_CAMPAIGN, (uint8_t)idx->row);
    window_stack_remove(s_camp_win, false);
    floor_window_push();
}

static void camp_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    s_camp_menu = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_camp_menu, NULL, (MenuLayerCallbacks) {
        .get_num_rows = camp_rows, .draw_row = camp_draw,
        .select_click = camp_select, .get_cell_height = camp_cell_h
    });
    menu_layer_set_highlight_colors(s_camp_menu, COL_FOE, GColorWhite);
    menu_layer_set_click_config_onto_window(s_camp_menu, window);
    layer_add_child(root, menu_layer_get_layer(s_camp_menu));
}

static void camp_unload(Window *window) {
    menu_layer_destroy(s_camp_menu);
    s_camp_menu = NULL;
    window_destroy(s_camp_win);
    s_camp_win = NULL;
}

static void campaign_window_push(void) {
    s_camp_win = window_create();
    window_set_background_color(s_camp_win, COL_BG);
    window_set_window_handlers(s_camp_win, (WindowHandlers) {
        .load = camp_load, .unload = camp_unload
    });
    window_stack_push(s_camp_win, true);
}

// --- title -----------------------------------------------------------------
enum { ROW_CONTINUE = 0, ROW_CAMPAIGN, ROW_ENDLESS, ROW_SCORES, ROW_HOWTO, ROW_MAX };

static bool has_run(void) { return g_run.active; }
static int  title_row_for(int row) { return has_run() ? row : row + 1; }

static uint16_t title_rows(MenuLayer *m, uint16_t s, void *c) {
    return (uint16_t)(has_run() ? ROW_MAX : ROW_MAX - 1);
}

static int16_t title_hh(MenuLayer *m, uint16_t s, void *c) { return 40; }

static void title_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
    GRect r = layer_get_bounds(cell);
    ui_draw_text_c(ctx, "DICEY DEPTHS", ui_font_title(),
                   GRect(r.origin.x + 2, r.origin.y + 2, r.size.w - 4, 30), COL_INK, GTextAlignmentCenter);
}

static void title_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
    char t[32], s[40];
    switch (title_row_for(idx->row)) {
        case ROW_CONTINUE:
            snprintf(t, sizeof(t), "Continue");
            if (g_run.mode == MODE_ENDLESS) snprintf(s, sizeof(s), "Endless, floor %d", g_run.floor);
            else snprintf(s, sizeof(s), "%s, floor %d", rule_name((RuleId)g_run.rule), g_run.floor);
            break;
        case ROW_CAMPAIGN:
            snprintf(t, sizeof(t), "Campaign");
            snprintf(s, sizeof(s), "%d of %d unlocked", g_prog.unlocked, RULE_COUNT);
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
        default:
            snprintf(t, sizeof(t), "How to play");
            snprintf(s, sizeof(s), "Rules, statuses, controls");
            break;
    }
    menu_cell_basic_draw(ctx, cell, t, s, NULL);
}

static void title_select(MenuLayer *m, MenuIndex *idx, void *c) {
    switch (title_row_for(idx->row)) {
        case ROW_CONTINUE: floor_window_push(); break;
        case ROW_CAMPAIGN: campaign_window_push(); break;
        case ROW_ENDLESS:  run_new(MODE_ENDLESS, 0); floor_window_push(); break;
        case ROW_SCORES:   scores_window_push(); break;
        default:           howto_window_push(); break;
    }
}

static void title_appear(Window *window) {
    if (s_title_menu) menu_layer_reload_data(s_title_menu);
}

static void title_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    s_title_menu = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_title_menu, NULL, (MenuLayerCallbacks) {
        .get_num_rows = title_rows, .draw_row = title_draw, .select_click = title_select,
        .get_header_height = title_hh, .draw_header = title_header
    });
    menu_layer_set_highlight_colors(s_title_menu, COL_FOE, GColorWhite);
    menu_layer_set_click_config_onto_window(s_title_menu, window);
    layer_add_child(root, menu_layer_get_layer(s_title_menu));
}

static void title_unload(Window *window) {
    menu_layer_destroy(s_title_menu);
    s_title_menu = NULL;
}

int main(void) {
    srand((unsigned)time(NULL));

    // Third-party apps get no touch navigation unless they ask, and without it
    // every MenuLayer here would be button-only on a touchscreen watch. The
    // combat window opts back out for itself, because it owns its own drags.
    app_touch_navigation_enable(true);

    progress_load();
    load_run();

    s_title_win = window_create();
    window_set_background_color(s_title_win, COL_BG);
    window_set_window_handlers(s_title_win, (WindowHandlers) {
        .load = title_load, .appear = title_appear, .unload = title_unload
    });
    window_stack_push(s_title_win, true);

#ifdef DD_BOOT_COMBAT
    // Debug entry point: skip the menus and drop straight into a fight, so the
    // combat screen can be captured in an emulator that cannot send SELECT.
    run_new(MODE_CAMPAIGN, DD_BOOT_RULE);
    floor_window_push();
    combat_window_push(DD_BOOT_ENEMY);
#endif

    app_event_loop();
    window_destroy(s_title_win);
    return 0;
}
