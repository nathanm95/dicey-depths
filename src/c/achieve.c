#include "game.h"
#include "achieve.h"
#include "sound.h"

static const char *s_names[ACH_COUNT] = {
    "First Blood", "Episode Cleared", "Giant Killer", "Lucky Six",
    "Pushed It", "Fully Loaded", "Ten Deep", "Untouched",
    "Iron Stomach", "Champion", "Undisputed"
};
static const char *s_blurbs[ACH_COUNT] = {
    "Win your first fight",
    "Clear a campaign episode",
    "Beat an elite",
    "Land a six on a card that wanted one",
    "Push the CPU once too often",
    "Carry six cards at once",
    "Reach floor 10 in endless",
    "Win a fight without losing any health",
    "Clear an episode on Hard",
    "Clear all five episodes on Normal",
    "Clear all five episodes on Hard"
};

const char *ach_name(AchId id)  { return (id < ACH_COUNT) ? s_names[id]  : "?"; }
const char *ach_blurb(AchId id) { return (id < ACH_COUNT) ? s_blurbs[id] : ""; }

bool ach_has(AchId id) {
    return (id < ACH_COUNT) && ((g_prog.achieved >> id) & 1);
}

void ach_unlock(AchId id) {
    if (id >= ACH_COUNT || ach_has(id)) return;
    g_prog.achieved |= (uint16_t)(1 << id);
    progress_save();
    vibes_short_pulse();
    sound_play(SFX_LIMIT);
}
