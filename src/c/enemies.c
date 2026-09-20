#include "game.h"

// ---------------------------------------------------------------------------
// The roster.
//
// Enemies play the same game you do, with the same dice and the same slot
// conditions, and their kit is drawn on screen during the fight. Mechanics came
// first and the fiction was fitted afterwards, which is how a health-draining
// monster ended up as a vacuum cleaner rather than a bat.
//
// depth_min/max are on a 1..6 scale; endless clamps into that range and then
// scales HP and dice separately, so the roster never runs out.
// ---------------------------------------------------------------------------

static const EnemyDef s_enemies[] = {
  /* 0*/ {"Rat",          10, 2, {24, 23},         2,  3, 1, 2, SHAPE_RAT,     false},
  /* 1*/ {"Slime",        14, 2, {23, 25},         2,  3, 1, 2, SHAPE_SLIME,   false},
  /* 2*/ {"Vacuum",       16, 2, {28, 23},         2,  4, 2, 4, SHAPE_VACUUM,  false},
  /* 3*/ {"Imp",          15, 2, {27, 23},         2,  4, 1, 3, SHAPE_IMP,     false},
  /* 4*/ {"Ice Sprite",   18, 2, {26, 23},         2,  5, 2, 4, SHAPE_SPRITE,  false},
  /* 5*/ {"Ghost",        22, 3, {31, 23, 25},     3,  6, 3, 6, SHAPE_GHOST,   false},
  /* 6*/ {"Toad",         24, 2, {30, 23},         2,  6, 3, 6, SHAPE_TOAD,    false},
  /* 7*/ {"Knight",       26, 3, {23, 25, 32},     3,  8, 3, 6, SHAPE_KNIGHT,  false},
  /* 8*/ {"Tesla Coil",   28, 3, {29, 23, 23},     3,  8, 4, 6, SHAPE_COIL,    false},
  /* 9*/ {"Witch Doctor", 30, 3, {33, 30, 23},     3,  9, 4, 6, SHAPE_DOCTOR,  false},

  // Mechanics first, fiction fitted afterwards — the same principle that turned
  // a health-draining monster into a vacuum cleaner.
  /*10*/ {"Wasp",         14, 3, {23, 23, 24},     3,  5, 2, 4, SHAPE_WASP,    false},
  /*11*/ {"Sponge",       30, 2, {25, 25},         2,  6, 2, 5, SHAPE_SPONGE,  false},
  /*12*/ {"Kettle",       20, 2, {27, 23},         2,  6, 3, 6, SHAPE_KETTLE,  false},
  /*13*/ {"Lantern",      22, 2, {31, 23},         2,  7, 3, 6, SHAPE_LANTERN, false},
  /*14*/ {"Boulder",      38, 1, {32},             1,  9, 4, 6, SHAPE_BOULDER, false},
  /*15*/ {"Snail",        34, 2, {28, 25},         2,  8, 4, 6, SHAPE_SNAIL,   false},

  // One boss per campaign level, themed to its ruleset.
  /*16*/ {"The Rat King", 32, 3, {40, 23, 25},     3, 20, 3, 3, SHAPE_RATKING, true},
  /*17*/ {"The Banker",   46, 3, {36, 23, 25},     3, 22, 3, 3, SHAPE_BANKER,  true},
  /*18*/ {"The Mirror",   44, 3, {37, 26, 25},     3, 24, 3, 3, SHAPE_MIRROR,  true},
  /*19*/ {"The Host",     50, 4, {38, 39, 23, 25}, 4, 26, 3, 3, SHAPE_HOST,    true},
  /*20*/ {"The Drake",    50, 4, {34, 35, 27, 25}, 4, 30, 3, 3, SHAPE_DRAKE,   true},
};

#define ENEMY_TOTAL ((uint8_t)(sizeof(s_enemies) / sizeof(s_enemies[0])))
#define BOSS_FIRST 16   // must match the real index of The Rat King below

const EnemyDef *enemy_def(uint8_t id) {
    return (id >= ENEMY_TOTAL) ? &s_enemies[0] : &s_enemies[id];
}

uint8_t enemy_boss_for_level(uint8_t campaign_level) {
    uint8_t id = (uint8_t)(BOSS_FIRST + campaign_level);
    return (id >= ENEMY_TOTAL) ? (uint8_t)(ENEMY_TOTAL - 1) : id;
}

uint8_t enemy_roll_for_depth(uint8_t depth) {
    if (depth < 1) depth = 1;
    if (depth > 6) depth = 6;     // endless keeps scaling past the table
    uint8_t pool[ENEMY_TOTAL];
    uint8_t n = 0;
    for (uint8_t i = 0; i < ENEMY_TOTAL; i++) {
        if (s_enemies[i].boss) continue;
        if (depth >= s_enemies[i].depth_min && depth <= s_enemies[i].depth_max) pool[n++] = i;
    }
    if (n == 0) return 0;
    return pool[rand() % n];
}
