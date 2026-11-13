#include "game.h"
#include "ui.h"
#include "skin.h"

// ---------------------------------------------------------------------------
// The backpack.
//
// Equipment is managed outside combat and you choose what to bring in. With
// only four slots in the combat grid, that choice is most of the build: a card
// you cannot fit is a card you do not have.
// ---------------------------------------------------------------------------

static Window    *s_window;
static MenuLayer *s_menu;
static Layer     *s_bg;

static int pack_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_BACKPACK; i++) if (g_run.pack[i] != ITEM_NONE) n++;
    return n;
}

static int pack_index_for_row(int row) {
    int n = 0;
    for (int i = 0; i < MAX_BACKPACK; i++) {
        if (g_run.pack[i] == ITEM_NONE) continue;
        if (n == row) return i;
        n++;
    }
    return -1;
}

static uint16_t get_num_sections(MenuLayer *m, void *c) { return 2; }

static uint16_t get_num_rows(MenuLayer *m, uint16_t s, void *c) {
    return (s == 0) ? run_equip_slots() : (uint16_t)pack_count();
}

static int16_t get_header_height(MenuLayer *m, uint16_t s, void *c) { return 20; }

static void draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
    skin_header(ctx, cell, section == 0 ? "In the grid (tap to remove)" : "Spare (hold to throw away)", NULL);
}

static void item_row(uint8_t id, bool upgraded, char *t, size_t tl, char *s, size_t sl) {
    const ItemDef *d = item_def(id);
    char eff[24], c0[10], c1[10];
    item_effect_label_up(id, upgraded && !item_upgrade_widens(id), eff, sizeof(eff));
    cond_label(item_cond_up(id, 0, upgraded), c0, sizeof(c0));
    snprintf(t, tl, "%s%s", d->name, upgraded ? " +" : "");
    if (d->slots > 1) {
        cond_label(d->cond[1], c1, sizeof(c1));
        snprintf(s, sl, "%s + %s  %s", c0, c1, eff);
    } else if (d->uses) {
        snprintf(s, sl, "%s  %s  x%d/fight", c0, eff, d->uses);
    } else {
        snprintf(s, sl, "%s  %s", c0, eff);
    }
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
    char t[44], s[56];
    uint8_t flags = 0;
    if (idx->section == 0) {
        uint8_t id = g_run.equip[idx->row];
        if (id == ITEM_NONE) {
            snprintf(t, sizeof(t), "Slot %d", idx->row + 1);
            snprintf(s, sizeof(s), "empty");
            flags = SKIN_DONE;
        } else {
            item_row(id, run_is_upgraded(true, idx->row), t, sizeof(t), s, sizeof(s));
        }
    } else {
        int pi = pack_index_for_row(idx->row);
        if (pi < 0) return;
        item_row(g_run.pack[pi], run_is_upgraded(false, pi), t, sizeof(t), s, sizeof(s));
    }
    skin_cell(ctx, cell, t, s, NULL, flags);
}

static void select_row(MenuLayer *m, MenuIndex *idx, void *c) {
    if (idx->section == 0) {
        uint8_t id = g_run.equip[idx->row];
        if (id == ITEM_NONE) { vibes_short_pulse(); return; }
        // Move it to the pack, carrying its upgrade with it.
        for (int i = 0; i < MAX_BACKPACK; i++) {
            if (g_run.pack[i] != ITEM_NONE) continue;
            g_run.pack[i] = id;
            if (run_is_upgraded(true, idx->row)) run_set_upgraded(false, i);
            else run_clear_upgraded(false, i);
            g_run.equip[idx->row] = ITEM_NONE;
            run_clear_upgraded(true, idx->row);
            save_run();
            menu_layer_reload_data(s_menu);
            return;
        }
        vibes_short_pulse();    // pack is full
        return;
    }

    int pi = pack_index_for_row(idx->row);
    if (pi < 0) return;
    int slot = run_equip_free_slot();
    if (slot < 0) { vibes_short_pulse(); return; }
    g_run.equip[slot] = g_run.pack[pi];
    if (run_is_upgraded(false, pi)) run_set_upgraded(true, slot);
    else run_clear_upgraded(true, slot);
    g_run.pack[pi] = ITEM_NONE;
    run_clear_upgraded(false, pi);
    save_run();
    menu_layer_reload_data(s_menu);
}

// Hold SELECT on a spare card to throw it away. A full kit and a full pack used
// to lock out every chest and elite drop for the rest of the run, with no way
// to make room. Only spares can go: a card has to leave the grid first, so one
// long press can never strip a slot you are fighting with.
static void select_long(MenuLayer *m, MenuIndex *idx, void *c) {
    if (idx->section != 1) { vibes_short_pulse(); return; }
    int pi = pack_index_for_row(idx->row);
    if (pi < 0) return;
    g_run.pack[pi] = ITEM_NONE;
    run_clear_upgraded(false, pi);
    save_run();
    vibes_double_pulse();
    menu_layer_reload_data(s_menu);
}

static void window_appear(Window *window)    { skin_show(window, floor_backdrop(), SKIN_DUNGEON); }
static void window_disappear(Window *window) { skin_hide(window); }

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    s_bg = skin_backdrop_layer(root);
    s_menu = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks) {
        .get_num_sections = get_num_sections,
        .get_num_rows = get_num_rows,
        .draw_row = draw_row,
        .select_click = select_row,
        .select_long_click = select_long,
        .get_header_height = get_header_height,
        .draw_header = draw_header,
        .draw_background = skin_menu_background
    });
    skin_menu(s_menu);
    menu_layer_set_click_config_onto_window(s_menu, window);
    layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void window_unload(Window *window) {
    menu_layer_destroy(s_menu);
    s_menu = NULL;
    skin_backdrop_layer_destroy(s_bg);
    s_bg = NULL;
    window_destroy(s_window);
    s_window = NULL;
}

void backpack_window_push(void) {
    // Each screen keeps its window and layers in file statics. An animated pop
    // unloads only when the slide ends, so a second push landing in that gap
    // would have the old unload destroy the new window. Refuse it instead.
    if (s_window) return;
    s_window = window_create();
    window_set_background_color(s_window, GColorBlack);
    window_set_window_handlers(s_window, (WindowHandlers) {
        .load = window_load, .appear = window_appear, .disappear = window_disappear,
        .unload = window_unload
    });
    window_stack_push(s_window, true);
}
