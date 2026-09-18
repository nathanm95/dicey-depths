#include "anim.h"

static uint32_t s_now;

void anim_tick(uint16_t dt_ms) { s_now += dt_ms; }
uint32_t anim_now(void) { return s_now; }
void anim_reset(void) { s_now = 0; }

void tw_start(Tween *t, uint16_t dur_ms) {
    t->start = s_now;
    t->dur = dur_ms ? dur_ms : 1;
    t->active = true;
}

void tw_stop(Tween *t) { t->active = false; }

int32_t tw_p(const Tween *t) {
    if (!t->active) return 1000;
    uint32_t e = s_now - t->start;
    if (e >= t->dur) return 1000;
    return (int32_t)((e * 1000) / t->dur);
}

bool tw_running(const Tween *t) {
    return t->active && (s_now - t->start) < t->dur;
}

int32_t ease_out_quad(int32_t p) {
    int32_t inv = 1000 - p;
    return 1000 - (inv * inv) / 1000;
}

int32_t ease_in_quad(int32_t p) {
    return (p * p) / 1000;
}

int32_t ease_out_back(int32_t p) {
    // Classic back-out with s ~= 1.70158, scaled into integers. Overshoots a
    // little past the target and settles, which is what makes a die feel like
    // it lands rather than arrives.
    int32_t x = p - 1000;
    int32_t x2 = (x * x) / 1000;
    int32_t x3 = (x2 * x) / 1000;
    return 1000 + x3 * 2701 / 1000 + x2 * 1701 / 1000;
}

int32_t ease_out_bounce(int32_t p) {
    if (p < 364) {
        return (7563 * p / 1000) * p / 1000;
    } else if (p < 727) {
        p -= 545;
        return (7563 * p / 1000) * p / 1000 + 750;
    } else if (p < 909) {
        p -= 818;
        return (7563 * p / 1000) * p / 1000 + 938;
    }
    p -= 955;
    return (7563 * p / 1000) * p / 1000 + 984;
}

int32_t lerp(int32_t a, int32_t b, int32_t p) {
    return a + ((b - a) * p) / 1000;
}

GPoint lerp_pt(GPoint a, GPoint b, int32_t p) {
    return GPoint((int16_t)lerp(a.x, b.x, p), (int16_t)lerp(a.y, b.y, p));
}

GRect lerp_rect(GRect a, GRect b, int32_t p) {
    return GRect((int16_t)lerp(a.origin.x, b.origin.x, p),
                 (int16_t)lerp(a.origin.y, b.origin.y, p),
                 (int16_t)lerp(a.size.w, b.size.w, p),
                 (int16_t)lerp(a.size.h, b.size.h, p));
}

int16_t anim_bob(uint16_t period_ms, int16_t amp, uint16_t phase_ms) {
    if (period_ms == 0) return 0;
    uint32_t t = (s_now + phase_ms) % period_ms;
    int32_t angle = (int32_t)((TRIG_MAX_ANGLE * t) / period_ms);
    return (int16_t)((sin_lookup(angle) * amp) / TRIG_MAX_RATIO);
}

GRect rect_scale(GRect r, int32_t sx, int32_t sy) {
    int16_t w = (int16_t)((r.size.w * sx) / 1000);
    int16_t h = (int16_t)((r.size.h * sy) / 1000);
    return GRect(r.origin.x + (r.size.w - w) / 2,
                 r.origin.y + (r.size.h - h) / 2, w, h);
}
