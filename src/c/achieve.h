#pragma once
#include <pebble.h>

// Small, specific, and each one a reason to play a level you have already
// beaten. Nothing here is a grind counter.
typedef enum {
    ACH_FIRST_WIN = 0,
    ACH_CLEAR_EPISODE,
    ACH_ELITE,          // beat an elite
    ACH_SIX_BONUS,      // land a six on a card that pays for it
    ACH_BUST,           // push the CPU once too often
    ACH_FULL_GRID,      // carry six cards at once
    ACH_DEPTH_10,       // endless floor 10
    ACH_UNTOUCHED,      // win a fight without losing a single point of health
    ACH_HARD_EPISODE,   // clear an episode on Hard
    ACH_SWEEP_NORMAL,   // all five on Normal
    ACH_SWEEP_HARD,     // all five on Hard
    ACH_COUNT
} AchId;

const char *ach_name(AchId id);
const char *ach_blurb(AchId id);
bool ach_has(AchId id);
void ach_unlock(AchId id);      // no-op if already held
