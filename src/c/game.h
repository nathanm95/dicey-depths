#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// Dicey Depths — shared types.
//
// A dice roguelike for Pebble Time 2 and Round 2, in the spirit of Terry
// Cavanagh's Dicey Dungeons. One class so far: the Warrior.
//
// The whole design hangs off one inversion of RPG combat — you see the
// randomness (the dice) BEFORE you commit, so a bad roll is a puzzle rather
// than a punishment. Everything here exists to keep that readable on a watch:
// four equipment cards in a grid that never scrolls, four dice at most, and
// every status effect attacking the roll instead of the health bar.
// ---------------------------------------------------------------------------

#define MAX_DICE        4
#define MAX_SLOTS       2   // slots on one piece of equipment
#define MAX_EQUIP       6   // hard ceiling; how many are UNLOCKED varies, see
                            // run_equip_slots(). Four fit the grid without
                            // scrolling; the last two arrive late on purpose.
#define MAX_BACKPACK   10
#define MAX_FOE_EQUIP   4
#define MAX_FOE_DICE    6   // endless hands enemies extra dice with depth
#define MAX_NODES       7   // 3 enemies + 2 support + door, worst case
#define FLOOR_COUNT     3   // a run is three floors; a watch session is short

// --- Status effects --------------------------------------------------------
// Statuses attack your rolls, not your health. Every one of these bar Poison
// turns into a dice puzzle rather than a subtraction — which is the point.
typedef enum {
    ST_BURN = 0,    // a die catches fire; 2 HP to put it out
    ST_FROZEN,      // your best die becomes a 1
    ST_SHOCK,       // a card goes offline; spend any die to clear it
    ST_WEAKEN,      // one card does half
    ST_BLIND,       // dice hidden until spent
    ST_POISON,      // HP each turn, ignores shields
    ST_FURY,        // next card fires twice
    ST_SILENCE,     // no Limit Break
    ST_COUNT
} StatusId;

const char *status_short(StatusId s);
const char *status_name(StatusId s);
const char *status_blurb(StatusId s);   // plain English, shown when it lands

// --- Slot conditions -------------------------------------------------------
typedef enum {
    COND_ANY = 0, COND_ODD, COND_EVEN, COND_EXACT, COND_MIN, COND_MAX
} CondKind;

typedef struct { uint8_t kind; uint8_t arg; } Cond;

bool cond_accepts(Cond c, uint8_t pips);
void cond_label(Cond c, char *out, size_t len);
// A ~20px slot box cannot hold the word "even". Lists use the words; the box
// uses this.
void cond_label_short(Cond c, char *out, size_t len);

// --- Equipment -------------------------------------------------------------
typedef enum {
    EFF_NONE = 0, EFF_DAMAGE, EFF_SHIELD, EFF_HEAL, EFF_INFLICT, EFF_SELF, EFF_DRAIN,
    EFF_REROLL      // rerolls the die fed to it and hands it straight back
} EffectKind;

// value = pips * mult + add. mult 0 gives a flat effect that ignores the die,
// which is what lets "exactly a 6, for 12 damage" cards exist.
// A negative EFF_HEAL is self-harm — how Backfire pays for its damage.
typedef struct {
    uint8_t kind;
    int8_t  mult;
    int8_t  add;
    uint8_t status;
} Effect;

typedef struct {
    const char *name;
    uint8_t  slots;
    Cond     cond[MAX_SLOTS];
    Effect   eff[2];
    uint8_t  uses;      // 0 = unlimited, else uses per battle
    uint8_t  tier;      // 1 common .. 3 rare
    uint8_t  price;
} ItemDef;

#define ITEM_NONE   0xFF
#define ITEM_REROLL   41    // starting kit; its uses scale with your dice

const ItemDef *item_def(uint8_t id);
void item_effect_label(uint8_t id, char *out, size_t len);
void item_effect_label_up(uint8_t id, bool upgraded, char *out, size_t len);
uint8_t item_roll_for_floor(uint8_t depth);
bool item_is_two_slot(uint8_t id);
bool item_is_reroll(uint8_t id);

// --- Enemy portraits -------------------------------------------------------
// Drawn procedurally from flat shapes — bright, bold and cheap, with no art
// pipeline and nothing eating the 256K resource budget.
typedef enum {
    SHAPE_RAT = 0, SHAPE_SLIME, SHAPE_VACUUM, SHAPE_IMP, SHAPE_SPRITE,
    SHAPE_GHOST, SHAPE_TOAD, SHAPE_KNIGHT, SHAPE_COIL, SHAPE_DOCTOR,
    SHAPE_RATKING, SHAPE_BANKER, SHAPE_MIRROR, SHAPE_HOST, SHAPE_DRAKE
} ShapeId;

