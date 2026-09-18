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
    "Sudden Death"
};

static const char *s_blurbs[RULE_COUNT] = {
    "Standard rules. Learn the game.",
    "Start with upgraded weapons - and take a curse at the start of every turn.",
    "Statuses mean different things. Frozen drops every die by 1. Burn is usable, but jams that card. Poison infects a card instead of you.",
    "Lose one piece of equipment after every fight. Travel light.",
    "Half the health. Double the gold. Good luck."
};

const char *rule_name(RuleId r)  { return (r < RULE_COUNT) ? s_names[r]  : "?"; }
const char *rule_blurb(RuleId r) { return (r < RULE_COUNT) ? s_blurbs[r] : ""; }
