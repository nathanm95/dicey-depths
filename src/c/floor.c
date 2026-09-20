#include "game.h"
#include "ui.h"
#include "achieve.h"

// ---------------------------------------------------------------------------
// The floor.
//
// Not an explorable space — a short list of nodes laid out in the open, with
// no timer and no fog of war. You can take as long as you like before a fight,
// reorganise the backpack, visit the other nodes first. The only tension on
// this screen is arithmetic: two of the three enemies open the door, so is the
// third worth the health it will cost you for the gold it pays?
// ---------------------------------------------------------------------------

static Window    *s_window;
static MenuLayer *s_menu;
static int8_t     s_pending = -1;   // node we walked into, awaiting its result

// One icon per node kind. Eight rows of near-identical text is hard to scan;
// menu_cell_basic_draw takes an icon and we were passing NULL.
static GBitmap *s_icons[NODE_TRADE + 1];

static void icons_load(void) {
    memset(s_icons, 0, sizeof(s_icons));
    s_icons[NODE_ENEMY] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ICON_ENEMY);
    s_icons[NODE_ELITE] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ICON_ELITE);
    s_icons[NODE_APPLE] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ICON_APPLE);
    s_icons[NODE_CHEST] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ICON_CHEST);
    s_icons[NODE_SHOP]  = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ICON_SHOP);
    s_icons[NODE_ANVIL] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ICON_ANVIL);
    s_icons[NODE_TRADE] = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ICON_TRADE);
    s_icons[NODE_DOOR]  = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ICON_DOOR);
}

static void icons_unload(void) {
    for (unsigned i = 0; i < sizeof(s_icons) / sizeof(s_icons[0]); i++) {
        if (s_icons[i]) { gbitmap_destroy(s_icons[i]); s_icons[i] = NULL; }
    }
}

static bool door_open(void) { return g_run.foes_cleared >= g_run.foes_required; }

static void node_line(const Node *n, char *title, size_t tl, char *sub, size_t sl) {
    switch (n->kind) {
        case NODE_ENEMY:
        case NODE_ELITE: {
            const EnemyDef *e = enemy_def(n->arg);
            bool elite = (n->kind == NODE_ELITE);
            snprintf(title, tl, "%s%s", e->boss ? "BOSS: " : (elite ? "ELITE " : ""), e->name);
            if (n->used) snprintf(sub, sl, "Beaten");
            else {
                int16_t hp = run_enemy_hp(n->arg);
                uint8_t dice = run_enemy_dice(n->arg);
                uint16_t gold = e->gold;
                if (elite) {
                    hp = (int16_t)((hp * ELITE_HP_PCT) / 100);
                    if (dice < MAX_FOE_DICE) dice++;
                    gold = (uint16_t)(gold * ELITE_GOLD_MUL);
                }
                snprintf(sub, sl, "%d HP  %d dice  %dg%s",
                         hp, dice, gold, elite ? "  +rare" : "");
            }
            break;
        }
        case NODE_APPLE:
            snprintf(title, tl, "Apple");
            snprintf(sub, sl, n->used ? "Eaten" : "Heal %d HP", n->arg);
            break;
        case NODE_CHEST:
            snprintf(title, tl, "Chest");
            snprintf(sub, sl, n->used ? "Empty" : "%s", item_def(n->arg)->name);
            break;
        case NODE_SHOP:
            snprintf(title, tl, "Shop");
            snprintf(sub, sl, "%d gold in pocket", g_run.gold);
            break;
        case NODE_ANVIL:
            snprintf(title, tl, "Anvil");
            snprintf(sub, sl, n->used ? "Used" : "Upgrade a card");
            break;
        case NODE_TRADE:
            snprintf(title, tl, "Trading post");
            snprintf(sub, sl, n->used ? "Closed" : "Swap a card you own");
            break;
        case NODE_DOOR:
            snprintf(title, tl, "Door");
            if (door_open()) snprintf(sub, sl, "Down to floor %d", g_run.floor + 1);
            else snprintf(sub, sl, "Beat %d more to pass",
                          g_run.foes_required - g_run.foes_cleared);
            break;
        default:
            snprintf(title, tl, "-");
            sub[0] = '\0';
            break;
    }
}

static uint16_t get_num_rows(MenuLayer *m, uint16_t s, void *c) {
    return (uint16_t)(g_run.node_count + 1);   // nodes, then the backpack
}

static int16_t get_header_height(MenuLayer *m, uint16_t s, void *c) {
    return 36;
}

static void draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
    GRect r = ui_cell_inset(layer_get_bounds(cell));
    char a[40], b[40];
    if (g_run.mode == MODE_ENDLESS) snprintf(a, sizeof(a), "Endless - Floor %d", g_run.floor);
    else snprintf(a, sizeof(a), "Floor %d/%d - %s", g_run.floor, FLOOR_COUNT, rule_name((RuleId)g_run.rule));
    snprintf(b, sizeof(b), "HP %d/%d   %dg   Lv%d", g_run.hp, g_run.max_hp, g_run.gold, g_run.level);
    ui_draw_text_c(ctx, a, ui_font_tiny(), GRect(r.origin.x + 4, r.origin.y, r.size.w - 8, 16),
                   COL_INK, GTextAlignmentCenter);
    ui_draw_text_c(ctx, b, ui_font_tiny(), GRect(r.origin.x + 4, r.origin.y + 16, r.size.w - 8, 16),
                   COL_INK, GTextAlignmentCenter);
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
    char title[44], sub[52];
    if (idx->row == g_run.node_count) {
        int owned = 0;
        for (int i = 0; i < MAX_BACKPACK; i++) if (g_run.pack[i] != ITEM_NONE) owned++;
        snprintf(title, sizeof(title), "Backpack");
        snprintf(sub, sizeof(sub), "%d spare", owned);
        menu_cell_basic_draw(ctx, cell, title, sub, NULL);
        return;
    }
    const Node *n = &g_run.nodes[idx->row];
    node_line(n, title, sizeof(title), sub, sizeof(sub));
    GBitmap *icon = (n->kind < sizeof(s_icons) / sizeof(s_icons[0])) ? s_icons[n->kind] : NULL;
    menu_cell_basic_draw(ctx, cell, title, sub, icon);
}

