#include "game.h"
#include "ui.h"
#include "achieve.h"
#include "skin.h"

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
static Layer     *s_bg;
static GBitmap   *s_pack_ic;
static int8_t     s_pending = -1;   // node we walked into, awaiting its result

// Pushing or removing a window from inside a window's own appear handler is
// asking for trouble: the window is mid-transition and the stack is not in a
// settled state. Everything this screen wants to do on appearing is therefore
// recorded and performed a tick later, from a timer, with the transition over.
typedef enum { FA_NONE = 0, FA_RESUME, FA_OVER_WON, FA_OVER_LOST, FA_LEAVE } FloorAction;
static FloorAction s_after_appear;
static AppTimer   *s_after_timer;
static uint8_t     s_over_floor;      // where the run ended, for the results screen
static bool        s_over_endless;

static void after_appear(void *ctx) {
    s_after_timer = NULL;
    FloorAction a = s_after_appear;
    s_after_appear = FA_NONE;
    switch (a) {
        case FA_RESUME: {
            uint8_t node = g_run.fight_node;
            if (node < g_run.node_count) {
                s_pending = (int8_t)node;
                combat_window_push(g_run.nodes[node].arg,
                                   g_run.nodes[node].kind == NODE_ELITE);
            }
            break;
        }
        case FA_OVER_WON:  gameover_window_push(true, s_over_floor, s_over_endless);  break;
        case FA_OVER_LOST: gameover_window_push(false, s_over_floor, s_over_endless); break;
        case FA_LEAVE:     if (s_window) window_stack_remove(s_window, false); break;
        default: break;
    }
}

static void schedule_after_appear(FloorAction a) {
    s_after_appear = a;
    if (s_after_timer) app_timer_cancel(s_after_timer);
    s_after_timer = app_timer_register(1, after_appear, NULL);
}

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

// Three depths, each darker than the last; Endless cycles them.
uint32_t floor_backdrop(void) {
    static const uint32_t ids[3] = {
        RESOURCE_ID_BD_DUN_FLOOR1, RESOURCE_ID_BD_DUN_FLOOR2, RESOURCE_ID_BD_DUN_FLOOR3
    };
    uint8_t f = (uint8_t)(g_run.floor ? g_run.floor - 1 : 0);
    if (g_run.mode == MODE_ENDLESS) return ids[f % 3];
    return ids[f < 3 ? f : 2];
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
                if (g_run.rule == RULE_SUDDEN) gold *= 2;   // as combat pays it
                snprintf(sub, sl, "%d HP  %d dice  %dg%s",
                         hp, dice, gold, elite ? "  +rare" : "");
            }
            break;
        }
        case NODE_APPLE:
            snprintf(title, tl, "Apple");
            snprintf(sub, sl, n->used ? "Eaten" : "Heal %d HP",
                     (g_run.curse == CURSE_LEAN) ? (n->arg / 2) : n->arg);
            break;
        case NODE_CHEST:
            snprintf(title, tl, "Chest");
            if (n->used) snprintf(sub, sl, "Empty");
            else {
                char eff[24], c0[10];
                item_effect_label(n->arg, eff, sizeof(eff));
                cond_label(item_def(n->arg)->cond[0], c0, sizeof(c0));
                snprintf(sub, sl, "%s: %s %s", item_def(n->arg)->name, c0, eff);
            }
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

// Endless draws a ruleset per floor from floor 4, and on Hard a curse too, so
// its header gets a line of its own to name them. In the campaign the ruleset
// is the episode you picked, and the curse (on Hard) takes its place instead.
static bool endless_rules_line(char *out, size_t len) {
    if (g_run.mode != MODE_ENDLESS) return false;
    bool rule = g_run.rule != RULE_PLAIN, curse = g_run.curse != CURSE_NONE;
    if (rule && curse) snprintf(out, len, "%s + %s", rule_tag((RuleId)g_run.rule), curse_name(g_run.curse));
    else if (rule)     snprintf(out, len, "%s", rule_name((RuleId)g_run.rule));
    else if (curse)    snprintf(out, len, "%s", curse_name(g_run.curse));
    else return false;
    return true;
}

static int16_t get_header_height(MenuLayer *m, uint16_t s, void *c) {
    char tmp[40];
    return endless_rules_line(tmp, sizeof(tmp)) ? 52 : 36;
}

