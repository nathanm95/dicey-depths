#include "game.h"

RunState g_run;
BattleResult g_last_battle = BATTLE_NONE;

#define PERSIST_KEY_RUN 100
#define PERSIST_KEY_VER 101
#define PERSIST_VERSION 2

uint8_t warrior_dice_for_level(uint8_t level) {
    // Two dice, then three, then four — one step per floor of a three-floor
    // run, so every descent is a visible jump in what you can do.
    if (level >= 3) return 4;
    if (level >= 2) return 3;
    return 2;
}

uint8_t run_depth(void) { return g_run.floor; }

// --- endless scaling -------------------------------------------------------
int16_t run_enemy_hp(uint8_t enemy_id) {
    int16_t base = (int16_t)enemy_def(enemy_id)->hp;
    if (g_run.mode != MODE_ENDLESS) return base;
    // +12% per floor, compounding gently rather than exponentially.
    int32_t hp = ((int32_t)base * (100 + 12 * g_run.floor)) / 100;
    if (hp > 900) hp = 900;
    return (int16_t)hp;
}

uint8_t run_enemy_dice(uint8_t enemy_id) {
    uint8_t d = enemy_def(enemy_id)->dice;
    if (g_run.mode == MODE_ENDLESS) d = (uint8_t)(d + g_run.floor / 4);
    if (d > MAX_FOE_DICE) d = MAX_FOE_DICE;
    return d;
}

// --- upgrades --------------------------------------------------------------
bool run_is_upgraded(bool equipped, int index) {
    if (index < 0) return false;
    return equipped ? ((g_run.equip_up >> index) & 1) : ((g_run.pack_up >> index) & 1);
}

void run_set_upgraded(bool equipped, int index) {
    if (index < 0) return;
    if (equipped) g_run.equip_up |= (uint8_t)(1 << index);
    else          g_run.pack_up  |= (uint8_t)(1 << index);
}

// --- floors ----------------------------------------------------------------
// Floors 1 and 2 hold three enemies and two support nodes; clearing any two
// enemies opens the door. That is the whole floor puzzle: is the third fight
// worth the health it will cost you for the gold it pays?
void run_build_floor(void) {
    memset(g_run.nodes, 0, sizeof(g_run.nodes));
    g_run.node_count  = 0;
    g_run.foes_cleared = 0;

    bool boss_floor = (g_run.mode == MODE_CAMPAIGN) && (g_run.floor >= FLOOR_COUNT);

    if (boss_floor) {
        g_run.nodes[g_run.node_count].kind = NODE_APPLE;
        g_run.nodes[g_run.node_count].arg  = 8;
        g_run.node_count++;
        g_run.nodes[g_run.node_count].kind = NODE_ENEMY;
        g_run.nodes[g_run.node_count].arg  = enemy_boss_for_level(g_run.campaign_level);
        g_run.node_count++;
        g_run.foes_required = 1;
        return;
    }

    // Three different enemies. A floor offering the same creature three times
    // is not a choice between fights, it is just the same fight three times.
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t id = enemy_roll_for_depth(g_run.floor);
        for (int tries = 0; tries < 8; tries++) {
            bool clash = false;
            for (uint8_t j = 0; j < i; j++) if (g_run.nodes[j].arg == id) clash = true;
            if (!clash) break;
            id = enemy_roll_for_depth(g_run.floor);
        }
        g_run.nodes[g_run.node_count].kind = NODE_ENEMY;
        g_run.nodes[g_run.node_count].arg  = id;
        g_run.node_count++;
    }

    uint8_t kinds[4];
    uint8_t kn = 0;
    kinds[kn++] = NODE_APPLE;
    kinds[kn++] = NODE_CHEST;
    if (g_run.floor >= 2) kinds[kn++] = NODE_SHOP;
    if (g_run.floor >= 2) kinds[kn++] = NODE_ANVIL;

    for (uint8_t i = 0; i < 2 && g_run.node_count < MAX_NODES - 1; i++) {
        uint8_t k = kinds[(rand() + i) % kn];
        // Do not hand out the same support node twice on one floor.
        if (i == 1 && k == g_run.nodes[g_run.node_count - 1].kind) k = kinds[(k + 1) % kn];
        g_run.nodes[g_run.node_count].kind = k;
        if (k == NODE_APPLE)      g_run.nodes[g_run.node_count].arg = (uint8_t)(6 + rand() % 5);
        else if (k == NODE_CHEST) g_run.nodes[g_run.node_count].arg = item_roll_for_floor(g_run.floor);
        g_run.node_count++;
    }

    g_run.nodes[g_run.node_count].kind = NODE_DOOR;
    g_run.node_count++;
    g_run.foes_required = 2;
}