static void select_row(MenuLayer *m, MenuIndex *idx, void *c) {
    if (idx->row == g_run.node_count) { backpack_window_push(); return; }

    Node *n = &g_run.nodes[idx->row];
    s_pending = (int8_t)idx->row;

    switch (n->kind) {
        case NODE_ENEMY:
        case NODE_ELITE:
            if (n->used) { vibes_short_pulse(); return; }
            combat_window_push(n->arg, n->kind == NODE_ELITE);
            break;
        case NODE_APPLE:
            if (n->used) { vibes_short_pulse(); return; }
            g_run.hp += n->arg;
            if (g_run.hp > g_run.max_hp) g_run.hp = g_run.max_hp;
            n->used = true;
            save_run();
            menu_layer_reload_data(s_menu);
            break;
        case NODE_CHEST:
            if (n->used) { vibes_short_pulse(); return; }
            if (run_backpack_add(n->arg)) {
                n->used = true;
                save_run();
                menu_layer_reload_data(s_menu);
                backpack_window_push();
            } else {
                vibes_short_pulse();
            }
            break;
        case NODE_SHOP:  shop_window_push((uint8_t)idx->row);  break;
        case NODE_TRADE:
            if (n->used) { vibes_short_pulse(); return; }
            trade_window_push((uint8_t)idx->row);
            break;
        case NODE_ANVIL:
            if (n->used) { vibes_short_pulse(); return; }
            anvil_window_push((uint8_t)idx->row);
            break;
        case NODE_DOOR:
            if (!door_open()) { vibes_short_pulse(); return; }
            run_descend();
            if (g_run.mode == MODE_ENDLESS && g_run.floor >= 10) ach_unlock(ACH_DEPTH_10);
            menu_layer_set_selected_index(s_menu, MenuIndex(0, 0), MenuRowAlignCenter, false);
            menu_layer_reload_data(s_menu);
            break;
        default: break;
    }
}

// Losing a piece of equipment after every fight is the Elimination Round's
// whole rule, and it is why a lean loadout matters there.
static void elimination_toll(void) {
    int owned[MAX_EQUIP], n = 0;
    for (int i = 0; i < MAX_EQUIP; i++) if (g_run.equip[i] != ITEM_NONE) owned[n++] = i;
    if (n <= 1) return;     // never strip you down to nothing
    int pick = owned[rand() % n];
    g_run.equip[pick] = ITEM_NONE;
    g_run.equip_up &= (uint8_t)~(1 << pick);
}

static void window_appear(Window *window) {
    // Deal with whatever happened in the fight we walked into.
    if (g_last_battle == BATTLE_WON && s_pending >= 0 && s_pending < g_run.node_count) {
        Node *n = &g_run.nodes[s_pending];
        n->used = true;
        g_run.foes_cleared++;

        bool was_boss = enemy_def(n->arg)->boss;
        // An elite is worth fighting because it always leaves something rare.
        if (n->kind == NODE_ELITE) {
            uint8_t drop = item_roll_for_floor((uint8_t)(g_run.floor + 2));
            if (run_backpack_add(drop)) vibes_short_pulse();
        }
        if (g_run.rule == RULE_ELIMINATION) elimination_toll();
        save_run();

        if (was_boss && g_run.mode == MODE_CAMPAIGN) {
            progress_mark_cleared(g_run.campaign_level);
            ach_unlock(ACH_CLEAR_EPISODE);
            g_run.active = false;
            clear_run();
            g_last_battle = BATTLE_NONE;
            s_pending = -1;
            gameover_window_push(true);
            return;
        }
    } else if (g_last_battle == BATTLE_LOST) {
        if (g_run.mode == MODE_ENDLESS) progress_submit_score();
        g_run.active = false;
        clear_run();
        g_last_battle = BATTLE_NONE;
        s_pending = -1;
        gameover_window_push(false);
        return;
    }
    g_last_battle = BATTLE_NONE;
    s_pending = -1;

    // The run ended while we were away — get off this screen.
    if (!g_run.active) { window_stack_remove(s_window, false); return; }
    menu_layer_reload_data(s_menu);
}

static void window_load(Window *window) {
    icons_load();
    Layer *root = window_get_root_layer(window);
    s_menu = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks) {
        .get_num_rows = get_num_rows,
        .draw_row = draw_row,
        .select_click = select_row,
        .get_header_height = get_header_height,
        .draw_header = draw_header
    });
    menu_layer_set_highlight_colors(s_menu, COL_FOE, GColorWhite);
    menu_layer_set_click_config_onto_window(s_menu, window);
    layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void window_unload(Window *window) {
    icons_unload();
    menu_layer_destroy(s_menu);
    s_menu = NULL;
    window_destroy(s_window);
    s_window = NULL;
}

void floor_window_push(void) {
    s_pending = -1;
    s_window = window_create();
    window_set_background_color(s_window, COL_BG);
    window_set_window_handlers(s_window, (WindowHandlers) {
        .load = window_load, .appear = window_appear, .unload = window_unload
    });
    window_stack_push(s_window, true);
}
