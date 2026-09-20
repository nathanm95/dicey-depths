#include "game.h"

// ---------------------------------------------------------------------------
// Rulesets.
//
// Each campaign level is a rule that rewrites the game rather than a difficulty
// step. This is where the variety lives: the maps are deliberately thin so the
// rules can carry it, and a rule is a flag rather than a system.
// ---------------------------------------------------------------------------

static const char *s_names[RULE_COUNT] = {
    "Opening Round",
    "Curse of Greed",
    "Parallel Universe",
    "Elimination Round",
    "Sudden Death",
    "Bonus: Hoard",
    "Bonus: Swarm",
    "Bonus: Backstage"
};

static const char *s_blurbs[RULE_COUNT] = {
    "Standard rules. Learn the game.",
    "Start with upgraded weapons - and take a curse at the start of every turn.",
    "Statuses mean different things. Frozen drops every die by 1. Burn is usable, but jams that card. Poison infects a card instead of you.",
    "Lose one piece of equipment after every fight. Travel light.",
    "Half the health. Double the gold. Good luck.",
    "Six card slots from the very first floor - and everything you meet is tougher for it.",
    "Every enemy rolls an extra die. There is no safe turn.",
    "Six slots, tougher enemies, an extra die each. Everything at once."
};

const char *rule_name(RuleId r)  { return (r < RULE_COUNT) ? s_names[r]  : "?"; }
const char *rule_blurb(RuleId r) { return (r < RULE_COUNT) ? s_blurbs[r] : ""; }

bool rule_is_bonus(uint8_t r) { return r >= RULE_CAMPAIGN_COUNT && r < RULE_COUNT; }

// The Bonus Rounds are the campaign's encore: they only appear once every one
// of the five episodes has actually been beaten.
bool bonus_rounds_unlocked(void) {
    // Clearing an episode on any difficulty counts towards the encore.
    uint8_t any = 0;
    for (int d = 0; d < DIFF_COUNT; d++) any |= g_prog.cleared[d];
    uint8_t all = (uint8_t)((1 << RULE_CAMPAIGN_COUNT) - 1);
    return (any & all) == all;
}
