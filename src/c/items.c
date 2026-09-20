#include "game.h"

// ---------------------------------------------------------------------------
// The equipment catalogue and the loot tables.
//
// The loot tables are the least visible and most important part of the level
// design: which items can appear at which depth is curated, not weighted
// against a global rarity roll. Two-slot equipment only appears from depth 3,
// because that is the floor where you get a fourth die and can afford to spend
// two of them on one card.
//
// On a two-slot item the value fed to the effects is the SUM of its dice.
// A negative EFF_HEAL is self-harm, which is how Backfire pays for its damage.
// ---------------------------------------------------------------------------

#define C_ANY       {COND_ANY, 0}
#define C_ODD       {COND_ODD, 0}
#define C_EVEN      {COND_EVEN, 0}
#define C_EXACT(n)  {COND_EXACT, (n)}
#define C_MIN(n)    {COND_MIN, (n)}
#define C_MAX(n)    {COND_MAX, (n)}
#define E_NONE      {EFF_NONE, 0, 0, 0}

static const ItemDef s_items[] = {
    /* 0 */ {"Sword",       1, {C_ANY},        {{EFF_DAMAGE, 1, 0, 0}, E_NONE}, 0, 1,  6},
    /* 1 */ {"Shield",      1, {C_ANY},        {{EFF_SHIELD, 1, 0, 0}, E_NONE}, 0, 1,  6},
    /* 2 */ {"Hammer",      1, {C_EVEN},       {{EFF_DAMAGE, 1, 2, 0}, E_NONE}, 0, 1,  8},
    /* 3 */ {"Spear",       1, {C_ODD},        {{EFF_DAMAGE, 1, 1, 0}, E_NONE}, 0, 1,  8},
    /* 4 */ {"Dagger",      1, {C_MAX(3)},     {{EFF_DAMAGE, 1, 3, 0}, E_NONE}, 0, 1,  8},
    /* 5 */ {"Bandage",     1, {C_ANY},        {{EFF_HEAL,   1, 0, 0}, E_NONE}, 2, 1,  7},
    /* 6 */ {"Backfire",    1, {C_ANY},        {{EFF_DAMAGE, 2, 0, 0}, {EFF_HEAL, 0, -2, 0}}, 0, 1, 9},
    /* 7 */ {"Buckler",     1, {C_MAX(4)},     {{EFF_SHIELD, 1, 2, 0}, E_NONE}, 0, 1,  8},

    /* 8 */ {"Broadsword",  1, {C_ANY},        {{EFF_DAMAGE, 1, 2, 0}, E_NONE}, 0, 2, 14},
    /* 9 */ {"Flame Tongue",1, {C_ANY},        {{EFF_DAMAGE, 1, 0, 0}, {EFF_INFLICT, 0, 0, ST_BURN}}, 0, 2, 15},
    /*10 */ {"Frost Brand", 1, {C_ANY},        {{EFF_DAMAGE, 1, 0, 0}, {EFF_INFLICT, 0, 0, ST_FROZEN}}, 0, 2, 15},
    /*11 */ {"Tower Shield",1, {C_MIN(4)},     {{EFF_SHIELD, 2, 0, 0}, E_NONE}, 0, 2, 14},
    /*12 */ {"Vampire Fang",1, {C_ANY},        {{EFF_DRAIN,  1, 0, 0}, E_NONE}, 0, 2, 16},
    /*13 */ {"Poison Dirk", 1, {C_ODD},        {{EFF_DAMAGE, 1, 0, 0}, {EFF_INFLICT, 0, 0, ST_POISON}}, 0, 2, 15},
    /*14 */ {"Warhammer",   1, {C_EVEN},       {{EFF_DAMAGE, 2, 0, 0}, E_NONE}, 0, 2, 16},
    /*15 */ {"Taser",       1, {C_MIN(3)},     {{EFF_DAMAGE, 1, 0, 0}, {EFF_INFLICT, 0, 0, ST_SHOCK}}, 0, 2, 15},
    /*16 */ {"Tonic",       1, {C_ANY},        {{EFF_HEAL,   1, 2, 0}, E_NONE}, 2, 2, 13},

    /*17 */ {"Thunderbolt", 1, {C_EXACT(6)},   {{EFF_DAMAGE, 0, 12, 0}, E_NONE}, 0, 3, 22},
    /*18 */ {"Executioner", 2, {C_ANY, C_ANY}, {{EFF_DAMAGE, 1, 3, 0}, E_NONE}, 0, 3, 24},
    /*19 */ {"Rage",        1, {C_ANY},        {{EFF_SELF, 0, 0, ST_FURY}, E_NONE}, 1, 3, 20},
    /*20 */ {"Greataxe",    1, {C_MIN(5)},     {{EFF_DAMAGE, 2, 2, 0}, E_NONE}, 0, 3, 23},
    /*21 */ {"Elixir",      1, {C_ANY},        {{EFF_HEAL,   1, 3, 0}, E_NONE}, 1, 3, 18},
    /*22 */ {"Duel Blade",  2, {C_ODD, C_ODD}, {{EFF_DAMAGE, 2, 0, 0}, E_NONE}, 0, 3, 25},

    // Enemy-only kit. Drawn on screen during a fight, so nothing an enemy does
    // should ever be a surprise — only a problem.
    /*23 */ {"Claw",        1, {C_ANY},        {{EFF_DAMAGE, 1, 0, 0}, E_NONE}, 0, 1, 0},
    /*24 */ {"Nibble",      1, {C_MAX(3)},     {{EFF_DAMAGE, 1, 1, 0}, E_NONE}, 0, 1, 0},
    /*25 */ {"Hide",        1, {C_ANY},        {{EFF_SHIELD, 1, 0, 0}, E_NONE}, 0, 1, 0},
    /*26 */ {"Chill Touch", 1, {C_ODD},        {{EFF_INFLICT, 0, 0, ST_FROZEN}, E_NONE}, 0, 1, 0},
    /*27 */ {"Ember",       1, {C_EVEN},       {{EFF_INFLICT, 0, 0, ST_BURN}, E_NONE}, 0, 1, 0},
    /*28 */ {"Siphon",      1, {C_ANY},        {{EFF_DRAIN,  1, 0, 0}, E_NONE}, 0, 1, 0},
    /*29 */ {"Static",      1, {C_MIN(4)},     {{EFF_INFLICT, 0, 0, ST_SHOCK}, E_NONE}, 0, 1, 0},
    /*30 */ {"Spit",        1, {C_ANY},        {{EFF_INFLICT, 0, 0, ST_POISON}, E_NONE}, 0, 1, 0},
    /*31 */ {"Smother",     1, {C_ANY},        {{EFF_INFLICT, 0, 0, ST_BLIND}, E_NONE}, 0, 1, 0},
    /*32 */ {"Crush",       1, {C_MIN(5)},     {{EFF_DAMAGE, 2, 0, 0}, E_NONE}, 0, 1, 0},
    /*33 */ {"Hex",         1, {C_ANY},        {{EFF_INFLICT, 0, 0, ST_WEAKEN}, E_NONE}, 0, 1, 0},
    /*34 */ {"Dragon Bite", 1, {C_ANY},        {{EFF_DAMAGE, 1, 3, 0}, E_NONE}, 0, 1, 0},
    /*35 */ {"Wing Buffet", 1, {C_EVEN},       {{EFF_DAMAGE, 1, 0, 0}, {EFF_INFLICT, 0, 0, ST_SILENCE}}, 0, 1, 0},
    /*36 */ {"Audit",       1, {C_ANY},        {{EFF_DAMAGE, 1, 1, 0}, {EFF_INFLICT, 0, 0, ST_WEAKEN}}, 0, 1, 0},
    /*37 */ {"Reflect",     1, {C_ODD},        {{EFF_DAMAGE, 2, 0, 0}, E_NONE}, 0, 1, 0},
    /*38 */ {"Applause",    1, {C_EVEN},       {{EFF_SHIELD, 1, 2, 0}, E_NONE}, 0, 1, 0},
    /*39 */ {"Buzzer",      1, {C_ANY},        {{EFF_INFLICT, 0, 0, ST_SILENCE}, E_NONE}, 0, 1, 0},
    /*40 */ {"Gnaw",        1, {C_ODD},        {{EFF_DRAIN, 1, 1, 0}, E_NONE}, 0, 1, 0},

    // The Warrior's signature move, and the one card exempt from the
    // once-per-turn rule: its allowance is set each turn from how many dice you
    // have, so a bigger handful of dice also buys you more chances to fix one.
    // Feeding it a die rerolls THAT die and gives it straight back, so the cost
    // is the card's allowance rather than the die itself.
    /*41 */ {"Reroll",      1, {C_ANY},        {{EFF_REROLL, 0, 0, 0}, E_NONE}, 1, 1, 12},

    // --- Sixes ------------------------------------------------------------
    // A 6 is the roll you are happiest to see and the one hardest to place,
    // since so many cards want something small or something odd. These give it
    // somewhere to go, and pay accordingly.
    /*42 */ {"Sunburst",    1, {C_EXACT(6)},   {{EFF_DAMAGE, 0, 9, 0}, {EFF_INFLICT, 0, 0, ST_BURN}}, 0, 2, 18},
    /*43 */ {"Six Shooter", 1, {C_EXACT(6)},   {{EFF_DAMAGE, 0, 7, 0}, {EFF_SELF, 0, 0, ST_FURY}}, 1, 3, 24},
    /*44 */ {"Crown",       1, {C_EXACT(6)},   {{EFF_SHIELD, 0, 8, 0}, {EFF_HEAL, 0, 4, 0}}, 0, 2, 17},
    /*45 */ {"Loaded Die",  1, {C_MIN(5)},     {{EFF_DAMAGE, 2, 1, 0}, E_NONE}, 0, 2, 16},
    /*46 */ {"Jackpot",     2, {C_EXACT(6), C_EXACT(6)}, {{EFF_DAMAGE, 0, 24, 0}, E_NONE}, 0, 3, 28},

    // --- Fussy, but worth it ----------------------------------------------
    // Cards with a narrow mouth and a big payoff. They turn a die you would
    // have thrown away into the best move of the turn — and are dead weight on
    // the turn the roll does not cooperate. That trade is the whole point.
    /*47 */ {"Stiletto",    1, {C_EXACT(1)},   {{EFF_DAMAGE, 0, 8, 0}, E_NONE}, 0, 2, 16},
    /*48 */ {"Snake Eyes",  2, {C_EXACT(1), C_EXACT(1)}, {{EFF_DAMAGE, 0, 20, 0}, E_NONE}, 0, 3, 26},
    /*49 */ {"Garrote",     1, {C_MAX(2)},     {{EFF_DAMAGE, 0, 6, 0}, {EFF_INFLICT, 0, 0, ST_POISON}}, 0, 2, 17},
    /*50 */ {"Pin",         1, {C_EXACT(2)},   {{EFF_DAMAGE, 0, 7, 0}, E_NONE}, 0, 1, 11},
    /*51 */ {"Trident",     1, {C_EXACT(3)},   {{EFF_DAMAGE, 0, 9, 0}, E_NONE}, 0, 2, 15},
    /*52 */ {"Quarterstaff",1, {C_EXACT(4)},   {{EFF_DAMAGE, 0, 6, 0}, {EFF_SHIELD, 0, 4, 0}}, 0, 2, 15},
    /*53 */ {"Gambit",      1, {C_EXACT(1)},   {{EFF_REROLL, 0, 0, 0}, {EFF_SHIELD, 0, 5, 0}}, 2, 2, 16},
};