static void draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *data) {
    char a[40], mid[40], b[40];
    snprintf(b, sizeof(b), "HP %d/%d  %dg  Lv%d", g_run.hp, g_run.max_hp, g_run.gold, g_run.level);
    if (g_run.mode == MODE_ENDLESS) {
        snprintf(a, sizeof(a), "Endless - Floor %d", g_run.floor);
        if (endless_rules_line(mid, sizeof(mid))) skin_header3(ctx, cell, a, mid, b);
        else skin_header(ctx, cell, a, b);
        return;
    }
    // On Hard the floor's curse takes the ruleset's place on the first line --
    // never the floor number, which is the thing you most need to know.
    const char *what = (g_run.curse != CURSE_NONE) ? curse_name(g_run.curse) : rule_name((RuleId)g_run.rule);
    snprintf(a, sizeof(a), "Floor %d/%d - %s", g_run.floor, FLOOR_COUNT, what);
    skin_header(ctx, cell, a, b);
}

static void draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *data) {
    char title[44], sub[52];
    if (idx->row == g_run.node_count) {
        int owned = 0;
        for (int i = 0; i < MAX_BACKPACK; i++) if (g_run.pack[i] != ITEM_NONE) owned++;
        snprintf(title, sizeof(title), "Backpack");
        snprintf(sub, sizeof(sub), "%d spare", owned);
        skin_cell(ctx, cell, title, sub, s_pack_ic, 0);
        return;
    }
    const Node *n = &g_run.nodes[idx->row];
    node_line(n, title, sizeof(title), sub, sizeof(sub));
    GBitmap *icon = (n->kind < sizeof(s_icons) / sizeof(s_icons[0])) ? s_icons[n->kind] : NULL;
    // A shut door is drawn as a silhouette; anything spent is dimmed.
    uint8_t flags = (n->kind == NODE_DOOR) ? (door_open() ? 0 : SKIN_LOCKED)
                  : (n->used && n->kind != NODE_SHOP) ? SKIN_DONE : 0;
    skin_cell(ctx, cell, title, sub, icon, flags);
}

static void select_row(MenuLayer *m, MenuIndex *idx, void *c) {
    if (idx->row == g_run.node_count) { backpack_window_push(); return; }

    Node *n = &g_run.nodes[idx->row];
    s_pending = (int8_t)idx->row;

    switch (n->kind) {
        case NODE_ENEMY:
        case NODE_ELITE:
            if (n->used) { vibes_short_pulse(); return; }
            // Holding BACK force-quits the app and the firmware will not let us
            // catch it, so a fight can always be walked out of. Remember which
            // one was open: on the way back in we drop straight into it again,
            // at the health it started with, rather than letting the player out
            // of a fight that was going badly.
            g_run.in_fight = true;
            g_run.fight_node = (uint8_t)idx->row;
            save_run();
            combat_window_push(n->arg, n->kind == NODE_ELITE);
            break;
        case NODE_APPLE:
            if (n->used) { vibes_short_pulse(); return; }
            g_run.hp += (g_run.curse == CURSE_LEAN) ? (n->arg / 2) : n->arg;
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
                // Everything is full: open the pack, where a spare can be
                // thrown away to make room, rather than just buzzing.
                vibes_short_pulse();
                backpack_window_push();
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
            skin_show(s_window, floor_backdrop(), SKIN_DUNGEON);   // one floor deeper
            if (s_bg) layer_mark_dirty(s_bg);
            menu_layer_set_selected_index(s_menu, MenuIndex(0, 0), MenuRowAlignCenter, false);
            menu_layer_reload_data(s_menu);
            break;
        default: break;
    }
}

static void window_disappear(Window *window) { skin_hide(window); }

