#include "portraits.h"
#include "ui.h"

// Percent-of-box coordinates, so one portrait definition works at any size.
#define X(p) ((int16_t)(r.origin.x + (r.size.w * (p)) / 100))
#define Y(p) ((int16_t)(r.origin.y + (r.size.h * (p)) / 100))
#define W(p) ((int16_t)((r.size.w * (p)) / 100))
#define H(p) ((int16_t)((r.size.h * (p)) / 100))

static bool s_flash;
static GColor pc(GColor c) { return s_flash ? GColorWhite : c; }

// A filled triangle by scanline — the SDK has no polygon fill that can be
// scaled per frame, and at portrait size this is a few dozen line draws.
static void fill_tri(GContext *ctx, GPoint p0, GPoint p1, GPoint p2, GColor col) {
    graphics_context_set_stroke_color(ctx, col);
    graphics_context_set_stroke_width(ctx, 1);
    GPoint t;
    if (p1.y < p0.y) { t = p0; p0 = p1; p1 = t; }
    if (p2.y < p0.y) { t = p0; p0 = p2; p2 = t; }
    if (p2.y < p1.y) { t = p1; p1 = p2; p2 = t; }
    if (p2.y == p0.y) return;
    for (int16_t y = p0.y; y <= p2.y; y++) {
        int32_t xa = p0.x + (int32_t)(p2.x - p0.x) * (y - p0.y) / (p2.y - p0.y);
        int32_t xb;
        bool second = (y > p1.y) || (p1.y == p0.y);
        if (second) {
            if (p2.y == p1.y) xb = p1.x;
            else xb = p1.x + (int32_t)(p2.x - p1.x) * (y - p1.y) / (p2.y - p1.y);
        } else {
            if (p1.y == p0.y) xb = p1.x;
            else xb = p0.x + (int32_t)(p1.x - p0.x) * (y - p0.y) / (p1.y - p0.y);
        }
        if (xa > xb) { int32_t s = xa; xa = xb; xb = s; }
        graphics_draw_line(ctx, GPoint((int16_t)xa, y), GPoint((int16_t)xb, y));
    }
}

// A stadium/blob: a rounded rect with the radius maxed out.
static void blob(GContext *ctx, GRect b, GColor col) {
    if (b.size.w <= 0 || b.size.h <= 0) return;
    int16_t rad = (b.size.w < b.size.h ? b.size.w : b.size.h) / 2;
    graphics_context_set_fill_color(ctx, col);
    graphics_fill_rect(ctx, b, rad, GCornersAll);
}

static void dot(GContext *ctx, int16_t x, int16_t y, uint16_t rad, GColor col) {
    graphics_context_set_fill_color(ctx, col);
    graphics_fill_circle(ctx, GPoint(x, y), rad);
}

static void eyes(GContext *ctx, GRect r, int16_t ly, uint16_t rad) {
    dot(ctx, X(35), ly, rad, pc(GColorWhite));
    dot(ctx, X(65), ly, rad, pc(GColorWhite));
    dot(ctx, X(35), ly, rad / 2 ? rad / 2 : 1, pc(GColorBlack));
    dot(ctx, X(65), ly, rad / 2 ? rad / 2 : 1, pc(GColorBlack));
}

// Pixel art exists for a handful of enemies so far. Anything not listed here
// falls through to the procedural portrait below, so the roster never has a
// hole in it and art can be added one enemy at a time.
uint32_t portrait_resource_for(uint8_t shape) {
    switch (shape) {
        case SHAPE_RAT:    return RESOURCE_ID_IMAGE_ENEMY_RAT;
        case SHAPE_SLIME:  return RESOURCE_ID_IMAGE_ENEMY_SLIME;
        case SHAPE_KNIGHT: return RESOURCE_ID_IMAGE_ENEMY_KNIGHT;
        case SHAPE_DRAKE:  return RESOURCE_ID_IMAGE_ENEMY_DRAKE;
        case SHAPE_WASP:    return RESOURCE_ID_IMAGE_ENEMY_WASP;
        case SHAPE_SPONGE:  return RESOURCE_ID_IMAGE_ENEMY_SPONGE;
        case SHAPE_KETTLE:  return RESOURCE_ID_IMAGE_ENEMY_KETTLE;
        case SHAPE_LANTERN: return RESOURCE_ID_IMAGE_ENEMY_LANTERN;
        case SHAPE_BOULDER: return RESOURCE_ID_IMAGE_ENEMY_BOULDER;
        case SHAPE_SNAIL:   return RESOURCE_ID_IMAGE_ENEMY_SNAIL;
        default:           return 0;
    }
}

