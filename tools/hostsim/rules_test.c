// Regression tests for the 1.2.1 rules fixes, on the host.
//
// Like fuzz_touch.c this includes combat.c so it can reach the statics. Each
// test sets up the state it needs directly and asserts the rule, so a failure
// names the rule that broke rather than an invariant three steps later.
//
//   tools/hostsim/build.sh && tools/hostsim/rules_test
#include "../../src/c/combat.c"
#include "achieve.h"

#include <assert.h>

static int s_fails, s_checks;
#define CHECK(cond, ...) do { s_checks++; if (!(cond)) { s_fails++; \
    fprintf(stderr, "FAIL %s:%d  ", __func__, __LINE__); fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); } } while (0)

static bool queued(const char *needle) {
    if (s_toast_on && strstr(s_toast, needle)) return true;
    for (int i = 0; i < s_toast_qn; i++) if (strstr(s_toast_q[i], needle)) return true;
    return false;
}

static void fresh_run(GameMode mode, uint8_t level, uint8_t klass) {
    memset(&g_prog, 0, sizeof(g_prog));
    g_prog.unlocked = 1;
    run_new(mode, level, klass);
}

static void open_fight(uint8_t enemy, bool elite) {
    if (s_window) { window_disappear(s_window); window_unload(s_window); }
    combat_window_push(enemy, elite);
    window_load(s_window);
}

static void test_anvil_labels(void) {
    char a[24];
    item_effect_label_up(14, true, a, sizeof(a));   // Warhammer, dmg x2
    CHECK(!strcmp(a, "dmgx2+2"), "Warhammer improved reads '%s'", a);
    item_effect_label_up(2, true, a, sizeof(a));    // Hammer, dmg +2
    CHECK(!strcmp(a, "dmg+3"), "Hammer improved reads '%s'", a);
    item_effect_label_up(0, true, a, sizeof(a));    // Sword
    CHECK(!strcmp(a, "dmg+1"), "Sword improved reads '%s'", a);
    item_effect_label_up(14, false, a, sizeof(a));
    CHECK(!strcmp(a, "dmgx2"), "Warhammer plain reads '%s'", a);
}

static void test_weaken_halves_output(void) {
    fresh_run(MODE_CAMPAIGN, 0, CLASS_WARRIOR);
    open_fight(14, false);                  // Boulder: 38 HP, plenty to hit
    s_foe.shield = 0;
    int16_t hp = s_foe.hp;
    apply_effects(item_def(47), 1, 1, true, true, false);   // Stiletto: flat 8
    CHECK(hp - s_foe.hp == 4, "weakened Stiletto did %d, want 4", hp - s_foe.hp);
    hp = s_foe.hp;
    apply_effects(item_def(0), 6, 6, true, true, false);    // Sword on a 6
    CHECK(hp - s_foe.hp == 3, "weakened Sword on 6 did %d, want 3", hp - s_foe.hp);
    hp = s_foe.hp;
    int16_t me = s_hero.hp;
    apply_effects(item_def(6), 5, 5, true, true, false);    // Backfire: x2, 2 self
    CHECK(hp - s_foe.hp == 5, "weakened Backfire on 5 did %d, want 5", hp - s_foe.hp);
    CHECK(me - s_hero.hp == 2, "weakened Backfire's recoil was %d, want 2 (not halved)", me - s_hero.hp);
    hp = s_foe.hp;
    apply_effects(item_def(0), 6, 6, true, false, false);   // and unweakened
    CHECK(hp - s_foe.hp == 6, "plain Sword on 6 did %d, want 6", hp - s_foe.hp);
}

