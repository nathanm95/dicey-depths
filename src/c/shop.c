#include "game.h"
#include "ui.h"
#include "skin.h"

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
static uint8_t    s_slot_of[SHOP_STOCK];   // shelf slot behind each row
static uint8_t    s_stock_n;
static Layer     *s_shop_bg, *s_anvil_bg, *s_trade_bg;
static GBitmap   *s_coin_ic;

static void any_disappear(Window *window)   { skin_hide(window); }
static void shop_appear(Window *window)     { skin_show(window, RESOURCE_ID_BD_DUN_SHOP, SKIN_DUNGEON); }
static void anvil_appear(Window *window)    { skin_show(window, RESOURCE_ID_BD_DUN_ANVIL, SKIN_DUNGEON); }
static void trade_appear(Window *window)    { skin_show(window, RESOURCE_ID_BD_DUN_TRADE, SKIN_DUNGEON); }

// The three counters share a menu set-up: art behind, see-through rows.
static MenuLayer *counter_menu(Window *window, Layer **bg, MenuLayerCallbacks cb) {
    Layer *root = window_get_root_layer(window);
    *bg = skin_backdrop_layer(root);
    MenuLayer *m = menu_layer_create(layer_get_bounds(root));
    cb.draw_background = skin_menu_background;
    menu_layer_set_callbacks(m, NULL, cb);
    skin_menu(m);
    menu_layer_set_click_config_onto_window(m, window);
    layer_add_child(root, menu_layer_get_layer(m));
    return m;
}

// A small deterministic hash so the same shop always holds the same goods.
static uint32_t stock_hash(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = 2166136261u;
    h = (h ^ a) * 16777619u;
    h = (h ^ b) * 16777619u;
    h = (h ^ c) * 16777619u;
    return h;
}

// The shop node's own `arg` holds which of its slots are sold, so a purchase
// survives walking out and back in (and a quit). Nothing else uses it.
static bool slot_sold(uint8_t slot) {
    return s_node < g_run.node_count && ((g_run.nodes[s_node].arg >> slot) & 1);
}

// Something that differs from run to run but is fixed for the floor: the
// enemies drawn for it. Without it every run's floor-2 shop sold the same cards.
static uint32_t run_salt(void) {
    return (uint32_t)g_run.nodes[0].arg | ((uint32_t)g_run.nodes[1].arg << 8) |
           ((uint32_t)g_run.nodes[2].arg << 16);
}

// A shop that offers you a second Sword is wasting one of its three slots.
static bool already_owned(uint8_t id) {
    for (int i = 0; i < MAX_EQUIP; i++)    if (g_run.equip[i] == id) return true;
    for (int i = 0; i < MAX_BACKPACK; i++) if (g_run.pack[i] == id) return true;
    return false;
}

static void build_stock(void) {
    unsigned saved = (unsigned)rand();
    uint8_t n = 0;
    for (int slot = 0; slot < SHOP_STOCK; slot++) {
        srand(stock_hash(g_run.floor ^ run_salt(), s_node, (uint32_t)slot));
        // A sold slot is rebuilt without the ownership check: you own what was
        // on it BECAUSE you bought it here, and filtering it out used to put a
        // fresh card on the shelf in its place -- a free restock.
        bool sold = slot_sold((uint8_t)slot);
        for (int tries = 0; tries < 24; tries++) {
            uint8_t cand = item_roll_for_floor(g_run.floor);
            if (!sold && already_owned(cand)) continue;
            bool dup = false;
            for (uint8_t j = 0; j < n; j++) if (s_stock[j] == cand) dup = true;
            if (dup) continue;
            s_stock[n] = cand;
            s_sold[n] = sold;
            s_slot_of[n] = (uint8_t)slot;
            n++;
            break;
        }
    }
    s_stock_n = n;
    srand(saved);   // put the general-purpose stream back where we found it
}

// --- shop ------------------------------------------------------------------
static uint16_t shop_rows(MenuLayer *m, uint16_t s, void *c) {
    return s_stock_n ? s_stock_n : 1;   // one row to say the shelves are bare
}
static int16_t  shop_hh(MenuLayer *m, uint16_t s, void *c)   { return 36; }

static void shop_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
    char b[32];
    snprintf(b, sizeof(b), "You have %dg", g_run.gold);
    skin_header(ctx, cell, "Shop", b);
}

