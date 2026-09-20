#include "game.h"

RunState g_run;
BattleResult g_last_battle = BATTLE_NONE;

#define PERSIST_KEY_RUN 100
#define PERSIST_KEY_DIFF 121
#define PERSIST_KEY_VER 101
#define PERSIST_VERSION 6

uint8_t warrior_dice_for_level(uint8_t level) {
    return class_dice_for_level(CLASS_WARRIOR, level);
}

uint8_t run_depth(void) { return g_run.floor; }

// --- difficulty ------------------------------------------------------------
// Easy softens the numbers and leaves every rule alone: the puzzle is the same
// puzzle, there is just more room to get it wrong.
// Tuned with tools/simulate.py. At 75/70/125 Easy won every boss fight in the
// simulator even with a thin loadout, which is not a difficulty, it is a
// cutscene. These numbers keep Easy very forgiving while leaving the final boss
// able to beat a player who never improved their kit.
static const int16_t s_enemy_hp_pct[DIFF_COUNT]  = { 100, 85 };
static const int16_t s_incoming_pct[DIFF_COUNT]  = { 100, 80 };
static const int16_t s_hero_hp_pct[DIFF_COUNT]   = { 100, 115 };

uint8_t difficulty_get(void) {
    if (!persist_exists(PERSIST_KEY_DIFF)) return DIFF_NORMAL;
    int d = persist_read_int(PERSIST_KEY_DIFF);
    return (d >= 0 && d < DIFF_COUNT) ? (uint8_t)d : DIFF_NORMAL;
}

void difficulty_set(uint8_t d) {
    if (d >= DIFF_COUNT) d = DIFF_NORMAL;
    persist_write_int(PERSIST_KEY_DIFF, d);
}

const char *difficulty_name(uint8_t d) {
    return (d == DIFF_EASY) ? "Easy" : "Normal";
}

const char *difficulty_blurb(uint8_t d) {
    return (d == DIFF_EASY) ? "Softer numbers, same rules"
                            : "The game as intended";
}

static uint8_t run_diff(void) {
    return (g_run.difficulty < DIFF_COUNT) ? g_run.difficulty : DIFF_NORMAL;
}

int16_t diff_enemy_hp(int16_t hp) { return (int16_t)((hp * s_enemy_hp_pct[run_diff()]) / 100); }
int16_t diff_incoming(int16_t d)  {
    int16_t v = (int16_t)((d * s_incoming_pct[run_diff()]) / 100);
    return (d > 0 && v < 1) ? 1 : v;    // a hit always lands for something
}
int16_t diff_hero_hp(int16_t hp)  { return (int16_t)((hp * s_hero_hp_pct[run_diff()]) / 100); }

// Four cards fit the grid at once, and seeing your whole hand is the puzzle, so
// four is the default and the early game never asks you to scroll. The last two
// slots are a late-game complication: by then you know the cards well enough
// that holding two of them in your head is a fair thing to ask.
uint8_t run_equip_slots(void) {
    // Hoard and Backstage hand you the full grid immediately, and charge for it.
    if (g_run.rule == RULE_HOARD || g_run.rule == RULE_BACKSTAGE) return 6;
    if (g_run.mode == MODE_ENDLESS) return (g_run.floor >= 5) ? 6 : 4;
    return (g_run.campaign_level >= 3) ? 6 : 4;   // Elimination Round onward
}

// --- endless scaling -------------------------------------------------------
int16_t run_enemy_hp(uint8_t enemy_id) {
    int16_t base = (int16_t)enemy_def(enemy_id)->hp;
    if (g_run.rule == RULE_HOARD || g_run.rule == RULE_BACKSTAGE) {
        base = (int16_t)((base * 160) / 100);
    }
    if (g_run.mode != MODE_ENDLESS) return diff_enemy_hp(base);
    // +12% per floor, compounding gently rather than exponentially.
    int32_t hp = ((int32_t)base * (100 + 12 * g_run.floor)) / 100;
    if (hp > 900) hp = 900;
    return diff_enemy_hp((int16_t)hp);
}

uint8_t run_enemy_dice(uint8_t enemy_id) {
    uint8_t d = enemy_def(enemy_id)->dice;
    if (g_run.rule == RULE_SWARM || g_run.rule == RULE_BACKSTAGE) d++;
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
        // From floor 2 one of the three is an elite: tougher, worth double, and
        // it drops something rare. That is what makes "which two do I fight"
        // an actual decision rather than a formality.
        bool elite = (g_run.floor >= 2) && (i == 0) && ((rand() % 100) < 60);
        g_run.nodes[g_run.node_count].kind = elite ? NODE_ELITE : NODE_ENEMY;
        g_run.nodes[g_run.node_count].arg  = id;
        g_run.node_count++;
    }

    uint8_t kinds[5];
    uint8_t kn = 0;
    kinds[kn++] = NODE_APPLE;
    kinds[kn++] = NODE_CHEST;
    if (g_run.floor >= 2) kinds[kn++] = NODE_SHOP;
    if (g_run.floor >= 2) kinds[kn++] = NODE_ANVIL;
    if (g_run.floor >= 2) kinds[kn++] = NODE_TRADE;

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
        // Endless rotates the campaign rulesets only — the Bonus Rounds are a
        // reward for finishing the campaign, not a random ambush.
        g_run.rule = (g_run.floor < 4) ? RULE_PLAIN
                                       : (uint8_t)(1 + rand() % (RULE_CAMPAIGN_COUNT - 1));
    }
}

void run_new(GameMode mode, uint8_t campaign_level, uint8_t klass) {
    memset(&g_run, 0, sizeof(g_run));
    g_run.active = true;
    g_run.mode   = (uint8_t)mode;
    g_run.klass  = klass;
    g_run.campaign_level = campaign_level;
    g_run.difficulty = difficulty_get();
    g_run.floor  = 1;
    g_run.level  = 1;
    g_run.max_hp = diff_hero_hp((int16_t)class_def(klass)->base_hp);

    for (int i = 0; i < MAX_EQUIP; i++)    g_run.equip[i] = ITEM_NONE;
    for (int i = 0; i < MAX_BACKPACK; i++) g_run.pack[i]  = ITEM_NONE;

    pick_rule();

    // A starting kit of one card that wants a big number and one happy with a
    // small one, so the very first turn already poses the question the whole
    // game is about. The third card is the class's signature.
    const ClassDef *cd = class_def(klass);
    for (uint8_t i = 0; i < cd->kit_n && i < MAX_EQUIP; i++) g_run.equip[i] = cd->kit[i];

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
        g_run.max_hp = diff_hero_hp((int16_t)(cd->base_hp / 2));
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
    uint8_t n = run_equip_slots();
    for (int i = 0; i < n; i++) if (g_run.equip[i] == ITEM_NONE) return i;
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