static void test_blind_enemy_plays_at_random(void) {
    fresh_run(MODE_CAMPAIGN, 0, CLASS_WARRIOR);
    open_fight(7, false);                   // Knight: Claw, Hide, Crush (5+)
    int fumbles = 0, plans = 0, sighted_fumbles = 0;
    for (int t = 0; t < 3000; t++) {
        bool blind = (t % 2) == 0;
        s_foe.status[ST_BLIND] = blind ? 1 : 0;
        for (int i = 0; i < s_foe_card_n; i++) {
            s_foe_cards[i].spent = false; s_foe_cards[i].disabled = false; s_foe_cards[i].jam = 0;
        }
        for (int i = 0; i < s_foe_dice_n; i++) {
            s_foe_dice[i].pips = (uint8_t)(1 + rand() % 6); s_foe_dice[i].burning = false;
        }
        plan_foe_turn();
        bool card_seen[MAX_FOE_EQUIP] = {false}, die_seen[MAX_FOE_DICE] = {false};
        for (int i = 0; i < s_plan_n; i++) {
            int8_t c = s_plan_card[i], d = s_plan_die[i];
            CHECK(c >= 0 && c < s_foe_card_n, "plan card %d out of range", c);
            CHECK(d >= 0 && d < s_foe_dice_n, "plan die %d out of range", d);
            if (c < 0 || c >= s_foe_card_n || d < 0 || d >= s_foe_dice_n) continue;
            CHECK(!card_seen[c], "card %d planned twice", c);
            CHECK(!die_seen[d], "die %d planned twice", d);
            card_seen[c] = die_seen[d] = true;
            CHECK(cond_accepts(item_def(s_foe_cards[c].id)->cond[0], s_foe_dice[d].pips),
                  "planned a die the card cannot take");
        }
        plans += s_plan_n;
        if (blind) fumbles += s_foe_fumbles; else sighted_fumbles += s_foe_fumbles;
    }
    CHECK(fumbles > 0, "a blind Knight never missed a card in 1500 turns");
    CHECK(sighted_fumbles == 0, "a sighted enemy fumbled %d times", sighted_fumbles);
    CHECK(plans > 0, "nothing was ever planned");
}

static void test_spoils_named_and_applied(void) {
    // Elimination Round (campaign level 3): six slots, all full; pack full too.
    fresh_run(MODE_CAMPAIGN, 3, CLASS_WARRIOR);
    static const uint8_t kit[MAX_EQUIP] = {0, 1, ITEM_REROLL, 5, 2, 3};
    for (int i = 0; i < MAX_EQUIP; i++) g_run.equip[i] = kit[i];
    for (int i = 0; i < MAX_BACKPACK; i++) g_run.pack[i] = 7;
    open_fight(7, true);
    s_foe.hp = 0;
    check_end();
    CHECK(g_last_battle == BATTLE_WON, "the fight was not won");
    CHECK(g_spoils.toll >= 0 && g_spoils.toll < MAX_EQUIP, "toll %d with six cards", g_spoils.toll);
    CHECK(g_spoils.drop != ITEM_NONE, "an elite left no drop");
    CHECK(g_spoils.drop_kept, "the toll emptied a slot, so the drop fits");
    CHECK(queued("Rare drop"), "the drop was not announced");
    CHECK(queued("Elimination takes your"), "the toll was not announced");

    // Four cards on Normal is the toll's floor, and a full pack: the drop is lost.
    fresh_run(MODE_CAMPAIGN, 3, CLASS_WARRIOR);
    for (int i = 0; i < MAX_BACKPACK; i++) g_run.pack[i] = 7;
    g_run.equip[4] = 2; g_run.equip[5] = 3;  // fill the two late slots: no free slot
    g_run.equip[3] = ITEM_NONE;              // ...except this one
    g_run.equip[2] = ITEM_NONE;              // four cards held, under the floor
    open_fight(7, true);
    s_foe.hp = 0;
    check_end();
    CHECK(g_spoils.toll == -1, "toll fired at the floor of four");
    CHECK(g_spoils.drop_kept, "an empty grid slot should take the drop");

    fresh_run(MODE_CAMPAIGN, 0, CLASS_WARRIOR);   // four slots, four cards
    for (int i = 0; i < MAX_BACKPACK; i++) g_run.pack[i] = 7;
    open_fight(0, true);
    s_foe.hp = 0;
    check_end();
    CHECK(!g_spoils.drop_kept, "nowhere to put it, yet drop_kept");
    CHECK(queued("Pack full"), "a lost drop was not announced");

    // A campaign boss ends the run: no toll to announce on the final win.
    fresh_run(MODE_CAMPAIGN, 3, CLASS_WARRIOR);
    for (int i = 0; i < MAX_EQUIP; i++) g_run.equip[i] = kit[i];
    open_fight(enemy_boss_for_level(3), false);
    s_foe.hp = 0;
    check_end();
    CHECK(g_spoils.toll == -1, "the toll fired on the final boss");
}

static void test_untouched_means_untouched(void) {
    fresh_run(MODE_CAMPAIGN, 0, CLASS_WARRIOR);
    g_run.hp = 20;
    open_fight(0, false);
    deal_damage(&s_hero, 3, true, true);
    heal_up(&s_hero, 10, true);                  // back above where it started
    s_foe.hp = 0;
    check_end();
    CHECK(!ach_has(ACH_UNTOUCHED), "healing back up still counted as untouched");

    fresh_run(MODE_CAMPAIGN, 0, CLASS_WARRIOR);
    open_fight(0, false);
    s_foe.hp = 0;
    check_end();
    CHECK(ach_has(ACH_UNTOUCHED), "a clean win did not count");
}

