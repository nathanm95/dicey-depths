#include "game.h"

// ---------------------------------------------------------------------------
// The classes.
//
// Each one is a different answer to the same question: how do you introduce
// uncertainty into a turn so that turn-to-turn play stays interesting? The
// Warrior rerolls what he is given. The Robot is handed nothing at all — it
// builds each number itself, and can push that too far.
// ---------------------------------------------------------------------------

static const ClassDef s_classes[CLASS_COUNT] = {
    [CLASS_WARRIOR] = {
        "Warrior",
        "Rolls dice, and rerolls them. The place to start.",
        24,
        {0, 1, ITEM_REROLL}, 3        // Sword, Shield, Reroll
    },
    [CLASS_ROBOT] = {
        "Robot",
        "No dice. Build each number, or bust trying.",
        28,
        {0, 1, 7}, 3                  // Sword, Shield, Buckler
    },
};

const ClassDef *class_def(uint8_t id) {
    return (id < CLASS_COUNT) ? &s_classes[id] : &s_classes[0];
}

// Both classes step 2 / 3 / 4 across a three-floor run: for the Warrior that is
// dice rolled, for the Robot it is numbers it may build.
uint8_t class_dice_for_level(uint8_t klass, uint8_t level) {
    (void)klass;
    if (level >= 3) return 4;
    if (level >= 2) return 3;
    return 2;
}