// Endless rotates the campaign rulesets from floor 4, so the modes teach each
// other: the campaign is where you learn a rule, endless is where it ambushes
// you.
static void pick_rule(void) {
    if (g_run.mode == MODE_CAMPAIGN) {
        g_run.rule = g_run.campaign_level;
    } else {
        g_run.rule = (g_run.floor < 4) ? RULE_PLAIN : (uint8_t)(1 + rand() % (RULE_COUNT - 1));
    }
}

void run_new(GameMode mode, uint8_t campaign_level) {
    memset(&g_run, 0, sizeof(g_run));
    g_run.active = true;
    g_run.mode   = (uint8_t)mode;
    g_run.campaign_level = campaign_level;
    g_run.floor  = 1;
    g_run.level  = 1;
    g_run.max_hp = WARRIOR_BASE_HP;

    for (int i = 0; i < MAX_EQUIP; i++)    g_run.equip[i] = ITEM_NONE;
    for (int i = 0; i < MAX_BACKPACK; i++) g_run.pack[i]  = ITEM_NONE;

    pick_rule();

    // The Warrior opens with a sword and a shield: one card that wants a big
    // die and one happy with a small one, so the very first roll already poses
    // the question the whole game is about.
    g_run.equip[0] = 0;             // Sword
    g_run.equip[1] = 1;             // Shield
    g_run.equip[2] = ITEM_REROLL;   // the Warrior always brings this

    if (g_run.rule == RULE_GREED) {
        // Curse of Greed opens generous and then bills you for it every turn.
        g_run.equip[0] = 2;             // Hammer
        g_run.equip[1] = 8;             // Broadsword
        g_run.equip[2] = ITEM_REROLL;
        g_run.equip[3] = 1;             // Shield
        run_set_upgraded(true, 0);
        run_set_upgraded(true, 1);
    }
    if (g_run.rule == RULE_SUDDEN) {
        g_run.max_hp = WARRIOR_BASE_HP / 2;
    }

    g_run.hp = g_run.max_hp;
    run_build_floor();
    save_run();
}

void run_descend(void) {
    g_run.floor++;
    if (g_run.mode == MODE_CAMPAIGN && g_run.floor > FLOOR_COUNT) g_run.floor = FLOOR_COUNT;

    // Levelling raises max HP — which also raises the damage you must soak to
    // earn a Limit Break. Getting tougher makes your best move rarer, and that
    // tension is deliberate.
    g_run.level++;
    g_run.max_hp += WARRIOR_HP_PER_LVL;
    g_run.hp = g_run.max_hp;      // a level up puts you back on your feet

    pick_rule();
    run_build_floor();
    save_run();
}

bool run_backpack_add(uint8_t item_id) {
    int slot = run_equip_free_slot();
    if (slot >= 0) { g_run.equip[slot] = item_id; return true; }
    for (int i = 0; i < MAX_BACKPACK; i++) {
        if (g_run.pack[i] == ITEM_NONE) { g_run.pack[i] = item_id; return true; }
    }
    return false;
}

int run_equip_free_slot(void) {
    for (int i = 0; i < MAX_EQUIP; i++) if (g_run.equip[i] == ITEM_NONE) return i;
    return -1;
}

// --- persistence -----------------------------------------------------------
void save_run(void) {
    persist_write_int(PERSIST_KEY_VER, PERSIST_VERSION);
    persist_write_data(PERSIST_KEY_RUN, &g_run, sizeof(g_run));
}

bool load_run(void) {
    if (!persist_exists(PERSIST_KEY_RUN)) return false;
    if (persist_read_int(PERSIST_KEY_VER) != PERSIST_VERSION) return false;
    if (persist_read_data(PERSIST_KEY_RUN, &g_run, sizeof(g_run)) != (int)sizeof(g_run)) return false;
    return g_run.active;
}

void clear_run(void) {
    memset(&g_run, 0, sizeof(g_run));
    if (persist_exists(PERSIST_KEY_RUN)) persist_delete(PERSIST_KEY_RUN);
}