typedef struct {
    const char *name;
    uint8_t hp;
    uint8_t dice;
    uint8_t equip[MAX_FOE_EQUIP];
    uint8_t equip_count;
    uint8_t gold;
    uint8_t depth_min;
    uint8_t depth_max;
    uint8_t shape;
    bool    boss;
} EnemyDef;

const EnemyDef *enemy_def(uint8_t id);
uint8_t enemy_roll_for_depth(uint8_t depth);
uint8_t enemy_boss_for_level(uint8_t campaign_level);

// --- Rulesets --------------------------------------------------------------
// Episodes are the real level design: each campaign level is a rule that
// rewrites the game rather than a difficulty step. The maps stay thin so the
// rules can carry the variety.
typedef enum {
    RULE_PLAIN = 0,     // Opening Round
    RULE_GREED,         // Curse of Greed — upgraded kit, a curse every turn
    RULE_PARALLEL,      // Parallel Universe — statuses mean different things
    RULE_ELIMINATION,   // lose a card after every fight
    RULE_SUDDEN,        // half max HP, double gold
    RULE_COUNT
} RuleId;

const char *rule_name(RuleId r);
const char *rule_blurb(RuleId r);

// --- Run state -------------------------------------------------------------
typedef enum {
    NODE_EMPTY = 0, NODE_ENEMY, NODE_APPLE, NODE_CHEST, NODE_SHOP, NODE_ANVIL, NODE_DOOR
} NodeKind;

typedef struct { uint8_t kind; uint8_t arg; bool used; } Node;

typedef enum { MODE_CAMPAIGN = 0, MODE_ENDLESS } GameMode;

typedef struct {
    bool     active;
    uint8_t  mode;          // GameMode
    uint8_t  campaign_level;// 0..4 when MODE_CAMPAIGN
    uint8_t  rule;          // RuleId in force on this floor
    uint8_t  floor;         // 1..3 in campaign; unbounded in endless
    uint8_t  level;         // hero level, drives dice and HP
    int16_t  hp, max_hp;
    uint16_t gold;
    uint16_t limit_charge;
    bool     limit_ready;

    uint8_t  equip[MAX_EQUIP];
    uint8_t  equip_up;      // bitmask: which equipped cards the anvil improved
    uint8_t  pack[MAX_BACKPACK];
    uint8_t  pack_up;

    Node     nodes[MAX_NODES];
    uint8_t  node_count;
    uint8_t  foes_cleared;
    uint8_t  foes_required;
} RunState;

extern RunState g_run;

#define WARRIOR_BASE_HP     24
#define WARRIOR_HP_PER_LVL   4

uint8_t warrior_dice_for_level(uint8_t level);
uint8_t run_equip_slots(void);
uint8_t run_depth(void);            // campaign floor, or endless floor
void    run_new(GameMode mode, uint8_t campaign_level);
void    run_build_floor(void);
void    run_descend(void);
bool    run_backpack_add(uint8_t item_id);
int     run_equip_free_slot(void);
bool    run_is_upgraded(bool equipped, int index);
void    run_set_upgraded(bool equipped, int index);
int16_t run_enemy_hp(uint8_t enemy_id);   // endless scaling folded in
uint8_t run_enemy_dice(uint8_t enemy_id);

void    save_run(void);
bool    load_run(void);
void    clear_run(void);

// --- Progress & scores -----------------------------------------------------
#define SCORE_SLOTS 10

typedef struct {
    uint8_t floors;
    uint8_t level;
    uint16_t gold;
    uint8_t kit[MAX_EQUIP];
} ScoreEntry;

typedef struct {
    uint8_t     unlocked;               // campaign levels available, 1..5
    uint8_t     cleared;                // bitmask of levels beaten
    uint8_t     score_count;
    ScoreEntry  scores[SCORE_SLOTS];
} Progress;

extern Progress g_prog;

void progress_load(void);
void progress_save(void);
void progress_mark_cleared(uint8_t campaign_level);
int  progress_submit_score(void);       // returns the rank, or -1

// --- Windows ---------------------------------------------------------------
void floor_window_push(void);
void combat_window_push(uint8_t enemy_id);
void backpack_window_push(void);
void shop_window_push(uint8_t node_index);
void anvil_window_push(uint8_t node_index);
void scores_window_push(void);
void howto_window_push(void);
void gameover_window_push(bool won);
void splash_window_push(void);

typedef enum { BATTLE_NONE = 0, BATTLE_WON, BATTLE_LOST } BattleResult;
extern BattleResult g_last_battle;