static void test_full_grid_means_six_cards(void) {
    fresh_run(MODE_CAMPAIGN, 3, CLASS_WARRIOR);  // six slots, four cards
    open_fight(0, false);
    CHECK(!ach_has(ACH_FULL_GRID), "six slots with four cards counted as fully loaded");
    for (int i = 0; i < MAX_EQUIP; i++) if (g_run.equip[i] == ITEM_NONE) g_run.equip[i] = 2;
    open_fight(0, false);
    CHECK(ach_has(ACH_FULL_GRID), "six cards did not count");
}

static void test_endless_rules_per_floor(void) {
    for (int diff = 0; diff < DIFF_COUNT; diff++) {
        difficulty_set((uint8_t)diff);
        fresh_run(MODE_ENDLESS, 0, CLASS_WARRIOR);
        int16_t base = diff_hero_hp(WARRIOR_BASE_HP);
        CHECK(g_run.max_hp == base, "endless floor 1 max %d, want %d", g_run.max_hp, base);
        int sudden = 0;
        for (int f = 0; f < 300; f++) {
            g_run.limit_charge = (uint16_t)g_run.max_hp;   // as charged as it gets
            g_run.limit_ready = false;
            run_descend();
            int16_t full = (int16_t)(base + WARRIOR_HP_PER_LVL * (g_run.level - 1));
            int16_t want = (g_run.rule == RULE_SUDDEN) ? (int16_t)(full * 75 / 100) : full;
            if (g_run.rule == RULE_SUDDEN) sudden++;
            CHECK(g_run.max_hp == want, "floor %d rule %d: max %d want %d",
                  g_run.floor, g_run.rule, g_run.max_hp, want);
            CHECK(g_run.hp == g_run.max_hp, "floor %d: not healed to max", g_run.floor);
            CHECK(g_run.limit_charge <= (uint16_t)g_run.max_hp, "charge %d over max %d",
                  g_run.limit_charge, g_run.max_hp);
            CHECK(g_run.floor < 4 ? g_run.rule == RULE_PLAIN : !rule_is_bonus(g_run.rule),
                  "floor %d drew rule %d", g_run.floor, g_run.rule);
        }
        CHECK(sudden > 0, "300 endless floors and never Sudden Death");
    }
    difficulty_set(DIFF_NORMAL);

    // Campaign is unchanged: +4 a floor, Sudden Death's cut taken at run_new.
    fresh_run(MODE_CAMPAIGN, RULE_SUDDEN, CLASS_WARRIOR);
    int16_t m = g_run.max_hp;
    CHECK(m == 18, "Sudden Death campaign starts at %d, want 18", m);
    run_descend();
    CHECK(g_run.max_hp == m + 4, "campaign descend gave %d, want %d", g_run.max_hp, m + 4);

    // An Endless Greed floor fights with every improvable card improved.
    fresh_run(MODE_ENDLESS, 0, CLASS_WARRIOR);
    g_run.floor = 5; g_run.rule = RULE_GREED;
    open_fight(0, false);
    for (int i = 0; i < s_card_n; i++) {
        if (s_cards[i].id == ITEM_NONE) continue;
        CHECK(s_cards[i].upgraded == item_upgrade_useful(s_cards[i].id),
              "Greed floor: card %d upgraded=%d", s_cards[i].id, s_cards[i].upgraded);
    }
    CHECK(queued("Curse of Greed"), "the floor's ruleset was not announced");

    fresh_run(MODE_ENDLESS, 0, CLASS_WARRIOR);
    g_run.floor = 2;
    open_fight(0, false);
    CHECK(queued("blocks your way"), "a plain floor lost its opening line");
}

static void test_limit_break_does_not_save_mid_fight(void) {
    fresh_run(MODE_CAMPAIGN, 0, CLASS_WARRIOR);
    g_run.hp = 15;
    g_run.in_fight = true;
    save_run();
    open_fight(14, false);
    deal_damage(&s_hero, 5, true, true);
    g_run.limit_ready = true;
    do_limit_break();
    end_turn();
    RunState disk;
    persist_read_data(100, &disk, sizeof(disk));
    CHECK(disk.hp == 15, "disk holds hp %d mid-fight, want the starting 15", disk.hp);
    CHECK(disk.in_fight, "the fight is no longer marked open on disk");
    CHECK(g_run.hp == 15, "g_run.hp moved mid-fight to %d", g_run.hp);
}

