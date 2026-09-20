#include "game.h"

Progress g_prog;

#define PERSIST_KEY_PROG 110
#define PERSIST_KEY_PVER 111
#define PROG_VERSION 2

void progress_load(void) {
    memset(&g_prog, 0, sizeof(g_prog));
    g_prog.unlocked = 1;
    if (!persist_exists(PERSIST_KEY_PROG)) return;
    if (persist_read_int(PERSIST_KEY_PVER) != PROG_VERSION) return;
    if (persist_read_data(PERSIST_KEY_PROG, &g_prog, sizeof(g_prog)) != (int)sizeof(g_prog)) {
        memset(&g_prog, 0, sizeof(g_prog));
        g_prog.unlocked = 1;
    }
    if (g_prog.unlocked < 1) g_prog.unlocked = 1;
    if (g_prog.unlocked > RULE_COUNT) g_prog.unlocked = RULE_COUNT;
    if (g_prog.score_count > SCORE_SLOTS) g_prog.score_count = SCORE_SLOTS;
}

void progress_save(void) {
    persist_write_int(PERSIST_KEY_PVER, PROG_VERSION);
    persist_write_data(PERSIST_KEY_PROG, &g_prog, sizeof(g_prog));
}

void progress_mark_cleared(uint8_t campaign_level) {
    g_prog.cleared |= (uint8_t)(1 << campaign_level);
    if (campaign_level + 1 >= g_prog.unlocked && g_prog.unlocked < RULE_COUNT) {
        g_prog.unlocked = (uint8_t)(campaign_level + 2);
        if (g_prog.unlocked > RULE_COUNT) g_prog.unlocked = RULE_COUNT;
    }
    progress_save();
}

// Endless scores: floors cleared first, gold as the tiebreak. Kept on the
// watch — no phone, no backend, nothing to go down.
int progress_submit_score(void) {
    ScoreEntry e;
    memset(&e, 0, sizeof(e));
    e.floors = (uint8_t)(g_run.floor > 0 ? g_run.floor - 1 : 0);
    e.level  = g_run.level;
    e.gold   = g_run.gold;
    for (int i = 0; i < MAX_EQUIP; i++) e.kit[i] = g_run.equip[i];

    int rank = -1;
    for (int i = 0; i < g_prog.score_count; i++) {
        if (e.floors > g_prog.scores[i].floors ||
            (e.floors == g_prog.scores[i].floors && e.gold > g_prog.scores[i].gold)) {
            rank = i;
            break;
        }
    }
    if (rank < 0) {
        if (g_prog.score_count >= SCORE_SLOTS) return -1;
        rank = g_prog.score_count;
    }
    for (int i = (g_prog.score_count < SCORE_SLOTS ? g_prog.score_count : SCORE_SLOTS - 1); i > rank; i--) {
        g_prog.scores[i] = g_prog.scores[i - 1];
    }
    g_prog.scores[rank] = e;
    if (g_prog.score_count < SCORE_SLOTS) g_prog.score_count++;
    progress_save();
    return rank;
}
