// Fuzz the combat screen's touch state machine on the host, under ASan/UBSan.
//
// combat.c is included rather than linked so the fuzzer can reach its statics:
// the point is to drive touch_handler directly and then assert on the internal
// state it leaves behind.
#include "../../src/c/combat.c"

#include <assert.h>

static uint32_t s_seed = 1;
static uint32_t rnd(void) { s_seed = s_seed * 1103515245u + 12345u; return s_seed >> 8; }
static int      rr(int n) { return n > 0 ? (int)(rnd() % (uint32_t)n) : 0; }

static long g_events, g_checks;

// Every invariant the rest of combat.c relies on. A touch must never be able
// to break one of these, whatever order the events arrive in.
static void check_invariants(const char *where) {
    g_checks++;
    if (!(s_dice_n <= MAX_DICE))
        { fprintf(stderr, "FAIL %s: s_dice_n=%d > MAX_DICE\n", where, s_dice_n); abort(); }
    if (!(s_card_n <= MAX_EQUIP))
        { fprintf(stderr, "FAIL %s: s_card_n=%d > MAX_EQUIP\n", where, s_card_n); abort(); }
    if (s_drag_die >= 0 && s_drag_die >= (int8_t)s_dice_n)
        { fprintf(stderr, "FAIL %s: s_drag_die=%d but s_dice_n=%d\n", where, s_drag_die, s_dice_n); abort(); }
    if (s_pick_die >= 0 && s_pick_die >= (int8_t)s_dice_n)
        { fprintf(stderr, "FAIL %s: s_pick_die=%d but s_dice_n=%d\n", where, s_pick_die, s_dice_n); abort(); }
    if (s_hover_card >= 0 && s_hover_card >= (int8_t)s_card_n)
        { fprintf(stderr, "FAIL %s: s_hover_card=%d but s_card_n=%d\n", where, s_hover_card, s_card_n); abort(); }
    if (s_cursor < 0 || s_cursor > (int8_t)(s_card_n + s_action_n))
        { fprintf(stderr, "FAIL %s: s_cursor=%d out of 0..%d\n", where, s_cursor, s_card_n + s_action_n); abort(); }
    for (int i = 0; i < s_card_n; i++) {
        const ItemDef *d = item_def(s_cards[i].id);
        for (int sl = 0; sl < MAX_SLOTS; sl++) {
            int8_t f = s_cards[i].filled[sl];
            if (f >= 0 && f >= (int8_t)s_dice_n) {
                fprintf(stderr, "FAIL %s: card %d slot %d holds die %d but s_dice_n=%d\n",
                        where, i, sl, f, s_dice_n); abort();
            }
            if (sl >= d->slots && f >= 0) {
                fprintf(stderr, "FAIL %s: card %d has a die in unused slot %d\n", where, i, sl); abort();
            }
        }
    }
}

static void fire(TouchEventType t, int16_t x, int16_t y, bool nonnav) {
    TouchEvent e; e.type = t; e.x = x; e.y = y; e.non_navigational = nonnav;
    g_events++;
    touch_handler(&e, NULL);
    check_invariants("after touch");
}

// A gesture that lands wherever the fuzzer aims, with real intermediate motion.
static void gesture(int16_t w, int16_t h) {
    int16_t x1 = (int16_t)rr(w), y1 = (int16_t)rr(h);
    int16_t x2 = (int16_t)rr(w), y2 = (int16_t)rr(h);
    bool nonnav = (rr(20) == 0);
    fire(TouchEvent_Touchdown, x1, y1, nonnav);
    int steps = rr(6);
    for (int i = 1; i <= steps; i++)
        fire(TouchEvent_PositionUpdate,
             (int16_t)(x1 + (x2 - x1) * i / (steps + 1)),
             (int16_t)(y1 + (y2 - y1) * i / (steps + 1)), nonnav);
    if (rr(12)) fire(TouchEvent_Liftoff, x2, y2, nonnav);   // sometimes never lift
}

int main(int argc, char **argv) {
    long iters = (argc > 1) ? atol(argv[1]) : 200000;
    if (argc > 2) s_seed = (uint32_t)atol(argv[2]);
    int16_t W = 200, H = 228;

    for (long it = 0; it < iters; it++) {
        if (it % 500 == 0) {           // restart a fight periodically
            // Alternate the two shipping displays: the round one computes a
            // completely different layout, so its hit rects differ too.
            bool round = (rr(2) == 0);
            host_set_round(round);
            W = round ? 260 : 200; H = round ? 260 : 228;
            host_set_screen(W, H);
            g_run.active = true;
            g_run.mode = MODE_CAMPAIGN;
            g_run.klass = (uint8_t)rr(CLASS_COUNT);
            g_run.rule = (uint8_t)rr(RULE_COUNT);
            g_run.floor = (uint8_t)rr(4);
            g_run.max_hp = 40; g_run.hp = 40;
            g_run.level = (uint8_t)(1 + rr(6));
            for (int i = 0; i < MAX_EQUIP; i++) g_run.equip[i] = (uint8_t)rr(50);
            combat_window_push((uint8_t)rr(16), rr(5) == 0);
            window_load(s_window);      // the SDK would call this; stubs do not
            begin_player_turn();        // rolls the dice
            check_invariants("after begin");
        }
        // Mix touch with every other way the screen can be driven. A player
        // does not politely finish one gesture before the foe's timer fires.
        switch (rr(10)) {
            case 0: case 1: case 2: case 3:
                gesture(W, H); break;
            case 4: click_up(NULL, NULL);     check_invariants("up");     break;
            case 5: click_down(NULL, NULL);   check_invariants("down");   break;
            case 6: click_select(NULL, NULL); check_invariants("select"); break;
            case 7: click_back(NULL, NULL);   check_invariants("back");   break;
            case 8: click_select_long(NULL, NULL); check_invariants("long"); break;
            case 9: {
                Layer *l = s_layer; GContext *c = NULL;
                if (l) { layer_update(l, c); check_invariants("draw"); }
                break;
            }
        }
        if (rr(40) == 0) { frame(NULL);            check_invariants("frame"); }
        if (rr(60) == 0) { foe_step(NULL);         check_invariants("foe_step"); }
        if (rr(70) == 0) { begin_player_turn();    check_invariants("new turn"); }
        if (rr(50) == 0) { s_phase = (Phase)rr(3); }        // a turn ends under the finger
        if (rr(80) == 0) { s_phase = PHASE_PLAYER; roll_side(true); check_invariants("after reroll"); }
        if (rr(90) == 0) { s_phase = PHASE_PLAYER; do_action((int8_t)rr(s_action_n ? s_action_n : 1));
                           check_invariants("action"); }
    }
    printf("ok: %ld gestures, %ld touch events, %ld invariant checks\n",
           iters, g_events, g_checks);
    return 0;
}
