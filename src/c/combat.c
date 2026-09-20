#include "game.h"
#include "ui.h"
#include "anim.h"
#include "portraits.h"
#include "combat.h"

// ---------------------------------------------------------------------------
// The fight. This screen is the game.
//
// You are shown the randomness first — the dice are rolled face-up in front of
// you — and only then do you decide how to spend it across four pieces of
// equipment. A bad roll is therefore a puzzle, not a punishment, and every
// status effect attacks the roll rather than the health bar so it stays one.
//
// Three input paths, none of them second-class:
//   drag    pick a die out of the tray and drop it on a card
//   tap-tap tap a die, then tap a card
//   buttons Up/Down move a cursor, Select picks then drops
// The button path exists because it is genuinely useful, and because the
// emulator cannot inject a drag, so it is also the only path that can be
// verified until the hardware arrives.
//
// Everything animates: dice tumble and snap, cards flash and kick when they
// fire, hit combatants recoil, damage floats off, health bars drain behind a
// pale ghost, and nothing is ever still.
// ---------------------------------------------------------------------------

#define TOAST_MS        1600
#define FOE_STEP_MS      900
#define TAP_SLOP           8
#define BURN_EXTINGUISH    2
#define MAX_FLOAT          6
#define ACTION_COUNT       2    // Limit Break, End turn — rerolling is a card now

typedef struct {
    uint8_t pips;
    bool    used;
    bool    burning;
    Tween   settle;     // the tumble as it lands
    Tween   fly;        // tray -> slot
    GRect   from;
} Die;

typedef struct {
    int16_t hp, max_hp;
    int16_t shield;
    uint8_t status[ST_COUNT];
} Combatant;

typedef struct {
    uint8_t id;
    int8_t  filled[MAX_SLOTS];
    uint8_t uses_left;
    bool    disabled;       // Shocked
    bool    weakened;
    bool    spent;          // already fired this turn
    uint8_t jam;            // Parallel Universe: burning die jams the card
    bool    upgraded;
    Tween   fx;             // fires
} Card;

typedef struct {
    GPoint  at;
    int16_t val;
    GColor  col;
    Tween   t;
    bool    active;
} Floater;

typedef enum { PHASE_PLAYER, PHASE_FOE, PHASE_OVER } Phase;
typedef enum { SEL_BROWSE, SEL_PICK } SelMode;

typedef struct {
    GRect bounds;
    GRect foe_band, portrait, grid, hero_strip, dice_band, action_band;
    GRect cell[MAX_EQUIP];
    GRect slot[MAX_EQUIP][MAX_SLOTS];
    GRect die[MAX_FOE_DICE];
    GRect action[2];
} Layout;

// --- state -----------------------------------------------------------------
static Window    *s_window;
static Layer     *s_layer;
static AppTimer  *s_frame;
static AppTimer  *s_foe_timer;
static Layout     s_L;
static GBitmap   *s_foe_bmp;      // NULL when this enemy has no sprite yet

static Combatant  s_hero, s_foe;
static uint8_t    s_foe_id;
static Card       s_cards[MAX_EQUIP];
static Card       s_foe_cards[MAX_FOE_EQUIP];
static Die        s_dice[MAX_DICE];
static Die        s_foe_dice[MAX_FOE_DICE];
static uint8_t    s_dice_n, s_foe_dice_n, s_card_n, s_foe_card_n;
static Phase      s_phase;

static SelMode    s_mode;
static int8_t     s_cursor;
static int8_t     s_pick_die;
static bool       s_show_kit;
static int8_t     s_grid_row;     // index of the top visible row of the card grid

static char       s_toast[64];
static Tween      s_toast_t;
static bool       s_toast_on;

static Tween      s_roll, s_foe_hit, s_hero_hit, s_death, s_shake;
static int16_t    s_foe_ghost, s_hero_ghost;
static Floater    s_float[MAX_FLOAT];

static bool       s_touch_live, s_dragging;
static int8_t     s_drag_die, s_hover_card;
static GPoint     s_drag_pt, s_touch_start;

static int8_t     s_plan_card[MAX_FOE_DICE], s_plan_die[MAX_FOE_DICE];
static uint8_t    s_plan_n, s_plan_i;

static void compute_layout(Layout *L);
static void check_end(void);
static void grid_reveal(int8_t idx);

static bool parallel(void) { return g_run.rule == RULE_PARALLEL; }
static int roll_d6(void)   { return 1 + (rand() % 6); }
static void redraw(void)   { if (s_layer) layer_mark_dirty(s_layer); }

// --- toast -----------------------------------------------------------------
// The firmware has no vsnprintf, so the variadic part lives in a macro and the
// formatting is done by snprintf at the call site.
static void toast_raw(const char *s) {
    if (s != s_toast) strncpy(s_toast, s, sizeof(s_toast) - 1);
    s_toast[sizeof(s_toast) - 1] = '\0';
    s_toast_on = true;
    tw_start(&s_toast_t, TOAST_MS);
}
#define toast(...) do { \
        snprintf(s_toast, sizeof(s_toast), __VA_ARGS__); \
        toast_raw(s_toast); \
    } while (0)

// --- floaters --------------------------------------------------------------
static void float_spawn(GPoint at, int16_t val, GColor col) {
    for (int i = 0; i < MAX_FLOAT; i++) {
        if (s_float[i].active && tw_running(&s_float[i].t)) continue;
        s_float[i].active = true;
        s_float[i].at = at;
        s_float[i].val = val;
        s_float[i].col = col;
        tw_start(&s_float[i].t, 950);
        return;
    }
}

// --- helpers ---------------------------------------------------------------
static bool card_ready(const Card *c) {
    const ItemDef *d = item_def(c->id);
    for (int i = 0; i < d->slots; i++) if (c->filled[i] < 0) return false;
    return true;
}

static int card_next_open_slot(const Card *c) {
    const ItemDef *d = item_def(c->id);
    for (int i = 0; i < d->slots; i++) if (c->filled[i] < 0) return i;
    return -1;
}

static bool card_usable(const Card *c) {
    if (c->id == ITEM_NONE) return false;
    if (c->jam > 0) return false;
    if (c->spent) return false;     // one firing per card per turn
    const ItemDef *d = item_def(c->id);
    if (d->uses && c->uses_left == 0) return false;
    return true;
}

static bool can_place(const Card *c, const Die *die, const char **why) {
    // In the Parallel Universe a burning die is usable — it just jams the card
    // it lands in. Everywhere else it has to be put out first.
    if (die->burning && !parallel()) { if (why) *why = "That die is on fire"; return false; }
    if (c->disabled) { if (why) *why = "Clears the shock"; return true; }
    if (c->jam > 0)  { if (why) *why = "Jammed"; return false; }
    if (c->spent)    { if (why) *why = "Already fired this turn"; return false; }
    if (!card_usable(c)) { if (why) *why = "All used up"; return false; }
    int slot = card_next_open_slot(c);
    if (slot < 0) { if (why) *why = "No room"; return false; }
    if (!cond_accepts(item_def(c->id)->cond[slot], die->pips)) {
        if (why) *why = "Wrong number for that slot";
        return false;
    }
    return true;
}

// --- damage ----------------------------------------------------------------
// Shields soak; poison ignores them. Any HP the hero actually loses charges
// the Limit Break — including the 2 HP paid to put out a burning die, which is
// exactly how you charge it on purpose.
static void deal_damage(Combatant *t, int16_t amount, bool pierce, bool is_hero) {
    if (amount <= 0) return;
    if (!pierce && t->shield > 0) {
        int16_t soak = (t->shield >= amount) ? amount : t->shield;
        t->shield -= soak;
        amount    -= soak;
    }
    if (amount <= 0) {
        float_spawn(is_hero ? GPoint(s_L.hero_strip.origin.x + 20, s_L.hero_strip.origin.y)
                            : GPoint(s_L.portrait.origin.x + s_L.portrait.size.w / 2,
                                     s_L.portrait.origin.y + s_L.portrait.size.h / 2),
                    0, COL_HERO);
        return;
    }
    t->hp -= amount;
    if (t->hp < 0) t->hp = 0;

    if (is_hero) {
        tw_start(&s_hero_hit, 420);
        float_spawn(GPoint(s_L.hero_strip.origin.x + 30, s_L.hero_strip.origin.y), amount, COL_BAD);
        g_run.limit_charge += amount;
        if (!g_run.limit_ready && g_run.limit_charge >= (uint16_t)g_run.max_hp) {
            g_run.limit_ready = true;
            g_run.limit_charge = (uint16_t)g_run.max_hp;
            toast("LIMIT BREAK ready");
        }
    } else {
        tw_start(&s_foe_hit, 420);
        tw_start(&s_shake, 300);
        float_spawn(GPoint(s_L.portrait.origin.x + s_L.portrait.size.w / 2,
                           s_L.portrait.origin.y + s_L.portrait.size.h / 2), amount, COL_BAD);
    }
}

