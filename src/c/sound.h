#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// Chiptune.
//
// speaker_play_notes takes MIDI notes and square/triangle/sawtooth waveforms,
// which is a Game Boy in all but name — fitting, since the original's music was
// written on one. Only Time 2 has a speaker; Round 2 has none, and the API is
// declared on both, so every call here is simply silent there. The game must be
// as good with the sound off, and it is: nothing below carries information that
// is not already on screen.
// ---------------------------------------------------------------------------

typedef enum {
    SFX_BANK = 0,   // a die lands
    SFX_PLACE,      // a die goes into a card
    SFX_FIRE,       // a card goes off
    SFX_HURT,       // you take a hit
    SFX_HEAL,
    SFX_BUST,       // the Robot pushed once too often
    SFX_LIMIT,      // a Limit Break charges
    SFX_WIN,
    SFX_LOSE,
    SFX_COUNT
} SfxId;

void sound_init(void);
bool sound_enabled(void);
void sound_set_enabled(bool on);
void sound_play(SfxId id);