#define ITEM_TOTAL ((uint8_t)(sizeof(s_items) / sizeof(s_items[0])))

const ItemDef *item_def(uint8_t id) {
    return (id >= ITEM_TOTAL) ? &s_items[0] : &s_items[id];
}

bool item_is_two_slot(uint8_t id) { return item_def(id)->slots > 1; }
bool item_is_reroll(uint8_t id)   { return id == ITEM_REROLL; }

// --- Loot tables, curated by depth -----------------------------------------
// Depth 1 holds nothing that a player with two dice cannot solve. Two-slot
// cards wait for depth 3, when the fourth die arrives.
static const uint8_t s_early[] = {0, 1, 2, 3, 5, 7, 50};
static const uint8_t s_mid[]   = {2, 3, 4, 6, 7, 8, 9, 10, 11, 16,
                                  42, 44, 45, 47, 49, 51, 52, 53};
static const uint8_t s_late[]  = {9, 10, 11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 22,
                                  42, 43, 44, 45, 46, 47, 48, 49, 51, 52, 53};

uint8_t item_roll_for_floor(uint8_t depth) {
    if (depth <= 1)      return s_early[rand() % (sizeof(s_early))];
    else if (depth == 2) return s_mid[rand() % (sizeof(s_mid))];
    return s_late[rand() % (sizeof(s_late))];
}