static void heal_up(Combatant *t, int16_t amount, bool is_hero) {
    if (amount <= 0) return;
    t->hp += amount;
    if (t->hp > t->max_hp) t->hp = t->max_hp;
    float_spawn(is_hero ? GPoint(s_L.hero_strip.origin.x + 30, s_L.hero_strip.origin.y)
                        : GPoint(s_L.portrait.origin.x + s_L.portrait.size.w / 2,
                                 s_L.portrait.origin.y + s_L.portrait.size.h / 2),
                amount, COL_OK);
}

static void inflict(Combatant *t, StatusId s, uint8_t n, bool on_hero) {
    if (s >= ST_COUNT) return;
    if (t->status[s] < 250) t->status[s] += n;
    if (on_hero) toast("%s: %s", status_name(s), status_blurb(s));
}

// --- firing ----------------------------------------------------------------
static void apply_effects(const ItemDef *d, int16_t value, bool by_hero, bool weak, bool upgraded) {
    Combatant *self  = by_hero ? &s_hero : &s_foe;
    Combatant *other = by_hero ? &s_foe  : &s_hero;

    if (upgraded) value += 1;              // what the anvil buys you
    if (weak)     { value /= 2; if (value < 1) value = 1; }

    for (int e = 0; e < 2; e++) {
        const Effect *ef = &d->eff[e];
        if (ef->kind == EFF_NONE) continue;
        int16_t v = (int16_t)(value * ef->mult + ef->add);
        switch (ef->kind) {
            case EFF_DAMAGE: deal_damage(other, v, false, !by_hero); break;
            case EFF_SHIELD: self->shield += v; break;
            case EFF_HEAL:
                if (v < 0) deal_damage(self, (int16_t)-v, true, by_hero);
                else       heal_up(self, v, by_hero);
                break;
            case EFF_DRAIN:
                deal_damage(other, v, false, !by_hero);
                heal_up(self, v, by_hero);
                break;
            case EFF_INFLICT: inflict(other, (StatusId)ef->status, 1, !by_hero); break;
            case EFF_SELF:    inflict(self,  (StatusId)ef->status, 1, by_hero);  break;
            default: break;
        }
    }
}

static void fire_card(Card *c, bool by_hero) {
    const ItemDef *d = item_def(c->id);
    Die *pool = by_hero ? s_dice : s_foe_dice;
    Combatant *self = by_hero ? &s_hero : &s_foe;

    // The Reroll card is its own thing: it rerolls the die you fed it and
    // returns it to the tray unspent, so what it costs you is one of its own
    // limited uses rather than the die. It is also the single exception to the
    // once-per-turn rule — it stays live until its allowance runs out.
    if (d->eff[0].kind == EFF_REROLL) {
        int8_t di = c->filled[0];
        c->filled[0] = -1;
        if (di >= 0) {
            uint8_t before = pool[di].pips;
            pool[di].pips = (uint8_t)roll_d6();
            pool[di].used = false;
            tw_start(&pool[di].settle, 480);
            tw_stop(&pool[di].fly);
            if (c->uses_left > 0) c->uses_left--;
            if (c->uses_left == 0) c->spent = true;
            tw_start(&c->fx, 420);
            if (by_hero) toast("Reroll: %d becomes %d  (%d left)", before, pool[di].pips, c->uses_left);
        }
        return;
    }

    int16_t value = 0;
    bool had_fire = false;
    for (int i = 0; i < d->slots; i++) {
        if (c->filled[i] >= 0) {
            value += pool[c->filled[i]].pips;
            if (pool[c->filled[i]].burning) had_fire = true;
        }
    }

    // Fury doubles the next card fired — the Warrior's Limit Break, and what
    // the Rage card hands you.
    int times = 1;
    if (self->status[ST_FURY] > 0) { times = 2; self->status[ST_FURY]--; }

    for (int t = 0; t < times; t++) apply_effects(d, value, by_hero, c->weakened, c->upgraded);

    if (d->uses && c->uses_left > 0) c->uses_left--;
    c->weakened = false;
    c->spent = true;
    if (parallel() && had_fire) c->jam = 2;     // the Parallel Universe's Burn

    tw_start(&c->fx, 420);

    char eff[24];
    item_effect_label_up(c->id, c->upgraded, eff, sizeof(eff));
    toast("%s%s %d  (%s)", times > 1 ? "FURY! " : "", d->name, value, eff);

    for (int i = 0; i < d->slots; i++) c->filled[i] = -1;
    check_end();
}

// --- turn starts -----------------------------------------------------------
// Statuses land here, at the roll. That is the point: they change the dice you
// are handed rather than quietly subtracting from your health.
static void roll_side(bool hero) {
    Combatant *cb = hero ? &s_hero : &s_foe;
    Die   *pool   = hero ? s_dice  : s_foe_dice;
    uint8_t n     = hero ? s_dice_n : s_foe_dice_n;
    Card  *cards  = hero ? s_cards  : s_foe_cards;
    uint8_t cardn = hero ? s_card_n : s_foe_card_n;

    cb->shield = 0;     // a shield lasts until your own next turn

    for (int i = 0; i < n; i++) {
        pool[i].pips = (uint8_t)roll_d6();
        pool[i].used = false;
        pool[i].burning = false;
        tw_start(&pool[i].settle, (uint16_t)(420 + i * 130));
        tw_stop(&pool[i].fly);
    }
    for (int i = 0; i < cardn; i++) {
        for (int s = 0; s < MAX_SLOTS; s++) cards[i].filled[s] = -1;
        cards[i].disabled = false;
        cards[i].weakened = false;
        cards[i].spent = false;
        if (cards[i].jam > 0) cards[i].jam--;
        // More dice means more chances to fix one: the Reroll card's allowance
        // is refilled every turn from the size of your handful.
        if (item_is_reroll(cards[i].id)) {
            uint8_t allow = (uint8_t)(n > 1 ? n - 1 : 1);
            cards[i].uses_left = allow;
        }
    }
    if (hero) tw_start(&s_roll, 700);

    if (cb->status[ST_FROZEN] > 0) {
        if (parallel()) {
            for (int i = 0; i < n; i++) if (pool[i].pips > 1) pool[i].pips--;
            if (hero) toast("Frozen: every die drops by 1");
        } else {
            int best = 0;
            for (int i = 1; i < n; i++) if (pool[i].pips > pool[best].pips) best = i;
            pool[best].pips = 1;
            if (hero) toast("Frozen: your best die is a 1");
        }
        cb->status[ST_FROZEN]--;
    }
    if (cb->status[ST_BURN] > 0) {
        uint8_t burn = cb->status[ST_BURN];
        for (int i = 0; i < n && burn > 0; i++) if (!pool[i].burning) { pool[i].burning = true; burn--; }
        cb->status[ST_BURN]--;
        if (hero) toast(parallel() ? "Burn: usable, but it jams the card"
                                   : "Burn: a die is alight, 2 HP to put out");
    }
    if (cb->status[ST_SHOCK] > 0 && cardn > 0) {
        cards[rand() % cardn].disabled = true;
        cb->status[ST_SHOCK]--;
        if (hero) toast("Shocked: drop any die on it to clear");
    }
    if (cb->status[ST_WEAKEN] > 0 && cardn > 0) {
        cards[rand() % cardn].weakened = true;
        cb->status[ST_WEAKEN]--;
        if (hero) toast("Weakened: one card does half");
    }
    if (cb->status[ST_POISON] > 0) {
        if (parallel() && cardn > 0) {
            cards[rand() % cardn].jam = 2;      // infects a card instead of you
            if (hero) toast("Poison infects a card");
        } else {
            deal_damage(cb, 3, true, hero);
            if (hero) toast("Poison: 3 HP through your shield");
        }
        cb->status[ST_POISON]--;
    }
    if (cb->status[ST_BLIND] > 0) cb->status[ST_BLIND]--;
    if (cb->status[ST_SILENCE] > 0) cb->status[ST_SILENCE]--;
}

