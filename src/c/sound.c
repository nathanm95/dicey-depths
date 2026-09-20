#include "sound.h"

#define PERSIST_KEY_SOUND 120

static bool s_on = true;

#define N(note, wave, ms, vel) {(note), (wave), (ms), (vel), 0}
#define REST(ms)               {0, 0, (ms), 0, 0}

static const SpeakerNote s_bank[]  = { N(72, SpeakerWaveformSquare, 55, 70) };
static const SpeakerNote s_place[] = { N(67, SpeakerWaveformSquare, 45, 60) };
static const SpeakerNote s_fire[]  = { N(64, SpeakerWaveformSquare, 45, 85),
                                       N(71, SpeakerWaveformSquare, 70, 85) };
static const SpeakerNote s_hurt[]  = { N(48, SpeakerWaveformSawtooth, 90, 95),
                                       N(43, SpeakerWaveformSawtooth, 110, 90) };
static const SpeakerNote s_heal[]  = { N(69, SpeakerWaveformTriangle, 70, 70),
                                       N(76, SpeakerWaveformTriangle, 110, 70) };
// Falling, because that is what busting feels like.
static const SpeakerNote s_bust[]  = { N(60, SpeakerWaveformSquare, 60, 90),
                                       N(55, SpeakerWaveformSquare, 60, 90),
                                       N(48, SpeakerWaveformSawtooth, 160, 95) };
static const SpeakerNote s_limit[] = { N(72, SpeakerWaveformSquare, 50, 90),
                                       N(76, SpeakerWaveformSquare, 50, 90),
                                       N(79, SpeakerWaveformSquare, 120, 95) };
static const SpeakerNote s_win[]   = { N(72, SpeakerWaveformSquare, 90, 90),
                                       N(76, SpeakerWaveformSquare, 90, 90),
                                       N(79, SpeakerWaveformSquare, 90, 90),
                                       N(84, SpeakerWaveformSquare, 220, 95) };
static const SpeakerNote s_lose[]  = { N(60, SpeakerWaveformTriangle, 140, 80),
                                       N(56, SpeakerWaveformTriangle, 140, 80),
                                       N(53, SpeakerWaveformTriangle, 320, 85) };

typedef struct { const SpeakerNote *notes; uint32_t n; } Sfx;

static const Sfx s_sfx[SFX_COUNT] = {
    [SFX_BANK]  = {s_bank,  sizeof(s_bank)  / sizeof(SpeakerNote)},
    [SFX_PLACE] = {s_place, sizeof(s_place) / sizeof(SpeakerNote)},
    [SFX_FIRE]  = {s_fire,  sizeof(s_fire)  / sizeof(SpeakerNote)},
    [SFX_HURT]  = {s_hurt,  sizeof(s_hurt)  / sizeof(SpeakerNote)},
    [SFX_HEAL]  = {s_heal,  sizeof(s_heal)  / sizeof(SpeakerNote)},
    [SFX_BUST]  = {s_bust,  sizeof(s_bust)  / sizeof(SpeakerNote)},
    [SFX_LIMIT] = {s_limit, sizeof(s_limit) / sizeof(SpeakerNote)},
    [SFX_WIN]   = {s_win,   sizeof(s_win)   / sizeof(SpeakerNote)},
    [SFX_LOSE]  = {s_lose,  sizeof(s_lose)  / sizeof(SpeakerNote)},
};

void sound_init(void) {
    s_on = persist_exists(PERSIST_KEY_SOUND) ? persist_read_bool(PERSIST_KEY_SOUND) : true;
}

bool sound_enabled(void) { return s_on; }

void sound_set_enabled(bool on) {
    s_on = on;
    persist_write_bool(PERSIST_KEY_SOUND, on);   // remembered across launches
    if (!on) speaker_stop();
}

// Starting a sequence while another is still playing is what took the emulator
// down: winning a fight fires SFX_WIN and the first-win achievement fires
// SFX_LIMIT a microsecond later, and the two overlapping calls killed the whole
// qemu process - so the app did not merely exit, it vanished along with the
// emulator. Stop whatever is playing before starting anything, and never let
// two sounds be asked for in the same instant.
void sound_play(SfxId id) {
    if (!s_on || id >= SFX_COUNT) return;
    if (speaker_is_muted()) return;          // the watch's own setting wins
    const Sfx *f = &s_sfx[id];
    if (!f->notes || !f->n) return;
    speaker_stop();
    speaker_play_notes(f->notes, f->n, 80);
}