static void shop_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
    if (!s_stock_n) {
        skin_cell(ctx, cell, "Bare shelves", "Nothing here you do not own", NULL, SKIN_DONE);
        return;
    }
    const ItemDef *d = item_def(s_stock[idx->row]);
    char t[44], s[56], eff[24], c0[10];
    item_effect_label(s_stock[idx->row], eff, sizeof(eff));
    cond_label(d->cond[0], c0, sizeof(c0));
    snprintf(t, sizeof(t), "%s", d->name);
    if (s_sold[idx->row]) snprintf(s, sizeof(s), "sold");
    else snprintf(s, sizeof(s), "%dg   %s %s", d->price, c0, eff);
    // Greyed when you cannot pay for it, so the shelf reads at a glance.
    uint8_t flags = s_sold[idx->row] ? SKIN_DONE : (g_run.gold < d->price ? SKIN_LOCKED : 0);
    skin_cell(ctx, cell, t, s, s_coin_ic, flags);
}

static void shop_select(MenuLayer *m, MenuIndex *idx, void *c) {
    if (!s_stock_n || idx->row >= s_stock_n) { vibes_short_pulse(); return; }
    if (s_sold[idx->row]) { vibes_short_pulse(); return; }
    const ItemDef *d = item_def(s_stock[idx->row]);
    if (g_run.gold < d->price) { vibes_short_pulse(); return; }
    if (!run_backpack_add(s_stock[idx->row])) { vibes_short_pulse(); return; }
    g_run.gold -= d->price;
    s_sold[idx->row] = true;
    if (s_node < g_run.node_count) g_run.nodes[s_node].arg |= (uint8_t)(1 << s_slot_of[idx->row]);
    save_run();
    menu_layer_reload_data(s_shop_menu);
}

static void shop_load(Window *window) {
    s_coin_ic = gbitmap_create_with_resource(RESOURCE_ID_IC_COIN);
    s_shop_menu = counter_menu(window, &s_shop_bg, (MenuLayerCallbacks) {
        .get_num_rows = shop_rows, .draw_row = shop_draw, .select_click = shop_select,
        .get_header_height = shop_hh, .draw_header = shop_header
    });
}

static void shop_unload(Window *window) {
    menu_layer_destroy(s_shop_menu);
    s_shop_menu = NULL;
    skin_backdrop_layer_destroy(s_shop_bg);
    s_shop_bg = NULL;
    if (s_coin_ic) { gbitmap_destroy(s_coin_ic); s_coin_ic = NULL; }
    window_destroy(s_shop_win);
    s_shop_win = NULL;
}

void shop_window_push(uint8_t node_index) {
    // Each screen keeps its window and layers in file statics. An animated pop
    // unloads only when the slide ends, so a second push landing in that gap
    // would have the old unload destroy the new window. Refuse it instead.
    if (s_shop_win) return;
    s_node = node_index;
    build_stock();
    s_shop_win = window_create();
    window_set_background_color(s_shop_win, GColorBlack);
    window_set_window_handlers(s_shop_win, (WindowHandlers) {
        .load = shop_load, .appear = shop_appear, .disappear = any_disappear,
        .unload = shop_unload
    });
    window_stack_push(s_shop_win, true);
}

// --- anvil -----------------------------------------------------------------
static uint16_t anvil_rows(MenuLayer *m, uint16_t s, void *c) { return run_equip_slots(); }
static int16_t  anvil_hh(MenuLayer *m, uint16_t s, void *c)   { return 36; }

static void anvil_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
    skin_header(ctx, cell, "Anvil", "Improve one card");
}

static void anvil_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
    uint8_t id = g_run.equip[idx->row];
    char t[44], s[56];
    uint8_t flags = 0;
    if (id == ITEM_NONE) {
        snprintf(t, sizeof(t), "Slot %d", idx->row + 1);
        snprintf(s, sizeof(s), "empty");
        flags = SKIN_DONE;
    } else {
        snprintf(t, sizeof(t), "%s", item_def(id)->name);
        if (run_is_upgraded(true, idx->row)) { snprintf(s, sizeof(s), "already improved"); flags = SKIN_DONE; }
        else if (!item_upgrade_useful(id)) { snprintf(s, sizeof(s), "nothing to improve"); flags = SKIN_DONE; }
        else item_upgrade_label(id, s, sizeof(s));
    }
    skin_cell(ctx, cell, t, s, NULL, flags);
}

static void anvil_select(MenuLayer *m, MenuIndex *idx, void *c) {
    uint8_t id = g_run.equip[idx->row];
    if (id == ITEM_NONE || run_is_upgraded(true, idx->row) || !item_upgrade_useful(id)) {
        vibes_short_pulse();
        return;
    }
    run_set_upgraded(true, idx->row);
    if (s_node < g_run.node_count) g_run.nodes[s_node].used = true;
    save_run();
    vibes_short_pulse();
    window_stack_remove(s_anvil_win, true);
}