static void begin_player_turn(void) {
    s_phase = PHASE_PLAYER;
    s_mode = SEL_BROWSE;
    s_pick_die = -1;
    s_cursor = 0;
    s_grid_row = 0;
    for (int i = 0; i < s_card_n; i++) {
        if (s_cards[i].id != ITEM_NONE) { s_cursor = (int8_t)i; break; }
    }
    grid_reveal(s_cursor);
    roll_side(true);

    // Curse of Greed opens generous and bills you every single turn.
    if (g_run.rule == RULE_GREED && s_phase == PHASE_PLAYER) {
        StatusId curse = (StatusId)(rand() % ST_FURY);   // never gifts you Fury
        inflict(&s_hero, curse, 1, true);
        toast("Curse of Greed: %s", status_name(curse));
    }
    check_end();
    redraw();
}

// --- the foe's turn --------------------------------------------------------
static int16_t score_card(const Card *c, uint8_t pips) {
    const ItemDef *d = item_def(c->id);
    int16_t s = 0;
    for (int e = 0; e < 2; e++) {
        const Effect *ef = &d->eff[e];
        int16_t v = (int16_t)(pips * ef->mult + ef->add);
        switch (ef->kind) {
            case EFF_DAMAGE:  s += v; break;
            case EFF_DRAIN:   s += v + v / 2; break;
            case EFF_SHIELD:  s += (v * 3) / 4; break;
            case EFF_HEAL:    s += (s_foe.hp < s_foe.max_hp / 2) ? v : 1; break;
            case EFF_INFLICT: s += 4; break;
            case EFF_SELF:    s += 3; break;
            default: break;
        }
    }
    return s;
}

static void plan_foe_turn(void) {
    s_plan_n = 0;
    s_plan_i = 0;
    bool die_taken[MAX_FOE_DICE] = {false};
    bool card_taken[MAX_FOE_EQUIP] = {false};

    for (int pass = 0; pass < s_foe_dice_n; pass++) {
        int best_d = -1, best_c = -1;
        int16_t best_s = -1;
        for (int di = 0; di < s_foe_dice_n; di++) {
            if (die_taken[di] || s_foe_dice[di].burning) continue;
            for (int ci = 0; ci < s_foe_card_n; ci++) {
                if (card_taken[ci]) continue;
                Card *c = &s_foe_cards[ci];
                if (!card_usable(c) || c->disabled) continue;
                if (item_def(c->id)->slots != 1) continue;
                if (!cond_accepts(item_def(c->id)->cond[0], s_foe_dice[di].pips)) continue;
                int16_t sc = score_card(c, s_foe_dice[di].pips);
                if (sc > best_s) { best_s = sc; best_d = di; best_c = ci; }
            }
        }
        if (best_d < 0) break;
        die_taken[best_d] = true;
        card_taken[best_c] = true;
        s_plan_die[s_plan_n] = (int8_t)best_d;
        s_plan_card[s_plan_n] = (int8_t)best_c;
        s_plan_n++;
    }
}

static void foe_step(void *ctx);

static void begin_foe_turn(void) {
    s_phase = PHASE_FOE;
    roll_side(false);
    if (s_phase == PHASE_OVER) return;
    plan_foe_turn();
    toast("%s rolls...", enemy_def(s_foe_id)->name);
    s_foe_timer = app_timer_register(FOE_STEP_MS, foe_step, NULL);
    redraw();
}

static void foe_step(void *ctx) {
    s_foe_timer = NULL;
    if (s_phase != PHASE_FOE) return;
    if (s_plan_i >= s_plan_n) { begin_player_turn(); return; }

    int8_t ci = s_plan_card[s_plan_i];
    int8_t di = s_plan_die[s_plan_i];
    s_plan_i++;
    s_foe_cards[ci].filled[0] = di;
    s_foe_dice[di].used = true;
    fire_card(&s_foe_cards[ci], false);
    redraw();
    if (s_phase != PHASE_OVER) s_foe_timer = app_timer_register(FOE_STEP_MS, foe_step, NULL);
}

// --- end -------------------------------------------------------------------
static void finish(void *ctx) { if (s_window) window_stack_remove(s_window, true); }

static void check_end(void) {
    if (s_phase == PHASE_OVER) return;
    if (s_foe.hp <= 0) {
        s_phase = PHASE_OVER;
        g_last_battle = BATTLE_WON;
        tw_start(&s_death, 900);
        const EnemyDef *d = enemy_def(s_foe_id);
        uint16_t gold = d->gold;
        if (g_run.rule == RULE_SUDDEN) gold *= 2;    // Sudden Death pays double
        g_run.hp = s_hero.hp;
        g_run.gold += gold;
        save_run();
        toast("%s is out! +%d gold", d->name, gold);
        if (s_foe_timer) { app_timer_cancel(s_foe_timer); s_foe_timer = NULL; }
        app_timer_register(2100, finish, NULL);
    } else if (s_hero.hp <= 0) {
        s_phase = PHASE_OVER;
        g_last_battle = BATTLE_LOST;
        g_run.hp = 0;
        toast("You are out of the game.");
        if (s_foe_timer) { app_timer_cancel(s_foe_timer); s_foe_timer = NULL; }
        app_timer_register(1900, finish, NULL);
    }
}

// --- player actions --------------------------------------------------------
static bool any_die_free(void) {
    for (int i = 0; i < s_dice_n; i++) if (!s_dice[i].used) return true;
    return false;
}

static void place_die(int8_t ci, int8_t di) {
    if (ci < 0 || ci >= s_card_n || di < 0 || di >= s_dice_n) return;
    Card *c = &s_cards[ci];
    Die *d = &s_dice[di];
    if (d->used) return;
    if (c->id == ITEM_NONE) { toast("Nothing equipped there"); return; }

    const char *why = NULL;
    if (d->burning && !parallel()) { toast("Put that fire out first"); vibes_short_pulse(); return; }

    if (c->disabled) {
        c->disabled = false;
        d->used = true;
        d->from = s_L.die[di];
        tw_start(&d->fly, 220);
        tw_start(&c->fx, 300);
        toast("Shock cleared");
        return;
    }
    if (!can_place(c, d, &why)) {
        toast("%s", why ? why : "No");
        vibes_short_pulse();
        return;
    }
    int slot = card_next_open_slot(c);
    c->filled[slot] = di;
    d->used = true;
    d->from = s_L.die[di];
    tw_start(&d->fly, 220);
    if (card_ready(c)) fire_card(c, true);
}

static void return_die_from(int8_t ci) {
    if (ci < 0 || ci >= s_card_n) return;
    Card *c = &s_cards[ci];
    const ItemDef *d = item_def(c->id);
    for (int i = d->slots - 1; i >= 0; i--) {
        if (c->filled[i] >= 0) {
            s_dice[c->filled[i]].used = false;
            tw_start(&s_dice[c->filled[i]].settle, 260);
            c->filled[i] = -1;
            return;
        }
    }
}

static void extinguish(int8_t di) {
    if (di < 0 || di >= s_dice_n || !s_dice[di].burning) return;
    s_dice[di].burning = false;
    deal_damage(&s_hero, BURN_EXTINGUISH, true, true);
    toast("Fire out - %d HP", BURN_EXTINGUISH);
    check_end();
}

static void do_limit_break(void) {
    if (!g_run.limit_ready) { toast("Limit Break not charged"); return; }
    if (s_hero.status[ST_SILENCE] > 0) { toast("Silenced - no Limit Break"); return; }
    g_run.limit_ready = false;
    g_run.limit_charge = 0;
    s_hero.status[ST_FURY]++;
    toast("FURY! Your next card fires twice");
    vibes_double_pulse();
    save_run();
}

static void end_turn(void) {
    if (s_phase != PHASE_PLAYER) return;
    g_run.hp = s_hero.hp;
    begin_foe_turn();
}