// --- Conditions ------------------------------------------------------------
bool cond_accepts(Cond c, uint8_t pips) {
    switch (c.kind) {
        case COND_ANY:   return true;
        case COND_ODD:   return (pips & 1) == 1;
        case COND_EVEN:  return (pips & 1) == 0;
        case COND_EXACT: return pips == c.arg;
        case COND_MIN:   return pips >= c.arg;
        case COND_MAX:   return pips <= c.arg;
        default:         return false;
    }
}

void cond_label(Cond c, char *out, size_t len) {
    switch (c.kind) {
        case COND_ANY:   snprintf(out, len, "any");        break;
        case COND_ODD:   snprintf(out, len, "odd");        break;
        case COND_EVEN:  snprintf(out, len, "even");       break;
        case COND_EXACT: snprintf(out, len, "=%d", c.arg); break;
        case COND_MIN:   snprintf(out, len, "%d+", c.arg); break;
        case COND_MAX:   snprintf(out, len, "%d-", c.arg); break;
        default:         snprintf(out, len, "?");          break;
    }
}

void cond_label_short(Cond c, char *out, size_t len) {
    switch (c.kind) {
        case COND_ANY:   out[0] = '\0';                   break;
        case COND_ODD:   snprintf(out, len, "O");          break;
        case COND_EVEN:  snprintf(out, len, "E");          break;
        case COND_EXACT: snprintf(out, len, "=%d", c.arg); break;
        case COND_MIN:   snprintf(out, len, "%d+", c.arg); break;
        case COND_MAX:   snprintf(out, len, "%d-", c.arg); break;
        default:         out[0] = '\0';                   break;
    }
}