static void test_autoroll_is_a_six(void) {
    fresh_run(MODE_CAMPAIGN, 0, CLASS_ROBOT);
    open_fight(14, false);
    s_phase = PHASE_PLAYER;
    s_pending_frozen = 1;
    s_pending_burn = 1;
    g_run.limit_ready = true;
    s_hero.status[ST_SILENCE] = 0;
    uint8_t before = s_dice_n;
    do_action(0);
    CHECK(s_dice_n == before + 1, "Autoroll banked nothing");
    CHECK(s_dice[before].pips == 6, "Autoroll banked a %d", s_dice[before].pips);
    CHECK(!s_dice[before].burning, "Autoroll's six came out burning");
    CHECK(s_pending_frozen == 1 && s_pending_burn == 1, "the waiting statuses were used up");
    s_cpu = 3;
    cpu_bank();
    CHECK(s_dice[s_dice_n - 1].pips == 1, "Frozen did not land on the next bank");
}

// ui.c picks its circle maths at compile time, so build.sh compiles this file
// twice and each binary checks its own display.
static void test_six_foe_dice_fit_the_round(void) {
#ifdef PBL_ROUND
    int round = 1;
#else
    int round = 0;
#endif
    {
        host_set_round(round);
        int16_t W = round ? 260 : 200, H = round ? 260 : 228;
        host_set_screen(W, H);
        fresh_run(MODE_CAMPAIGN, 0, CLASS_WARRIOR);
        open_fight(0, false);
        s_phase = PHASE_FOE;
        s_foe_dice_n = 6;
        Layout L;
        compute_layout(&L);
        for (int i = 0; i < 6; i++) {
            GRect r = L.die[i];
            int16_t xs[2] = { (int16_t)(r.origin.x + 2), (int16_t)(r.origin.x + r.size.w - 2) };
            int16_t ys[2] = { (int16_t)(r.origin.y + 2), (int16_t)(r.origin.y + r.size.h - 2) };
            for (int a = 0; a < 2; a++) for (int b = 0; b < 2; b++) {
                bool in;
                if (round) {
                    int32_t dx = xs[a] - W / 2, dy = ys[b] - H / 2;
                    in = dx * dx + dy * dy <= 129 * 129;
                } else {
                    in = xs[a] >= 0 && xs[a] < W && ys[b] >= 0 && ys[b] < H;
                }
                CHECK(in, "%s: die %d corner (%d,%d) is off the glass",
                      round ? "round" : "rect", i, xs[a], ys[b]);
            }
        }
        s_phase = PHASE_PLAYER;
        s_foe_dice_n = 2;
    }
#ifdef PBL_ROUND
    host_set_screen(260, 260);
#else
    host_set_screen(200, 228);
#endif
}

static void test_blind_hover_hides_the_number(void) {
    fresh_run(MODE_CAMPAIGN, 0, CLASS_WARRIOR);
    open_fight(14, false);
    Card c; memset(&c, 0, sizeof(c));
    c.id = 47;                                  // Stiletto: exactly a 1
    for (int s = 0; s < MAX_SLOTS; s++) c.filled[s] = -1;
    Die d; memset(&d, 0, sizeof(d));
    d.pips = 6;
    CHECK(!can_place_ex(&c, &d, NULL, true), "a 6 fits Stiletto?");
    CHECK(can_place_ex(&c, &d, NULL, false), "the hover gave the hidden number away");
    c.spent = true;
    CHECK(!can_place_ex(&c, &d, NULL, false), "a spent card should still read as no");
}

int main(void) {
    srand(7);
#ifdef PBL_ROUND
    host_set_round(true);
    host_set_screen(260, 260);
#else
    host_set_round(false);
    host_set_screen(200, 228);
#endif
    test_anvil_labels();
    test_weaken_halves_output();
    test_blind_enemy_plays_at_random();
    test_spoils_named_and_applied();
    test_untouched_means_untouched();
    test_full_grid_means_six_cards();
    test_endless_rules_per_floor();
    test_limit_break_does_not_save_mid_fight();
    test_autoroll_is_a_six();
    test_six_foe_dice_fit_the_round();
    test_blind_hover_hides_the_number();
    printf("%s: %d checks, %d failed\n", s_fails ? "FAIL" : "ok", s_checks, s_fails);
    return s_fails ? 1 : 0;
}