static void do_action(int8_t a) {
    switch (a) {
        case 0: do_limit_break(); break;
        case 1: end_turn(); break;
        default: break;
    }
}

// --- layout ----------------------------------------------------------------
static int8_t grid_rows(void)    { return (int8_t)((s_card_n + 1) / 2); }
static int8_t grid_max_row(void) { int8_t m = (int8_t)(grid_rows() - 2); return m > 0 ? m : 0; }

// Keep the cursor on screen. Called after any cursor move and at the start of a
// turn, so the selection is never sitting on a row you cannot see.
static void grid_reveal(int8_t idx) {
    if (idx < 0 || idx >= s_card_n) return;
    int8_t row = (int8_t)(idx / 2);
    if (row < s_grid_row) s_grid_row = row;
    else if (row > s_grid_row + 1) s_grid_row = (int8_t)(row - 1);
    if (s_grid_row > grid_max_row()) s_grid_row = grid_max_row();
    if (s_grid_row < 0) s_grid_row = 0;
}

static void compute_layout(Layout *L) {
    L->bounds = s_layer ? layer_get_bounds(s_layer) : GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
    int16_t H = L->bounds.size.h;

    // Round and rectangular displays get different insets because a band near
    // the top of a circle is barely half as wide as the screen.
    int16_t top    = PBL_IF_ROUND_ELSE(30, 2);
    int16_t bottom = PBL_IF_ROUND_ELSE(18, 2);
    int16_t foe_h  = PBL_IF_ROUND_ELSE(54, 62);
    int16_t hero_h = 14;
    int16_t act_h  = PBL_IF_ROUND_ELSE(20, 22);
    int16_t dice_h = (int16_t)(ui_die_size() + PBL_IF_ROUND_ELSE(4, 6));
    int16_t grid_h = (int16_t)(H - top - bottom - foe_h - hero_h - act_h - dice_h - 4);
    if (grid_h < 40) grid_h = 40;

    int16_t y = top;
    L->foe_band = ui_band(&L->bounds, y, foe_h, 3);  y += foe_h + 2;
    L->grid     = ui_band(&L->bounds, y, grid_h, 3); y += grid_h + 2;
    L->hero_strip = ui_band(&L->bounds, y, hero_h, 4); y += hero_h;
    L->dice_band  = ui_band(&L->bounds, y, dice_h, 4); y += dice_h;
    L->action_band = ui_band(&L->bounds, y, act_h, 4);

    // Portrait sits at the left of the enemy band.
    int16_t ps = (int16_t)(foe_h - 6);
    if (ps > 52) ps = 52;
    L->portrait = GRect(L->foe_band.origin.x + 4, L->foe_band.origin.y + 3, ps, ps);

    // Two columns, two visible rows. With more than four cards the grid scrolls,
    // but ONLY in whole rows: Pebble has no way to clip a context to a sub-rect
    // (only whole layers clip), so a half-scrolled row would spill over the HUD.
    // Snapping to rows means every drawn cell is fully inside the grid, and no
    // clipping is needed at all.
    int16_t bar = (int16_t)((s_card_n > 4) ? 7 : 0);   // room for the scrollbar
    int16_t cw = (int16_t)((L->grid.size.w - bar - 4) / 2);
    int16_t ch = (int16_t)((L->grid.size.h - 3) / 2);
    for (int i = 0; i < MAX_EQUIP; i++) {
        int16_t row = (int16_t)(i / 2);
        int16_t vis = (int16_t)(row - s_grid_row);
        if (i >= s_card_n || vis < 0 || vis > 1) {
            L->cell[i] = GRect(0, 0, 0, 0);     // off-window: also fails hit tests
            for (int s = 0; s < MAX_SLOTS; s++) L->slot[i][s] = GRect(0, 0, 0, 0);
            continue;
        }
        int16_t col = (int16_t)(i % 2);
        L->cell[i] = GRect((int16_t)(L->grid.origin.x + col * (cw + 4)),
                           (int16_t)(L->grid.origin.y + vis * (ch + 3)), cw, ch);
        const ItemDef *d = item_def(s_cards[i].id);
        int16_t ss = (int16_t)(ch - 20);
        if (ss > 22) ss = 22;
        if (ss < 12) ss = 12;
        for (int s = 0; s < MAX_SLOTS; s++) {
            L->slot[i][s] = GRect((int16_t)(L->cell[i].origin.x + cw - 3 - (d->slots - s) * (ss + 2)),
                                  (int16_t)(L->cell[i].origin.y + ch - ss - 3), ss, ss);
        }
    }

    // Dice tray, centred. During the foe's turn this shows ITS dice, so you can
    // see exactly what it drew.
    uint8_t n = (s_phase == PHASE_FOE) ? s_foe_dice_n : s_dice_n;
    int16_t ds = ui_die_size();
    int16_t gap = 5;
    int16_t total = (int16_t)(n * ds + (n - 1) * gap);
    int16_t x0 = (int16_t)(L->bounds.size.w / 2 - total / 2);
    for (int i = 0; i < MAX_FOE_DICE; i++) {
        L->die[i] = GRect((int16_t)(x0 + i * (ds + gap)), (int16_t)(L->dice_band.origin.y + 2), ds, ds);
    }

    int16_t aw = (int16_t)((L->action_band.size.w - 4) / ACTION_COUNT);
    for (int i = 0; i < ACTION_COUNT; i++) {
        L->action[i] = GRect((int16_t)(L->action_band.origin.x + 2 + i * aw),
                             L->action_band.origin.y, (int16_t)(aw - 2), L->action_band.size.h);
    }
}

// --- drawing ---------------------------------------------------------------
static void draw_status_row(GContext *ctx, const Combatant *c, GRect r, GTextAlignment al) {
    char buf[44] = "";
    size_t len = 0;
    for (int i = 0; i < ST_COUNT; i++) {
        if (c->status[i] == 0) continue;
        int w = snprintf(buf + len, sizeof(buf) - len, "%s%s", len ? " " : "", status_short((StatusId)i));
        if (w <= 0) break;
        len += (size_t)w;
        if (len >= sizeof(buf) - 5) break;
    }
    if (!len) return;
    ui_draw_text_c(ctx, buf, ui_font_tiny(), r, COL_INK, al);
}