static void anvil_load(Window *window) {
    s_anvil_menu = counter_menu(window, &s_anvil_bg, (MenuLayerCallbacks) {
        .get_num_rows = anvil_rows, .draw_row = anvil_draw, .select_click = anvil_select,
        .get_header_height = anvil_hh, .draw_header = anvil_header
    });
}

static void anvil_unload(Window *window) {
    menu_layer_destroy(s_anvil_menu);
    s_anvil_menu = NULL;
    skin_backdrop_layer_destroy(s_anvil_bg);
    s_anvil_bg = NULL;
    window_destroy(s_anvil_win);
    s_anvil_win = NULL;
}

void anvil_window_push(uint8_t node_index) {
    if (s_anvil_win) return;
    s_node = node_index;
    s_anvil_win = window_create();
    window_set_background_color(s_anvil_win, GColorBlack);
    window_set_window_handlers(s_anvil_win, (WindowHandlers) {
        .load = anvil_load, .appear = anvil_appear, .disappear = any_disappear,
        .unload = anvil_unload
    });
    window_stack_push(s_anvil_win, true);
}

// ---------------------------------------------------------------------------
// The trading post: give up a card you own, take a different one. No gold
// changes hands, which makes it the node you visit when your build is wrong
// rather than when your purse is full.
// ---------------------------------------------------------------------------
static Window    *s_trade_win;
static MenuLayer *s_trade_menu;
static uint8_t    s_offer[MAX_EQUIP];

static void build_offers(void) {
    unsigned saved = (unsigned)rand();
    for (uint8_t i = 0; i < MAX_EQUIP; i++) {
        srand(stock_hash(g_run.floor, (uint32_t)(s_node + 77), (uint32_t)i));
        uint8_t cand = item_roll_for_floor(g_run.floor);
        for (int tries = 0; tries < 16 && already_owned(cand); tries++) {
            cand = item_roll_for_floor(g_run.floor);
        }
        s_offer[i] = cand;
    }
    srand(saved);
}

static uint16_t trade_rows(MenuLayer *m, uint16_t s, void *c) { return run_equip_slots(); }
static int16_t  trade_hh(MenuLayer *m, uint16_t s, void *c)   { return 36; }

static void trade_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
    skin_header(ctx, cell, "Trading post", "Trade away - pick one");
}

static void trade_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
    uint8_t id = g_run.equip[idx->row];
    char t[44], sub[56];
    uint8_t flags = 0;
    if (id == ITEM_NONE) {
        snprintf(t, sizeof(t), "Slot %d", idx->row + 1);
        snprintf(sub, sizeof(sub), "empty");
        flags = SKIN_DONE;
    } else {
        uint8_t got = s_offer[idx->row];
        const ItemDef *gd = item_def(got);
        char eff[24], c0[10];
        item_effect_label(got, eff, sizeof(eff));
        cond_label(gd->cond[0], c0, sizeof(c0));
        snprintf(t, sizeof(t), "%s -> %s", item_def(id)->name, gd->name);
        snprintf(sub, sizeof(sub), "%s %s%s", c0, eff, gd->uses ? " (limited)" : "");
    }
    skin_cell(ctx, cell, t, sub, NULL, flags);
}

static void trade_select(MenuLayer *m, MenuIndex *idx, void *c) {
    if (g_run.equip[idx->row] == ITEM_NONE) { vibes_short_pulse(); return; }
    g_run.equip[idx->row] = s_offer[idx->row];
    run_clear_upgraded(true, idx->row);   // a traded card arrives unimproved
    if (s_node < g_run.node_count) g_run.nodes[s_node].used = true;
    save_run();
    vibes_short_pulse();
    window_stack_remove(s_trade_win, true);
}

static void trade_load(Window *window) {
    s_trade_menu = counter_menu(window, &s_trade_bg, (MenuLayerCallbacks) {
        .get_num_rows = trade_rows, .draw_row = trade_draw, .select_click = trade_select,
        .get_header_height = trade_hh, .draw_header = trade_header
    });
}

static void trade_unload(Window *window) {
    menu_layer_destroy(s_trade_menu);
    s_trade_menu = NULL;
    skin_backdrop_layer_destroy(s_trade_bg);
    s_trade_bg = NULL;
    window_destroy(s_trade_win);
    s_trade_win = NULL;
}

void trade_window_push(uint8_t node_index) {
    if (s_trade_win) return;
    s_node = node_index;
    build_offers();
    s_trade_win = window_create();
    window_set_background_color(s_trade_win, GColorBlack);
    window_set_window_handlers(s_trade_win, (WindowHandlers) {
        .load = trade_load, .appear = trade_appear, .disappear = any_disappear,
        .unload = trade_unload
    });
    window_stack_push(s_trade_win, true);
}