// --- Labels ----------------------------------------------------------------
static void effect_tag(const Effect *e, int8_t bonus, char *out, size_t len) {
    const char *verb;
    switch (e->kind) {
        case EFF_DAMAGE: verb = "dmg";  break;
        case EFF_SHIELD: verb = "shld"; break;
        case EFF_HEAL:   verb = (e->mult == 0 && e->add < 0) ? "self" : "heal"; break;
        case EFF_DRAIN:  verb = "drain";break;
        case EFF_REROLL:  snprintf(out, len, "reroll"); return;
        case EFF_INFLICT: snprintf(out, len, "%s", status_short((StatusId)e->status)); return;
        case EFF_SELF:    snprintf(out, len, "+%s", status_short((StatusId)e->status)); return;
        default: out[0] = '\0'; return;
    }
    int add = e->add + bonus;
    if (e->mult == 0) {
        snprintf(out, len, "%s%d", verb, add < 0 ? -add : add);
    } else if (e->mult == 1 && add == 0) {
        snprintf(out, len, "%s", verb);
    } else if (e->mult == 1) {
        snprintf(out, len, "%s%+d", verb, add);
    } else if (add == 0) {
        snprintf(out, len, "%sx%d", verb, e->mult);
    } else {
        snprintf(out, len, "%sx%d%+d", verb, e->mult, add);
    }
}

void item_effect_label_up(uint8_t id, bool upgraded, char *out, size_t len) {
    const ItemDef *d = item_def(id);
    char a[18] = "", b[18] = "";
    effect_tag(&d->eff[0], upgraded ? 1 : 0, a, sizeof(a));
    effect_tag(&d->eff[1], 0, b, sizeof(b));
    if (b[0]) snprintf(out, len, "%s,%s", a, b);
    else      snprintf(out, len, "%s", a);
}

void item_effect_label(uint8_t id, char *out, size_t len) {
    item_effect_label_up(id, false, out, len);
}

// --- Status text -----------------------------------------------------------
static const char *s_short[ST_COUNT] = { "BRN","FRZ","SHK","WKN","BLD","PSN","FRY","SIL" };
static const char *s_name[ST_COUNT]  = {
    "Burn","Frozen","Shocked","Weakened","Blind","Poison","Fury","Silence"
};
// Spoken in plain English the moment a status lands. On a screen with no room
// for a tooltip, narration is how the game teaches itself.
static const char *s_blurb[ST_COUNT] = {
    "A die catches fire. 2 HP to put out",
    "Your best die becomes a 1",
    "A card is offline. Drop any die on it",
    "One card does half",
    "Dice hidden until you spend them",
    "3 HP a turn, straight through shields",
    "Your next card fires twice",
    "No Limit Break this turn"
};

const char *status_short(StatusId s) { return (s < ST_COUNT) ? s_short[s] : "?"; }
const char *status_name(StatusId s)  { return (s < ST_COUNT) ? s_name[s]  : "?"; }
const char *status_blurb(StatusId s) { return (s < ST_COUNT) ? s_blurb[s] : ""; }