static void draw_foe(GContext *ctx, const Layout *L) {
    GRect r = L->foe_band;
    ui_draw_panel(ctx, r, COL_FOE_DIM, COL_INK, true);

    const EnemyDef *ed = enemy_def(s_foe_id);

    // Idle bounce with squash and stretch: the screen is never still, which is
    // what keeps a long think from feeling like staring at a spreadsheet.
    int16_t bob = anim_bob(1500, 3, 0);
    int32_t sq = 1000 + anim_bob(1500, 60, 375);
    GRect pr = L->portrait;
    pr.origin.y = (int16_t)(pr.origin.y + bob);
    pr = rect_scale(pr, (int32_t)(2000 - sq), sq);

    if (tw_running(&s_shake)) {
        int32_t p = tw_p(&s_shake);
        int16_t amp = (int16_t)(6 - (6 * p) / 1000);
        pr.origin.x = (int16_t)(pr.origin.x + ((p / 60) % 2 ? amp : -amp));
    }
    if (tw_running(&s_death)) {
        int32_t p = ease_in_quad(tw_p(&s_death));
        pr = rect_scale(pr, 1000 - p, 1000 - p);
    } else if (s_phase == PHASE_OVER && s_foe.hp <= 0) {
        pr = GRect(0, 0, 0, 0);
    }
    if (s_foe_bmp) {
        // A bitmap cannot be squashed on this hardware: draw_bitmap_in_rect tiles
        // rather than scales, and rotation is the only transform on offer. It is
        // also far too slow to run every frame — rotating a 48x48 sprite at 30fps
        // pegged the CPU at 100%, exactly the "performance limitations" the SDK
        // warns about. So a sprite enemy BOBS while it idles, which is a plain
        // blit, and rotation is spent only on the death tumble, where it plays
        // once and a dropped frame costs nothing.
        GSize bs = gbitmap_get_bounds(s_foe_bmp).size;
        GPoint centre = GPoint((int16_t)(pr.origin.x + pr.size.w / 2),
                               (int16_t)(pr.origin.y + pr.size.h / 2));
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        if (tw_running(&s_death)) {
            int32_t p = tw_p(&s_death);
            graphics_draw_rotated_bitmap(ctx, s_foe_bmp, GPoint(bs.w / 2, bs.h / 2),
                                         DEG_TO_TRIGANGLE(10 + (80 * p) / 1000),
                                         GPoint(centre.x, (int16_t)(centre.y + (p * 14) / 1000)));
        } else {
            graphics_draw_bitmap_in_rect(ctx, s_foe_bmp,
                GRect((int16_t)(centre.x - bs.w / 2), (int16_t)(centre.y - bs.h / 2), bs.w, bs.h));
        }
        graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    } else {
        portrait_draw(ctx, ed->shape, pr, tw_running(&s_foe_hit) && (tw_p(&s_foe_hit) < 300));
    }

    int16_t tx = (int16_t)(L->portrait.origin.x + L->portrait.size.w + 5);
    int16_t tw_ = (int16_t)(r.origin.x + r.size.w - tx - 4);
    char hp[20];
    snprintf(hp, sizeof(hp), "%d/%d", s_foe.hp, s_foe.max_hp);
    ui_draw_text_c(ctx, ed->name, ui_font_small(), GRect(tx, r.origin.y + 1, tw_, 17), COL_INK, GTextAlignmentLeft);
    ui_draw_text_c(ctx, hp, ui_font_tiny(), GRect(tx, r.origin.y + 16, tw_, 16), COL_INK, GTextAlignmentLeft);
    ui_draw_hp_bar(ctx, GRect(tx, (int16_t)(r.origin.y + 31), tw_, 7), s_foe.hp, s_foe_ghost, s_foe.max_hp, COL_FOE);

    char sh[14] = "";
    if (s_foe.shield > 0) snprintf(sh, sizeof(sh), "SH%d ", s_foe.shield);
    if (sh[0]) ui_draw_text_c(ctx, sh, ui_font_tiny(), GRect(tx, (int16_t)(r.origin.y + 38), tw_, 15), COL_INK, GTextAlignmentLeft);
    draw_status_row(ctx, &s_foe, GRect(tx, (int16_t)(r.origin.y + 38), tw_, 15), GTextAlignmentRight);
}

static void draw_card(GContext *ctx, const Layout *L, int i) {
    const Card *c = &s_cards[i];
    if (c->id == ITEM_NONE) {
        // An empty slot still has to show the cursor, or the cursor disappears
        // whenever it passes over one.
        bool sel = (s_cursor == i) && (s_mode == SEL_BROWSE);
        if (sel) {
            ui_draw_panel(ctx, L->cell[i], COL_CARD_SEL, COL_INK, true);
            ui_draw_text_c(ctx, "empty", ui_font_tiny(),
                           GRect(L->cell[i].origin.x, (int16_t)(L->cell[i].origin.y + 4),
                                 L->cell[i].size.w, 16), COL_FRAME, GTextAlignmentCenter);
        } else {
            graphics_context_set_stroke_color(ctx, GColorLightGray);
            graphics_draw_round_rect(ctx, L->cell[i], 4);
        }
        return;
    }
    const ItemDef *d = item_def(c->id);
    GRect r = L->cell[i];

    // A card kicks when it fires.
    if (tw_running(&c->fx)) {
        int32_t p = tw_p(&c->fx);
        int32_t s = 1000 + (int32_t)((p < 300) ? (p / 3) : ((1000 - p) / 7));
        r = rect_scale(r, s, s);
    }

    bool selected = (s_cursor == i);
    GColor fill = COL_CARD;
    if (c->disabled || c->jam > 0 || c->spent) fill = GColorLightGray;
    if (selected) fill = COL_CARD_SEL;
    if (tw_running(&c->fx) && tw_p(&c->fx) < 250) fill = GColorWhite;

    // While a die is in hand, say plainly whether this card will take it.
    int8_t held = s_dragging ? s_drag_die : (s_mode == SEL_PICK ? s_pick_die : -1);
    bool hovering = s_dragging ? (i == s_hover_card) : (s_mode == SEL_PICK && s_cursor == i);
    if (held >= 0 && hovering) {
        const char *why = NULL;
        fill = can_place(c, &s_dice[held], &why) ? COL_OK : COL_BAD;
    }
    ui_draw_panel(ctx, r, fill, COL_INK, selected || hovering);

    char eff[24], cond[10], line[44];
    item_effect_label_up(c->id, c->upgraded, eff, sizeof(eff));
    cond_label(d->cond[0], cond, sizeof(cond));

    int16_t slot_w = (int16_t)(d->slots * (L->slot[i][0].size.w + 2) + 4);

    // The upgrade mark and the remaining-uses count live in a badge at the top
    // right, because a cell this narrow cannot spare the characters on the
    // effect line — that is what truncated "any reroll x1" to "any rerol...".
    char badge[10] = "";
    size_t bl = 0;
    if (c->upgraded) badge[bl++] = '+';
    if (d->uses) snprintf(badge + bl, sizeof(badge) - bl, "x%d", c->uses_left);
    else badge[bl] = '\0';
    int16_t badge_w = badge[0] ? 22 : 0;

    ui_draw_text_c(ctx, d->name, ui_font_tiny(),
                   GRect((int16_t)(r.origin.x + 4), (int16_t)(r.origin.y + 1),
                         (int16_t)(r.size.w - 8 - badge_w), 16), COL_INK, GTextAlignmentLeft);
    if (badge[0]) {
        ui_draw_text_c(ctx, badge, ui_font_tiny(),
                       GRect((int16_t)(r.origin.x + r.size.w - 4 - badge_w),
                             (int16_t)(r.origin.y + 1), badge_w, 16),
                       COL_HERO, GTextAlignmentRight);
    }

    // The condition is written inside the slot box, which is the thing that
    // actually accepts the die — so the line below the name is free to be
    // nothing but the effect. That matters: once the grid reserves a strip for
    // its scrollbar a cell is about sixty pixels wide, and "=6 dmg9,BRN" on one
    // line simply does not fit.
    if (c->disabled)      snprintf(line, sizeof(line), "SHOCKED");
    else if (c->spent)    snprintf(line, sizeof(line), "used");
    else if (c->jam > 0)  snprintf(line, sizeof(line), "JAMMED %d", c->jam);
    else                  snprintf(line, sizeof(line), "%s%s", eff, c->weakened ? " !" : "");
    ui_draw_text_c(ctx, line, ui_font_tiny(),
                   GRect((int16_t)(r.origin.x + 4), (int16_t)(r.origin.y + 15),
                         (int16_t)(r.size.w - 8 - slot_w), 16), COL_INK, GTextAlignmentLeft);

    for (int s = 0; s < d->slots; s++) {
        GRect sr = L->slot[i][s];
        if (c->filled[s] >= 0) {
            ui_draw_die(ctx, sr, s_dice[c->filled[s]].pips, false, GColorWhite, COL_INK);
        } else {
            graphics_context_set_stroke_color(ctx, COL_INK);
            graphics_draw_round_rect(ctx, sr, 3);
            char cl[10];
            cond_label_short(d->cond[s], cl, sizeof(cl));
            // "any" takes anything, so an empty box says it better than any label.
            if (cl[0]) {
                ui_draw_text_c(ctx, cl, ui_font_tiny(),
                               GRect(sr.origin.x, (int16_t)(sr.origin.y + 1), sr.size.w, 14),
                               COL_INK, GTextAlignmentCenter);
            }
        }
    }
}

static void draw_die_at(GContext *ctx, const Die *d, GRect r, bool blind, bool selected) {
    uint8_t pips = d->pips;
    // While a die is settling it tumbles through faces before landing, and the
    // landing overshoots slightly so it reads as a die coming to rest.
    if (tw_running(&d->settle)) {
        int32_t p = tw_p(&d->settle);
        if (p < 700) pips = (uint8_t)(1 + ((anim_now() / 60 + (uintptr_t)d) % 6));
        int32_t s = ease_out_back(p);
        r = rect_scale(r, 400 + (s * 600) / 1000, 400 + (s * 600) / 1000);
    }
    GColor face = d->burning ? COL_FIRE : GColorWhite;
    ui_draw_die(ctx, r, pips, blind, face, COL_INK);
    if (d->burning) ui_draw_flame(ctx, r, anim_now());
    if (selected) {
        graphics_context_set_stroke_color(ctx, COL_HERO);
        graphics_context_set_stroke_width(ctx, 3);
        graphics_draw_round_rect(ctx, grect_inset(r, GEdgeInsets(-3)), 5);
        graphics_context_set_stroke_width(ctx, 1);
    }
}

