#pragma once
#include <pebble.h>

// ---------------------------------------------------------------------------
// A very small tween toolkit.
//
// There is no floating point on this hardware and no need for any: progress is
// carried as 0..1000 and every easing curve below is integer arithmetic. The
// clock is advanced by whichever window is currently animating, so the whole
// thing costs one counter.
// ---------------------------------------------------------------------------

#define ANIM_FRAME_MS 33        // ~30fps

typedef struct {
    uint32_t start;
    uint16_t dur;
    bool     active;
} Tween;

void     anim_tick(uint16_t dt_ms);
uint32_t anim_now(void);
void     anim_reset(void);

void     tw_start(Tween *t, uint16_t dur_ms);
void     tw_stop(Tween *t);
bool     tw_running(const Tween *t);    // started and not yet finished
int32_t  tw_p(const Tween *t);          // 0..1000, saturating at 1000

// Easing, all taking and returning 0..1000.
int32_t ease_out_quad(int32_t p);
int32_t ease_in_quad(int32_t p);
int32_t ease_out_back(int32_t p);       // overshoots, then settles — for snaps
int32_t ease_out_bounce(int32_t p);

// Interpolate a..b by p (0..1000).
int32_t lerp(int32_t a, int32_t b, int32_t p);
GPoint  lerp_pt(GPoint a, GPoint b, int32_t p);
GRect   lerp_rect(GRect a, GRect b, int32_t p);

// A continuous oscillation for idle motion: returns -amp..+amp over `period`.
int16_t anim_bob(uint16_t period_ms, int16_t amp, uint16_t phase_ms);

// Scale a rect about its own centre, in per-mille (1000 = unchanged).
GRect rect_scale(GRect r, int32_t sx, int32_t sy);
