#include "game.h"
#include "ui.h"

// ---------------------------------------------------------------------------
// The backpack.
//
// Equipment is managed outside combat and you choose what to bring in. With
// only four slots in the combat grid, that choice is most of the build: a card
// you cannot fit is a card you do not have.
// ---------------------------------------------------------------------------

static Window    *s_window;
static MenuLayer *s_menu;

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

static int16_t get_header_height(MenuLayer *m, uint16_t s, void *c) { return 18; }

static void draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
    GRect r = ui_cell_inset(layer_get_bounds(cell));
    ui_draw_text_c(ctx, section == 0 ? "In the grid (tap to remove)" : "Spare (tap to equip)",
                   ui_font_tiny(), GRect(r.origin.x + 4, r.origin.y, r.size.w - 8, 16),
                   COL_INK, GTextAlignmentLeft);
}

static void item_row(uint8_t id, bool upgraded, char *t, size_t tl, char *s, size_t sl) {
    const ItemDef *d = item_def(id);
    char eff[24], c0[10], c1[10];
    item_effect_label_up(id, upgraded, eff, sizeof(eff));
    cond_label(d->cond[0], c0, sizeof(c0));
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
    if (idx->section == 0) {
        uint8_t id = g_run.equip[idx->row];
        if (id == ITEM_NONE) {
            snprintf(t, sizeof(t), "Slot %d", idx->row + 1);
            snprintf(s, sizeof(s), "empty");
        } else {
            item_row(id, run_is_upgraded(true, idx->row), t, sizeof(t), s, sizeof(s));
        }
    } else {
        int pi = pack_index_for_row(idx->row);
        if (pi < 0) return;
        item_row(g_run.pack[pi], run_is_upgraded(false, pi), t, sizeof(t), s, sizeof(s));
    }
    menu_cell_basic_draw(ctx, cell, t, s, NULL);
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
            else g_run.pack_up &= (uint8_t)~(1 << i);
            g_run.equip[idx->row] = ITEM_NONE;
            g_run.equip_up &= (uint8_t)~(1 << idx->row);
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
    else g_run.equip_up &= (uint8_t)~(1 << slot);
    g_run.pack[pi] = ITEM_NONE;
    g_run.pack_up &= (uint8_t)~(1 << pi);
    save_run();
    menu_layer_reload_data(s_menu);
}

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    s_menu = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks) {
        .get_num_sections = get_num_sections,
        .get_num_rows = get_num_rows,
        .draw_row = draw_row,
        .select_click = select_row,
        .get_header_height = get_header_height,
        .draw_header = draw_header
    });
    menu_layer_set_highlight_colors(s_menu, COL_HERO, GColorWhite);
    menu_layer_set_click_config_onto_window(s_menu, window);
    layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void window_unload(Window *window) {
    menu_layer_destroy(s_menu);
    s_menu = NULL;
    window_destroy(s_window);
    s_window = NULL;
}

void backpack_window_push(void) {
    s_window = window_create();
    window_set_background_color(s_window, COL_BG);
    window_set_window_handlers(s_window, (WindowHandlers) {
        .load = window_load, .unload = window_unload
    });
    window_stack_push(s_window, true);
}