// Only drawn when the grid holds more than it can show. A thumb whose size and
// position track the window makes "there is another row down there" obvious
// without spending a cell on saying so.
static void draw_grid_scrollbar(GContext *ctx, const Layout *L) {
    if (s_card_n <= 4) return;
    int8_t rows = grid_rows();
    int16_t x = (int16_t)(L->grid.origin.x + L->grid.size.w - 5);
    int16_t top = L->grid.origin.y;
    int16_t h = L->grid.size.h;

    graphics_context_set_fill_color(ctx, GColorLightGray);
    graphics_fill_rect(ctx, GRect(x, top, 3, h), 1, GCornersAll);

    int16_t th = (int16_t)((h * 2) / rows);
    if (th < 10) th = 10;
    int16_t ty = (int16_t)(top + ((h - th) * s_grid_row) / (rows - 2 > 0 ? rows - 2 : 1));
    graphics_context_set_fill_color(ctx, COL_HERO);
    graphics_fill_rect(ctx, GRect(x, ty, 3, th), 1, GCornersAll);
}

static void draw_dice(GContext *ctx, const Layout *L) {
    if (s_phase == PHASE_FOE) {
        for (int i = 0; i < s_foe_dice_n; i++) {
            if (s_foe_dice[i].used) {
                graphics_context_set_stroke_color(ctx, COL_FRAME);
                graphics_draw_round_rect(ctx, L->die[i], 4);
                continue;
            }
            draw_die_at(ctx, &s_foe_dice[i], L->die[i], false, false);
        }
        return;
    }
    bool blind = s_hero.status[ST_BLIND] > 0;
    for (int i = 0; i < s_dice_n; i++) {
        GRect r = L->die[i];
        r.origin.y = (int16_t)(r.origin.y + anim_bob(1700, 2, (uint16_t)(i * 300)));
        if (s_dragging && i == s_drag_die) {
            graphics_context_set_stroke_color(ctx, COL_FRAME);
            graphics_draw_round_rect(ctx, r, 4);
            continue;
        }
        if (s_dice[i].used) {
            graphics_context_set_stroke_color(ctx, COL_FRAME);
            graphics_draw_round_rect(ctx, r, 4);
            continue;
        }
        draw_die_at(ctx, &s_dice[i], r, blind, s_mode == SEL_PICK && i == s_pick_die);
    }
}

static void draw_hero_strip(GContext *ctx, const Layout *L) {
    GRect r = L->hero_strip;
    if (tw_running(&s_hero_hit)) {
        int32_t p = tw_p(&s_hero_hit);
        r.origin.x = (int16_t)(r.origin.x + (((p / 55) % 2) ? 3 : -3));
    }
    char buf[36];
    if (s_hero.shield > 0) snprintf(buf, sizeof(buf), "HP %d/%d  SH%d", s_hero.hp, s_hero.max_hp, s_hero.shield);
    else                   snprintf(buf, sizeof(buf), "HP %d/%d", s_hero.hp, s_hero.max_hp);
    ui_draw_text_c(ctx, buf, ui_font_tiny(), r, COL_INK, GTextAlignmentLeft);
    draw_status_row(ctx, &s_hero, r, GTextAlignmentRight);
}

static void draw_actions(GContext *ctx, const Layout *L) {
    char fury[16];
    if (g_run.limit_ready) snprintf(fury, sizeof(fury), "FURY");
    else snprintf(fury, sizeof(fury), "FURY %d%%",
                  (int)(g_run.max_hp ? (g_run.limit_charge * 100 / g_run.max_hp) : 0));
    const char *labels[ACTION_COUNT] = { fury, "END TURN" };

    for (int i = 0; i < ACTION_COUNT; i++) {
        bool sel = (s_mode == SEL_BROWSE) && (s_cursor == s_card_n + i);
        GColor fill = GColorWhite;
        if (i == 0 && g_run.limit_ready) {
            // A charged Limit Break pulses so you cannot miss it.
            fill = (anim_bob(700, 100, 0) > 0) ? COL_FIRE : COL_GOLD;
        }
        if (sel) fill = COL_CARD_SEL;
        ui_draw_panel(ctx, L->action[i], fill, COL_INK, sel);
        ui_draw_text_c(ctx, labels[i], ui_font_tiny(),
                       GRect(L->action[i].origin.x,
                             (int16_t)(L->action[i].origin.y + L->action[i].size.h / 2 - 8),
                             L->action[i].size.w, 16), COL_INK, GTextAlignmentCenter);
    }
}

static void draw_floaters(GContext *ctx) {
    for (int i = 0; i < MAX_FLOAT; i++) {
        if (!s_float[i].active) continue;
        if (!tw_running(&s_float[i].t)) { s_float[i].active = false; continue; }
        int32_t p = ease_out_quad(tw_p(&s_float[i].t));
        char b[10];
        if (s_float[i].val == 0) snprintf(b, sizeof(b), "blocked");
        else snprintf(b, sizeof(b), "%d", s_float[i].val);
        ui_draw_text_c(ctx, b, ui_font_small(),
                       GRect((int16_t)(s_float[i].at.x - 24),
                             (int16_t)(s_float[i].at.y - (p * 26) / 1000 - 8), 48, 18),
                       s_float[i].col, GTextAlignmentCenter);
    }
}

static void draw_toast(GContext *ctx, const Layout *L) {
    if (!s_toast_on) return;
    if (!tw_running(&s_toast_t)) { s_toast_on = false; return; }
    int32_t p = tw_p(&s_toast_t);
    // The toast sits over the ENEMY, never over the cards. While you are
    // deciding, your grid and your dice are the two things you cannot afford to
    // have covered; the enemy's name and bar can wait a second and a half, and
    // the damage number floats up here anyway.
    int16_t h = L->foe_band.size.h;
    int16_t y = L->foe_band.origin.y;
    // Slide down out of the top, hold, slide back.
    int16_t off = 0;
    if (p < 120)      off = (int16_t)(-h + (h * p) / 120);
    else if (p > 880) off = (int16_t)(-(h * (p - 880)) / 120);
    GRect r = ui_band(&L->bounds, (int16_t)(y + off), h, 3);
    ui_draw_panel(ctx, r, GColorWhite, COL_INK, true);
    ui_draw_text_c(ctx, s_toast, ui_font_tiny(),
                   GRect((int16_t)(r.origin.x + 4), (int16_t)(r.origin.y + 4),
                         (int16_t)(r.size.w - 8), (int16_t)(h - 6)), COL_INK, GTextAlignmentCenter);
}

// What the enemy is carrying. Nothing it does should ever be a surprise —
// only a problem.
static void draw_kit(GContext *ctx, const Layout *L) {
    int16_t h = (int16_t)(26 + s_foe_card_n * 17);
    GRect r = ui_band(&L->bounds, (int16_t)(L->bounds.size.h / 2 - h / 2), h, 8);
    ui_draw_panel(ctx, r, GColorWhite, COL_INK, true);
    ui_draw_text_c(ctx, enemy_def(s_foe_id)->name, ui_font_small(),
                   GRect((int16_t)(r.origin.x + 4), (int16_t)(r.origin.y + 2),
                         (int16_t)(r.size.w - 8), 18), COL_INK, GTextAlignmentCenter);
    for (int i = 0; i < s_foe_card_n; i++) {
        char line[40], eff[20], cond[10];
        const ItemDef *d = item_def(s_foe_cards[i].id);
        item_effect_label(s_foe_cards[i].id, eff, sizeof(eff));
        cond_label(d->cond[0], cond, sizeof(cond));
        snprintf(line, sizeof(line), "%s  %s %s", d->name, cond, eff);
        ui_draw_text_c(ctx, line, ui_font_tiny(),
                       GRect((int16_t)(r.origin.x + 5), (int16_t)(r.origin.y + 20 + i * 17),
                             (int16_t)(r.size.w - 10), 16), COL_INK, GTextAlignmentLeft);
    }
}

