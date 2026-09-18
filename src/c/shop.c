#include "game.h"
#include "ui.h"

// ---------------------------------------------------------------------------
// Shop and anvil.
//
// Shop stock is derived from the floor and the node index rather than rolled,
// so walking out and back in does not reroll the shelves — the floor is a
// resource-allocation puzzle, and a puzzle you can reroll is not one.
// ---------------------------------------------------------------------------

#define SHOP_STOCK 3

static Window    *s_shop_win, *s_anvil_win;
static MenuLayer *s_shop_menu, *s_anvil_menu;
static uint8_t    s_node;
static uint8_t    s_stock[SHOP_STOCK];
static bool       s_sold[SHOP_STOCK];

// A small deterministic hash so the same shop always holds the same goods.
static uint32_t stock_hash(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = 2166136261u;
    h = (h ^ a) * 16777619u;
    h = (h ^ b) * 16777619u;
    h = (h ^ c) * 16777619u;
    return h;
}

static void build_stock(void) {
    unsigned saved = (unsigned)rand();
    for (int i = 0; i < SHOP_STOCK; i++) {
        srand(stock_hash(g_run.floor, s_node, (uint32_t)i));
        s_stock[i] = item_roll_for_floor(g_run.floor);
        s_sold[i] = false;
    }
    srand(saved);   // put the general-purpose stream back where we found it
}

// --- shop ------------------------------------------------------------------
static uint16_t shop_rows(MenuLayer *m, uint16_t s, void *c) { return SHOP_STOCK; }
static int16_t  shop_hh(MenuLayer *m, uint16_t s, void *c)   { return 20; }

static void shop_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
    GRect r = layer_get_bounds(cell);
    char b[32];
    snprintf(b, sizeof(b), "Shop - you have %dg", g_run.gold);
    ui_draw_text_c(ctx, b, ui_font_tiny(), GRect(r.origin.x + 4, r.origin.y, r.size.w - 8, 18),
                   COL_INK, GTextAlignmentCenter);
}

static void shop_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
    const ItemDef *d = item_def(s_stock[idx->row]);
    char t[44], s[56], eff[24], c0[10];
    item_effect_label(s_stock[idx->row], eff, sizeof(eff));
    cond_label(d->cond[0], c0, sizeof(c0));
    snprintf(t, sizeof(t), "%s", d->name);
    if (s_sold[idx->row]) snprintf(s, sizeof(s), "sold");
    else snprintf(s, sizeof(s), "%dg   %s %s", d->price, c0, eff);
    menu_cell_basic_draw(ctx, cell, t, s, NULL);
}

static void shop_select(MenuLayer *m, MenuIndex *idx, void *c) {
    if (s_sold[idx->row]) { vibes_short_pulse(); return; }
    const ItemDef *d = item_def(s_stock[idx->row]);
    if (g_run.gold < d->price) { vibes_short_pulse(); return; }
    if (!run_backpack_add(s_stock[idx->row])) { vibes_short_pulse(); return; }
    g_run.gold -= d->price;
    s_sold[idx->row] = true;
    save_run();
    menu_layer_reload_data(s_shop_menu);
}

static void shop_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    s_shop_menu = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_shop_menu, NULL, (MenuLayerCallbacks) {
        .get_num_rows = shop_rows, .draw_row = shop_draw, .select_click = shop_select,
        .get_header_height = shop_hh, .draw_header = shop_header
    });
    menu_layer_set_highlight_colors(s_shop_menu, COL_GOLD, GColorBlack);
    menu_layer_set_click_config_onto_window(s_shop_menu, window);
    layer_add_child(root, menu_layer_get_layer(s_shop_menu));
}

static void shop_unload(Window *window) {
    menu_layer_destroy(s_shop_menu);
    s_shop_menu = NULL;
    window_destroy(s_shop_win);
    s_shop_win = NULL;
}

void shop_window_push(uint8_t node_index) {
    s_node = node_index;
    build_stock();
    s_shop_win = window_create();
    window_set_background_color(s_shop_win, COL_BG);
    window_set_window_handlers(s_shop_win, (WindowHandlers) {
        .load = shop_load, .unload = shop_unload
    });
    window_stack_push(s_shop_win, true);
}

// --- anvil -----------------------------------------------------------------
static uint16_t anvil_rows(MenuLayer *m, uint16_t s, void *c) { return MAX_EQUIP; }
static int16_t  anvil_hh(MenuLayer *m, uint16_t s, void *c)   { return 20; }

static void anvil_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
    GRect r = layer_get_bounds(cell);
    ui_draw_text_c(ctx, "Anvil - improve one card", ui_font_tiny(),
                   GRect(r.origin.x + 4, r.origin.y, r.size.w - 8, 18), COL_INK, GTextAlignmentCenter);
}

static void anvil_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
    uint8_t id = g_run.equip[idx->row];
    char t[44], s[56];
    if (id == ITEM_NONE) {
        snprintf(t, sizeof(t), "Slot %d", idx->row + 1);
        snprintf(s, sizeof(s), "empty");
    } else {
        char before[24], after[24];
        item_effect_label_up(id, run_is_upgraded(true, idx->row), before, sizeof(before));
        item_effect_label_up(id, true, after, sizeof(after));
        snprintf(t, sizeof(t), "%s", item_def(id)->name);
        if (run_is_upgraded(true, idx->row)) snprintf(s, sizeof(s), "already improved");
        else snprintf(s, sizeof(s), "%s  ->  %s", before, after);
    }
    menu_cell_basic_draw(ctx, cell, t, s, NULL);
}

static void anvil_select(MenuLayer *m, MenuIndex *idx, void *c) {
    uint8_t id = g_run.equip[idx->row];
    if (id == ITEM_NONE || run_is_upgraded(true, idx->row)) { vibes_short_pulse(); return; }
    run_set_upgraded(true, idx->row);
    if (s_node < g_run.node_count) g_run.nodes[s_node].used = true;
    save_run();
    vibes_short_pulse();
    window_stack_remove(s_anvil_win, true);
}

static void anvil_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    s_anvil_menu = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_anvil_menu, NULL, (MenuLayerCallbacks) {
        .get_num_rows = anvil_rows, .draw_row = anvil_draw, .select_click = anvil_select,
        .get_header_height = anvil_hh, .draw_header = anvil_header
    });
    menu_layer_set_highlight_colors(s_anvil_menu, COL_HERO, GColorWhite);
    menu_layer_set_click_config_onto_window(s_anvil_menu, window);
    layer_add_child(root, menu_layer_get_layer(s_anvil_menu));
}

static void anvil_unload(Window *window) {
    menu_layer_destroy(s_anvil_menu);
    s_anvil_menu = NULL;
    window_destroy(s_anvil_win);
    s_anvil_win = NULL;
}

void anvil_window_push(uint8_t node_index) {
    s_node = node_index;
    s_anvil_win = window_create();
    window_set_background_color(s_anvil_win, COL_BG);
    window_set_window_handlers(s_anvil_win, (WindowHandlers) {
        .load = anvil_load, .unload = anvil_unload
    });
    window_stack_push(s_anvil_win, true);
}