static void window_appear(Window *window) {
    skin_show(window, floor_backdrop(), SKIN_DUNGEON);
    // Walked out of a fight (or the watch died mid-fight): go straight back in.
    if (g_run.active && g_run.in_fight && g_last_battle == BATTLE_NONE) {
        uint8_t node = g_run.fight_node;
        if (node < g_run.node_count &&
            (g_run.nodes[node].kind == NODE_ENEMY || g_run.nodes[node].kind == NODE_ELITE) &&
            !g_run.nodes[node].used) {
            schedule_after_appear(FA_RESUME);
            return;
        }
        g_run.in_fight = false;   // node is gone or already beaten: let it go
        save_run();
    }

    // Deal with whatever happened in the fight we walked into.
    if (g_last_battle == BATTLE_WON && s_pending >= 0 && s_pending < g_run.node_count) {
        Node *n = &g_run.nodes[s_pending];
        n->used = true;
        g_run.foes_cleared++;

        g_run.in_fight = false;
        bool was_boss = enemy_def(n->arg)->boss;
        // Combat decided both of these at the win and named them in its
        // closing toast (see g_spoils); this is where they land, in the same
        // save as the rest of the win. The toll goes first, so the rare card
        // just won is never the one it takes.
        if (g_run.rule == RULE_ELIMINATION && g_spoils.toll >= 0 && g_spoils.toll < MAX_EQUIP) {
            g_run.equip[g_spoils.toll] = ITEM_NONE;
            run_clear_upgraded(true, g_spoils.toll);
        }
        // An elite is worth fighting because it always leaves something rare.
        if (n->kind == NODE_ELITE && g_spoils.drop != ITEM_NONE) run_backpack_add(g_spoils.drop);
        g_spoils.drop = ITEM_NONE;
        g_spoils.toll = -1;
        save_run();

        if (was_boss && g_run.mode == MODE_CAMPAIGN) {
            s_over_floor = g_run.floor;
            s_over_endless = false;
            progress_mark_cleared(g_run.campaign_level);
            ach_unlock(ACH_CLEAR_EPISODE);
            g_run.active = false;
            clear_run();
            g_last_battle = BATTLE_NONE;
            s_pending = -1;
            schedule_after_appear(FA_OVER_WON);
            return;
        }
    } else if (g_last_battle == BATTLE_LOST) {
        // combat.c's run_record_loss has already submitted any Endless score
        // and taken the run off disk, the moment the hero went down.
        g_run.in_fight = false;
        s_over_floor = g_run.floor;
        s_over_endless = (g_run.mode == MODE_ENDLESS);
        g_run.active = false;
        clear_run();
        g_last_battle = BATTLE_NONE;
        s_pending = -1;
        schedule_after_appear(FA_OVER_LOST);
        return;
    }
    g_last_battle = BATTLE_NONE;
    s_pending = -1;

    // The run ended while we were away — get off this screen.
    if (!g_run.active) { schedule_after_appear(FA_LEAVE); return; }
    menu_layer_reload_data(s_menu);
}

static void window_load(Window *window) {
    icons_load();
    s_pack_ic = gbitmap_create_with_resource(RESOURCE_ID_IC_BACKPACK);
    Layer *root = window_get_root_layer(window);
    s_bg = skin_backdrop_layer(root);
    s_menu = menu_layer_create(layer_get_bounds(root));
    menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks) {
        .get_num_rows = get_num_rows,
        .draw_row = draw_row,
        .select_click = select_row,
        .get_header_height = get_header_height,
        .draw_header = draw_header,
        .draw_background = skin_menu_background
    });
    skin_menu(s_menu);
    menu_layer_set_click_config_onto_window(s_menu, window);
    layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void window_unload(Window *window) {
    if (s_after_timer) { app_timer_cancel(s_after_timer); s_after_timer = NULL; }
    s_after_appear = FA_NONE;
    icons_unload();
    if (s_pack_ic) { gbitmap_destroy(s_pack_ic); s_pack_ic = NULL; }
    menu_layer_destroy(s_menu);
    s_menu = NULL;
    skin_backdrop_layer_destroy(s_bg);
    s_bg = NULL;
    window_destroy(s_window);
    s_window = NULL;
}

void floor_window_push(void) {
    // Each screen keeps its window and layers in file statics. An animated pop
    // unloads only when the slide ends, so a second push landing in that gap
    // would have the old unload destroy the new window. Refuse it instead.
    if (s_window) return;
    s_pending = -1;
    s_window = window_create();
    window_set_background_color(s_window, GColorBlack);
    window_set_window_handlers(s_window, (WindowHandlers) {
        .load = window_load, .appear = window_appear, .disappear = window_disappear,
        .unload = window_unload
    });
    window_stack_push(s_window, true);
}