static void layer_update(Layer *layer, GContext *ctx) {
    compute_layout(&s_L);
    const Layout *L = &s_L;

    graphics_context_set_fill_color(ctx, COL_BG);
    graphics_fill_rect(ctx, L->bounds, 0, GCornerNone);

    draw_foe(ctx, L);
    for (int i = 0; i < MAX_EQUIP; i++) {
        if (L->cell[i].size.w == 0) continue;   // scrolled out of the window
        draw_card(ctx, L, i);
    }
    draw_grid_scrollbar(ctx, L);
    draw_hero_strip(ctx, L);
    draw_dice(ctx, L);
    draw_actions(ctx, L);
    draw_floaters(ctx);

    // A die in flight from tray to slot, and a die under the finger, both ride
    // above everything else.
    for (int i = 0; i < s_dice_n; i++) {
        if (!s_dice[i].used || !tw_running(&s_dice[i].fly)) continue;
        int8_t ci = -1, sl = 0;
        for (int c = 0; c < s_card_n && ci < 0; c++) {
            for (int s = 0; s < item_def(s_cards[c].id)->slots; s++) {
                if (s_cards[c].filled[s] == i) { ci = (int8_t)c; sl = (int8_t)s; break; }
            }
        }
        GRect to = (ci >= 0) ? L->slot[ci][sl] : L->die[i];
        GRect r = lerp_rect(s_dice[i].from, to, ease_out_quad(tw_p(&s_dice[i].fly)));
        ui_draw_die(ctx, r, s_dice[i].pips, false, GColorWhite, COL_INK);
    }
    if (s_dragging && s_drag_die >= 0) {
        int16_t ds = (int16_t)(ui_die_size() + 4);
        ui_draw_die(ctx, GRect((int16_t)(s_drag_pt.x - ds / 2), (int16_t)(s_drag_pt.y - ds / 2), ds, ds),
                    s_dice[s_drag_die].pips, s_hero.status[ST_BLIND] > 0, GColorWhite, COL_INK);
    }

    if (s_show_kit) draw_kit(ctx, L);
    else            draw_toast(ctx, L);
}

// --- frame loop ------------------------------------------------------------
// Runs the whole time the window is up. Nothing on this screen is ever still.
static void frame(void *ctx) {
    s_frame = NULL;
    anim_tick(ANIM_FRAME_MS);

    // Health bars chase the real value so a hit reads as a chunk coming off.
    if (s_foe_ghost > s_foe.hp) {
        s_foe_ghost -= (int16_t)(1 + (s_foe_ghost - s_foe.hp) / 6);
        if (s_foe_ghost < s_foe.hp) s_foe_ghost = s_foe.hp;
    } else if (s_foe_ghost < s_foe.hp) s_foe_ghost = s_foe.hp;
    if (s_hero_ghost > s_hero.hp) {
        s_hero_ghost -= (int16_t)(1 + (s_hero_ghost - s_hero.hp) / 6);
        if (s_hero_ghost < s_hero.hp) s_hero_ghost = s_hero.hp;
    } else if (s_hero_ghost < s_hero.hp) s_hero_ghost = s_hero.hp;

    redraw();
    s_frame = app_timer_register(ANIM_FRAME_MS, frame, NULL);
}

// --- touch -----------------------------------------------------------------
static int8_t hit_die(const Layout *L, GPoint p) {
    for (int i = 0; i < s_dice_n; i++) {
        if (s_dice[i].used) continue;
        GRect r = grect_inset(L->die[i], GEdgeInsets(-4));   // forgiving to a fingertip
        if (grect_contains_point(&r, &p)) return (int8_t)i;
    }
    return -1;
}

static int8_t hit_card(const Layout *L, GPoint p) {
    for (int i = 0; i < s_card_n; i++) {
        if (grect_contains_point(&L->cell[i], &p)) return (int8_t)i;
    }
    return -1;
}

static int8_t hit_action(const Layout *L, GPoint p) {
    for (int i = 0; i < ACTION_COUNT; i++) {
        if (grect_contains_point(&L->action[i], &p)) return (int8_t)i;
    }
    return -1;
}

static void touch_handler(const TouchEvent *e, void *ctx) {
    // An unarmed contact — the touch that woke the watch — must never play a
    // die. The flag is latched at Touchdown and carried for the whole gesture.
    if (e->non_navigational) return;
    if (s_show_kit) { if (e->type == TouchEvent_Liftoff) { s_show_kit = false; } return; }
    if (s_phase != PHASE_PLAYER) return;

    const Layout *L = &s_L;
    GPoint p = GPoint(e->x, e->y);

    switch (e->type) {
        case TouchEvent_Touchdown:
            s_touch_live = true;
            s_touch_start = p;
            s_hover_card = -1;
            s_drag_die = hit_die(L, p);
            if (s_drag_die >= 0) {
                s_dragging = true;
                s_drag_pt = p;
            }
            break;

        case TouchEvent_PositionUpdate:
            if (!s_touch_live) break;
            if (s_dragging) {
                s_drag_pt = p;
                s_hover_card = hit_card(L, p);
            }
            break;

        case TouchEvent_Liftoff: {
            if (!s_touch_live) break;
            s_touch_live = false;
            int16_t dx = (int16_t)(p.x - s_touch_start.x), dy = (int16_t)(p.y - s_touch_start.y);
            bool tapped = (dx > -TAP_SLOP && dx < TAP_SLOP && dy > -TAP_SLOP && dy < TAP_SLOP);
            int8_t ci = hit_card(L, p);
            int8_t di = hit_die(L, p);
            int8_t a  = hit_action(L, p);

            if (s_dragging && !tapped) {
                // A real drag: drop it wherever it landed.
                if (ci >= 0) place_die(ci, s_drag_die);
                s_dragging = false;
                s_drag_die = -1;
                s_hover_card = -1;
                break;
            }
            // Not carrying a die, and the gesture started over the grid: a
            // vertical swipe there scrolls it a row.
            if (!s_dragging && !tapped && s_card_n > 4 &&
                grect_contains_point(&L->grid, &s_touch_start) &&
                (dy > 20 || dy < -20)) {
                s_grid_row = (int8_t)(s_grid_row + (dy < 0 ? 1 : -1));
                if (s_grid_row < 0) s_grid_row = 0;
                if (s_grid_row > grid_max_row()) s_grid_row = grid_max_row();
                s_drag_die = -1;
                redraw();
                break;
            }
            s_dragging = false;
            s_hover_card = -1;

            if (!tapped) { s_drag_die = -1; break; }

            // A tap. Tap-a-die-then-tap-a-card is a first-class path, not a
            // fallback: it never hides the target under your finger.
            if (grect_contains_point(&L->foe_band, &p)) { s_show_kit = true; s_drag_die = -1; break; }
            if (a >= 0) { s_cursor = (int8_t)(s_card_n + a); do_action(a); s_drag_die = -1; break; }
            if (di >= 0) {
                if (s_dice[di].burning && !parallel()) { extinguish(di); }
                else if (s_mode == SEL_PICK && s_pick_die == di) { s_mode = SEL_BROWSE; s_pick_die = -1; }
                else { s_mode = SEL_PICK; s_pick_die = di; }
                s_drag_die = -1;
                break;
            }
            if (ci >= 0) {
                if (s_mode == SEL_PICK && s_pick_die >= 0) {
                    place_die(ci, s_pick_die);
                    s_mode = SEL_BROWSE;
                    s_pick_die = -1;
                } else {
                    s_cursor = ci;
                    return_die_from(ci);
                }
                s_drag_die = -1;
                break;
            }
            // Tapped nothing: put the held die down.
            s_mode = SEL_BROWSE;
            s_pick_die = -1;
            s_drag_die = -1;
            break;
        }
        default: break;
    }
    redraw();
}

// --- buttons ---------------------------------------------------------------
static void next_free_die(int dir) {
    if (s_dice_n == 0) return;
    int8_t start = (s_pick_die < 0) ? 0 : s_pick_die;
    for (int i = 1; i <= s_dice_n; i++) {
        int8_t cand = (int8_t)(((int)start + dir * i + s_dice_n * 8) % s_dice_n);
        if (!s_dice[cand].used) { s_pick_die = cand; return; }
    }
}