void portrait_draw(GContext *ctx, uint8_t shape, GRect r, bool flash) {
    if (r.size.w < 8 || r.size.h < 8) return;
    s_flash = flash;
    uint16_t er = (uint16_t)(W(9) < 2 ? 2 : W(9));

    switch (shape) {
        case SHAPE_RAT:
            dot(ctx, X(20), Y(28), (uint16_t)W(13), pc(GColorLightGray));
            dot(ctx, X(80), Y(28), (uint16_t)W(13), pc(GColorLightGray));
            blob(ctx, GRect(X(12), Y(25), W(76), H(65)), pc(GColorLightGray));
            graphics_context_set_stroke_color(ctx, pc(GColorMelon));
            graphics_context_set_stroke_width(ctx, 3);
            graphics_draw_line(ctx, GPoint(X(88), Y(80)), GPoint(X(104), Y(58)));
            graphics_context_set_stroke_width(ctx, 1);
            eyes(ctx, r, Y(52), er);
            dot(ctx, X(50), Y(72), (uint16_t)W(6), pc(GColorMelon));
            break;

        case SHAPE_SLIME:
            blob(ctx, GRect(X(6), Y(30), W(88), H(66)), pc(GColorGreen));
            blob(ctx, GRect(X(20), Y(38), W(18), H(12)), pc(GColorMintGreen));
            eyes(ctx, r, Y(62), er);
            break;

        case SHAPE_VACUUM:
            graphics_context_set_fill_color(ctx, pc(GColorLightGray));
            graphics_fill_rect(ctx, GRect(X(18), Y(20), W(64), H(52)), 4, GCornersAll);
            fill_tri(ctx, GPoint(X(24), Y(72)), GPoint(X(60), Y(72)), GPoint(X(8), Y(98)),
                     pc(GColorDarkGray));
            dot(ctx, X(50), Y(42), er, pc(GColorWhite));
            dot(ctx, X(50), Y(42), er / 2 ? er / 2 : 1, pc(GColorBlack));
            break;

        case SHAPE_IMP:
            fill_tri(ctx, GPoint(X(18), Y(36)), GPoint(X(30), Y(10)), GPoint(X(38), Y(36)),
                     pc(GColorDarkCandyAppleRed));
            fill_tri(ctx, GPoint(X(62), Y(36)), GPoint(X(70), Y(10)), GPoint(X(82), Y(36)),
                     pc(GColorDarkCandyAppleRed));
            blob(ctx, GRect(X(14), Y(28), W(72), H(66)), pc(GColorRed));
            eyes(ctx, r, Y(56), er);
            break;

        case SHAPE_SPRITE:
            fill_tri(ctx, GPoint(X(50), Y(8)), GPoint(X(8), Y(56)), GPoint(X(92), Y(56)),
                     pc(GColorCeleste));
            fill_tri(ctx, GPoint(X(50), Y(98)), GPoint(X(8), Y(56)), GPoint(X(92), Y(56)),
                     pc(GColorCyan));
            eyes(ctx, r, Y(52), (uint16_t)(er * 3 / 4));
            break;

        case SHAPE_GHOST: {
            blob(ctx, GRect(X(12), Y(14), W(76), H(64)), pc(GColorWhite));
            graphics_context_set_fill_color(ctx, pc(GColorWhite));
            graphics_fill_rect(ctx, GRect(X(12), Y(44), W(76), H(38)), 0, GCornerNone);
            for (int i = 0; i < 3; i++) {
                dot(ctx, (int16_t)(X(24) + i * W(26)), Y(82), (uint16_t)W(13), pc(GColorWhite));
            }
            graphics_context_set_stroke_color(ctx, pc(GColorBlack));
            graphics_draw_round_rect(ctx, GRect(X(12), Y(14), W(76), H(64)), 12);
            dot(ctx, X(36), Y(46), er, pc(GColorBlack));
            dot(ctx, X(64), Y(46), er, pc(GColorBlack));
            break;
        }

        case SHAPE_TOAD:
            blob(ctx, GRect(X(4), Y(44), W(92), H(52)), pc(GColorIslamicGreen));
            dot(ctx, X(30), Y(38), (uint16_t)W(15), pc(GColorWhite));
            dot(ctx, X(70), Y(38), (uint16_t)W(15), pc(GColorWhite));
            dot(ctx, X(30), Y(40), (uint16_t)W(7), pc(GColorBlack));
            dot(ctx, X(70), Y(40), (uint16_t)W(7), pc(GColorBlack));
            graphics_context_set_stroke_color(ctx, pc(GColorBlack));
            graphics_draw_line(ctx, GPoint(X(28), Y(74)), GPoint(X(72), Y(74)));
            break;

        case SHAPE_KNIGHT:
            graphics_context_set_fill_color(ctx, pc(GColorLightGray));
            graphics_fill_rect(ctx, GRect(X(18), Y(16), W(64), H(72)), 6, GCornersAll);
            graphics_context_set_fill_color(ctx, pc(GColorBlack));
            graphics_fill_rect(ctx, GRect(X(26), Y(42), W(48), H(12)), 2, GCornersAll);
            fill_tri(ctx, GPoint(X(50), Y(0)), GPoint(X(38), Y(18)), GPoint(X(62), Y(18)),
                     pc(GColorFolly));
            break;

        case SHAPE_COIL:
            graphics_context_set_fill_color(ctx, pc(GColorDarkGray));
            graphics_fill_rect(ctx, GRect(X(38), Y(30), W(24), H(64)), 3, GCornersAll);
            dot(ctx, X(50), Y(26), (uint16_t)W(22), pc(GColorDarkGray));
            for (int i = 0; i < 3; i++) {
                int16_t yy = (int16_t)(Y(20) + anim_bob((uint16_t)(220 + i * 70), (int16_t)H(6),
                                                        (uint16_t)(i * 90)));
                graphics_context_set_stroke_color(ctx, pc(GColorYellow));
                graphics_context_set_stroke_width(ctx, 2);
                graphics_draw_line(ctx, GPoint(X(20 + i * 22), yy), GPoint(X(34 + i * 22), Y(6)));
            }
            graphics_context_set_stroke_width(ctx, 1);
            break;

        case SHAPE_DOCTOR:
            fill_tri(ctx, GPoint(X(50), Y(20)), GPoint(X(6), Y(98)), GPoint(X(94), Y(98)),
                     pc(GColorPurple));
            dot(ctx, X(50), Y(24), (uint16_t)W(20), pc(GColorWindsorTan));
            graphics_context_set_fill_color(ctx, pc(GColorWhite));
            graphics_fill_rect(ctx, GRect(X(34), Y(20), W(32), H(10)), 2, GCornersAll);
            break;

        case SHAPE_RATKING:
            dot(ctx, X(20), Y(34), (uint16_t)W(13), pc(GColorLightGray));
            dot(ctx, X(80), Y(34), (uint16_t)W(13), pc(GColorLightGray));
            blob(ctx, GRect(X(12), Y(30), W(76), H(64)), pc(GColorLightGray));
            eyes(ctx, r, Y(56), er);
            dot(ctx, X(50), Y(74), (uint16_t)W(6), pc(GColorMelon));
            fill_tri(ctx, GPoint(X(26), Y(28)), GPoint(X(34), Y(6)), GPoint(X(42), Y(28)), pc(COL_GOLD));
            fill_tri(ctx, GPoint(X(42), Y(28)), GPoint(X(50), Y(2)), GPoint(X(58), Y(28)), pc(COL_GOLD));
            fill_tri(ctx, GPoint(X(58), Y(28)), GPoint(X(66), Y(6)), GPoint(X(74), Y(28)), pc(COL_GOLD));
            break;

        case SHAPE_BANKER:
            graphics_context_set_fill_color(ctx, pc(GColorDarkGreen));
            graphics_fill_rect(ctx, GRect(X(14), Y(26), W(72), H(70)), 5, GCornersAll);
            graphics_context_set_fill_color(ctx, pc(GColorBlack));
            graphics_fill_rect(ctx, GRect(X(10), Y(14), W(80), H(12)), 3, GCornersAll);
            dot(ctx, X(50), Y(60), (uint16_t)W(20), pc(COL_GOLD));
            ui_draw_text_c(ctx, "$", ui_font_small(),
                           GRect(X(30), Y(44), W(40), H(34)), pc(GColorBlack), GTextAlignmentCenter);
            break;

        case SHAPE_MIRROR:
            fill_tri(ctx, GPoint(X(50), Y(4)), GPoint(X(10), Y(50)), GPoint(X(90), Y(50)),
                     pc(GColorCeleste));
            fill_tri(ctx, GPoint(X(50), Y(96)), GPoint(X(10), Y(50)), GPoint(X(90), Y(50)),
                     pc(GColorBlueMoon));
            graphics_context_set_stroke_color(ctx, pc(GColorWhite));
            graphics_context_set_stroke_width(ctx, 3);
            graphics_draw_line(ctx, GPoint(X(32), Y(38)), GPoint(X(46), Y(24)));
            graphics_context_set_stroke_width(ctx, 1);
            break;

        case SHAPE_HOST:
            blob(ctx, GRect(X(26), Y(6), W(48), H(44)), pc(GColorWindsorTan));
            fill_tri(ctx, GPoint(X(50), Y(50)), GPoint(X(4), Y(98)), GPoint(X(96), Y(98)),
                     pc(GColorImperialPurple));
            graphics_context_set_fill_color(ctx, pc(GColorWhite));
            graphics_fill_rect(ctx, GRect(X(44), Y(52), W(12), H(28)), 2, GCornersAll);
            dot(ctx, X(38), Y(26), er, pc(GColorBlack));
            dot(ctx, X(62), Y(26), er, pc(GColorBlack));
            dot(ctx, X(84), Y(60), (uint16_t)W(11), pc(GColorDarkGray));
            break;

        case SHAPE_WASP: {
            int16_t buzz = anim_bob(160, 2, 0);   // wings too fast to see properly
            fill_tri(ctx, GPoint(X(30), Y(30)), GPoint(X(4), (int16_t)(Y(6) + buzz)),
                     GPoint(X(34), Y(54)), pc(GColorCeleste));
            fill_tri(ctx, GPoint(X(70), Y(30)), GPoint(X(96), (int16_t)(Y(6) - buzz)),
                     GPoint(X(66), Y(54)), pc(GColorCeleste));
            blob(ctx, GRect(X(30), Y(26), W(40), H(60)), pc(COL_GOLD));
            graphics_context_set_fill_color(ctx, pc(GColorBlack));
            graphics_fill_rect(ctx, GRect(X(30), Y(50), W(40), H(8)), 0, GCornerNone);
            graphics_fill_rect(ctx, GRect(X(30), Y(68), W(40), H(8)), 0, GCornerNone);
            dot(ctx, X(40), Y(36), er, pc(GColorBlack));
            dot(ctx, X(60), Y(36), er, pc(GColorBlack));
            break;
        }

        case SHAPE_SPONGE:
            graphics_context_set_fill_color(ctx, pc(COL_GOLD));
            graphics_fill_rect(ctx, GRect(X(10), Y(20), W(80), H(70)), 6, GCornersAll);
            for (int i = 0; i < 6; i++) {
                dot(ctx, (int16_t)(X(22) + (i % 3) * W(28)),
                    (int16_t)(Y(34) + (i / 3) * H(30)), (uint16_t)W(7), pc(GColorWindsorTan));
            }
            eyes(ctx, r, Y(56), er);
            break;

        case SHAPE_KETTLE: {
            blob(ctx, GRect(X(16), Y(38), W(68), H(54)), pc(GColorLightGray));
            graphics_context_set_fill_color(ctx, pc(GColorDarkGray));
            graphics_fill_rect(ctx, GRect(X(38), Y(26), W(24), H(14)), 3, GCornersAll);
            fill_tri(ctx, GPoint(X(84), Y(52)), GPoint(X(102), Y(44)), GPoint(X(86), Y(66)),
                     pc(GColorLightGray));
            // steam, because what it does is set you on fire
            for (int i = 0; i < 3; i++) {
                dot(ctx, (int16_t)(X(50) + anim_bob((uint16_t)(600 + i * 130), (int16_t)W(6),
                                                    (uint16_t)(i * 200))),
                    (int16_t)(Y(20) - i * H(8)), (uint16_t)W(5), pc(GColorMelon));
            }
            eyes(ctx, r, Y(62), er);
            break;
        }

        case SHAPE_LANTERN:
            graphics_context_set_fill_color(ctx, pc(GColorDarkGray));
            graphics_fill_rect(ctx, GRect(X(40), Y(4), W(20), H(14)), 2, GCornersAll);
            graphics_context_set_fill_color(ctx, pc(COL_GOLD));
            graphics_fill_rect(ctx, GRect(X(22), Y(18), W(56), H(64)), 5, GCornersAll);
            graphics_context_set_fill_color(ctx, pc(GColorYellow));
            graphics_fill_rect(ctx, GRect(X(32), Y(30), W(36), H(40)), 4, GCornersAll);
            dot(ctx, X(40), Y(46), er, pc(GColorBlack));
            dot(ctx, X(60), Y(46), er, pc(GColorBlack));
            break;

        case SHAPE_BOULDER:
            blob(ctx, GRect(X(6), Y(26), W(88), H(68)), pc(GColorDarkGray));
            blob(ctx, GRect(X(18), Y(34), W(26), H(18)), pc(GColorLightGray));
            dot(ctx, X(38), Y(62), er, pc(GColorWhite));
            dot(ctx, X(64), Y(62), er, pc(GColorWhite));
            dot(ctx, X(38), Y(62), er / 2 ? er / 2 : 1, pc(GColorBlack));
            dot(ctx, X(64), Y(62), er / 2 ? er / 2 : 1, pc(GColorBlack));
            break;

        case SHAPE_SNAIL:
            blob(ctx, GRect(X(6), Y(58), W(72), H(32)), pc(GColorMelon));
            dot(ctx, X(62), Y(48), (uint16_t)W(28), pc(GColorWindsorTan));
            dot(ctx, X(62), Y(48), (uint16_t)W(18), pc(GColorPastelYellow));
            dot(ctx, X(62), Y(48), (uint16_t)W(8),  pc(GColorWindsorTan));
            graphics_context_set_stroke_color(ctx, pc(GColorMelon));
            graphics_context_set_stroke_width(ctx, 2);
            graphics_draw_line(ctx, GPoint(X(16), Y(60)), GPoint(X(10), Y(40)));
            graphics_draw_line(ctx, GPoint(X(26), Y(60)), GPoint(X(30), Y(40)));
            graphics_context_set_stroke_width(ctx, 1);
            dot(ctx, X(10), Y(38), (uint16_t)W(5), pc(GColorBlack));
            dot(ctx, X(30), Y(38), (uint16_t)W(5), pc(GColorBlack));
            break;

        case SHAPE_DRAKE:
        default: {
            int16_t flap = anim_bob(700, (int16_t)H(9), 0);
            fill_tri(ctx, GPoint(X(22), Y(46)), GPoint(X(-8), (int16_t)(Y(22) + flap)),
                     GPoint(X(10), Y(76)), pc(GColorDarkCandyAppleRed));
            fill_tri(ctx, GPoint(X(78), Y(46)), GPoint(X(108), (int16_t)(Y(22) + flap)),
                     GPoint(X(90), Y(76)), pc(GColorDarkCandyAppleRed));
            blob(ctx, GRect(X(20), Y(24), W(60), H(68)), pc(GColorRed));
            fill_tri(ctx, GPoint(X(34), Y(24)), GPoint(X(42), Y(2)), GPoint(X(50), Y(24)),
                     pc(GColorOrange));
            fill_tri(ctx, GPoint(X(50), Y(24)), GPoint(X(58), Y(2)), GPoint(X(66), Y(24)),
                     pc(GColorOrange));
            dot(ctx, X(38), Y(52), er, pc(COL_GOLD));
            dot(ctx, X(62), Y(52), er, pc(COL_GOLD));
            dot(ctx, X(38), Y(52), er / 2 ? er / 2 : 1, pc(GColorBlack));
            dot(ctx, X(62), Y(52), er / 2 ? er / 2 : 1, pc(GColorBlack));
            break;
        }
    }
    s_flash = false;
}