// Walk the cursor over cards then the three action buttons, stepping over any
// empty grid slot — stopping on nothing is just a wasted press.
static void move_cursor(int dir) {
    int8_t last = (int8_t)(s_card_n + ACTION_COUNT - 1);
    int8_t c = s_cursor;
    for (int guard = 0; guard <= last + 1; guard++) {
        c = (int8_t)(c + dir);
        if (c < 0) { c = 0; break; }
        if (c > last) { c = last; break; }
        if (c >= s_card_n || s_cards[c].id != ITEM_NONE) break;
    }
    // If we ran aground on an empty slot at an end, back off to something real.
    if (c < s_card_n && s_cards[c].id == ITEM_NONE) {
        for (int8_t k = 0; k <= last; k++) {
            int8_t t = (int8_t)(c - dir * k);
            if (t < 0 || t > last) continue;
            if (t >= s_card_n || s_cards[t].id != ITEM_NONE) { c = t; break; }
        }
    }
    s_cursor = c;
}

static void click_up(ClickRecognizerRef r, void *ctx) {
    if (s_show_kit) { s_show_kit = false; redraw(); return; }
    if (s_phase != PHASE_PLAYER) return;
    move_cursor(-1);
    grid_reveal(s_cursor);
    redraw();
}

static void click_down(ClickRecognizerRef r, void *ctx) {
    if (s_show_kit) { s_show_kit = false; redraw(); return; }
    if (s_phase != PHASE_PLAYER) return;
    move_cursor(1);
    grid_reveal(s_cursor);
    redraw();
}

static void click_select(ClickRecognizerRef r, void *ctx) {
    if (s_show_kit) { s_show_kit = false; return; }
    if (s_phase != PHASE_PLAYER) return;

    if (s_mode == SEL_PICK) {
        if (s_pick_die >= 0 && s_dice[s_pick_die].burning && !parallel()) {
            extinguish(s_pick_die);
        } else if (s_cursor < s_card_n) {
            place_die(s_cursor, s_pick_die);
        }
        s_mode = SEL_BROWSE;
        s_pick_die = -1;
        redraw();
        return;
    }

    if (s_cursor >= s_card_n) { do_action((int8_t)(s_cursor - s_card_n)); redraw(); return; }

    const Card *c = &s_cards[s_cursor];
    if (c->id == ITEM_NONE) { toast("Nothing equipped there"); redraw(); return; }
    if (!c->disabled && c->filled[0] >= 0 && !card_ready(c)) { return_die_from(s_cursor); redraw(); return; }
    if (!any_die_free()) { toast("No dice left - end your turn"); redraw(); return; }
    s_mode = SEL_PICK;
    s_pick_die = -1;
    next_free_die(1);
    redraw();
}

static void click_select_long(ClickRecognizerRef r, void *ctx) { s_show_kit = !s_show_kit; redraw(); }

static void click_back(ClickRecognizerRef r, void *ctx) {
    if (s_show_kit) { s_show_kit = false; redraw(); return; }
    if (s_mode == SEL_PICK) {
        s_mode = SEL_BROWSE;
        s_pick_die = -1;
        redraw();
        return;
    }
    toast("No way out but through - hold BACK to quit");
}

static void click_back_long(ClickRecognizerRef r, void *ctx) {
    if (s_phase == PHASE_OVER) return;
    s_phase = PHASE_OVER;
    g_last_battle = BATTLE_LOST;
    g_run.hp = 0;
    toast("You walk off the show.");
    app_timer_register(1400, finish, NULL);
}

static void click_config(void *ctx) {
    window_single_click_subscribe(BUTTON_ID_UP, click_up);
    window_single_click_subscribe(BUTTON_ID_DOWN, click_down);
    window_single_click_subscribe(BUTTON_ID_SELECT, click_select);
    window_long_click_subscribe(BUTTON_ID_SELECT, 500, click_select_long, NULL);
    window_single_click_subscribe(BUTTON_ID_BACK, click_back);
    window_long_click_subscribe(BUTTON_ID_BACK, 700, click_back_long, NULL);
}

// --- window ----------------------------------------------------------------
static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    s_layer = layer_create(layer_get_bounds(root));
    layer_set_update_proc(s_layer, layer_update);
    layer_add_child(root, s_layer);
    compute_layout(&s_L);
}

static void window_appear(Window *window) {
    // This screen is a drag-and-drop surface, not a list to be swiped, so take
    // the touch stream away from the system gesture bridge.
    window_set_touch_bridge_disabled(window, true);
    if (touch_service_is_enabled()) touch_service_subscribe(touch_handler, NULL);
    if (!s_frame) s_frame = app_timer_register(ANIM_FRAME_MS, frame, NULL);
}

static void window_disappear(Window *window) {
    touch_service_unsubscribe();
    if (s_frame) { app_timer_cancel(s_frame); s_frame = NULL; }
    if (s_foe_timer) { app_timer_cancel(s_foe_timer); s_foe_timer = NULL; }
}

static void window_unload(Window *window) {
    if (s_foe_bmp) { gbitmap_destroy(s_foe_bmp); s_foe_bmp = NULL; }
    layer_destroy(s_layer);
    s_layer = NULL;
    window_destroy(s_window);
    s_window = NULL;
}

void combat_window_push(uint8_t enemy_id) {
    const EnemyDef *ed = enemy_def(enemy_id);
    s_foe_id = enemy_id;
    g_last_battle = BATTLE_NONE;
    anim_reset();

    memset(&s_hero, 0, sizeof(s_hero));
    memset(&s_foe, 0, sizeof(s_foe));
    memset(s_float, 0, sizeof(s_float));
    s_hero.hp = g_run.hp;         s_hero.max_hp = g_run.max_hp;
    s_foe.max_hp = run_enemy_hp(enemy_id);
    s_foe.hp = s_foe.max_hp;
    s_foe_ghost = s_foe.hp;
    s_hero_ghost = s_hero.hp;

    // The player's four cards map 1:1 onto the grid, empty slots included, so
    // a card never moves between fights.
    s_card_n = run_equip_slots();
    s_grid_row = 0;
    for (int i = 0; i < MAX_EQUIP; i++) {
        Card *c = &s_cards[i];
        memset(c, 0, sizeof(*c));
        c->id = g_run.equip[i];
        c->upgraded = run_is_upgraded(true, i);
        c->uses_left = (c->id == ITEM_NONE) ? 0 : item_def(c->id)->uses;
        for (int s = 0; s < MAX_SLOTS; s++) c->filled[s] = -1;
    }
    s_foe_card_n = 0;
    for (int i = 0; i < ed->equip_count && i < MAX_FOE_EQUIP; i++) {
        Card *c = &s_foe_cards[s_foe_card_n++];
        memset(c, 0, sizeof(*c));
        c->id = ed->equip[i];
        c->uses_left = item_def(c->id)->uses;
        for (int s = 0; s < MAX_SLOTS; s++) c->filled[s] = -1;
    }

    // Only the enemy actually on screen is held in memory, so the sprite count
    // can grow without the heap growing with it.
    if (s_foe_bmp) { gbitmap_destroy(s_foe_bmp); s_foe_bmp = NULL; }
    uint32_t rid = portrait_resource_for(ed->shape);
    if (rid) s_foe_bmp = gbitmap_create_with_resource(rid);

    s_dice_n = warrior_dice_for_level(g_run.level);
    s_foe_dice_n = run_enemy_dice(enemy_id);
    s_drag_die = -1;
    s_hover_card = -1;
    s_dragging = false;
    s_touch_live = false;
    s_toast_on = false;
    s_show_kit = false;
    s_phase = PHASE_PLAYER;

    s_window = window_create();
    window_set_background_color(s_window, COL_BG);
    window_set_click_config_provider(s_window, click_config);
    window_set_window_handlers(s_window, (WindowHandlers) {
        .load = window_load, .appear = window_appear,
        .disappear = window_disappear, .unload = window_unload
    });
    window_stack_push(s_window, true);

    begin_player_turn();
    toast("%s blocks your way", ed->name);
}
